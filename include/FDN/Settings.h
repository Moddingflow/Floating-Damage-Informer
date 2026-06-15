#pragma once

#include "FDN/Types.h"

#include <filesystem>
#include <string>

namespace FDN
{
    struct GeneralSettings
    {
        bool enabled{ true };
        bool showPlayerDealtDamage{ true };
        bool showFollowerDealtDamage{ false };
        bool showNPCvsNPCDamage{ false };
        bool showDamageTakenByPlayer{ false };
        bool showNonHostileTargets{ false };
        bool disableInMenus{ true };
        bool disableWhenHUDHidden{ true };
        bool debugLogging{ false };
    };

    struct DetectionSettings
    {
        float minDamageToShow{ 1.0F };
        bool roundDamage{ true };
        int decimalPlaces{ 0 };
        float mergeWindowMs{ 75.0F };
        float healthSampleDelayMs{ 16.0F };
        int healthSampleFrames{ 3 };
        bool showZeroDamage{ false };
        bool aggregateDamageOverTime{ true };
        float eventTimeoutMs{ 500.0F };
        float magicSourceMemoryMs{ 15000.0F };
    };

    struct DisplaySettings
    {
        float lifetime{ 1.25F };
        float startVerticalOffset{ 120.0F };
        float risePixels{ 80.0F };
        float randomDriftPixels{ 20.0F };
        float maxDistance{ 18000.0F };
        bool hideBehindCamera{ true };
        bool clampToScreen{ false };
        bool anchorToHead{ true };
        float worldZOffset{ 110.0F };
        float impactPointVerticalOffset{ 18.0F };
        int maxActiveNumbers{ 128 };
        int maxNumbersPerActor{ 8 };
    };

    struct FontSettings
    {
        bool useCurrentUIFont{ true };
        std::string fontName;
        float fontSize{ 28.0F };
        bool bold{ true };
        bool italic{ false };
        bool outline{ true };
        bool shadow{ true };
        bool glow{ false };
    };

    struct AnimationSettings
    {
        bool effectsEnabled{ true };
        bool fadeOut{ true };
        bool popAnimation{ true };
        bool bounceAnimation{ false };
        float startScale{ 1.25F };
        float endScale{ 0.90F };
        float popDurationMs{ 120.0F };
    };

    struct ColorSettings
    {
        Color normalDamageColor{ Color::FromARGB(0xFFFFFFFF) };
        Color largeDamageColor{ Color::FromARGB(0xFFFFD45A) };
        Color criticalDamageColor{ Color::FromARGB(0xFFFF5555) };
        Color magicDamageColor{ Color::FromARGB(0xFF66CCFF) };
        Color fireDamageColor{ Color::FromARGB(0xFFFF6633) };
        Color frostDamageColor{ Color::FromARGB(0xFF99DDFF) };
        Color shockDamageColor{ Color::FromARGB(0xFFCC88FF) };
        Color poisonDamageColor{ Color::FromARGB(0xFF88CC44) };
        Color playerTakenDamageColor{ Color::FromARGB(0xFFFF3333) };
        float largeDamageThreshold{ 100.0F };
        float criticalDamageThreshold{ 250.0F };
    };

    struct RendererSettings
    {
        std::string preferredRenderer{ "Scaleform" };
        bool allowD3D11Fallback{ false };
        bool vrUseScaleformOnly{ true };
        bool failSafeDisableRendererOnError{ true };
    };

    struct Settings
    {
        GeneralSettings general;
        DetectionSettings detection;
        DisplaySettings display;
        FontSettings font;
        AnimationSettings animation;
        ColorSettings colors;
        RendererSettings renderer;

        [[nodiscard]] Color ColorFor(DamageType type, float amount) const noexcept;
        [[nodiscard]] std::string FormatDamage(float amount) const;
        [[nodiscard]] bool ShouldShowDamage(float amount) const noexcept;
        [[nodiscard]] bool ShouldCaptureDamage(float amount) const noexcept;
    };

    void Normalize(Settings& settings) noexcept;

    class SettingsManager
    {
    public:
        [[nodiscard]] static std::string DefaultIni();
        [[nodiscard]] static Settings LoadFromString(std::string_view text);
        [[nodiscard]] static Settings LoadOrCreate(const std::filesystem::path& path);
        static void WriteDefaultIni(const std::filesystem::path& path);
    };

    [[nodiscard]] bool ParseBool(std::string_view value, bool fallback) noexcept;
    [[nodiscard]] float ParseFloat(std::string_view value, float fallback) noexcept;
    [[nodiscard]] int ParseInt(std::string_view value, int fallback) noexcept;
    [[nodiscard]] Color ParseColor(std::string_view value, Color fallback) noexcept;
    [[nodiscard]] std::string Trim(std::string_view value);
}
