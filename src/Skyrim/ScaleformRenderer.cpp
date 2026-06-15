#include "FDN/Skyrim/ScaleformRenderer.h"

#include "FDN/Skyrim/WorldToScreen.h"

#include "RE/A/Actor.h"
#include "RE/G/GFxMovieView.h"

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <spdlog/spdlog.h>

namespace FDN::Skyrim
{
    namespace
    {
        constexpr double kTextFieldWidth = 260.0;
        constexpr double kTextFieldHeight = 80.0;
        constexpr std::string_view kLayerName = "FDN_DamageLayer";
        constexpr std::string_view kLayerPath = "_root.FDN_DamageLayer";
        constexpr std::size_t kRootSlotDepthBase = 9000;
        constexpr std::size_t kLayerSlotDepthBase = 1;
        constexpr int kMaxCreateFailuresBeforeDisable = 3;
        constexpr std::string_view kDefaultSkyrimFontFace = "$EverywhereFont";
        constexpr std::string_view kTextFormatClass = "TextFormat";
        constexpr std::string_view kFlashTextFormatClass = "flash.text.TextFormat";
        constexpr std::string_view kDropShadowFilterClass = "DropShadowFilter";
        constexpr std::string_view kFlashDropShadowFilterClass = "flash.filters.DropShadowFilter";
        constexpr double kShadowDistance = 1.5;
        constexpr double kShadowAngle = 45.0;
        constexpr double kShadowColor = 0x000000;
        constexpr double kShadowAlpha = 1.0;
        constexpr double kShadowBlur = 0.0;
        constexpr double kShadowStrength = 1.0;
        constexpr double kShadowQuality = 1.0;

        [[nodiscard]] RE::Actor* ResolveActor(RE::ObjectRefHandle handle)
        {
            auto ref = handle.get();
            return ref ? ref->As<RE::Actor>() : nullptr;
        }

        [[nodiscard]] float TextVisualHeight(const Settings& settings) noexcept
        {
            return std::clamp(settings.font.fontSize * 1.35F, 24.0F, static_cast<float>(kTextFieldHeight));
        }
    }

    bool ScaleformRenderer::Initialize(const Settings& settings)
    {
        settings_ = settings;
        slots_.resize(static_cast<std::size_t>(settings_.display.maxActiveNumbers));
        serialToSlot_.clear();
        serialToSlot_.reserve(slots_.size());
        available_ = true;
        return true;
    }

    bool ScaleformRenderer::BeginFrame(RE::HUDMenu& hudMenu)
    {
        if (!available_) {
            return false;
        }

        auto nextMovie = hudMenu.uiMovie;
        if (!nextMovie) {
            movie_.reset();
            projection_.reset();
            return false;
        }

        if (nextMovie != movie_) {
            movie_ = std::move(nextMovie);
            ResetMovieObjects();
        }

        if (!ResolveRoot(hudMenu)) {
            return false;
        }

        projection_ = BuildHUDProjectionContext(*movie_);
        return true;
    }

    void ScaleformRenderer::Render(const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle)
    {
        if (!movie_) {
            return;
        }

        auto* slot = SlotFor(update.serial);
        if (!update.animation.alive) {
            if (slot) {
                ReleaseSlot(*slot);
            }
            serialToSlot_.erase(update.serial);
            return;
        }

        if (!slot) {
            slot = AllocateSlot(update.serial);
        }
        if (!slot) {
            return;
        }

        if (!EnsureSlotCreated(*slot)) {
            return;
        }

        ApplySlot(*slot, update, targetHandle);
    }

    void ScaleformRenderer::Clear()
    {
        for (auto& slot : slots_) {
            ReleaseSlot(slot);
        }
        serialToSlot_.clear();
    }

