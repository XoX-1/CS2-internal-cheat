#include "bhop.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include <buttons.hpp>
#include <windows.h>
#include <cstdint>

namespace Bunnyhop {

    // CS2 button state values for direct memory write
    // The jump button global at clientBase + buttons::jump is a uint32 state:
    //   65537 (0x10001) = force press
    //   256   (0x100)   = release / idle
    static constexpr uint32_t BTN_FORCE_DOWN = 65537;
    static constexpr uint32_t BTN_RELEASE    = 256;

    // State: tracks whether we pressed jump and are waiting for airborne release
    static bool s_jumpPressed = false;

    void Run() {
        if (!Hooks::g_bBhopEnabled.load()) {
            s_jumpPressed = false;
            return;
        }

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

            // Read movement flags
            uint32_t flags = 0;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_fFlags, flags)) return;

            bool onGround = (flags & Constants::Game::FL_ONGROUND) != 0;
            bool spaceHeld = (GetAsyncKeyState(Constants::Keys::BHOP) & 0x8000) != 0;

            if (!spaceHeld) {
                // User released space — make sure button is released
                if (s_jumpPressed) {
                    Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_RELEASE);
                    s_jumpPressed = false;
                }
                return;
            }

            // Space is held — bhop logic
            if (onGround) {
                // On ground: press jump
                Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_FORCE_DOWN);
                s_jumpPressed = true;
            } else if (s_jumpPressed) {
                // In air after our jump: release so it can re-trigger on next landing
                Memory::SafeWrite<uint32_t>(clientBase + cs2_dumper::buttons::jump, BTN_RELEASE);
                s_jumpPressed = false;
            }
            
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s_jumpPressed = false;
        }
    }

    // Called from game thread (Present hook) — zeroes stamina & velocity modifier
    // to prevent any speed penalty from consecutive jumps.
    void RunGameThread() {
        if (!Hooks::g_bBhopEnabled.load()) return;

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

            // Zero stamina on MovementServices — prevents landing slowdown
            uintptr_t moveServices = 0;
            if (Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_pMovementServices, moveServices) &&
                Memory::IsValidPtr(moveServices)) {
                Memory::SafeWrite<float>(moveServices + cs2_dumper::schemas::client_dll::CCSPlayer_MovementServices::m_flStamina, 0.0f);
            }

            // Keep velocity modifier at 1.0 — prevents any speed reduction
            Memory::SafeWrite<float>(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_flVelocityModifier, 1.0f);

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }

    void Reset() {
        s_jumpPressed = false;
    }
}
