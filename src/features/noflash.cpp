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

            uintptr_t localPawn = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
                !Memory::IsValidPtr(localPawn)) return;

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