    ScaleformRenderer::Slot* ScaleformRenderer::SlotFor(std::uint32_t serial)
    {
        const auto it = serialToSlot_.find(serial);
        if (it == serialToSlot_.end() || it->second >= slots_.size()) {
            return nullptr;
        }
        auto& slot = slots_[it->second];
        if (!slot.active || slot.serial != serial) {
            serialToSlot_.erase(it);
            return nullptr;
        }
        return std::addressof(slot);
    }

    ScaleformRenderer::Slot* ScaleformRenderer::AllocateSlot(std::uint32_t serial)
    {
        auto freeSlot = std::ranges::find_if(slots_, [](const Slot& slot) {
            return !slot.active;
        });
        if (freeSlot == slots_.end()) {
            freeSlot = slots_.begin();
        }
        if (freeSlot == slots_.end()) {
            return nullptr;
        }

        serialToSlot_.erase(freeSlot->serial);
        HideSlot(*freeSlot);
        freeSlot->active = false;
        if (!EnsureSlotCreated(*freeSlot)) {
            return nullptr;
        }

        const auto index = SlotIndex(*freeSlot);
        freeSlot->active = true;
        freeSlot->serial = serial;
        freeSlot->target = 0;
        freeSlot->contentApplied = false;
        ResetSlotCachedState(*freeSlot);
        serialToSlot_[serial] = index;
        return std::addressof(*freeSlot);
    }

    std::size_t ScaleformRenderer::SlotIndex(const Slot& slot) const noexcept
    {
        return static_cast<std::size_t>(std::addressof(slot) - slots_.data());
    }

    bool ScaleformRenderer::ResolveRoot(RE::HUDMenu& hudMenu)
    {
        if (movie_ && movie_->GetVariable(std::addressof(root_), "_root") && root_.IsDisplayObject()) {
            return true;
        }

        const auto& runtimeData = hudMenu.GetRuntimeData();
        if (runtimeData.root.IsDisplayObject()) {
            root_ = runtimeData.root;
            return true;
        }

        root_.SetUndefined();
        return false;
    }

    bool ScaleformRenderer::EnsureSlotCreated(Slot& slot)
    {
        if (slot.created) {
            return true;
        }

        const auto index = SlotIndex(slot);
        if (CreateSlot(index, slot)) {
            createFailures_ = 0;
            return true;
        }

        ++createFailures_;
        spdlog::warn("Failed to create Scaleform text slot {} (failure {}/{})", index, createFailures_, kMaxCreateFailuresBeforeDisable);
        if (settings_.renderer.failSafeDisableRendererOnError && createFailures_ >= kMaxCreateFailuresBeforeDisable) {
            available_ = false;
            spdlog::error("Scaleform renderer disabled after repeated text field creation failures");
        }
        return false;
    }

    void ScaleformRenderer::ResetMovieObjects()
    {
        layerCreated_ = false;
        createFailures_ = 0;
        root_.SetUndefined();
        layer_.SetUndefined();
        projection_.reset();
        for (auto& slot : slots_) {
            slot.created = false;
            slot.visible = false;
            slot.contentApplied = false;
            slot.usesObjectApi = false;
            slot.object.SetUndefined();
            ResetSlotCachedState(slot);
        }
    }

    bool ScaleformRenderer::EnsureLayer()
    {
        if (!movie_) {
            return false;
        }
        if (layerCreated_) {
            return true;
        }

        if (root_.IsDisplayObject()) {
            if (root_.GetMember(kLayerName.data(), std::addressof(layer_)) && layer_.IsDisplayObject()) {
                ConfigureLayer();
                layerCreated_ = true;
                return true;
            }

            if (root_.CreateEmptyMovieClip(
                    std::addressof(layer_),
                    kLayerName.data(),
                    static_cast<std::int32_t>(kRootSlotDepthBase - 1u)) &&
                layer_.IsDisplayObject()) {
                ConfigureLayer();
                layerCreated_ = true;
                return true;
            }
        }

        if (movie_->IsAvailable(kLayerPath.data())) {
            ConfigureLayer();
            layerCreated_ = true;
            return true;
        }

        RE::GFxValue args[2];
        movie_->CreateString(std::addressof(args[0]), kLayerName.data());
        args[1] = RE::GFxValue(static_cast<double>(kRootSlotDepthBase - 1));

        if (!movie_->Invoke("_root.createEmptyMovieClip", nullptr, args, 2)) {
            return false;
        }

        movie_->SetVariableDouble("_root.FDN_DamageLayer._x", 0.0);
        movie_->SetVariableDouble("_root.FDN_DamageLayer._y", 0.0);
        ConfigureLayer();
        layerCreated_ = true;
        return true;
    }

