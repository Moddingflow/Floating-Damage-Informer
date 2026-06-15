#pragma once

#include "FDN/DamageAggregator.h"
#include "FDN/FloatingNumberManager.h"
#include "FDN/Skyrim/DamageEventManager.h"
#include "FDN/Skyrim/Renderer.h"

#include <memory>
#include <unordered_map>

namespace FDN::Skyrim
{
    class RuntimeService
    {
    public:
        [[nodiscard]] static RuntimeService& Get();

        bool Initialize();
        bool InitializeGameSystems();
        void OnFrame(float deltaSeconds, RE::HUDMenu& hudMenu);
        void Clear();

        [[nodiscard]] Milliseconds NowMs() const noexcept { return nowMs_; }
        [[nodiscard]] const Settings& GetSettings() const noexcept { return settings_; }

    private:
        RuntimeService();

        void OnDamage(const DamageEvent& event, RE::ObjectRefHandle targetHandle);
        void RenderUpdate(const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle);

        Settings settings_;
        DamageAggregator aggregator_;
        FloatingNumberManager numbers_;
        DamageEventManager damageEvents_;
        std::unique_ptr<IRenderer> renderer_;
        std::unordered_map<std::uint32_t, RE::ObjectRefHandle> targetBySerial_;
        Milliseconds nowMs_{ 0.0 };
        bool initialized_{ false };
        bool gameSystemsInitialized_{ false };
        bool renderingSuppressed_{ false };
        bool frameLoopLogged_{ false };
    };
}
