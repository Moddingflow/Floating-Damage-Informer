#pragma once

#include "FDN/Types.h"

#include <cstddef>
#include <optional>
#include <unordered_map>
#include <vector>

namespace FDN
{
    struct AggregationKey
    {
        ActorKey target{ 0 };
        ActorKey source{ 0 };
        DamageType type{ DamageType::Normal };
        bool playerTaken{ false };

        [[nodiscard]] friend constexpr bool operator==(const AggregationKey& lhs, const AggregationKey& rhs) noexcept
        {
            return lhs.target == rhs.target &&
                   lhs.source == rhs.source &&
                   lhs.type == rhs.type &&
                   lhs.playerTaken == rhs.playerTaken;
        }
    };

    struct AggregationKeyHash
    {
        [[nodiscard]] std::size_t operator()(const AggregationKey& key) const noexcept
        {
            std::size_t seed = static_cast<std::size_t>(key.target);
            const auto combine = [&seed](std::size_t value) noexcept {
                seed ^= value + 0x9E3779B97F4A7C15ull + (seed << 6u) + (seed >> 2u);
            };

            combine(static_cast<std::size_t>(key.source));
            combine(static_cast<std::size_t>(key.type));
            combine(key.playerTaken ? 1u : 0u);
            return seed;
        }
    };

    struct AggregatedDamage
    {
        AggregationKey key;
        float amount{ 0.0F };
        Milliseconds firstTimestampMs{ 0.0 };
        Milliseconds lastTimestampMs{ 0.0 };
        std::uint64_t id{ 0 };
        bool merged{ false };
        WorldPoint impactPoint;
        bool hasImpactPoint{ false };
    };

    class DamageAggregator
    {
    public:
        explicit DamageAggregator(float mergeWindowMs = 75.0F);

        void SetMergeWindow(float mergeWindowMs) noexcept;
        [[nodiscard]] AggregatedDamage Submit(const DamageEvent& event);
        void ClearExpired(Milliseconds nowMs);
        void Clear() noexcept;
        [[nodiscard]] std::size_t ActiveCount() const noexcept { return active_.size(); }

    private:
        static constexpr std::size_t kMaxActiveAggregates = 2048;

        [[nodiscard]] std::uint64_t NextAggregateId() noexcept;
        void RemoveAt(std::size_t index) noexcept;

        float mergeWindowMs_{ 75.0F };
        std::vector<AggregatedDamage> active_;
        std::unordered_map<AggregationKey, std::size_t, AggregationKeyHash> indexByKey_;
        std::uint64_t nextAggregateId_{ 1 };
    };
}
