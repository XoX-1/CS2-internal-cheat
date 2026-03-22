#include "noflash.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <iostream>

namespace NoFlash {

    void Run() {
        if (!Hooks::g_bNoFlashEnabled.load()) return;

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t entityList = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) ||
                !Memory::IsValidPtr(entityList)) return;

            uintptr_t localController = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) ||
                !Memory::IsValidPtr(localController)) return;

            uintptr_t localPawn = Memory::ResolvePawnFromController(entityList, localController);
            if (!localPawn) return;

            float flashAlpha = 0.0f;
            if (Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashMaxAlpha, flashAlpha)) {
                if (flashAlpha > 0.0f) {
                    Memory::SafeWrite<float>(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashMaxAlpha, 0.0f);
                    Memory::SafeWrite<float>(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration, 0.0f);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }
}
