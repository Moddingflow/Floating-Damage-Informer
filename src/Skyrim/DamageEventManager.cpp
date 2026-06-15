#include "FDN/Skyrim/DamageEventManager.h"

#include "RE/A/Actor.h"
#include "RE/A/ActorCause.h"
#include "RE/A/AIProcess.h"
#include "RE/A/ActorValueOwner.h"
#include "RE/A/ActorValues.h"
#include "RE/E/EffectSetting.h"
#include "RE/F/FormTraits.h"
#include "RE/H/HitData.h"
#include "RE/M/MiddleHighProcessData.h"
#include "RE/N/NiAVObject.h"
#include "RE/P/PlayerCharacter.h"
#include "RE/S/ScriptEventSourceHolder.h"
#include "RE/T/TESForm.h"
#include "REL/Relocation.h"

#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>
#include <utility>

namespace FDN::Skyrim
{
    namespace
    {
        using HandleHealthDamage_t = void(RE::Actor*, RE::Actor*, float);

        constexpr std::size_t kMaxRecentDirectDamage = 256;
        constexpr std::size_t kMaxRecentMagicApplications = 512;
        constexpr std::size_t kMaxActiveMagicSources = 512;
        constexpr std::size_t kMaxRecentUnattributedDamage = 256;

        REL::Relocation<HandleHealthDamage_t> originalHandleHealthDamage;
        DamageEventManager* activeManager = nullptr;
        bool healthDamageHookInstalled = false;

        [[nodiscard]] bool IsFinite(float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsNearZero(const WorldPoint& point) noexcept
        {
            constexpr float kZeroTolerance = 0.001F;
            return std::fabs(point.x) <= kZeroTolerance &&
                   std::fabs(point.y) <= kZeroTolerance &&
                   std::fabs(point.z) <= kZeroTolerance;
        }

        [[nodiscard]] WorldPoint ToWorldPoint(const RE::NiPoint3& point) noexcept
        {
            return WorldPoint{ point.x, point.y, point.z };
        }

        [[nodiscard]] RE::Actor* ResolveSourceActor(RE::TESObjectREFR* ref) noexcept
        {
            if (!ref) {
                return nullptr;
            }

            if (auto* actor = ref->As<RE::Actor>(); actor) {
                return actor;
            }

            if (auto* actorCause = ref->GetActorCause(); actorCause) {
                auto causeRef = actorCause->actor.get();
                return causeRef ? causeRef->As<RE::Actor>() : nullptr;
            }

            return nullptr;
        }

        [[nodiscard]] float DistanceSquared(const WorldPoint& lhs, const RE::NiPoint3& rhs) noexcept
        {
            const float dx = lhs.x - rhs.x;
            const float dy = lhs.y - rhs.y;
            const float dz = lhs.z - rhs.z;
            return dx * dx + dy * dy + dz * dz;
        }

        [[nodiscard]] bool IsUsableImpactPoint(RE::Actor& target, const WorldPoint& point) noexcept
        {
            if (!IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(point.z)) {
                return false;
            }

            if (auto* root = target.Get3D(); root) {
                const auto& bound = root->worldBound;
                if (IsFinite(bound.center.x) &&
                    IsFinite(bound.center.y) &&
                    IsFinite(bound.center.z) &&
                    IsFinite(bound.radius) &&
                    bound.radius > 0.0F) {
                    const float allowedDistance = std::clamp(bound.radius + 256.0F, 384.0F, 1024.0F);
                    return DistanceSquared(point, bound.center) <= allowedDistance * allowedDistance;
                }
            }

            const RE::NiPoint3 targetPosition{
                target.GetPositionX(),
                target.GetPositionY(),
                target.GetPositionZ()
            };
            constexpr float kFallbackAllowedDistance = 384.0F;
            return DistanceSquared(point, targetPosition) <= kFallbackAllowedDistance * kFallbackAllowedDistance;
        }

        [[nodiscard]] bool CanDeferUnattributedDamage(const DamageContext& context, const Settings& settings) noexcept
        {
            if (!settings.general.enabled) {
                return false;
            }
            if (!context.targetIsValidActor || context.targetIsDisabled || context.targetIsDeleted || !context.target3DLoaded) {
                return false;
            }
            if (context.playerTaken || context.targetRelation == TargetRelation::Player) {
                return settings.general.showDamageTakenByPlayer;
            }
            if (context.targetIsDead) {
                return false;
            }
            return context.targetRelation == TargetRelation::HostileToPlayer || settings.general.showNonHostileTargets;
        }

        void HandleHealthDamage(RE::Actor* targetActor, RE::Actor* sourceActor, float damage)
        {
            if (auto* manager = activeManager) {
                try {
                    manager->ReportHealthDamage(targetActor, sourceActor, damage);
                } catch (const std::exception& e) {
                    spdlog::error("Unhandled exception while reporting health damage: {}", e.what());
                } catch (...) {
                    spdlog::error("Unhandled non-standard exception while reporting health damage");
                }
            }

            originalHandleHealthDamage(targetActor, sourceActor, damage);
        }

        bool InstallHealthDamageHook()
        {
            if (healthDamageHookInstalled) {
                return true;
            }

            REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_Actor[0] };
            originalHandleHealthDamage = vtbl.write_vfunc(0x104, HandleHealthDamage);
            healthDamageHookInstalled = true;
            spdlog::info("Actor::HandleHealthDamage hook installed");
            return true;
        }
    }

