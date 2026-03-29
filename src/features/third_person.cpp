#include "third_person.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <iostream>

namespace ThirdPerson {

    static uintptr_t s_tpResetAddr = 0;
    static bool s_lastTpState = false;

    // The signature provided by the user for the camera reset branch
    const uint8_t sigThirdPersonReset[] = { 
        0x48, 0x8B, 0x40, 0x08, 0x44, 0x38, 0x20, 0x75, 0x10, 0x44, 0x88, 0x67, 0x01
    };
    const char* maskThirdPersonReset = "xxxxxxxxxxxxx";

    void Initialize() {
        s_tpResetAddr = 0;
        s_lastTpState = false;
    }

    void Run() {
        if (!Hooks::IsGameReady()) return;

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        // 1. Find the patch address if we haven't already
        if (!s_tpResetAddr) {
            size_t clientSize = Memory::GetModuleSize(clientBase);
            if (clientSize > 0) {
                s_tpResetAddr = Memory::PatternScan(clientBase, clientSize, sigThirdPersonReset, maskThirdPersonReset);
            }
        }

        bool isEnabled = Hooks::g_bThirdPersonEnabled.load();

        // 2. Handle the assembly patch statefully: only write to .text section on toggle change
        if (s_tpResetAddr && isEnabled != s_lastTpState) {
            DWORD oldProtect;
            if (VirtualProtect(reinterpret_cast<void*>(s_tpResetAddr), 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                if (isEnabled) {
                    // Patch JNE (0x75) to JMP (0xEB) to skip camera reset
                    reinterpret_cast<uint8_t*>(s_tpResetAddr)[7] = 0xEB; 
                } else {
                    // Restore JNE (0x75)
                    reinterpret_cast<uint8_t*>(s_tpResetAddr)[7] = 0x75; 
                }
                VirtualProtect(reinterpret_cast<void*>(s_tpResetAddr), 16, oldProtect, &oldProtect);
                s_lastTpState = isEnabled;
            }
        }

        // 3. Keep writing the state continuously to survive respawns and prevent jittering
        Memory::SafeWrite<bool>(clientBase + cs2_dumper::offsets::client_dll::dwCSGOInput + 0x229, isEnabled);
    }

    void Shutdown() {
        // Restore the .text JNE patch only if the address is still valid.
        // During warmup-end the game reloads the map so client.dll may be
        // mid-rebuild; VirtualProtect on a stale address causes a crash.
        // We use IsBadWritePtr as a lightweight guard before touching the page.
        if (s_tpResetAddr) {
            if (!IsBadWritePtr(reinterpret_cast<void*>(s_tpResetAddr), 16)) {
                DWORD oldProtect;
                if (VirtualProtect(reinterpret_cast<void*>(s_tpResetAddr), 16, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                    reinterpret_cast<uint8_t*>(s_tpResetAddr)[7] = 0x75; // Restore JNE
                    VirtualProtect(reinterpret_cast<void*>(s_tpResetAddr), 16, oldProtect, &oldProtect);
                }
            }
            // Always zero the cached address so Run() rescans on the next map.
            s_tpResetAddr = 0;
        }
        s_lastTpState = false;

        // Disable the third-person camera flag only if the module is still live.
        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (clientBase) {
            Memory::SafeWrite<bool>(clientBase + cs2_dumper::offsets::client_dll::dwCSGOInput + 0x229, false);
        }
    }
}
