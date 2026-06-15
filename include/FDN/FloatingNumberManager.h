#pragma once

#include "FDN/Animation.h"
#include "FDN/DamageAggregator.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace FDN
{
    struct FloatingNumber
    {
        bool active{ false };
        AggregationKey key;
        ActorKey target{ 0 };
        std::uint64_t aggregateId{ 0 };
        float amount{ 0.0F };
        DamageType type{ DamageType::Normal };
        Color color;
        std::string text;
        WorldPoint impactPoint;
        bool hasImpactPoint{ false };
        float ageSeconds{ 0.0F };
        float lifetimeSeconds{ 1.25F };
        float driftSign{ 1.0F };
        std::uint32_t serial{ 0 };
    };

    struct FloatingNumberUpdate
    {
        std::uint32_t serial{ 0 };
        ActorKey target{ 0 };
        std::string_view text;
        Color color;
        AnimationFrame animation;
        WorldPoint impactPoint;
        bool hasImpactPoint{ false };
    };

    class FloatingNumberManager
    {
    public:
        explicit FloatingNumberManager(Settings settings = {});

        void Reconfigure(Settings settings);
        [[nodiscard]] std::optional<FloatingNumberUpdate> Submit(const AggregatedDamage& damage);
        [[nodiscard]] const std::vector<FloatingNumberUpdate>& Update(float deltaSeconds);
        void Clear() noexcept;

        [[nodiscard]] std::size_t ActiveCount() const noexcept;
        [[nodiscard]] const std::vector<FloatingNumber>& Pool() const noexcept { return pool_; }

    private:
        struct SlotAllocation
        {
            FloatingNumber* slot{ nullptr };
            bool resetAnimation{ false };
        };

        [[nodiscard]] FloatingNumber* FindByAggregateId(std::uint64_t aggregateId) noexcept;
        [[nodiscard]] SlotAllocation AllocateSlot(ActorKey target) noexcept;
        [[nodiscard]] std::size_t SlotIndex(const FloatingNumber& number) const noexcept;
        [[nodiscard]] float DriftForSerial(std::uint32_t serial) const noexcept;
        [[nodiscard]] FloatingNumberUpdate MakeUpdate(const FloatingNumber& number) const;
        void RebuildActiveIndexes();

        Settings settings_;
        std::vector<FloatingNumber> pool_;
        std::vector<std::size_t> activeIndices_;
        std::vector<FloatingNumberUpdate> updates_;
        std::unordered_map<std::uint64_t, std::size_t> activeByAggregateId_;
        std::size_t activeCount_{ 0 };
        std::uint32_t nextSerial_{ 1 };
    };

    [[nodiscard]] bool ShouldDisplayContext(const DamageContext& context, const Settings& settings) noexcept;
}