    void ScaleformRenderer::ConfigureLayer()
    {
        if (!movie_) {
            return;
        }

        if (layer_.IsDisplayObject()) {
            RE::GFxValue::DisplayInfo info;
            info.Set(0.0, 0.0, 0.0, 100.0, 100.0, 100.0, true);
            layer_.SetDisplayInfo(info);
            layer_.SetMember("mouseEnabled", RE::GFxValue(false));
            layer_.SetMember("mouseChildren", RE::GFxValue(false));
            layer_.SetMember("tabEnabled", RE::GFxValue(false));
        }

        if (movie_->IsAvailable(kLayerPath.data())) {
            movie_->SetVariableDouble("_root.FDN_DamageLayer._x", 0.0);
            movie_->SetVariableDouble("_root.FDN_DamageLayer._y", 0.0);
            movie_->SetVariableDouble("_root.FDN_DamageLayer._xscale", 100.0);
            movie_->SetVariableDouble("_root.FDN_DamageLayer._yscale", 100.0);
            movie_->SetVariableDouble("_root.FDN_DamageLayer._alpha", 100.0);
            movie_->SetVariable("_root.FDN_DamageLayer._visible", RE::GFxValue(true));
            movie_->SetVariable("_root.FDN_DamageLayer.mouseEnabled", RE::GFxValue(false));
            movie_->SetVariable("_root.FDN_DamageLayer.mouseChildren", RE::GFxValue(false));
            movie_->SetVariable("_root.FDN_DamageLayer.tabEnabled", RE::GFxValue(false));
        }
    }

    bool ScaleformRenderer::CreateSlot(std::size_t index, Slot& slot)
    {
        if (!movie_) {
            return false;
        }

        slot.name = "FDN_Number_" + std::to_string(index);
        slot.usesObjectApi = false;
        slot.object.SetUndefined();

        if (EnsureLayer()) {
            slot.path = std::string(kLayerPath) + "." + slot.name;
            RefreshSlotPaths(slot);
            if (layer_.IsDisplayObject() && CreateTextField(layer_, kLayerSlotDepthBase + index, slot)) {
                return true;
            }

            if (CreateTextField(kLayerPath, kLayerSlotDepthBase + index, slot)) {
                return true;
            }
        }

        slot.path = "_root." + slot.name;
        RefreshSlotPaths(slot);
        if (root_.IsDisplayObject() && CreateTextField(root_, kRootSlotDepthBase + index, slot)) {
            return true;
        }

        return CreateTextField("_root", kRootSlotDepthBase + index, slot);
    }

    bool ScaleformRenderer::CreateTextField(RE::GFxValue& parent, std::size_t depth, Slot& slot)
    {
        if (!movie_ || !parent.IsDisplayObject()) {
            return false;
        }

        RE::GFxValue args[6];
        movie_->CreateString(std::addressof(args[0]), slot.name.c_str());
        args[1] = RE::GFxValue(static_cast<double>(depth));
        args[2] = RE::GFxValue(0.0);
        args[3] = RE::GFxValue(0.0);
        args[4] = RE::GFxValue(kTextFieldWidth);
        args[5] = RE::GFxValue(kTextFieldHeight);

        if (!parent.Invoke("createTextField", nullptr, args, 6)) {
            return false;
        }

        RE::GFxValue textField;
        if (!parent.GetMember(slot.name.c_str(), std::addressof(textField)) || !textField.IsDisplayObject()) {
            return false;
        }

        slot.object = std::move(textField);
        slot.usesObjectApi = true;
        ConfigureTextField(slot);
        return true;
    }

