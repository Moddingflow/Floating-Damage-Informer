#include "FDN/Skyrim/WorldToScreen.h"

#include "RE/B/BSFixedString.h"
#include "RE/F/FixedStrings.h"
#include "RE/G/GViewport.h"
#include "RE/M/Main.h"
#include "RE/N/NiAVObject.h"
#include "RE/N/NiCamera.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace FDN::Skyrim
{
    namespace
    {
        struct BoundAnchor
        {
            WorldPoint center;
            float radius{ 0.0F };
        };

        struct HudRect
        {
            float left{ 0.0F };
            float top{ 0.0F };
            float right{ 0.0F };
            float bottom{ 0.0F };
        };

        [[nodiscard]] RE::NiCamera* GetWorldCamera() noexcept
        {
            return RE::Main::WorldRootCamera();
        }

        [[nodiscard]] bool IsFinite(float value) noexcept
        {
            return std::isfinite(value);
        }

        [[nodiscard]] bool IsFinite(const WorldPoint& point) noexcept
        {
            return IsFinite(point.x) && IsFinite(point.y) && IsFinite(point.z);
        }

        [[nodiscard]] WorldPoint ToWorldPoint(const RE::NiPoint3& point) noexcept
        {
            return WorldPoint{ point.x, point.y, point.z };
        }

        [[nodiscard]] bool IsUsableRect(const HudRect& rect) noexcept
        {
            return IsFinite(rect.left) &&
                   IsFinite(rect.top) &&
                   IsFinite(rect.right) &&
                   IsFinite(rect.bottom) &&
                   rect.right > rect.left + 1.0F &&
                   rect.bottom > rect.top + 1.0F;
        }

        [[nodiscard]] std::optional<HudRect> GetHUDStageRect(RE::GFxMovieView& movie)
        {
            const auto visible = movie.GetVisibleFrameRect();
            const HudRect visibleRect{
                visible.left,
                visible.top,
                visible.right,
                visible.bottom
            };
            if (IsUsableRect(visibleRect)) {
                return visibleRect;
            }

            RE::GViewport viewport;
            movie.GetViewport(&viewport);
            const HudRect viewportRect{
                0.0F,
                0.0F,
                static_cast<float>(std::max(viewport.width, 0)),
                static_cast<float>(std::max(viewport.height, 0))
            };
            if (IsUsableRect(viewportRect)) {
                return viewportRect;
            }

            return std::nullopt;
        }

        [[nodiscard]] std::optional<BoundAnchor> GetBoundAnchor(RE::NiAVObject& root) noexcept
        {
            const auto& bound = root.worldBound;
            const WorldPoint center = ToWorldPoint(bound.center);
            if (!IsFinite(center) || !IsFinite(bound.radius) || bound.radius <= 1.0F || bound.radius > 10000.0F) {
                return std::nullopt;
            }

            return BoundAnchor{ center, bound.radius };
        }

        [[nodiscard]] bool IsNearBoundAnchor(const WorldPoint& point, const BoundAnchor& bound) noexcept
        {
            const float dx = point.x - bound.center.x;
            const float dy = point.y - bound.center.y;
            const float dz = point.z - bound.center.z;
            const float allowedDistance = std::clamp(bound.radius + 64.0F, 96.0F, 2048.0F);
            return (dx * dx + dy * dy + dz * dz) <= allowedDistance * allowedDistance;
        }

        [[nodiscard]] bool IsNearBoundCenterline(const WorldPoint& point, const BoundAnchor& bound) noexcept
        {
            const float dx = point.x - bound.center.x;
            const float dy = point.y - bound.center.y;
            const float allowedDistance = std::clamp(bound.radius * 0.55F, 24.0F, 192.0F);
            return (dx * dx + dy * dy) <= allowedDistance * allowedDistance;
        }

        [[nodiscard]] WorldPoint GetVisualActorAnchor(
            const BoundAnchor& bound,
            const std::optional<WorldPoint>& upperBodyNode) noexcept
        {
            WorldPoint anchor = bound.center;
            const float baseBias = std::clamp(bound.radius * 0.18F, 8.0F, 42.0F);
            anchor.z += baseBias;

            if (!upperBodyNode || !IsNearBoundCenterline(*upperBodyNode, bound)) {
                return anchor;
            }

            anchor.x = upperBodyNode->x;
            anchor.y = upperBodyNode->y;
            if (upperBodyNode->z > bound.center.z) {
                const float nodeBias = std::clamp(
                    (upperBodyNode->z - bound.center.z) * 0.45F,
                    baseBias,
                    std::clamp(bound.radius * 0.55F, 24.0F, 96.0F));
                anchor.z = bound.center.z + nodeBias;
            }

            return anchor;
        }

        [[nodiscard]] std::optional<WorldPoint> GetNodeWorldPoint(
            RE::NiAVObject& root,
            const RE::BSFixedString& nodeName,
            const std::optional<BoundAnchor>& bound) noexcept
        {
            if (nodeName.empty()) {
                return std::nullopt;
            }

            auto* node = root.GetObjectByName(nodeName);
            if (!node) {
                return std::nullopt;
            }

            const auto point = ToWorldPoint(node->world.translate);
            if (!IsFinite(point) || (bound && !IsNearBoundAnchor(point, *bound))) {
                return std::nullopt;
            }

            return point;
        }

        [[nodiscard]] std::optional<WorldPoint> GetPreferredActorNodeAnchor(
            RE::NiAVObject& root,
            const std::optional<BoundAnchor>& bound)
        {
            if (const auto* fixedStrings = RE::FixedStrings::GetSingleton(); fixedStrings) {
                const std::array<const RE::BSFixedString*, 5> nodeNames{
                    &fixedStrings->npcHead,
                    &fixedStrings->npcHeadMagicNode,
                    &fixedStrings->npcNeck,
                    &fixedStrings->npcSpine2,
                    &fixedStrings->npcSpine1
                };

                for (const auto* nodeName : nodeNames) {
                    if (auto point = GetNodeWorldPoint(root, *nodeName, bound); point) {
                        return point;
                    }
                }
            }

            static const std::array<const char*, 9> kFallbackNodeNames{
                "NPCHead[Head]",
                "NPC Head [Head]",
                "NPC Head",
                "NPCHead",
                "NPCNeck[Neck]",
                "NPC Neck [Neck]",
                "NPCSpine2[Spn2]",
                "NPC Spine2 [Spn2]",
                "NPC Spine2"
            };

            for (const auto* nodeName : kFallbackNodeNames) {
                const RE::BSFixedString fixedName{ nodeName };
                if (auto point = GetNodeWorldPoint(root, fixedName, bound); point) {
                    return point;
                }
            }

            return std::nullopt;
        }

        [[nodiscard]] WorldPoint GetActorPositionAnchor(RE::Actor& actor, const Settings& settings) noexcept
        {
            return WorldPoint{
                actor.GetPositionX(),
                actor.GetPositionY(),
                actor.GetPositionZ() + settings.display.worldZOffset
            };
        }

        [[nodiscard]] bool ProjectWithUnitPort(
            RE::NiCamera& camera,
            const RE::NiPoint3& point,
            float& x,
            float& y,
            float& z)
        {
            static const RE::NiRect<float> kUnitPort{ 0.0F, 1.0F, 1.0F, 0.0F };
            auto port = camera.GetRuntimeData2().port;
            if (!IsFinite(port.GetWidth()) || !IsFinite(port.GetHeight()) || port.GetWidth() <= 0.0F || port.GetHeight() <= 0.0F) {
                port = kUnitPort;
            }

            return RE::NiCamera::WorldPtToScreenPt3(
                camera.GetRuntimeData().worldToCam,
                port,
                point,
                x,
                y,
                z,
                1.0e-5F);
        }
    }

    std::optional<WorldPoint> GetActorAnchorPoint(RE::Actor& actor, const Settings& settings)
    {
        if (auto* root = actor.Get3D(); root) {
            const auto bound = GetBoundAnchor(*root);
            std::optional<WorldPoint> upperBodyNode;
            if (settings.display.anchorToHead) {
                upperBodyNode = GetPreferredActorNodeAnchor(*root, bound);
            }

            if (bound) {
                return GetVisualActorAnchor(*bound, upperBodyNode);
            }

            if (upperBodyNode) {
                return upperBodyNode;
            }
        }

        return GetActorPositionAnchor(actor, settings);
    }

    std::optional<HUDProjectionContext> BuildHUDProjectionContext(RE::GFxMovieView& movie)
    {
        auto* camera = GetWorldCamera();
        if (!camera) {
            return std::nullopt;
        }

        const auto rect = GetHUDStageRect(movie);
        if (!rect) {
            return std::nullopt;
        }

        return HUDProjectionContext{
            camera,
            rect->left,
            rect->top,
            rect->right,
            rect->bottom
        };
    }

    std::optional<ScreenPoint> ProjectWorldToHUD(
        const WorldPoint& point,
        const HUDProjectionContext& context,
        const Settings& settings)
    {
        auto* camera = context.camera;
        if (!camera) {
            return std::nullopt;
        }

        if (settings.display.maxDistance > 0.0F) {
            const auto& cameraPosition = camera->world.translate;
            const float dx = point.x - cameraPosition.x;
            const float dy = point.y - cameraPosition.y;
            const float dz = point.z - cameraPosition.z;
            const float maxDistanceSquared = settings.display.maxDistance * settings.display.maxDistance;
            if ((dx * dx + dy * dy + dz * dz) > maxDistanceSquared) {
                return ScreenPoint{ 0.0F, 0.0F, 0.0F, false, false };
            }
        }

        float x = 0.0F;
        float y = 0.0F;
        float z = 0.0F;
        const RE::NiPoint3 niPoint{ point.x, point.y, point.z };

        if (!ProjectWithUnitPort(*camera, niPoint, x, y, z) || !IsFinite(x) || !IsFinite(y) || !IsFinite(z)) {
            return ScreenPoint{ 0.0F, 0.0F, z, false, true };
        }

        const bool behind = z < 0.0F;
        if (behind && settings.display.hideBehindCamera) {
            return ScreenPoint{ 0.0F, 0.0F, z, false, true };
        }

        const float width = context.right - context.left;
        const float height = context.bottom - context.top;

        float screenX = context.left + x * width;
        float screenY = context.top + y * height;
        bool onScreen = screenX >= context.left && screenX <= context.right && screenY >= context.top && screenY <= context.bottom && !behind;

        if (!onScreen && settings.display.clampToScreen) {
            screenX = std::clamp(screenX, context.left, context.right);
            screenY = std::clamp(screenY, context.top, context.bottom);
            onScreen = !behind;
        }

        return ScreenPoint{ screenX, screenY, z, onScreen, behind };
    }

    std::optional<ScreenPoint> ProjectWorldToHUD(const WorldPoint& point, RE::GFxMovieView& movie, const Settings& settings)
    {
        const auto context = BuildHUDProjectionContext(movie);
        if (!context) {
            return std::nullopt;
        }

        return ProjectWorldToHUD(point, *context, settings);
    }
}
