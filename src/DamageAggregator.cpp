#include "FDN/DamageAggregator.h"

#include <algorithm>
#include <cmath>

namespace FDN
{
    DamageAggregator::DamageAggregator(float mergeWindowMs) :
        mergeWindowMs_(std::max(0.0F, mergeWindowMs))
    {
        active_.reserve(kMaxActiveAggregates);
        indexByKey_.reserve(kMaxActiveAggregates);
    }

    void DamageAggregator::SetMergeWindow(float mergeWindowMs) noexcept
    {
        mergeWindowMs_ = std::max(0.0F, mergeWindowMs);
    }

    AggregatedDamage DamageAggregator::Submit(const DamageEvent& event)
    {
        const float amount = std::isfinite(event.amount) ? std::clamp(event.amount, -kMaxDisplayableDamage, kMaxDisplayableDamage) : 0.0F;
        const AggregationKey key{
            event.target,
            event.source,
            event.type,
            event.playerTaken
        };

        if (const auto it = indexByKey_.find(key); it != indexByKey_.end() && it->second < active_.size()) {
            auto& active = active_[it->second];
            const auto elapsedMs = event.timestampMs - active.lastTimestampMs;
            if (active.key == key && elapsedMs >= 0.0 && elapsedMs <= mergeWindowMs_) {
                active.amount = std::clamp(active.amount + amount, -kMaxDisplayableDamage, kMaxDisplayableDamage);
                active.lastTimestampMs = event.timestampMs;
                active.merged = true;
                if (event.hasImpactPoint) {
                    active.impactPoint = event.impactPoint;
                    active.hasImpactPoint = true;
                }
                return active;
            }

            active = AggregatedDamage{
                key,
                amount,
                event.timestampMs,
                event.timestampMs,
                NextAggregateId(),
                false,
                event.impactPoint,
                event.hasImpactPoint
            };
            return active;
        }

        if (active_.size() >= kMaxActiveAggregates) {
            const auto oldest = std::ranges::min_element(active_, {}, &AggregatedDamage::lastTimestampMs);
            if (oldest != active_.end()) {
                RemoveAt(static_cast<std::size_t>(oldest - active_.begin()));
            }
        }

        AggregatedDamage fresh{
            key,
            amount,
            event.timestampMs,
            event.timestampMs,
            NextAggregateId(),
            false,
            event.impactPoint,
            event.hasImpactPoint
        };
        active_.push_back(fresh);
        indexByKey_[key] = active_.size() - 1u;
        return fresh;
    }

    void DamageAggregator::ClearExpired(Milliseconds nowMs)
    {
        if (active_.empty()) {
            return;
        }

        for (std::size_t index = 0; index < active_.size();) {
            if (nowMs - active_[index].lastTimestampMs > mergeWindowMs_) {
                RemoveAt(index);
                continue;
            }
            ++index;
        }
    }

    void DamageAggregator::Clear() noexcept
    {
        active_.clear();
        indexByKey_.clear();
        nextAggregateId_ = 1;
    }

    std::uint64_t DamageAggregator::NextAggregateId() noexcept
    {
        const auto id = nextAggregateId_++;
        if (nextAggregateId_ == 0) {
            nextAggregateId_ = 1;
        }
        return id;
    }

    void DamageAggregator::RemoveAt(std::size_t index) noexcept
    {
        if (index >= active_.size()) {
            return;
        }

        indexByKey_.erase(active_[index].key);

        const auto lastIndex = active_.size() - 1u;
        if (index != lastIndex) {
            active_[index] = active_[lastIndex];
            if (const auto moved = indexByKey_.find(active_[index].key); moved != indexByKey_.end()) {
                moved->second = index;
            }
        }
        active_.pop_back();
    }
}
