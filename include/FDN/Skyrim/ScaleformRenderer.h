#pragma once

#include "FDN/Skyrim/Renderer.h"
#include "FDN/Skyrim/WorldToScreen.h"

#include "RE/G/GPtr.h"
#include "RE/G/GFxValue.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace FDN::Skyrim
{
    class ScaleformRenderer final : public IRenderer
    {
    public:
        [[nodiscard]] bool Initialize(const Settings& settings) override;
        [[nodiscard]] bool BeginFrame(RE::HUDMenu& hudMenu) override;
        void Render(const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle) override;
        void Clear() override;
        [[nodiscard]] bool IsAvailable() const noexcept override { return available_; }

    private:
        struct Slot
        {
            bool created{ false };
            bool active{ false };
            bool visible{ false };
            bool contentApplied{ false };
            bool usesObjectApi{ false };
            std::uint32_t serial{ 0 };
            ActorKey target{ 0 };
            Color lastColor;
            float lastX{ std::numeric_limits<float>::quiet_NaN() };
            float lastY{ std::numeric_limits<float>::quiet_NaN() };
            float lastXScale{ std::numeric_limits<float>::quiet_NaN() };
            float lastYScale{ std::numeric_limits<float>::quiet_NaN() };
            float lastAlpha{ std::numeric_limits<float>::quiet_NaN() };
            std::string name;
            std::string path;
            std::string lastText;
            std::string textPath;
            std::string xPath;
            std::string yPath;
            std::string widthPath;
            std::string heightPath;
            std::string xScalePath;
            std::string yScalePath;
            std::string alphaPath;
            std::string visiblePath;
            std::string selectablePath;
            std::string mouseEnabledPath;
            std::string htmlPath;
            std::string embedFontsPath;
            std::string textColorPath;
            std::string autoSizePath;
            std::string filtersPath;
            RE::GFxValue object;
        };

        [[nodiscard]] Slot* SlotFor(std::uint32_t serial);
        [[nodiscard]] Slot* AllocateSlot(std::uint32_t serial);
        [[nodiscard]] std::size_t SlotIndex(const Slot& slot) const noexcept;
        bool ResolveRoot(RE::HUDMenu& hudMenu);
        bool EnsureSlotCreated(Slot& slot);
        void ResetMovieObjects();
        bool EnsureLayer();
        void ConfigureLayer();
        bool CreateSlot(std::size_t index, Slot& slot);
        bool CreateTextField(RE::GFxValue& parent, std::size_t depth, Slot& slot);
        bool CreateTextField(std::string_view parentPath, std::size_t depth, Slot& slot);
        void RefreshSlotPaths(Slot& slot);
        void ConfigureTextField(Slot& slot);
        void ApplyTextFieldGeometry(Slot& slot);
        void HideSlot(Slot& slot);
        void ReleaseSlot(Slot& slot);
        void ApplyTextIfChanged(Slot& slot, const FloatingNumberUpdate& update);
        void ApplyTextFormat(Slot& slot, Color color);
        void ApplyTextFilters(Slot& slot);
        void ApplySlot(Slot& slot, const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle);
        void ResetSlotCachedState(Slot& slot) noexcept;
        void SetSlotVisible(Slot& slot, bool visible);
        void SetDisplayInfoIfChanged(Slot& slot, float x, float y, float scale, float alpha, bool visible);
        void SetVariableDoubleIfChanged(const std::string& path, float& cachedValue, float value);
        [[nodiscard]] bool BuildTextFormat(Color color, RE::GFxValue& format) const;
        [[nodiscard]] bool BuildTextShadowFilters(RE::GFxValue& filters) const;
        [[nodiscard]] std::string ResolveFontFace() const;

        Settings settings_;
        RE::GPtr<RE::GFxMovieView> movie_;
        RE::GFxValue root_;
        RE::GFxValue layer_;
        std::optional<HUDProjectionContext> projection_;
        std::vector<Slot> slots_;
        std::unordered_map<std::uint32_t, std::size_t> serialToSlot_;
        int createFailures_{ 0 };
        bool layerCreated_{ false };
        bool available_{ false };
    };
}
