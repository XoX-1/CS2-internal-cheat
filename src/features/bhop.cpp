#include "bhop.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <iostream>

namespace Bunnyhop {

    void Run() {
        if (!Hooks::g_bBhopEnabled.load()) return;

        // Only run if the user is holding their jump key (Spacebar)
        if (!(GetAsyncKeyState(Constants::Keys::BHOP) & 0x8000)) return;

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t localPawn = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
                !Memory::IsValidPtr(localPawn)) return;

            uint32_t flags = 0;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags, flags)) return;

            // CS2 jumping mechanism simulation
            // Since this is an internal cheat without SendInput/CreateMove hooks yet,
            // we simulate the spacebar release and press via SendMessage to trick the engine.
            bool onGround = (flags & Constants::Game::FL_ONGROUND) != 0;

            if (!onGround) {
                // In air, release spacebar to reset jump state
                SendMessage(GetForegroundWindow(), WM_KEYUP, Constants::Keys::BHOP, 0);
            } else {
                // On ground, press spacebar to jump instantly
                SendMessage(GetForegroundWindow(), WM_KEYDOWN, Constants::Keys::BHOP, 0);
            }
            
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }
}
