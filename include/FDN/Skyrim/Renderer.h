#pragma once

#include "FDN/FloatingNumberManager.h"

#include "RE/A/ActorValues.h"
#include "RE/B/BSPointerHandle.h"
#include "RE/H/HUDMenu.h"

namespace FDN::Skyrim
{
    class IRenderer
    {
    public:
        virtual ~IRenderer() = default;

        [[nodiscard]] virtual bool Initialize(const Settings& settings) = 0;
        [[nodiscard]] virtual bool BeginFrame(RE::HUDMenu& hudMenu) = 0;
        virtual void Render(const FloatingNumberUpdate& update, RE::ObjectRefHandle targetHandle) = 0;
        virtual void Clear() = 0;
        [[nodiscard]] virtual bool IsAvailable() const noexcept = 0;
    };
}
