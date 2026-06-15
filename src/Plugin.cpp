#include "FDN/Skyrim/Runtime.h"

#include "SKSE/SKSE.h"

#include <exception>
#include <memory>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <utility>

namespace
{
    void InitializeLogger()
    {
        try {
            auto path = SKSE::log::log_directory();
            if (!path) {
                return;
            }

            *path /= "FloatingDamageNumbersNG.log";
            auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
            auto logger = std::make_shared<spdlog::logger>("FloatingDamageNumbersNG", std::move(sink));
            logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
            logger->set_level(spdlog::level::info);
            spdlog::set_default_logger(std::move(logger));
            spdlog::flush_on(spdlog::level::info);
        } catch (...) {
            spdlog::set_level(spdlog::level::off);
        }
    }

    void MessageHandler(SKSE::MessagingInterface::Message* message)
    {
        try {
            if (!message) {
                return;
            }

            switch (message->type) {
            case SKSE::MessagingInterface::kDataLoaded:
                FDN::Skyrim::RuntimeService::Get().InitializeGameSystems();
                break;
            case SKSE::MessagingInterface::kPreLoadGame:
            case SKSE::MessagingInterface::kNewGame:
                FDN::Skyrim::RuntimeService::Get().Clear();
                break;
            default:
                break;
            }
        } catch (const std::exception& e) {
            spdlog::error("Unhandled exception in SKSE message handler: {}", e.what());
        } catch (...) {
            spdlog::error("Unhandled non-standard exception in SKSE message handler");
        }
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    try {
        SKSE::Init(skse);
        InitializeLogger();

        spdlog::info("{} {} loading", FDN_PLUGIN_NAME, FDN_PLUGIN_VERSION);

        auto& runtime = FDN::Skyrim::RuntimeService::Get();
        if (!runtime.Initialize()) {
            spdlog::error("Runtime initialization failed; plugin disabled");
            return true;
        }

        auto* messaging = SKSE::GetMessagingInterface();
        if (!messaging || !messaging->RegisterListener(MessageHandler)) {
            spdlog::error("Failed to register SKSE messaging listener");
            return true;
        }

        spdlog::info("SKSE messaging listener registered");
    } catch (const std::exception& e) {
        spdlog::error("Unhandled exception during plugin load: {}", e.what());
    } catch (...) {
        spdlog::error("Unhandled non-standard exception during plugin load");
    }
    return true;
}
