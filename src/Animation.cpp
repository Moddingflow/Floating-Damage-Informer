#include "FDN/Animation.h"

#include <algorithm>
#include <cmath>

namespace FDN
{
    float Clamp01(float value) noexcept
    {
        return std::clamp(value, 0.0F, 1.0F);
    }

    float EaseOutCubic(float value) noexcept
    {
        const float t = 1.0F - Clamp01(value);
        return 1.0F - (t * t * t);
    }

    float EaseOutBack(float value) noexcept
    {
        const float t = Clamp01(value) - 1.0F;
        constexpr float c1 = 1.70158F;
        constexpr float c3 = c1 + 1.0F;
        return 1.0F + c3 * t * t * t + c1 * t * t;
    }

    AnimationFrame ComputeAnimationFrame(
        float ageSeconds,
        float lifetimeSeconds,
        float driftSign,
        const DisplaySettings& display,
        const AnimationSettings& animation) noexcept
    {
        AnimationFrame frame;
        const float safeLifetime = std::max(lifetimeSeconds, 0.001F);
        frame.normalizedTime = Clamp01(ageSeconds / safeLifetime);
        frame.alive = ageSeconds < safeLifetime;

        const float eased = EaseOutCubic(frame.normalizedTime);
        frame.offsetY = display.startVerticalOffset + display.risePixels * eased;
        frame.driftX = driftSign * display.randomDriftPixels * eased;

        if (!animation.effectsEnabled) {
            frame.alpha = 1.0F;
            frame.scale = 1.0F;
            return frame;
        }

        frame.alpha = animation.fadeOut ? 1.0F - frame.normalizedTime : 1.0F;
        frame.alpha = Clamp01(frame.alpha);

        frame.scale = animation.endScale;
        if (animation.popAnimation) {
            const float popDuration = std::max(animation.popDurationMs / 1000.0F, 0.001F);
            const float popT = Clamp01(ageSeconds / popDuration);
            const float pop = EaseOutBack(popT);
            frame.scale = animation.startScale + (animation.endScale - animation.startScale) * pop;
        }

        if (animation.bounceAnimation) {
            frame.offsetY += std::sin(frame.normalizedTime * 3.14159265F) * 8.0F;
        }

        return frame;
    }
}