    bool ScaleformRenderer::CreateTextField(std::string_view parentPath, std::size_t depth, Slot& slot)
    {
        if (!movie_) {
            return false;
        }

        RE::GFxValue args[6];
        movie_->CreateString(std::addressof(args[0]), slot.name.c_str());
        args[1] = RE::GFxValue(static_cast<double>(depth));
        args[2] = RE::GFxValue(0.0);
        args[3] = RE::GFxValue(0.0);
        args[4] = RE::GFxValue(kTextFieldWidth);
        args[5] = RE::GFxValue(kTextFieldHeight);

        const auto method = std::string(parentPath) + ".createTextField";
        if (!movie_->Invoke(method.c_str(), nullptr, args, 6)) {
            return false;
        }

        ConfigureTextField(slot);
        return true;
    }

    void ScaleformRenderer::RefreshSlotPaths(Slot& slot)
    {
        slot.textPath = slot.path + ".text";
        slot.xPath = slot.path + "._x";
        slot.yPath = slot.path + "._y";
        slot.widthPath = slot.path + "._width";
        slot.heightPath = slot.path + "._height";
        slot.xScalePath = slot.path + "._xscale";
        slot.yScalePath = slot.path + "._yscale";
        slot.alphaPath = slot.path + "._alpha";
        slot.visiblePath = slot.path + "._visible";
        slot.selectablePath = slot.path + ".selectable";
        slot.mouseEnabledPath = slot.path + ".mouseEnabled";
        slot.htmlPath = slot.path + ".html";
        slot.embedFontsPath = slot.path + ".embedFonts";
        slot.textColorPath = slot.path + ".textColor";
        slot.autoSizePath = slot.path + ".autoSize";
        slot.filtersPath = slot.path + ".filters";
    }

    void ScaleformRenderer::ConfigureTextField(Slot& slot)
    {
        if (!movie_) {
            return;
        }

        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            slot.object.SetMember("selectable", RE::GFxValue(false));
            slot.object.SetMember("mouseEnabled", RE::GFxValue(false));
            slot.object.SetMember("html", RE::GFxValue(false));
            slot.object.SetMember("embedFonts", RE::GFxValue(true));
            slot.object.SetMember("autoSize", RE::GFxValue(false));
            slot.object.SetMember("multiline", RE::GFxValue(false));
            slot.object.SetMember("wordWrap", RE::GFxValue(false));
            slot.object.SetMember("textColor", RE::GFxValue(static_cast<double>(settings_.colors.normalDamageColor.ToRGB())));
            ApplyTextFieldGeometry(slot);
            ApplyTextFilters(slot);

            RE::GFxValue::DisplayInfo info;
            info.SetVisible(false);
            slot.object.SetDisplayInfo(info);
        } else {
            movie_->SetVariable(slot.selectablePath.c_str(), RE::GFxValue(false));
            movie_->SetVariable(slot.mouseEnabledPath.c_str(), RE::GFxValue(false));
            movie_->SetVariable(slot.htmlPath.c_str(), RE::GFxValue(false));
            movie_->SetVariable(slot.embedFontsPath.c_str(), RE::GFxValue(true));
            movie_->SetVariableDouble(slot.textColorPath.c_str(), static_cast<double>(settings_.colors.normalDamageColor.ToRGB()));
            movie_->SetVariable(slot.autoSizePath.c_str(), RE::GFxValue(false));
            ApplyTextFieldGeometry(slot);
            ApplyTextFilters(slot);
            movie_->SetVariable(slot.visiblePath.c_str(), RE::GFxValue(false));
        }

