#pragma once

#include "FDN/Settings.h"

namespace FDN
{
    struct AnimationFrame
    {
        float normalizedTime{ 0.0F };
        float offsetY{ 0.0F };
        float driftX{ 0.0F };
        float alpha{ 1.0F };
        float scale{ 1.0F };
        bool alive{ true };
    };

    [[nodiscard]] float Clamp01(float value) noexcept;
    [[nodiscard]] float EaseOutCubic(float value) noexcept;
    [[nodiscard]] float EaseOutBack(float value) noexcept;
    [[nodiscard]] AnimationFrame ComputeAnimationFrame(
        float ageSeconds,
        float lifetimeSeconds,
        float driftSign,
        const DisplaySettings& display,
        const AnimationSettings& animation) noexcept;
}