    void DamageEventManager::Configure(Settings settings, DamageCallback callback, TimeProvider timeProvider)
    {
        settings_ = std::move(settings);
        callback_ = std::move(callback);
        timeProvider_ = std::move(timeProvider);
        pending_.reserve(kMaxPendingHits);
        recentDirectDamage_.reserve(kMaxRecentDirectDamage);
        recentMagicApplications_.reserve(kMaxRecentMagicApplications);
        activeMagicSources_.reserve(kMaxActiveMagicSources);
        recentUnattributedDamage_.reserve(kMaxRecentUnattributedDamage);
    }

    bool DamageEventManager::Register()
    {
        if (registered_) {
            return true;
        }

        activeManager = this;
        if (!InstallHealthDamageHook()) {
            spdlog::warn("Failed to install Actor::HandleHealthDamage hook; falling back to TESHitEvent sampling only");
        }

        auto* sourceHolder = RE::ScriptEventSourceHolder::GetSingleton();
        if (!sourceHolder) {
            return false;
        }

        sourceHolder->AddEventSink<RE::TESHitEvent>(this);
        sourceHolder->AddEventSink<RE::TESMagicEffectApplyEvent>(this);
        registered_ = true;
        spdlog::info("TESHitEvent and TESMagicEffectApplyEvent sinks registered");
        return true;
    }

    void DamageEventManager::Update()
    {
        if (!timeProvider_ || !callback_) {
            return;
        }
        if (pending_.empty() && recentDirectDamage_.empty() && recentMagicApplications_.empty() && activeMagicSources_.empty() &&
            recentUnattributedDamage_.empty()) {
            return;
        }

        const auto nowMs = timeProvider_();
        PruneRecentDirectDamage(nowMs);
        PruneRecentMagicApplications(nowMs);
        PruneActiveMagicSources(nowMs);
        PruneRecentUnattributedDamage(nowMs);
        SampleActiveMagicSources(nowMs);

        if (pending_.empty()) {
            return;
        }

        auto write = pending_.begin();
        for (auto read = pending_.begin(); read != pending_.end(); ++read) {
            auto& pending = *read;
            Sample(pending, nowMs);

            const bool timedOut = nowMs - pending.createdMs > settings_.detection.eventTimeoutMs;
            const bool complete = pending.samplesTaken >= settings_.detection.healthSampleFrames;
            if (!timedOut && !complete) {
                if (write != read) {
                    *write = std::move(*read);
                }
                ++write;
                continue;
            }

            float damage = std::max(0.0F, pending.initialHealth - pending.lowestHealth);
            if (damage <= 0.0F && pending.estimatedDamage > 0.0F) {
                damage = pending.estimatedDamage;
            }

            if (settings_.ShouldCaptureDamage(damage) && ShouldDisplayContext(pending.context, settings_)) {
                DamageEvent event{
                    pending.context.target,
                    pending.context.source,
                    damage,
                    pending.context.playerTaken ? DamageType::PlayerTaken : pending.type,
                    pending.context.playerTaken,
                    nowMs,
                    pending.impactPoint,
                    pending.hasImpactPoint
                };
                callback_(event, pending.target);
                RememberDirectDamage(pending.target, pending.source, nowMs);
                if (auto* activeMagic = FindActiveMagicSource(pending.target, pending.source, nowMs); activeMagic) {
                    activeMagic->observedHealth = pending.lowestHealth;
                    activeMagic->hasObservedHealth = true;
                    activeMagic->lastTouchedMs = nowMs;
                    activeMagic->nextSampleMs = nowMs + std::max<Milliseconds>(settings_.detection.healthSampleDelayMs, 100.0);
                    if (pending.hasImpactPoint) {
                        activeMagic->impactPoint = pending.impactPoint;
                        activeMagic->hasImpactPoint = true;
                    }
                }
            }
        }

        pending_.erase(write, pending_.end());
    }

