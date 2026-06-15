#pragma once

#include "FDN/FloatingNumberManager.h"

#include "RE/B/BSPointerHandle.h"
#include "RE/B/BSTEvent.h"
#include "RE/T/TESHitEvent.h"
#include "RE/T/TESMagicEffectApplyEvent.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <vector>

namespace RE
{
    class EffectSetting;
}

namespace FDN::Skyrim
{
    class DamageEventManager final :
        public RE::BSTEventSink<RE::TESHitEvent>,
        public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>
    {
    public:
        using DamageCallback = std::function<void(const DamageEvent&, RE::ObjectRefHandle)>;
        using TimeProvider = std::function<Milliseconds()>;

        void Configure(Settings settings, DamageCallback callback, TimeProvider timeProvider);
        bool Register();
        void Update();
        void Clear();
        void ReportHealthDamage(RE::Actor* targetActor, RE::Actor* sourceActor, float damage);

        RE::BSEventNotifyControl ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>* source) override;
        RE::BSEventNotifyControl ProcessEvent(
            const RE::TESMagicEffectApplyEvent* event,
            RE::BSTEventSource<RE::TESMagicEffectApplyEvent>* source) override;

    private:
        static constexpr std::size_t kMaxPendingHits = 512;

        struct PendingHit
        {
            RE::ObjectRefHandle target;
            RE::ObjectRefHandle source;
            float initialHealth{ 0.0F };
            float lowestHealth{ 0.0F };
            float estimatedDamage{ 0.0F };
            int samplesTaken{ 0 };
            Milliseconds createdMs{ 0.0 };
            Milliseconds nextSampleMs{ 0.0 };
            DamageType type{ DamageType::Normal };
            DamageContext context;
            WorldPoint impactPoint;
            bool hasImpactPoint{ false };
        };

        struct RecentDirectDamage
        {
            RE::ObjectRefHandle target;
            RE::ObjectRefHandle source;
            Milliseconds timestampMs{ 0.0 };
        };

        struct RecentMagicApplication
        {
            RE::ObjectRefHandle target;
            RE::ObjectRefHandle source;
            DamageType type{ DamageType::Magic };
            Milliseconds timestampMs{ 0.0 };
        };

        struct ActiveMagicSource
        {
            RE::ObjectRefHandle target;
            RE::ObjectRefHandle source;
            DamageType type{ DamageType::Magic };
            Milliseconds lastTouchedMs{ 0.0 };
            Milliseconds nextSampleMs{ 0.0 };
            float observedHealth{ 0.0F };
            bool hasObservedHealth{ false };
            WorldPoint impactPoint;
            bool hasImpactPoint{ false };
            float pendingDamage{ 0.0F };
        };

        struct RecentUnattributedDamage
        {
            RE::ObjectRefHandle target;
            float damage{ 0.0F };
            Milliseconds timestampMs{ 0.0 };
            WorldPoint impactPoint;
            bool hasImpactPoint{ false };
        };

        struct HitSample
        {
            float damage{ 0.0F };
            WorldPoint impactPoint;
            bool hasImpactPoint{ false };
        };

        [[nodiscard]] static RE::Actor* AsActor(RE::TESObjectREFR* ref) noexcept;
        [[nodiscard]] static float GetHealth(RE::Actor& actor) noexcept;
        [[nodiscard]] static DamageType DetectDamageType(const RE::EffectSetting* effect) noexcept;
        [[nodiscard]] DamageContext BuildContext(
            RE::Actor& target,
            RE::Actor* sourceActor,
            RE::ObjectRefHandle targetHandle,
            RE::ObjectRefHandle sourceHandle) const;
        [[nodiscard]] DamageType DetectDamageType(const RE::TESHitEvent& event) const noexcept;
        [[nodiscard]] HitSample ReadLastHitSample(
            RE::Actor& target,
            RE::ObjectRefHandle targetHandle,
            RE::ObjectRefHandle sourceHandle) const noexcept;
        [[nodiscard]] std::optional<RecentMagicApplication> FindRecentMagicApplication(
            RE::ObjectRefHandle target,
            RE::ObjectRefHandle source,
            Milliseconds nowMs) const noexcept;
        [[nodiscard]] ActiveMagicSource* FindActiveMagicSource(
            RE::ObjectRefHandle target,
            RE::ObjectRefHandle source,
            Milliseconds nowMs) noexcept;
        [[nodiscard]] std::optional<RecentUnattributedDamage> ConsumeRecentUnattributedDamage(
            RE::ObjectRefHandle target,
            Milliseconds nowMs) noexcept;
        [[nodiscard]] bool HasRecentPending(RE::ObjectRefHandle target, RE::ObjectRefHandle source, Milliseconds nowMs) const noexcept;
        [[nodiscard]] bool HasRecentDirectDamage(
            RE::ObjectRefHandle target,
            RE::ObjectRefHandle source,
            Milliseconds nowMs) const noexcept;
        [[nodiscard]] Milliseconds RecentDirectWindowMs() const noexcept;
        [[nodiscard]] Milliseconds RecentMagicWindowMs() const noexcept;
        [[nodiscard]] Milliseconds ActiveMagicWindowMs() const noexcept;
        void RememberDirectDamage(RE::ObjectRefHandle target, RE::ObjectRefHandle source, Milliseconds nowMs);
        void PruneRecentDirectDamage(Milliseconds nowMs);
        void RememberMagicApplication(
            RE::ObjectRefHandle target,
            RE::ObjectRefHandle source,
            DamageType type,
            Milliseconds nowMs);
        void PruneRecentMagicApplications(Milliseconds nowMs);
        ActiveMagicSource& RememberActiveMagicSource(
            RE::ObjectRefHandle target,
            RE::ObjectRefHandle source,
            DamageType type,
            float observedHealth,
            const HitSample& hitSample,
            Milliseconds nowMs);
        void UpdateActiveMagicObservedHealth(
            ActiveMagicSource& active,
            RE::Actor& target,
            float damage,
            const HitSample& hitSample,
            Milliseconds nowMs) noexcept;
        void PruneActiveMagicSources(Milliseconds nowMs);
        void SampleActiveMagicSources(Milliseconds nowMs);
        void RememberUnattributedDamage(
            RE::ObjectRefHandle target,
            float damage,
            const HitSample& hitSample,
            Milliseconds nowMs);
        void PruneRecentUnattributedDamage(Milliseconds nowMs);
        void Sample(PendingHit& pending, Milliseconds nowMs);

        Settings settings_;
        DamageCallback callback_;
        TimeProvider timeProvider_;
        std::vector<PendingHit> pending_;
        std::vector<RecentDirectDamage> recentDirectDamage_;
        std::vector<RecentMagicApplication> recentMagicApplications_;
        std::vector<ActiveMagicSource> activeMagicSources_;
        std::vector<RecentUnattributedDamage> recentUnattributedDamage_;
        bool registered_{ false };
    };
}
