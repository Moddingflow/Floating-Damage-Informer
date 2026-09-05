#include "FDN/Skyrim/Runtime.h"

#include "FDN/Skyrim/HUDHooks.h"
#include "FDN/Skyrim/ScaleformRenderer.h"

#include "RE/U/UI.h"
#include "REX/W32/KERNEL32.h"
#include "SKSE/API.h"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <spdlog/spdlog.h>
#include <vector>

namespace FDN::Skyrim
{
    namespace
    {
        [[nodiscard]] std::filesystem::path DefaultIniPath()
        {
            return std::filesystem::path("Data") / "SKSE" / "Plugins" / "FloatingDamageNumbersNG.ini";
        }

        [[nodiscard]] std::filesystem::path PluginDirectoryIniPath()
        {
            const auto module = REX::W32::GetCurrentModule();
            if (!module) {
                return {};
            }

            std::vector<wchar_t> buffer;
            buffer.resize(REX::W32::MAX_PATH);
            std::uint32_t length = 0;
            do {
                length = REX::W32::GetModuleFileNameW(module, buffer.data(), static_cast<std::uint32_t>(buffer.size()));
                if (length == buffer.size()) {
                    buffer.resize(buffer.size() * 2u);
                }
            } while (length == buffer.size() && buffer.size() <= (std::numeric_limits<std::uint32_t>::max)());

            if (length == 0 || length >= buffer.size()) {
                return {};
            }

            const std::filesystem::path dllPath(buffer.data(), buffer.data() + length);
            return dllPath.parent_path() / "FloatingDamageNumbersNG.ini";
        }

        [[nodiscard]] std::filesystem::path ResolveIniPath()
        {
            auto iniPath = PluginDirectoryIniPath();
            if (!iniPath.empty()) {
                return iniPath;
            }
            return DefaultIniPath();
        }
    }

    RuntimeService& RuntimeService::Get()
    {
        static RuntimeService service;
        return service;
    }

    RuntimeService::RuntimeService() :
        aggregator_(settings_.detection.mergeWindowMs),
        numbers_(settings_)
    {}

    bool RuntimeService::Initialize()
    {
        if (initialized_) {
            return true;
        }

        const auto iniPath = ResolveIniPath();

        settings_ = SettingsManager::LoadOrCreate(iniPath);
        spdlog::set_level(settings_.general.debugLogging ? spdlog::level::trace : spdlog::level::info);
        aggregator_.SetMergeWindow(settings_.detection.mergeWindowMs);
        numbers_.Reconfigure(settings_);
        targetBySerial_.clear();
        targetBySerial_.reserve(static_cast<std::size_t>(settings_.display.maxActiveNumbers));

        renderer_ = std::make_unique<ScaleformRenderer>();
        if (!renderer_->Initialize(settings_)) {
            spdlog::warn("Scaleform renderer did not initialize immediately; it will retry when HUDMenu advances");
        }

        initialized_ = true;
        spdlog::info("FloatingDamageNumbersNG initialized. INI: {}", iniPath.string());
        return true;
    }

    bool RuntimeService::InitializeGameSystems()
    {
        if (!initialized_) {
            Initialize();
        }
        if (gameSystemsInitialized_) {
            return true;
        }

        if (!HUDHooks::Install()) {
            spdlog::error("Failed to install HUDMenu advance hook");
            return false;
        }

        damageEvents_.Configure(
            settings_,
            [this](const DamageEvent& event, RE::ObjectRefHandle targetHandle) {
                OnDamage(event, targetHandle);
            },
            [this]() {
                return NowMs();
            });

        if (!damageEvents_.Register()) {
            spdlog::error("Failed to register TESHitEvent sink");
            return false;
        }

        gameSystemsInitialized_ = true;
        spdlog::info("Game event and HUD hooks installed");
        return true;
    }

    void RuntimeService::OnFrame(float deltaSeconds, RE::HUDMenu& hudMenu)
    {
        if (!settings_.general.enabled) {
            if (!renderingSuppressed_) {
                Clear();
                renderingSuppressed_ = true;
            }
            return;
        }

        nowMs_ += static_cast<Milliseconds>(std::max(deltaSeconds, 0.0F)) * 1000.0;
        if (!frameLoopLogged_) {
            spdlog::info("HUD frame loop active");
            frameLoopLogged_ = true;
        }

        bool shouldSuppressRendering = false;
        if (auto* ui = RE::UI::GetSingleton()) {
            if (settings_.general.disableInMenus && ui->GameIsPaused()) {
                shouldSuppressRendering = true;
            }
            if (settings_.general.disableWhenHUDHidden && !ui->menuSystemVisible) {
                shouldSuppressRendering = true;
            }
        }

        if (shouldSuppressRendering) {
            if (!renderingSuppressed_) {
                Clear();
                renderingSuppressed_ = true;
            }
            return;
        }
        renderingSuppressed_ = false;

        damageEvents_.Update();
        if (aggregator_.ActiveCount() != 0) {
            aggregator_.ClearExpired(nowMs_);
        }

        if (numbers_.ActiveCount() == 0) {
            return;
        }

        const auto& updates = numbers_.Update(deltaSeconds);
        if (updates.empty()) {
            return;
        }

        const bool canRender = renderer_ && renderer_->BeginFrame(hudMenu) && renderer_->IsAvailable();

        for (const auto& update : updates) {
            const auto targetIt = targetBySerial_.find(update.serial);
            const auto targetHandle = targetIt == targetBySerial_.end() ? RE::ObjectRefHandle{} : targetIt->second;
            if (canRender) {
                RenderUpdate(update, targetHandle);
            }
            if (!update.animation.alive) {
                targetBySerial_.erase(update.serial);
            }
        }
    }

    void RuntimeService::Clear()
    {
        damageEvents_.Clear();
        aggregator_.Clear();
        numbers_.Clear();
        targetBySerial_.clear();
        if (renderer_) {
            renderer_->Clear();
        }
    }

    void RuntimeService::OnDamage(const DamageEvent& event, RE::ObjectRefHandle targetHandle)
    {
        if (!settings_.ShouldCaptureDamage(event.amount)) {
            return;
        }

        const auto aggregated = aggregator_.Submit(event);
        const auto update = numbers_.Submit(aggregated);
        if (!update) {
            return;
        }

        targetBySerial_[update->serial] = targetHandle;
    }

    void RuntimeService::RenderUpdate(const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle)
    {
        if (!renderer_ || !renderer_->IsAvailable()) {
            return;
        }
        renderer_->Render(update, targetHandle);
    }
}