    void DamageEventManager::Clear()
    {
        pending_.clear();
        recentDirectDamage_.clear();
        recentMagicApplications_.clear();
        activeMagicSources_.clear();
        recentUnattributedDamage_.clear();
    }

    void DamageEventManager::ReportHealthDamage(RE::Actor* targetActor, RE::Actor* sourceActor, float damage)
    {
        if (!targetActor || !settings_.general.enabled || !callback_ || !timeProvider_ || !std::isfinite(damage) || damage <= 0.0F) {
            return;
        }

        const RE::ObjectRefHandle targetHandle{ targetActor };
        RE::ObjectRefHandle sourceHandle{};
        if (sourceActor) {
            sourceHandle = sourceActor;
        }

        const auto nowMs = timeProvider_();
        PruneRecentDirectDamage(nowMs);
        PruneRecentMagicApplications(nowMs);
        PruneActiveMagicSources(nowMs);

        DamageType type = DamageType::Normal;
        auto* activeMagic = FindActiveMagicSource(targetHandle, sourceHandle, nowMs);
        if (activeMagic) {
            type = activeMagic->type;
            if (!sourceActor && activeMagic->source) {
                sourceHandle = activeMagic->source;
                auto sourceRef = sourceHandle.get();
                sourceActor = AsActor(sourceRef.get());
            }
        }
        if (const auto recentMagic = FindRecentMagicApplication(targetHandle, sourceHandle, nowMs); recentMagic) {
            type = recentMagic->type;
            if (!sourceActor && recentMagic->source) {
                sourceHandle = recentMagic->source;
                auto sourceRef = sourceHandle.get();
                sourceActor = AsActor(sourceRef.get());
                activeMagic = FindActiveMagicSource(targetHandle, sourceHandle, nowMs);
            }
        }

        const auto context = BuildContext(*targetActor, sourceActor, targetHandle, sourceHandle);
        const bool shouldCaptureDamage = settings_.ShouldCaptureDamage(damage);
        if (!ShouldDisplayContext(context, settings_) || !shouldCaptureDamage) {
            if (!sourceActor && shouldCaptureDamage && CanDeferUnattributedDamage(context, settings_)) {
                const auto hitSample = ReadLastHitSample(*targetActor, targetHandle, sourceHandle);
                RememberUnattributedDamage(targetHandle, damage, hitSample, nowMs);
            }
            return;
        }

        const auto hitSample = ReadLastHitSample(*targetActor, targetHandle, sourceHandle);

        for (std::size_t index = 0; index < pending_.size();) {
            if (pending_[index].target == targetHandle && pending_[index].source == sourceHandle) {
                pending_[index] = std::move(pending_.back());
                pending_.pop_back();
                continue;
            }
            ++index;
        }

        DamageEvent event{
            context.target,
            context.source,
            damage,
            context.playerTaken ? DamageType::PlayerTaken : type,
            context.playerTaken,
            nowMs,
            hitSample.impactPoint,
            hitSample.hasImpactPoint
        };

        spdlog::trace("Health damage captured: target={}, source={}, amount={}", context.target, context.source, damage);
        callback_(event, targetHandle);
        RememberDirectDamage(targetHandle, sourceHandle, nowMs);
        if (activeMagic) {
            UpdateActiveMagicObservedHealth(*activeMagic, *targetActor, damage, hitSample, nowMs);
        }
    }

