#include "FDN/Skyrim/HUDHooks.h"

#include "FDN/Skyrim/Runtime.h"

#include "RE/A/ActorValues.h"
#include "RE/H/HUDMenu.h"
#include "REL/Relocation.h"

#include <exception>
#include <spdlog/spdlog.h>

namespace FDN::Skyrim::HUDHooks
{
    namespace
    {
        using AdvanceMovie_t = void(RE::HUDMenu*, float, std::uint32_t);
        REL::Relocation<AdvanceMovie_t> originalAdvanceMovie;
        bool installed = false;

        void AdvanceMovie(RE::HUDMenu* hudMenu, float interval, std::uint32_t currentTime)
        {
            originalAdvanceMovie(hudMenu, interval, currentTime);
            try {
                if (hudMenu) {
                    RuntimeService::Get().OnFrame(interval, *hudMenu);
                }
            } catch (const std::exception& e) {
                spdlog::error("Unhandled exception during HUD update: {}", e.what());
            } catch (...) {
                spdlog::error("Unhandled non-standard exception during HUD update");
            }
        }
    }

    bool Install()
    {
        if (installed) {
            return true;
        }

        REL::Relocation<std::uintptr_t> vtbl{ RE::VTABLE_HUDMenu[0] };
        // CommonLibSSE declares HUDMenu::AdvanceMovie as virtual slot 0x05 in RE/H/HUDMenu.h.
        originalAdvanceMovie = vtbl.write_vfunc(0x05, AdvanceMovie);
        installed = true;
        spdlog::info("HUDMenu::AdvanceMovie hook installed");
        return true;
    }
}