        slot.visible = false;
        slot.contentApplied = false;
        ResetSlotCachedState(slot);
        slot.created = true;
    }

    void ScaleformRenderer::ApplyTextFieldGeometry(Slot& slot)
    {
        if (!movie_) {
            return;
        }

        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            slot.object.SetMember("autoSize", RE::GFxValue(false));
            slot.object.SetMember("_width", RE::GFxValue(kTextFieldWidth));
            slot.object.SetMember("_height", RE::GFxValue(kTextFieldHeight));
            return;
        }

        movie_->SetVariable(slot.autoSizePath.c_str(), RE::GFxValue(false));
        movie_->SetVariableDouble(slot.widthPath.c_str(), kTextFieldWidth);
        movie_->SetVariableDouble(slot.heightPath.c_str(), kTextFieldHeight);
    }

    void ScaleformRenderer::HideSlot(Slot& slot)
    {
        if (slot.usesObjectApi && slot.object.IsDisplayObject() && slot.created && slot.visible) {
            RE::GFxValue::DisplayInfo info;
            info.SetVisible(false);
            slot.object.SetDisplayInfo(info);
        } else if (movie_ && slot.created && slot.visible) {
            movie_->SetVariable(slot.visiblePath.c_str(), RE::GFxValue(false));
        }
        slot.visible = false;
    }

    void ScaleformRenderer::ReleaseSlot(Slot& slot)
    {
        HideSlot(slot);
        slot.active = false;
        slot.target = 0;
        slot.contentApplied = false;
        slot.usesObjectApi = slot.object.IsDisplayObject();
        ResetSlotCachedState(slot);
    }

    void ScaleformRenderer::ApplyTextIfChanged(Slot& slot, const FloatingNumberUpdate& update)
    {
        if (!movie_) {
            return;
        }

        const auto color = update.color.ToARGB();
        if (slot.contentApplied && std::string_view(slot.lastText) == update.text && slot.lastColor.ToARGB() == color) {
            return;
        }

        slot.lastText.assign(update.text);
        slot.lastColor = update.color;
        ApplyTextFormat(slot, update.color);
        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            if (!slot.object.SetText(slot.lastText.c_str())) {
                slot.object.SetMember("text", RE::GFxValue(slot.lastText.c_str()));
            }
        } else {
            movie_->SetVariable(slot.textPath.c_str(), slot.lastText.c_str());
        }
        ApplyTextFormat(slot, update.color);
        ApplyTextFieldGeometry(slot);
        slot.contentApplied = true;
    }

    void ScaleformRenderer::ApplyTextFormat(Slot& slot, Color color)
    {
        if (!movie_) {
            return;
        }

        RE::GFxValue format;
        const bool hasFormat = BuildTextFormat(color, format);
        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            slot.object.SetMember("html", RE::GFxValue(false));
            slot.object.SetMember("embedFonts", RE::GFxValue(true));
            slot.object.SetMember("textColor", RE::GFxValue(static_cast<double>(color.ToRGB())));
            if (hasFormat) {
                slot.object.SetMember("defaultTextFormat", format);
                RE::GFxValue args[1];
                args[0] = format;
                slot.object.Invoke("setNewTextFormat", nullptr, args, 1);
                slot.object.Invoke("setTextFormat", nullptr, args, 1);
            }
            return;
        }

        movie_->SetVariable(slot.htmlPath.c_str(), RE::GFxValue(false));
        movie_->SetVariable(slot.embedFontsPath.c_str(), RE::GFxValue(true));
        movie_->SetVariableDouble(slot.textColorPath.c_str(), static_cast<double>(color.ToRGB()));
        if (hasFormat) {
            const auto setNewTextFormatPath = slot.path + ".setNewTextFormat";
            const auto setTextFormatPath = slot.path + ".setTextFormat";
            RE::GFxValue args[1];
            args[0] = format;
            movie_->Invoke(setNewTextFormatPath.c_str(), nullptr, args, 1);
            movie_->Invoke(setTextFormatPath.c_str(), nullptr, args, 1);
        }
    }

    void ScaleformRenderer::ApplyTextFilters(Slot& slot)
    {
        if (!movie_) {
            return;
        }

        RE::GFxValue filters;
        if (!BuildTextShadowFilters(filters)) {
            return;
        }

        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            slot.object.SetMember("filters", filters);
            return;
        }

        movie_->SetVariable(slot.filtersPath.c_str(), filters);
    }

    void ScaleformRenderer::ApplySlot(Slot& slot, const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle)
    {
        if (!movie_) {
            return;
        }

        if (slot.target != update.target) {
            slot.target = update.target;
        }

        std::optional<WorldPoint> anchor;
        auto* actor = ResolveActor(targetHandle);
        if (actor && !actor->IsDisabled() && !actor->IsMarkedForDeletion() && actor->Is3DLoaded()) {
            anchor = GetActorAnchorPoint(*actor, settings_);
        }

        if (!anchor && update.hasImpactPoint) {
            anchor = update.impactPoint;
        }

        if (!anchor) {
            HideSlot(slot);
            return;
        }

        if (!projection_) {
            HideSlot(slot);
            return;
        }

        const auto projected = ProjectWorldToHUD(*anchor, *projection_, settings_);
        if (!projected || !projected->onScreen) {
            HideSlot(slot);
            return;
        }

        const float scale = update.animation.scale * 100.0F;
        const float scaleRatio = std::max(scale, 0.0F) / 100.0F;
        const float x = projected->x + update.animation.driftX - static_cast<float>(kTextFieldWidth * scaleRatio * 0.5);
        const float y = projected->y - update.animation.offsetY - TextVisualHeight(settings_) * scaleRatio * 0.5F;
        const float alpha = std::clamp(update.animation.alpha, 0.0F, 1.0F) * 100.0F;

        ApplyTextIfChanged(slot, update);
        SetDisplayInfoIfChanged(slot, x, y, scale, alpha, true);
        slot.active = true;
    }

    void ScaleformRenderer::ResetSlotCachedState(Slot& slot) noexcept
    {
        slot.lastX = std::numeric_limits<float>::quiet_NaN();
        slot.lastY = std::numeric_limits<float>::quiet_NaN();
        slot.lastXScale = std::numeric_limits<float>::quiet_NaN();
        slot.lastYScale = std::numeric_limits<float>::quiet_NaN();
        slot.lastAlpha = std::numeric_limits<float>::quiet_NaN();
    }

    void ScaleformRenderer::SetSlotVisible(Slot& slot, bool visible)
    {
        if (!movie_ || !slot.created || slot.visible == visible) {
            slot.visible = visible;
            return;
        }

        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            RE::GFxValue::DisplayInfo info;
            info.SetVisible(visible);
            slot.object.SetDisplayInfo(info);
        } else {
            movie_->SetVariable(slot.visiblePath.c_str(), RE::GFxValue(visible));
        }
        slot.visible = visible;
    }

    void ScaleformRenderer::SetDisplayInfoIfChanged(Slot& slot, float x, float y, float scale, float alpha, bool visible)
    {
        constexpr float kEpsilon = 0.01F;
        const bool changed =
            !std::isfinite(slot.lastX) ||
            std::fabs(slot.lastX - x) > kEpsilon ||
            std::fabs(slot.lastY - y) > kEpsilon ||
            std::fabs(slot.lastXScale - scale) > kEpsilon ||
            std::fabs(slot.lastYScale - scale) > kEpsilon ||
            std::fabs(slot.lastAlpha - alpha) > kEpsilon ||
            slot.visible != visible;

        if (!changed) {
            return;
        }

        if (slot.usesObjectApi && slot.object.IsDisplayObject()) {
            RE::GFxValue::DisplayInfo info;
            info.Set(x, y, 0.0, scale, scale, alpha, visible);
            slot.object.SetDisplayInfo(info);
        } else {
            SetVariableDoubleIfChanged(slot.xPath, slot.lastX, x);
            SetVariableDoubleIfChanged(slot.yPath, slot.lastY, y);
            SetVariableDoubleIfChanged(slot.xScalePath, slot.lastXScale, scale);
            SetVariableDoubleIfChanged(slot.yScalePath, slot.lastYScale, scale);
            SetVariableDoubleIfChanged(slot.alphaPath, slot.lastAlpha, alpha);
            SetSlotVisible(slot, visible);
            return;
        }

        slot.lastX = x;
        slot.lastY = y;
        slot.lastXScale = scale;
        slot.lastYScale = scale;
        slot.lastAlpha = alpha;
        slot.visible = visible;
    }

    void ScaleformRenderer::SetVariableDoubleIfChanged(const std::string& path, float& cachedValue, float value)
    {
        if (!movie_) {
            return;
        }

        constexpr float kEpsilon = 0.01F;
        if (std::isfinite(cachedValue) && std::fabs(cachedValue - value) <= kEpsilon) {
            return;
        }

        movie_->SetVariableDouble(path.c_str(), value);
        cachedValue = value;
    }

    bool ScaleformRenderer::BuildTextFormat(Color color, RE::GFxValue& format) const
    {
        if (!movie_) {
            return false;
        }

        movie_->CreateObject(std::addressof(format), kTextFormatClass.data());
        if (!format.IsObject()) {
            movie_->CreateObject(std::addressof(format), kFlashTextFormatClass.data());
        }
        if (!format.IsObject()) {
            return false;
        }

        const auto fontFace = ResolveFontFace();
        if (!fontFace.empty()) {
            RE::GFxValue fontValue;
            movie_->CreateString(std::addressof(fontValue), fontFace.c_str());
            format.SetMember("font", fontValue);
        }

        RE::GFxValue alignValue;
        movie_->CreateString(std::addressof(alignValue), "center");
        format.SetMember("align", alignValue);
        format.SetMember("size", RE::GFxValue(static_cast<double>(std::max(settings_.font.fontSize, 1.0F))));
        format.SetMember("color", RE::GFxValue(static_cast<double>(color.ToRGB())));
        format.SetMember("bold", RE::GFxValue(settings_.font.bold));
        format.SetMember("italic", RE::GFxValue(settings_.font.italic));
        return true;
    }

    bool ScaleformRenderer::BuildTextShadowFilters(RE::GFxValue& filters) const
    {
        if (!movie_) {
            return false;
        }

        movie_->CreateArray(std::addressof(filters));
        if (!filters.IsArray()) {
            return false;
        }

        if (!settings_.font.shadow) {
            return true;
        }

        RE::GFxValue args[11];
        args[0] = RE::GFxValue(kShadowDistance);
        args[1] = RE::GFxValue(kShadowAngle);
        args[2] = RE::GFxValue(kShadowColor);
        args[3] = RE::GFxValue(kShadowAlpha);
        args[4] = RE::GFxValue(kShadowBlur);
        args[5] = RE::GFxValue(kShadowBlur);
        args[6] = RE::GFxValue(kShadowStrength);
        args[7] = RE::GFxValue(kShadowQuality);
        args[8] = RE::GFxValue(false);
        args[9] = RE::GFxValue(false);
        args[10] = RE::GFxValue(false);

        RE::GFxValue shadow;
        movie_->CreateObject(std::addressof(shadow), kFlashDropShadowFilterClass.data(), args, 11);
        if (!shadow.IsObject()) {
            movie_->CreateObject(std::addressof(shadow), kDropShadowFilterClass.data(), args, 11);
        }
        if (!shadow.IsObject()) {
            return false;
        }

        filters.PushBack(shadow);
        return true;
    }

    std::string ScaleformRenderer::ResolveFontFace() const
    {
        if (!settings_.font.fontName.empty()) {
            return settings_.font.fontName;
        }

        return std::string(kDefaultSkyrimFontFace);
    }
}
