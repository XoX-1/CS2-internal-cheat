#include "player_fov.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>

namespace PlayerFov {

    static uint32_t s_originalFov = Constants::Game::DEFAULT_FOV;
    static bool s_hasStoredOriginal = false;
    static int s_restoreAttempts = 0;

    void Run() {
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

            // CS2 handles FOV in the CameraServices component of the Pawn
            uintptr_t cameraServices = 0;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_pCameraServices, cameraServices) || 
                !Memory::IsValidPtr(cameraServices)) return;

            uint32_t currentFov = 0;
            Memory::SafeRead(cameraServices + cs2_dumper::schemas::client_dll::CCSPlayerBase_CameraServices::m_iFOV, currentFov);

            if (Hooks::g_bFovChangerEnabled.load()) {
                // Reset restore attempts when enabled
                s_restoreAttempts = 0;
                
                // Store original FOV when first enabling (only once)
                if (!s_hasStoredOriginal) {
                    s_originalFov = currentFov;
                    if (s_originalFov < 60 || s_originalFov > 150) {
                        s_originalFov = Constants::Game::DEFAULT_FOV; // Fallback to default if invalid
                    }
                    s_hasStoredOriginal = true;
                }

                // Avoid modifying if scoped (unless you also want zoom FOV modified)
                bool isScoped = false;
                Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_bIsScoped, isScoped);
                
                if (!isScoped) {
                    uint32_t desiredFov = static_cast<uint32_t>(Hooks::g_fPlayerFov.load());
                    // Always write to ensure it sticks
                    Memory::SafeWrite<uint32_t>(cameraServices + cs2_dumper::schemas::client_dll::CCSPlayerBase_CameraServices::m_iFOV, desiredFov);
                }
            } else {
                // Restore original FOV when disabled
                if (s_hasStoredOriginal && s_restoreAttempts < Constants::Timing::RESTORE_ATTEMPTS_MAX) {
                    // Keep trying to restore for up to 100 frames
                    Memory::SafeWrite<uint32_t>(cameraServices + cs2_dumper::schemas::client_dll::CCSPlayerBase_CameraServices::m_iFOV, s_originalFov);
                    s_restoreAttempts++;
                    
                    // After 100 attempts, mark as restored
                    if (s_restoreAttempts >= Constants::Timing::RESTORE_ATTEMPTS_MAX) {
                        s_hasStoredOriginal = false;
                        s_restoreAttempts = 0;
                    }
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }
}
