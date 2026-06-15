#include "FDN/Settings.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>
#include <unordered_map>

namespace FDN
{
    namespace
    {
        using Section = std::unordered_map<std::string, std::string>;
        using Ini = std::unordered_map<std::string, Section>;

        [[nodiscard]] std::string Lower(std::string_view value)
        {
            std::string result(value);
            std::ranges::transform(result, result.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return result;
        }

        [[nodiscard]] bool IsHexDigit(char ch) noexcept
        {
            return std::isxdigit(static_cast<unsigned char>(ch)) != 0;
        }

        [[nodiscard]] bool StartsHexColor(std::string_view value, std::size_t index) noexcept
        {
            if (index >= value.size() || value[index] != '#') {
                return false;
            }

            std::size_t digits = 0;
            for (std::size_t i = index + 1; i < value.size() && IsHexDigit(value[i]); ++i) {
                ++digits;
            }

            if (digits != 6 && digits != 8) {
                return false;
            }

            const auto after = index + 1 + digits;
            return after == value.size() ||
                   std::isspace(static_cast<unsigned char>(value[after])) != 0 ||
                   value[after] == ';' ||
                   value[after] == '#';
        }

        [[nodiscard]] std::string StripInlineComment(std::string_view value)
        {
            bool inQuote = false;
            for (std::size_t i = 0; i < value.size(); ++i) {
                const char ch = value[i];
                if (ch == '"') {
                    inQuote = !inQuote;
                    continue;
                }
                if (!inQuote && (ch == ';' || (ch == '#' && !StartsHexColor(value, i)))) {
                    return Trim(value.substr(0, i));
                }
            }
            return Trim(value);
        }

        [[nodiscard]] Ini ParseIni(std::string_view text)
        {
            Ini ini;
            std::string currentSection;
            std::istringstream stream{ std::string(text) };
            std::string line;

            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }

                const auto trimmed = Trim(line);
                if (trimmed.empty() || trimmed.front() == ';' || trimmed.front() == '#') {
                    continue;
                }

                if (trimmed.front() == '[' && trimmed.back() == ']') {
                    currentSection = Trim(std::string_view(trimmed).substr(1, trimmed.size() - 2));
                    continue;
                }

                const auto equals = trimmed.find('=');
                if (equals == std::string::npos) {
                    continue;
                }

                auto key = Trim(std::string_view(trimmed).substr(0, equals));
                auto value = StripInlineComment(std::string_view(trimmed).substr(equals + 1));
                ini[currentSection][key] = value;
            }

            return ini;
        }

        [[nodiscard]] std::string GetString(const Ini& ini, std::string_view section, std::string_view key, std::string fallback)
        {
            const auto sectionIt = ini.find(std::string(section));
            if (sectionIt == ini.end()) {
                return fallback;
            }
            const auto keyIt = sectionIt->second.find(std::string(key));
            if (keyIt == sectionIt->second.end()) {
                return fallback;
            }
            return keyIt->second;
        }

        [[nodiscard]] bool GetBool(const Ini& ini, std::string_view section, std::string_view key, bool fallback)
        {
            return ParseBool(GetString(ini, section, key, fallback ? "true" : "false"), fallback);
        }

        [[nodiscard]] float GetFloat(const Ini& ini, std::string_view section, std::string_view key, float fallback)
        {
            return ParseFloat(GetString(ini, section, key, std::to_string(fallback)), fallback);
        }

        [[nodiscard]] int GetInt(const Ini& ini, std::string_view section, std::string_view key, int fallback)
        {
            return ParseInt(GetString(ini, section, key, std::to_string(fallback)), fallback);
        }

        [[nodiscard]] Color GetColor(const Ini& ini, std::string_view section, std::string_view key, Color fallback)
        {
            return ParseColor(GetString(ini, section, key, ""), fallback);
        }

