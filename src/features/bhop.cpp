#include "bhop.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <cstdint>

namespace Bunnyhop {

    // Track ground state
    static bool s_wasOnGround = false;
    static bool s_autoHopActive = false;

    void Run() {
        if (!Hooks::g_bBhopEnabled.load()) {
            s_wasOnGround = false;
            s_autoHopActive = false;
            return;
        }

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t localPawn = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
                !Memory::IsValidPtr(localPawn)) return;

            // Read movement flags
            uint32_t flags = 0;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags, flags)) return;

            bool onGround = (flags & Constants::Game::FL_ONGROUND) != 0;

            // Check if jump key is held (user pressing space)
            bool spaceHeld = (GetAsyncKeyState(Constants::Keys::BHOP) & 0x8000) != 0;
            
            // If user just started holding space, activate auto-hop
            if (spaceHeld && !s_autoHopActive) {
                s_autoHopActive = true;
            }
            
            // Deactivate auto-hop if user releases space
            if (!spaceHeld) {
                s_autoHopActive = false;
            }
            
            if (s_autoHopActive && onGround) {
                // Auto-hop: press jump immediately when on ground
                // Use keybd_event - simpler and we know it works
                keybd_event(VK_SPACE, 0x39, 0, 0);    // Key down
                keybd_event(VK_SPACE, 0x39, KEYEVENTF_KEYUP, 0);  // Key up immediately
                s_wasOnGround = true;
            } else if (!onGround && s_wasOnGround) {
                // Was on ground last frame, now in air - we jumped!
                s_wasOnGround = false;
            } else if (onGround) {
                s_wasOnGround = true;
            } else {
                s_wasOnGround = false;
            }
            
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
            s_wasOnGround = false;
            s_autoHopActive = false;
        }
    }

    void Reset() {
        s_wasOnGround = false;
        s_autoHopActive = false;
    }
}