    RE::BSEventNotifyControl DamageEventManager::ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*)
    {
        if (!event || !settings_.general.enabled || !timeProvider_) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* targetRef = event->target.get();
        auto* sourceRef = event->cause.get();
        auto* targetActor = AsActor(targetRef);
        auto* sourceActor = ResolveSourceActor(sourceRef);
        if (!targetActor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::ObjectRefHandle targetHandle{ targetActor };
        RE::ObjectRefHandle sourceHandle{};
        if (sourceActor) {
            sourceHandle = sourceActor;
        }
        const auto nowMs = timeProvider_();
        PruneRecentDirectDamage(nowMs);
        PruneRecentMagicApplications(nowMs);
        PruneActiveMagicSources(nowMs);
        PruneRecentUnattributedDamage(nowMs);
        if (HasRecentDirectDamage(targetHandle, sourceHandle, nowMs)) {
            return RE::BSEventNotifyControl::kContinue;
        }
        if (HasRecentPending(targetHandle, sourceHandle, nowMs)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto context = BuildContext(*targetActor, sourceActor, targetHandle, sourceHandle);
        if (!ShouldDisplayContext(context, settings_)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto hitSample = ReadLastHitSample(*targetActor, targetHandle, sourceHandle);
        const auto type = DetectDamageType(*event);
        if (type != DamageType::Normal && sourceHandle) {
            RememberMagicApplication(targetHandle, sourceHandle, type, nowMs);
            RememberActiveMagicSource(targetHandle, sourceHandle, type, GetHealth(*targetActor), hitSample, nowMs);
        }
        if (!settings_.detection.aggregateDamageOverTime && !settings_.ShouldShowDamage(hitSample.damage)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const float health = GetHealth(*targetActor);
        PendingHit pending{
            targetHandle,
            sourceHandle,
            health,
            health,
            hitSample.damage,
            0,
            nowMs,
            nowMs + settings_.detection.healthSampleDelayMs,
            type,
            context,
            hitSample.impactPoint,
            hitSample.hasImpactPoint
        };
        if (pending_.size() >= kMaxPendingHits) {
            pending_.front() = std::move(pending_.back());
            pending_.pop_back();
        }
        pending_.push_back(pending);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::BSEventNotifyControl DamageEventManager::ProcessEvent(
        const RE::TESMagicEffectApplyEvent* event,
        RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*)
    {
        if (!event || !settings_.general.enabled || !timeProvider_) {
            return RE::BSEventNotifyControl::kContinue;
        }

        auto* targetRef = event->target.get();
        auto* casterRef = event->caster.get();
        auto* targetActor = AsActor(targetRef);
        auto* sourceActor = ResolveSourceActor(casterRef);
        if (!targetActor || !sourceActor) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const RE::ObjectRefHandle targetHandle{ targetActor };
        const RE::ObjectRefHandle sourceHandle{ sourceActor };
        const auto nowMs = timeProvider_();
        PruneRecentDirectDamage(nowMs);
        PruneRecentMagicApplications(nowMs);
        PruneActiveMagicSources(nowMs);
        PruneRecentUnattributedDamage(nowMs);

        const auto context = BuildContext(*targetActor, sourceActor, targetHandle, sourceHandle);
        if (!ShouldDisplayContext(context, settings_)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(event->magicEffect);
        const auto type = DetectDamageType(effect);
        const auto hitSample = ReadLastHitSample(*targetActor, targetHandle, sourceHandle);
        RememberMagicApplication(targetHandle, sourceHandle, type, nowMs);
        RememberActiveMagicSource(targetHandle, sourceHandle, type, GetHealth(*targetActor), hitSample, nowMs);

        if (callback_) {
            if (const auto unattributed = ConsumeRecentUnattributedDamage(targetHandle, nowMs);
                unattributed && settings_.ShouldCaptureDamage(unattributed->damage)) {
                DamageEvent damageEvent{
                    context.target,
                    context.source,
                    unattributed->damage,
                    context.playerTaken ? DamageType::PlayerTaken : type,
                    context.playerTaken,
                    nowMs,
                    unattributed->impactPoint,
                    unattributed->hasImpactPoint
                };
                callback_(damageEvent, targetHandle);
                RememberDirectDamage(targetHandle, sourceHandle, nowMs);
                return RE::BSEventNotifyControl::kContinue;
            }
        }

        if (HasRecentDirectDamage(targetHandle, sourceHandle, nowMs)) {
            return RE::BSEventNotifyControl::kContinue;
        }
        if (HasRecentPending(targetHandle, sourceHandle, nowMs)) {
            return RE::BSEventNotifyControl::kContinue;
        }

        const float health = GetHealth(*targetActor);
        PendingHit pending{
            targetHandle,
            sourceHandle,
            health,
            health,
            0.0F,
            0,
            nowMs,
            nowMs + settings_.detection.healthSampleDelayMs,
            type,
            context,
            hitSample.impactPoint,
            hitSample.hasImpactPoint
        };
        if (pending_.size() >= kMaxPendingHits) {
            pending_.front() = std::move(pending_.back());
            pending_.pop_back();
        }
        pending_.push_back(pending);
        return RE::BSEventNotifyControl::kContinue;
    }

    RE::Actor* DamageEventManager::AsActor(RE::TESObjectREFR* ref) noexcept
    {
        return ref ? ref->As<RE::Actor>() : nullptr;
    }

    float DamageEventManager::GetHealth(RE::Actor& actor) noexcept
    {
        if (auto* values = actor.AsActorValueOwner()) {
            return values->GetActorValue(RE::ActorValue::kHealth);
        }
        return 0.0F;
    }

    DamageType DamageEventManager::DetectDamageType(const RE::EffectSetting* effect) noexcept
    {
        if (!effect) {
            return DamageType::Magic;
        }

        switch (effect->data.resistVariable) {
        case RE::ActorValue::kResistFire:
            return DamageType::Fire;
        case RE::ActorValue::kResistFrost:
            return DamageType::Frost;
        case RE::ActorValue::kResistShock:
            return DamageType::Shock;
        case RE::ActorValue::kPoisonResist:
            return DamageType::Poison;
        case RE::ActorValue::kResistMagic:
        default:
            return DamageType::Magic;
        }
    }

    DamageContext DamageEventManager::BuildContext(
        RE::Actor& target,
        RE::Actor* sourceActor,
        RE::ObjectRefHandle targetHandle,
        RE::ObjectRefHandle sourceHandle) const
    {
        auto* player = RE::PlayerCharacter::GetSingleton();

        SourceRelation sourceRelation = SourceRelation::Unknown;
        if (sourceActor) {
            if (player && sourceActor == player) {
                sourceRelation = SourceRelation::Player;
            } else if (sourceActor->IsPlayerTeammate()) {
                sourceRelation = SourceRelation::Follower;
            } else {
                sourceRelation = SourceRelation::NPC;
            }
        }

        TargetRelation targetRelation = TargetRelation::Unknown;
        bool playerTaken = false;
        if (player && std::addressof(target) == player) {
            targetRelation = TargetRelation::Player;
            playerTaken = true;
        } else if (player && target.IsHostileToActor(player)) {
            targetRelation = TargetRelation::HostileToPlayer;
        } else {
            targetRelation = TargetRelation::NonHostile;
        }

        return DamageContext{
            targetHandle.native_handle(),
            sourceHandle.native_handle(),
            sourceRelation,
            targetRelation,
            true,
            target.IsDead(),
            target.IsDisabled(),
            target.IsMarkedForDeletion(),
            target.Is3DLoaded(),
            playerTaken
        };
    }

    DamageType DamageEventManager::DetectDamageType(const RE::TESHitEvent& event) const noexcept
    {
        if (auto* effect = RE::TESForm::LookupByID<RE::EffectSetting>(event.source); effect) {
            return DetectDamageType(effect);
        }

        if (const auto* source = RE::TESForm::LookupByID(event.source); source) {
            switch (source->GetFormType()) {
            case RE::FormType::MagicEffect:
            case RE::FormType::Enchantment:
            case RE::FormType::Spell:
            case RE::FormType::Scroll:
                return DamageType::Magic;
            case RE::FormType::AlchemyItem:
                return DamageType::Poison;
            default:
                break;
            }
        }

        return DamageType::Normal;
    }

    DamageEventManager::HitSample DamageEventManager::ReadLastHitSample(
        RE::Actor& target,
        RE::ObjectRefHandle targetHandle,
        RE::ObjectRefHandle sourceHandle) const noexcept
    {
        const auto* process = target.GetActorRuntimeData().currentProcess;
        if (!process || !process->middleHigh || !process->middleHigh->lastHitData) {
            return {};
        }

        const auto* hitData = process->middleHigh->lastHitData;
        if (hitData->target && hitData->target.native_handle() != targetHandle.native_handle()) {
            return {};
        }
        if (sourceHandle && hitData->aggressor && hitData->aggressor.native_handle() != sourceHandle.native_handle()) {
            return {};
        }

        const float damage = std::max(hitData->totalDamage, hitData->physicalDamage);
        HitSample sample;
        if (std::isfinite(damage) && damage > 0.0F && damage <= kMaxDisplayableDamage) {
            sample.damage = damage;
        }

        const auto impactPoint = ToWorldPoint(hitData->hitPosition);
        if (!IsNearZero(impactPoint) && IsUsableImpactPoint(target, impactPoint)) {
            sample.impactPoint = impactPoint;
            sample.hasImpactPoint = true;
        }

        return sample;
    }

    std::optional<DamageEventManager::RecentMagicApplication> DamageEventManager::FindRecentMagicApplication(
        RE::ObjectRefHandle target,
        RE::ObjectRefHandle source,
        Milliseconds nowMs) const noexcept
    {
        const auto windowMs = RecentMagicWindowMs();
        const RecentMagicApplication* latest = nullptr;
        for (const auto& recent : recentMagicApplications_) {
            if (recent.target != target) {
                continue;
            }
            if (source && recent.source != source) {
                continue;
            }
            if (nowMs < recent.timestampMs || nowMs - recent.timestampMs > windowMs) {
                continue;
            }
            if (!latest || recent.timestampMs > latest->timestampMs) {
                latest = std::addressof(recent);
            }
        }

        if (!latest) {
            return std::nullopt;
        }
        return *latest;
    }

    DamageEventManager::ActiveMagicSource* DamageEventManager::FindActiveMagicSource(
        RE::ObjectRefHandle target,
        RE::ObjectRefHandle source,
        Milliseconds nowMs) noexcept
    {
        const auto windowMs = ActiveMagicWindowMs();
        ActiveMagicSource* latest = nullptr;
        for (auto& active : activeMagicSources_) {
            if (active.target != target) {
                continue;
            }
            if (source && active.source != source) {
                continue;
            }
            if (nowMs < active.lastTouchedMs || nowMs - active.lastTouchedMs > windowMs) {
                continue;
            }
            if (!latest || active.lastTouchedMs > latest->lastTouchedMs) {
                latest = std::addressof(active);
            }
        }

        return latest;
    }

    std::optional<DamageEventManager::RecentUnattributedDamage> DamageEventManager::ConsumeRecentUnattributedDamage(
        RE::ObjectRefHandle target,
        Milliseconds nowMs) noexcept
    {
        const auto windowMs = RecentDirectWindowMs();
        std::optional<std::size_t> latestIndex;
        for (std::size_t index = 0; index < recentUnattributedDamage_.size(); ++index) {
            const auto& recent = recentUnattributedDamage_[index];
            if (recent.target != target) {
                continue;
            }
            if (nowMs < recent.timestampMs || nowMs - recent.timestampMs > windowMs) {
                continue;
            }
            if (!latestIndex || recent.timestampMs > recentUnattributedDamage_[*latestIndex].timestampMs) {
                latestIndex = index;
            }
        }

        if (!latestIndex) {
            return std::nullopt;
        }

        auto result = recentUnattributedDamage_[*latestIndex];
        recentUnattributedDamage_[*latestIndex] = recentUnattributedDamage_.back();
        recentUnattributedDamage_.pop_back();
        return result;
    }

    bool DamageEventManager::HasRecentPending(RE::ObjectRefHandle target, RE::ObjectRefHandle source, Milliseconds nowMs) const noexcept
    {
        return std::ranges::any_of(pending_, [&](const PendingHit& pending) {
            return pending.target == target &&
                   pending.source == source &&
                   nowMs - pending.createdMs <= settings_.detection.mergeWindowMs;
        });
    }

    bool DamageEventManager::HasRecentDirectDamage(
        RE::ObjectRefHandle target,
        RE::ObjectRefHandle source,
        Milliseconds nowMs) const noexcept
    {
        const auto windowMs = RecentDirectWindowMs();
        return std::ranges::any_of(recentDirectDamage_, [&](const RecentDirectDamage& recent) {
            return recent.target == target &&
                   recent.source == source &&
                   nowMs >= recent.timestampMs &&
                   nowMs - recent.timestampMs <= windowMs;
        });
    }

    Milliseconds DamageEventManager::RecentDirectWindowMs() const noexcept
    {
        const auto configuredWindow = static_cast<Milliseconds>(
            std::max(settings_.detection.mergeWindowMs, settings_.detection.healthSampleDelayMs));
        return std::clamp(configuredWindow, 16.0, 250.0);
    }

    Milliseconds DamageEventManager::RecentMagicWindowMs() const noexcept
    {
        const auto configuredWindow = static_cast<Milliseconds>(
            std::max({ settings_.detection.mergeWindowMs, settings_.detection.healthSampleDelayMs, settings_.detection.eventTimeoutMs }));
        return std::clamp(configuredWindow, 100.0, 1000.0);
    }

    Milliseconds DamageEventManager::ActiveMagicWindowMs() const noexcept
    {
        return static_cast<Milliseconds>(std::clamp(settings_.detection.magicSourceMemoryMs, 1000.0F, 60000.0F));
    }

    void DamageEventManager::RememberDirectDamage(RE::ObjectRefHandle target, RE::ObjectRefHandle source, Milliseconds nowMs)
    {
        recentDirectDamage_.push_back(RecentDirectDamage{ target, source, nowMs });
        if (recentDirectDamage_.size() > kMaxRecentDirectDamage) {
            recentDirectDamage_.front() = recentDirectDamage_.back();
            recentDirectDamage_.pop_back();
        }
    }

    void DamageEventManager::PruneRecentDirectDamage(Milliseconds nowMs)
    {
        if (recentDirectDamage_.empty()) {
            return;
        }

        const auto windowMs = RecentDirectWindowMs();
        recentDirectDamage_.erase(
            std::remove_if(recentDirectDamage_.begin(), recentDirectDamage_.end(), [&](const RecentDirectDamage& recent) {
                return nowMs < recent.timestampMs || nowMs - recent.timestampMs > windowMs;
            }),
            recentDirectDamage_.end());
    }

    void DamageEventManager::RememberMagicApplication(
        RE::ObjectRefHandle target,
        RE::ObjectRefHandle source,
        DamageType type,
        Milliseconds nowMs)
    {
        recentMagicApplications_.push_back(RecentMagicApplication{ target, source, type, nowMs });
        if (recentMagicApplications_.size() > kMaxRecentMagicApplications) {
            recentMagicApplications_.front() = recentMagicApplications_.back();
            recentMagicApplications_.pop_back();
        }
    }

    void DamageEventManager::PruneRecentMagicApplications(Milliseconds nowMs)
    {
        if (recentMagicApplications_.empty()) {
            return;
        }

        const auto windowMs = RecentMagicWindowMs();
        recentMagicApplications_.erase(
            std::remove_if(recentMagicApplications_.begin(), recentMagicApplications_.end(), [&](const RecentMagicApplication& recent) {
                return nowMs < recent.timestampMs || nowMs - recent.timestampMs > windowMs;
            }),
            recentMagicApplications_.end());
    }

    DamageEventManager::ActiveMagicSource& DamageEventManager::RememberActiveMagicSource(
        RE::ObjectRefHandle target,
        RE::ObjectRefHandle source,
        DamageType type,
        float observedHealth,
        const HitSample& hitSample,
        Milliseconds nowMs)
    {
        if (auto* active = FindActiveMagicSource(target, source, nowMs); active && active->source == source) {
            active->type = type;
            active->lastTouchedMs = nowMs;
            if (!active->hasObservedHealth && std::isfinite(observedHealth)) {
                active->observedHealth = observedHealth;
                active->hasObservedHealth = true;
                active->nextSampleMs = nowMs + std::max<Milliseconds>(settings_.detection.healthSampleDelayMs, 100.0);
            }
            if (hitSample.hasImpactPoint) {
                active->impactPoint = hitSample.impactPoint;
                active->hasImpactPoint = true;
            }
            return *active;
        }

        activeMagicSources_.push_back(ActiveMagicSource{
            target,
            source,
            type,
            nowMs,
            nowMs + std::max<Milliseconds>(settings_.detection.healthSampleDelayMs, 100.0),
            std::isfinite(observedHealth) ? observedHealth : 0.0F,
            std::isfinite(observedHealth),
            hitSample.impactPoint,
            hitSample.hasImpactPoint,
            0.0F });
        if (activeMagicSources_.size() > kMaxActiveMagicSources) {
            activeMagicSources_.front() = activeMagicSources_.back();
            activeMagicSources_.pop_back();
            return activeMagicSources_.front();
        }
        return activeMagicSources_.back();
    }

    void DamageEventManager::UpdateActiveMagicObservedHealth(
        ActiveMagicSource& active,
        RE::Actor& target,
        float damage,
        const HitSample& hitSample,
        Milliseconds nowMs) noexcept
    {
        const float currentHealth = GetHealth(target);
        if (std::isfinite(currentHealth) && std::isfinite(damage)) {
            active.observedHealth = std::max(0.0F, currentHealth - std::max(damage, 0.0F));
            active.hasObservedHealth = true;
        }
        active.lastTouchedMs = nowMs;
        active.nextSampleMs = nowMs + std::max<Milliseconds>(settings_.detection.healthSampleDelayMs, 100.0);
        if (hitSample.hasImpactPoint) {
            active.impactPoint = hitSample.impactPoint;
            active.hasImpactPoint = true;
        }
    }

    void DamageEventManager::PruneActiveMagicSources(Milliseconds nowMs)
    {
        if (activeMagicSources_.empty()) {
            return;
        }

        const auto windowMs = ActiveMagicWindowMs();
        activeMagicSources_.erase(
            std::remove_if(activeMagicSources_.begin(), activeMagicSources_.end(), [&](const ActiveMagicSource& active) {
                return nowMs < active.lastTouchedMs || nowMs - active.lastTouchedMs > windowMs;
            }),
            activeMagicSources_.end());
    }

    void DamageEventManager::SampleActiveMagicSources(Milliseconds nowMs)
    {
        if (!callback_ || activeMagicSources_.empty()) {
            return;
        }

        const auto sampleIntervalMs = std::max<Milliseconds>(settings_.detection.healthSampleDelayMs, 100.0);
        for (auto& active : activeMagicSources_) {
            if (nowMs < active.nextSampleMs) {
                continue;
            }

            active.nextSampleMs = nowMs + sampleIntervalMs;
            if (HasRecentDirectDamage(active.target, active.source, nowMs)) {
                continue;
            }

            auto targetRef = active.target.get();
            auto sourceRef = active.source.get();
            auto* targetActor = AsActor(targetRef.get());
            auto* sourceActor = AsActor(sourceRef.get());
            if (!targetActor || !sourceActor || targetActor->IsDisabled() || targetActor->IsMarkedForDeletion()) {
                continue;
            }

            const float currentHealth = GetHealth(*targetActor);
            if (!std::isfinite(currentHealth)) {
                continue;
            }
            if (!active.hasObservedHealth) {
                active.observedHealth = currentHealth;
                active.hasObservedHealth = true;
                continue;
            }

            const float sampleDamage = std::max(0.0F, active.observedHealth - currentHealth);
            active.observedHealth = currentHealth;
            if (!settings_.ShouldCaptureDamage(sampleDamage)) {
                continue;
            }

            const auto context = BuildContext(*targetActor, sourceActor, active.target, active.source);
            if (!ShouldDisplayContext(context, settings_)) {
                continue;
            }

            float damage = sampleDamage;
            if (settings_.detection.aggregateDamageOverTime && sampleDamage > 0.0F) {
                active.pendingDamage = std::clamp(active.pendingDamage + sampleDamage, 0.0F, kMaxDisplayableDamage);
                if (!settings_.ShouldShowDamage(active.pendingDamage)) {
                    continue;
                }
                damage = active.pendingDamage;
                active.pendingDamage = 0.0F;
            }

            DamageEvent event{
                context.target,
                context.source,
                damage,
                context.playerTaken ? DamageType::PlayerTaken : active.type,
                context.playerTaken,
                nowMs,
                active.impactPoint,
                active.hasImpactPoint
            };
            callback_(event, active.target);
            RememberDirectDamage(active.target, active.source, nowMs);
            active.lastTouchedMs = nowMs;
        }
    }

    void DamageEventManager::RememberUnattributedDamage(
        RE::ObjectRefHandle target,
        float damage,
        const HitSample& hitSample,
        Milliseconds nowMs)
    {
        recentUnattributedDamage_.push_back(RecentUnattributedDamage{
            target,
            damage,
            nowMs,
            hitSample.impactPoint,
            hitSample.hasImpactPoint });
        if (recentUnattributedDamage_.size() > kMaxRecentUnattributedDamage) {
            recentUnattributedDamage_.front() = recentUnattributedDamage_.back();
            recentUnattributedDamage_.pop_back();
        }
    }

    void DamageEventManager::PruneRecentUnattributedDamage(Milliseconds nowMs)
    {
        if (recentUnattributedDamage_.empty()) {
            return;
        }

        const auto windowMs = RecentDirectWindowMs();
        recentUnattributedDamage_.erase(
            std::remove_if(recentUnattributedDamage_.begin(), recentUnattributedDamage_.end(), [&](const RecentUnattributedDamage& recent) {
                return nowMs < recent.timestampMs || nowMs - recent.timestampMs > windowMs;
            }),
            recentUnattributedDamage_.end());
    }

    void DamageEventManager::Sample(PendingHit& pending, Milliseconds nowMs)
    {
        if (nowMs < pending.nextSampleMs || pending.samplesTaken >= settings_.detection.healthSampleFrames) {
            return;
        }

        auto ref = pending.target.get();
        auto* actor = AsActor(ref.get());
        if (!actor || actor->IsDisabled() || actor->IsMarkedForDeletion()) {
            pending.samplesTaken = settings_.detection.healthSampleFrames;
            return;
        }

        pending.lowestHealth = std::min(pending.lowestHealth, GetHealth(*actor));
        ++pending.samplesTaken;
        pending.nextSampleMs = nowMs + settings_.detection.healthSampleDelayMs;
    }
}
