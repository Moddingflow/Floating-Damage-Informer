#pragma once

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace FDN
{
    using ActorKey = std::uint32_t;
    using Milliseconds = double;

    inline constexpr float kMaxDisplayableDamage = 1'000'000.0F;

    enum class DamageType : std::uint8_t
    {
        Normal,
        Magic,
        Fire,
        Frost,
        Shock,
        Poison,
        PlayerTaken
    };

    enum class SourceRelation : std::uint8_t
    {
        Unknown,
        Player,
        Follower,
        NPC
    };

    enum class TargetRelation : std::uint8_t
    {
        Unknown,
        HostileToPlayer,
        NonHostile,
        Player
    };

    struct Color
    {
        std::uint8_t a{ 255 };
        std::uint8_t r{ 255 };
        std::uint8_t g{ 255 };
        std::uint8_t b{ 255 };

        [[nodiscard]] constexpr std::uint32_t ToARGB() const noexcept
        {
            return (static_cast<std::uint32_t>(a) << 24) |
                   (static_cast<std::uint32_t>(r) << 16) |
                   (static_cast<std::uint32_t>(g) << 8) |
                   static_cast<std::uint32_t>(b);
        }

        [[nodiscard]] constexpr std::uint32_t ToRGB() const noexcept
        {
            return (static_cast<std::uint32_t>(r) << 16) |
                   (static_cast<std::uint32_t>(g) << 8) |
                   static_cast<std::uint32_t>(b);
        }

        [[nodiscard]] static constexpr Color FromARGB(std::uint32_t value) noexcept
        {
            return Color{
                static_cast<std::uint8_t>((value >> 24) & 0xFF),
                static_cast<std::uint8_t>((value >> 16) & 0xFF),
                static_cast<std::uint8_t>((value >> 8) & 0xFF),
                static_cast<std::uint8_t>(value & 0xFF)
            };
        }
    };

    struct DamageContext
    {
        ActorKey target{ 0 };
        ActorKey source{ 0 };
        SourceRelation sourceRelation{ SourceRelation::Unknown };
        TargetRelation targetRelation{ TargetRelation::Unknown };
        bool targetIsValidActor{ false };
        bool targetIsDead{ false };
        bool targetIsDisabled{ false };
        bool targetIsDeleted{ false };
        bool target3DLoaded{ true };
        bool playerTaken{ false };
    };

    struct WorldPoint
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float z{ 0.0F };
    };

    struct DamageEvent
    {
        ActorKey target{ 0 };
        ActorKey source{ 0 };
        float amount{ 0.0F };
        DamageType type{ DamageType::Normal };
        bool playerTaken{ false };
        Milliseconds timestampMs{ 0.0 };
        WorldPoint impactPoint;
        bool hasImpactPoint{ false };
    };

    struct ScreenPoint
    {
        float x{ 0.0F };
        float y{ 0.0F };
        float depth{ 0.0F };
        bool onScreen{ false };
        bool behindCamera{ false };
    };
}