        [[nodiscard]] std::string FormatFixed(float value, int precision)
        {
            char buffer[64]{};
            const auto result = std::to_chars(
                buffer,
                buffer + sizeof(buffer),
                value,
                std::chars_format::fixed,
                precision);
            if (result.ec == std::errc{}) {
                return std::string(buffer, result.ptr);
            }
            if (precision == 0) {
                return "0";
            }
            std::string fallback = "0.";
            fallback.append(static_cast<std::size_t>(precision), '0');
            return fallback;
        }
    }

    std::string Trim(std::string_view value)
    {
        auto first = value.begin();
        auto last = value.end();
        while (first != last && std::isspace(static_cast<unsigned char>(*first)) != 0) {
            ++first;
        }
        while (first != last && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
            --last;
        }
        return std::string(first, last);
    }

    bool ParseBool(std::string_view value, bool fallback) noexcept
    {
        const auto lower = Lower(Trim(value));
        if (lower == "true" || lower == "1" || lower == "yes" || lower == "on") {
            return true;
        }
        if (lower == "false" || lower == "0" || lower == "no" || lower == "off") {
            return false;
        }
        return fallback;
    }

    float ParseFloat(std::string_view value, float fallback) noexcept
    {
        const auto trimmed = Trim(value);
        float parsed = fallback;
        const auto* begin = trimmed.data();
        const auto* end = trimmed.data() + trimmed.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end || !std::isfinite(parsed)) {
            return fallback;
        }
        return parsed;
    }

    int ParseInt(std::string_view value, int fallback) noexcept
    {
        const auto trimmed = Trim(value);
        int parsed = fallback;
        const auto* begin = trimmed.data();
        const auto* end = trimmed.data() + trimmed.size();
        const auto result = std::from_chars(begin, end, parsed);
        if (result.ec != std::errc{} || result.ptr != end) {
            return fallback;
        }
        return parsed;
    }

    Color ParseColor(std::string_view value, Color fallback) noexcept
    {
        auto trimmed = Trim(value);
        if (trimmed.empty()) {
            return fallback;
        }
        if (trimmed.starts_with("0x") || trimmed.starts_with("0X")) {
            trimmed.erase(0, 2);
        }
        if (trimmed.starts_with('#')) {
            trimmed.erase(0, 1);
        }

        if (trimmed.size() != 6 && trimmed.size() != 8) {
            return fallback;
        }

        std::uint32_t parsed = 0;
        const auto result = std::from_chars(trimmed.data(), trimmed.data() + trimmed.size(), parsed, 16);
        if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size()) {
            return fallback;
        }

        if (trimmed.size() == 6) {
            parsed |= 0xFF000000;
        }
        return Color::FromARGB(parsed);
    }

    Color Settings::ColorFor(DamageType type, float amount) const noexcept
    {
        if (type == DamageType::PlayerTaken) {
            return colors.playerTakenDamageColor;
        }
        if (amount >= colors.criticalDamageThreshold) {
            return colors.criticalDamageColor;
        }
        if (amount >= colors.largeDamageThreshold) {
            return colors.largeDamageColor;
        }

        switch (type) {
        case DamageType::Magic:
            return colors.magicDamageColor;
        case DamageType::Fire:
            return colors.fireDamageColor;
        case DamageType::Frost:
            return colors.frostDamageColor;
        case DamageType::Shock:
            return colors.shockDamageColor;
        case DamageType::Poison:
            return colors.poisonDamageColor;
        case DamageType::PlayerTaken:
            return colors.playerTakenDamageColor;
        case DamageType::Normal:
        default:
            return colors.normalDamageColor;
        }
    }

    std::string Settings::FormatDamage(float amount) const
    {
        const auto clampedPlaces = std::clamp(detection.decimalPlaces, 0, 4);
        const float displayAmount = detection.roundDamage ? std::round(amount) : amount;
        return FormatFixed(displayAmount, detection.roundDamage ? 0 : clampedPlaces);
    }

    bool Settings::ShouldShowDamage(float amount) const noexcept
    {
        if (!std::isfinite(amount) || amount < 0.0F || amount > kMaxDisplayableDamage) {
            return false;
        }
        if (amount == 0.0F) {
            return detection.showZeroDamage;
        }
        return amount >= detection.minDamageToShow;
    }

    bool Settings::ShouldCaptureDamage(float amount) const noexcept
    {
        if (!std::isfinite(amount) || amount < 0.0F || amount > kMaxDisplayableDamage) {
            return false;
        }
        if (amount == 0.0F) {
            return detection.showZeroDamage;
        }
        if (detection.aggregateDamageOverTime) {
            return true;
        }
        return amount >= detection.minDamageToShow;
    }

    void Normalize(Settings& settings) noexcept
    {
        settings.detection.minDamageToShow = std::clamp(settings.detection.minDamageToShow, 0.0F, kMaxDisplayableDamage);
        settings.detection.decimalPlaces = std::clamp(settings.detection.decimalPlaces, 0, 4);
        settings.detection.mergeWindowMs = std::clamp(settings.detection.mergeWindowMs, 0.0F, 5000.0F);
        settings.detection.healthSampleDelayMs = std::clamp(settings.detection.healthSampleDelayMs, 0.0F, 1000.0F);
        settings.detection.healthSampleFrames = std::clamp(settings.detection.healthSampleFrames, 1, 16);
        settings.detection.eventTimeoutMs = std::clamp(settings.detection.eventTimeoutMs, 1.0F, 10000.0F);
        settings.detection.magicSourceMemoryMs = std::clamp(settings.detection.magicSourceMemoryMs, 1000.0F, 60000.0F);

        settings.display.lifetime = std::clamp(settings.display.lifetime, 0.1F, 10.0F);
        settings.display.startVerticalOffset = std::clamp(settings.display.startVerticalOffset, -1000.0F, 3000.0F);
        settings.display.risePixels = std::clamp(settings.display.risePixels, -1000.0F, 3000.0F);
        settings.display.randomDriftPixels = std::clamp(settings.display.randomDriftPixels, 0.0F, 1000.0F);
        settings.display.maxDistance = std::clamp(settings.display.maxDistance, 0.0F, 100000.0F);
        settings.display.worldZOffset = std::clamp(settings.display.worldZOffset, -1000.0F, 3000.0F);
        settings.display.impactPointVerticalOffset = std::clamp(settings.display.impactPointVerticalOffset, -1000.0F, 3000.0F);
        settings.display.maxActiveNumbers = std::clamp(settings.display.maxActiveNumbers, 1, 512);
        settings.display.maxNumbersPerActor = std::clamp(settings.display.maxNumbersPerActor, 1, settings.display.maxActiveNumbers);

        settings.font.fontSize = std::clamp(settings.font.fontSize, 1.0F, 128.0F);

        settings.animation.startScale = std::clamp(settings.animation.startScale, 0.1F, 5.0F);
        settings.animation.endScale = std::clamp(settings.animation.endScale, 0.1F, 5.0F);
        settings.animation.popDurationMs = std::clamp(settings.animation.popDurationMs, 1.0F, 5000.0F);

        settings.colors.largeDamageThreshold = std::clamp(settings.colors.largeDamageThreshold, 0.0F, kMaxDisplayableDamage);
        settings.colors.criticalDamageThreshold = std::clamp(settings.colors.criticalDamageThreshold, 0.0F, kMaxDisplayableDamage);
        if (settings.colors.criticalDamageThreshold < settings.colors.largeDamageThreshold) {
            settings.colors.criticalDamageThreshold = settings.colors.largeDamageThreshold;
        }
    }

    std::string SettingsManager::DefaultIni()
    {
        return R"(; FloatingDamageNumbersNG
; Native SKSE plugin configuration. This file is UTF-8.

[General]
bEnabled = true
bShowPlayerDealtDamage = true
bShowFollowerDealtDamage = false
bShowNPCvsNPCDamage = false
bShowDamageTakenByPlayer = false
bShowNonHostileTargets = false
bDisableInMenus = true
bDisableWhenHUDHidden = true
bDebugLogging = false

[Detection]
fMinDamageToShow = 1.0
bRoundDamage = true
iDecimalPlaces = 0
fMergeWindowMs = 75.0
fHealthSampleDelayMs = 16.0
iHealthSampleFrames = 3
bShowZeroDamage = false
bAggregateDamageOverTime = true
fEventTimeoutMs = 500.0
fMagicSourceMemoryMs = 15000.0

[Display]
fLifetime = 1.25
fStartVerticalOffset = 120.0
fRisePixels = 80.0
fRandomDriftPixels = 20.0
fMaxDistance = 18000.0
bHideBehindCamera = true
bClampToScreen = false
bAnchorToHead = true
fWorldZOffset = 110.0
fImpactPointVerticalOffset = 18.0
iMaxActiveNumbers = 128
iMaxNumbersPerActor = 8

[Font]
bUseCurrentUIFont = true
sFontName =
fFontSize = 28.0
bBold = true
bItalic = false
bOutline = true
bShadow = true
bGlow = false

[Animation]
bEffectsEnabled = true
bFadeOut = true
bPopAnimation = true
bBounceAnimation = false
fStartScale = 1.25
fEndScale = 0.90
fPopDurationMs = 120.0

[Colors]
uNormalDamageColor = 0xFFFFFFFF
uLargeDamageColor = 0xFFFFD45A
uCriticalDamageColor = 0xFFFF5555
uMagicDamageColor = 0xFF66CCFF
uFireDamageColor = 0xFFFF6633
uFrostDamageColor = 0xFF99DDFF
uShockDamageColor = 0xFFCC88FF
uPoisonDamageColor = 0xFF88CC44
uPlayerTakenDamageColor = 0xFFFF3333
fLargeDamageThreshold = 100.0
fCriticalDamageThreshold = 250.0

[Renderer]
sPreferredRenderer = Scaleform
bAllowD3D11Fallback = false
bVRUseScaleformOnly = true
bFailSafeDisableRendererOnError = true
)";
    }

    Settings SettingsManager::LoadFromString(std::string_view text)
    {
        const auto ini = ParseIni(text);
        Settings settings;

        settings.general.enabled = GetBool(ini, "General", "bEnabled", settings.general.enabled);
        settings.general.showPlayerDealtDamage = GetBool(ini, "General", "bShowPlayerDealtDamage", settings.general.showPlayerDealtDamage);
        settings.general.showFollowerDealtDamage = GetBool(ini, "General", "bShowFollowerDealtDamage", settings.general.showFollowerDealtDamage);
        settings.general.showNPCvsNPCDamage = GetBool(ini, "General", "bShowNPCvsNPCDamage", settings.general.showNPCvsNPCDamage);
        settings.general.showDamageTakenByPlayer = GetBool(ini, "General", "bShowDamageTakenByPlayer", settings.general.showDamageTakenByPlayer);
        settings.general.showNonHostileTargets = GetBool(ini, "General", "bShowNonHostileTargets", settings.general.showNonHostileTargets);
        settings.general.disableInMenus = GetBool(ini, "General", "bDisableInMenus", settings.general.disableInMenus);
        settings.general.disableWhenHUDHidden = GetBool(ini, "General", "bDisableWhenHUDHidden", settings.general.disableWhenHUDHidden);
        settings.general.debugLogging = GetBool(ini, "General", "bDebugLogging", settings.general.debugLogging);

        settings.detection.minDamageToShow = GetFloat(ini, "Detection", "fMinDamageToShow", settings.detection.minDamageToShow);
        settings.detection.roundDamage = GetBool(ini, "Detection", "bRoundDamage", settings.detection.roundDamage);
        settings.detection.decimalPlaces = GetInt(ini, "Detection", "iDecimalPlaces", settings.detection.decimalPlaces);
        settings.detection.mergeWindowMs = GetFloat(ini, "Detection", "fMergeWindowMs", settings.detection.mergeWindowMs);
        settings.detection.healthSampleDelayMs = GetFloat(ini, "Detection", "fHealthSampleDelayMs", settings.detection.healthSampleDelayMs);
        settings.detection.healthSampleFrames = GetInt(ini, "Detection", "iHealthSampleFrames", settings.detection.healthSampleFrames);
        settings.detection.showZeroDamage = GetBool(ini, "Detection", "bShowZeroDamage", settings.detection.showZeroDamage);
        settings.detection.aggregateDamageOverTime = GetBool(ini, "Detection", "bAggregateDamageOverTime", settings.detection.aggregateDamageOverTime);
        settings.detection.eventTimeoutMs = GetFloat(ini, "Detection", "fEventTimeoutMs", settings.detection.eventTimeoutMs);
        settings.detection.magicSourceMemoryMs =
            GetFloat(ini, "Detection", "fMagicSourceMemoryMs", settings.detection.magicSourceMemoryMs);

        settings.display.lifetime = GetFloat(ini, "Display", "fLifetime", settings.display.lifetime);
        settings.display.startVerticalOffset = GetFloat(ini, "Display", "fStartVerticalOffset", settings.display.startVerticalOffset);
        settings.display.risePixels = GetFloat(ini, "Display", "fRisePixels", settings.display.risePixels);
        settings.display.randomDriftPixels = GetFloat(ini, "Display", "fRandomDriftPixels", settings.display.randomDriftPixels);
        settings.display.maxDistance = GetFloat(ini, "Display", "fMaxDistance", settings.display.maxDistance);
        settings.display.hideBehindCamera = GetBool(ini, "Display", "bHideBehindCamera", settings.display.hideBehindCamera);
        settings.display.clampToScreen = GetBool(ini, "Display", "bClampToScreen", settings.display.clampToScreen);
        settings.display.anchorToHead = GetBool(ini, "Display", "bAnchorToHead", settings.display.anchorToHead);
        settings.display.worldZOffset = GetFloat(ini, "Display", "fWorldZOffset", settings.display.worldZOffset);
        settings.display.impactPointVerticalOffset =
            GetFloat(ini, "Display", "fImpactPointVerticalOffset", settings.display.impactPointVerticalOffset);
        settings.display.maxActiveNumbers = GetInt(ini, "Display", "iMaxActiveNumbers", settings.display.maxActiveNumbers);
        settings.display.maxNumbersPerActor = GetInt(ini, "Display", "iMaxNumbersPerActor", settings.display.maxNumbersPerActor);

        settings.font.useCurrentUIFont = GetBool(ini, "Font", "bUseCurrentUIFont", settings.font.useCurrentUIFont);
        settings.font.fontName = GetString(ini, "Font", "sFontName", settings.font.fontName);
        settings.font.fontSize = GetFloat(ini, "Font", "fFontSize", settings.font.fontSize);
        settings.font.bold = GetBool(ini, "Font", "bBold", settings.font.bold);
        settings.font.italic = GetBool(ini, "Font", "bItalic", settings.font.italic);
        settings.font.outline = GetBool(ini, "Font", "bOutline", settings.font.outline);
        settings.font.shadow = GetBool(ini, "Font", "bShadow", settings.font.shadow);
        settings.font.glow = GetBool(ini, "Font", "bGlow", settings.font.glow);

        settings.animation.effectsEnabled = GetBool(ini, "Animation", "bEffectsEnabled", settings.animation.effectsEnabled);
        settings.animation.fadeOut = GetBool(ini, "Animation", "bFadeOut", settings.animation.fadeOut);
        settings.animation.popAnimation = GetBool(ini, "Animation", "bPopAnimation", settings.animation.popAnimation);
        settings.animation.bounceAnimation = GetBool(ini, "Animation", "bBounceAnimation", settings.animation.bounceAnimation);
        settings.animation.startScale = GetFloat(ini, "Animation", "fStartScale", settings.animation.startScale);
        settings.animation.endScale = GetFloat(ini, "Animation", "fEndScale", settings.animation.endScale);
        settings.animation.popDurationMs = GetFloat(ini, "Animation", "fPopDurationMs", settings.animation.popDurationMs);

        settings.colors.normalDamageColor = GetColor(ini, "Colors", "uNormalDamageColor", settings.colors.normalDamageColor);
        settings.colors.largeDamageColor = GetColor(ini, "Colors", "uLargeDamageColor", settings.colors.largeDamageColor);
        settings.colors.criticalDamageColor = GetColor(ini, "Colors", "uCriticalDamageColor", settings.colors.criticalDamageColor);
        settings.colors.magicDamageColor = GetColor(ini, "Colors", "uMagicDamageColor", settings.colors.magicDamageColor);
        settings.colors.fireDamageColor = GetColor(ini, "Colors", "uFireDamageColor", settings.colors.fireDamageColor);
        settings.colors.frostDamageColor = GetColor(ini, "Colors", "uFrostDamageColor", settings.colors.frostDamageColor);
        settings.colors.shockDamageColor = GetColor(ini, "Colors", "uShockDamageColor", settings.colors.shockDamageColor);
        settings.colors.poisonDamageColor = GetColor(ini, "Colors", "uPoisonDamageColor", settings.colors.poisonDamageColor);
        settings.colors.playerTakenDamageColor = GetColor(ini, "Colors", "uPlayerTakenDamageColor", settings.colors.playerTakenDamageColor);
        settings.colors.largeDamageThreshold = GetFloat(ini, "Colors", "fLargeDamageThreshold", settings.colors.largeDamageThreshold);
        settings.colors.criticalDamageThreshold = GetFloat(ini, "Colors", "fCriticalDamageThreshold", settings.colors.criticalDamageThreshold);

        settings.renderer.preferredRenderer = GetString(ini, "Renderer", "sPreferredRenderer", settings.renderer.preferredRenderer);
        settings.renderer.allowD3D11Fallback = GetBool(ini, "Renderer", "bAllowD3D11Fallback", settings.renderer.allowD3D11Fallback);
        settings.renderer.vrUseScaleformOnly = GetBool(ini, "Renderer", "bVRUseScaleformOnly", settings.renderer.vrUseScaleformOnly);
        settings.renderer.failSafeDisableRendererOnError = GetBool(ini, "Renderer", "bFailSafeDisableRendererOnError", settings.renderer.failSafeDisableRendererOnError);

        Normalize(settings);

        return settings;
    }

    Settings SettingsManager::LoadOrCreate(const std::filesystem::path& path)
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) && !ec) {
            WriteDefaultIni(path);
        }
        if (ec) {
            return {};
        }

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return {};
        }

        std::ostringstream buffer;
        buffer << in.rdbuf();
        return LoadFromString(buffer.str());
    }

    void SettingsManager::WriteDefaultIni(const std::filesystem::path& path)
    {
        if (const auto parent = path.parent_path(); !parent.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return;
            }
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << DefaultIni();
    }
}
