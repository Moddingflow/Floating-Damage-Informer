#pragma once

#include "FDN/Settings.h"
#include "FDN/Types.h"

#include "RE/A/Actor.h"
#include "RE/G/GFxMovieView.h"

#include <optional>

namespace RE
{
    class NiCamera;
}

namespace FDN::Skyrim
{
    struct HUDProjectionContext
    {
        RE::NiCamera* camera{ nullptr };
        float left{ 0.0F };
        float top{ 0.0F };
        float right{ 0.0F };
        float bottom{ 0.0F };
    };

    [[nodiscard]] std::optional<WorldPoint> GetActorAnchorPoint(RE::Actor& actor, const Settings& settings);
    [[nodiscard]] std::optional<HUDProjectionContext> BuildHUDProjectionContext(RE::GFxMovieView& movie);
    [[nodiscard]] std::optional<ScreenPoint> ProjectWorldToHUD(
        const WorldPoint& point,
        const HUDProjectionContext& context,
        const Settings& settings);
    [[nodiscard]] std::optional<ScreenPoint> ProjectWorldToHUD(const WorldPoint& point, RE::GFxMovieView& movie, const Settings& settings);
}
