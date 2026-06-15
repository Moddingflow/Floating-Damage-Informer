#include "FDN/FloatingNumberManager.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace FDN
{
    FloatingNumberManager::FloatingNumberManager(Settings settings) :
        settings_(std::move(settings))
    {
        Normalize(settings_);
        pool_.resize(static_cast<std::size_t>(settings_.display.maxActiveNumbers));
        activeIndices_.reserve(pool_.size());
        updates_.reserve(pool_.size());
        activeByAggregateId_.reserve(pool_.size());
    }

    void FloatingNumberManager::Reconfigure(Settings settings)
    {
        settings_ = std::move(settings);
        Normalize(settings_);
        const auto targetSize = static_cast<std::size_t>(settings_.display.maxActiveNumbers);
        pool_.resize(targetSize);
        RebuildActiveIndexes();
        updates_.clear();
        updates_.reserve(targetSize);
    }

    std::optional<FloatingNumberUpdate> FloatingNumberManager::Submit(const AggregatedDamage& damage)
    {
        if (!settings_.ShouldShowDamage(damage.amount)) {
            return std::nullopt;
        }

        FloatingNumber* slot = damage.merged ? FindByAggregateId(damage.id) : nullptr;
        bool resetAnimation = false;
        if (slot == nullptr) {
            const auto allocation = AllocateSlot(damage.key.target);
            slot = allocation.slot;
            resetAnimation = allocation.resetAnimation;
        }
        if (slot == nullptr) {
            return std::nullopt;
        }

        const auto slotIndex = SlotIndex(*slot);
        if (!slot->active) {
            slot->serial = nextSerial_++;
            if (nextSerial_ == 0) {
                nextSerial_ = 1;
            }
            slot->driftSign = DriftForSerial(slot->serial);
            resetAnimation = true;
            ++activeCount_;
            activeIndices_.push_back(slotIndex);
        } else if (slot->aggregateId != damage.id && slot->aggregateId != 0) {
            activeByAggregateId_.erase(slot->aggregateId);
        }

        if (resetAnimation) {
            slot->ageSeconds = 0.0F;
            slot->lifetimeSeconds = settings_.display.lifetime;
        }

        slot->active = true;
        slot->key = damage.key;
        slot->target = damage.key.target;
        slot->aggregateId = damage.id;
        slot->amount = damage.amount;
        slot->type = damage.key.type;
        slot->color = settings_.ColorFor(slot->type, slot->amount);
        slot->text = settings_.FormatDamage(slot->amount);
        slot->impactPoint = damage.impactPoint;
        slot->hasImpactPoint = damage.hasImpactPoint;
        if (slot->aggregateId != 0) {
            activeByAggregateId_[slot->aggregateId] = slotIndex;
        }

        return MakeUpdate(*slot);
    }

    const std::vector<FloatingNumberUpdate>& FloatingNumberManager::Update(float deltaSeconds)
    {
        updates_.clear();
        if (activeCount_ == 0) {
            return updates_;
        }

        for (std::size_t cursor = 0; cursor < activeIndices_.size();) {
            const auto index = activeIndices_[cursor];
            if (index >= pool_.size() || !pool_[index].active) {
                activeIndices_[cursor] = activeIndices_.back();
                activeIndices_.pop_back();
                activeCount_ = activeIndices_.size();
                continue;
            }

            auto& number = pool_[index];
            number.ageSeconds += std::max(deltaSeconds, 0.0F);
            if (number.ageSeconds >= number.lifetimeSeconds) {
                updates_.push_back(MakeUpdate(number));
                if (number.aggregateId != 0) {
                    activeByAggregateId_.erase(number.aggregateId);
                }
                number.active = false;
                number.aggregateId = 0;
                activeIndices_[cursor] = activeIndices_.back();
                activeIndices_.pop_back();
                activeCount_ = activeIndices_.size();
                continue;
            }

            updates_.push_back(MakeUpdate(number));
            ++cursor;
        }

        return updates_;
    }

    void FloatingNumberManager::Clear() noexcept
    {
        if (activeCount_ == 0) {
            return;
        }
        for (const auto index : activeIndices_) {
            if (index < pool_.size()) {
                pool_[index].active = false;
                pool_[index].aggregateId = 0;
            }
        }
        activeIndices_.clear();
        activeByAggregateId_.clear();
        activeCount_ = 0;
    }

    std::size_t FloatingNumberManager::ActiveCount() const noexcept
    {
        return activeCount_;
    }

    FloatingNumber* FloatingNumberManager::FindByAggregateId(std::uint64_t aggregateId) noexcept
    {
        if (aggregateId == 0) {
            return nullptr;
        }

        const auto it = activeByAggregateId_.find(aggregateId);
        if (it == activeByAggregateId_.end() || it->second >= pool_.size()) {
            return nullptr;
        }

        auto& number = pool_[it->second];
        if (!number.active || number.aggregateId != aggregateId) {
            activeByAggregateId_.erase(it);
            return nullptr;
        }

        return std::addressof(number);
    }

    FloatingNumberManager::SlotAllocation FloatingNumberManager::AllocateSlot(ActorKey target) noexcept
    {
        FloatingNumber* freeSlot = nullptr;
        FloatingNumber* oldestForActor = nullptr;
        FloatingNumber* oldest = nullptr;
        std::size_t activeForActor = 0;

        for (auto& number : pool_) {
            if (!number.active) {
                if (freeSlot == nullptr) {
                    freeSlot = std::addressof(number);
                }
                continue;
            }

            if (oldest == nullptr || number.ageSeconds > oldest->ageSeconds) {
                oldest = std::addressof(number);
            }

            if (number.target == target) {
                ++activeForActor;
                if (oldestForActor == nullptr || number.ageSeconds > oldestForActor->ageSeconds) {
                    oldestForActor = std::addressof(number);
                }
            }
        }

        if (activeForActor >= static_cast<std::size_t>(settings_.display.maxNumbersPerActor)) {
            return SlotAllocation{ oldestForActor, oldestForActor != nullptr };
        }

        if (freeSlot != nullptr) {
            return SlotAllocation{ freeSlot, true };
        }

        return SlotAllocation{ oldest, oldest != nullptr };
    }

    std::size_t FloatingNumberManager::SlotIndex(const FloatingNumber& number) const noexcept
    {
        return static_cast<std::size_t>(std::addressof(number) - pool_.data());
    }

    float FloatingNumberManager::DriftForSerial(std::uint32_t serial) const noexcept
    {
        const auto hashed = serial * 1664525u + 1013904223u;
        const float normalized = static_cast<float>((hashed >> 8) & 0xFFFFu) / 65535.0F;
        return normalized < 0.5F ? -1.0F : 1.0F;
    }

    FloatingNumberUpdate FloatingNumberManager::MakeUpdate(const FloatingNumber& number) const
    {
        auto display = settings_.display;
        if (number.hasImpactPoint) {
            display.startVerticalOffset = display.impactPointVerticalOffset;
        }

        return FloatingNumberUpdate{
            number.serial,
            number.target,
            number.text,
            number.color,
            ComputeAnimationFrame(
                number.ageSeconds,
                number.lifetimeSeconds,
                number.driftSign,
                display,
                settings_.animation),
            number.impactPoint,
            number.hasImpactPoint
        };
    }

    void FloatingNumberManager::RebuildActiveIndexes()
    {
        activeIndices_.clear();
        activeByAggregateId_.clear();
        activeIndices_.reserve(pool_.size());
        activeByAggregateId_.reserve(pool_.size());

        for (std::size_t index = 0; index < pool_.size(); ++index) {
            auto& number = pool_[index];
            if (!number.active) {
                number.aggregateId = 0;
                continue;
            }

            activeIndices_.push_back(index);
            if (number.aggregateId != 0) {
                activeByAggregateId_[number.aggregateId] = index;
            }
        }

        activeCount_ = activeIndices_.size();
    }

    bool ShouldDisplayContext(const DamageContext& context, const Settings& settings) noexcept
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

        if (context.targetRelation != TargetRelation::HostileToPlayer && !settings.general.showNonHostileTargets) {
            return false;
        }

        switch (context.sourceRelation) {
        case SourceRelation::Player:
            return settings.general.showPlayerDealtDamage;
        case SourceRelation::Follower:
            return settings.general.showFollowerDealtDamage;
        case SourceRelation::NPC:
            return settings.general.showNPCvsNPCDamage;
        case SourceRelation::Unknown:
        default:
            return false;
        }
    }
}
