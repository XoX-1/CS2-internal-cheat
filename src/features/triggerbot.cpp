#include "triggerbot.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "keybind_manager.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <chrono>

// Settings - atomic initialization
namespace Triggerbot {
    std::atomic<bool> g_bEnabled{false};
    std::atomic<bool> g_bTeamCheck{true};
    std::atomic<int> g_nFireMode{MODE_TAPPING};
    std::atomic<float> g_fMaxDistance{5000.0f};

    // State tracking
    static std::chrono::steady_clock::time_point s_lastShotTime;
    static bool s_wasTriggering = false;
    static int s_shotsFired = 0;
    static DWORD s_lastEntityId = 0;

    void Run() {
        if (!g_bEnabled.load()) {
            if (s_wasTriggering) mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            s_wasTriggering = false;
            s_shotsFired = 0;
            s_lastEntityId = 0;
            return;
        }

        // Only run when TRIGGERBOT key is held
        if (!KeybindManager::IsTriggerbotKeyPressed()) {
            if (s_wasTriggering) mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            s_wasTriggering = false;
            s_shotsFired = 0;
            s_lastEntityId = 0;
            return;
        }

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            // Get local player pawn
            uintptr_t localPawn = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
                !Memory::IsValidPtr(localPawn)) {
                return;
            }

            // Check if local player is alive
            int localHealth = 0;
            uint8_t localLifeState = 1;
            Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, localHealth);
            Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, localLifeState);
            
            if (localLifeState != Constants::Game::LIFE_ALIVE || localHealth <= 0) {
                return; // Local player is dead
            }

            // Get local team
            uint8_t localTeam = 0;
            Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, localTeam);

            // Read entity ID under crosshair (m_iIDEntIndex)
            int idEntIndex = 0;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iIDEntIndex, idEntIndex)) {
                return;
            }

            // If no entity under crosshair, reset and return
            if (idEntIndex <= 0 || idEntIndex > 2048) {
                if (s_wasTriggering) mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                s_wasTriggering = false;
                s_shotsFired = 0;
                s_lastEntityId = 0;
                return;
            }

            // Check if entity changed - if so, reset burst counter
            if ((DWORD)idEntIndex != s_lastEntityId) {
                if (s_wasTriggering) mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                s_shotsFired = 0;
                s_wasTriggering = false;
            }
            s_lastEntityId = (DWORD)idEntIndex;

            // Get entity list
            uintptr_t entityList = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
                !Memory::IsValidPtr(entityList)) {
                return;
            }

            // Get the entity under crosshair from entity list
            uintptr_t listEntry = 0;
            if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                  sizeof(uintptr_t) * ((idEntIndex & Constants::EntityList::HANDLE_MASK) >> Constants::EntityList::ENTRY_SHIFT), 
                                  listEntry) || !Memory::IsValidPtr(listEntry)) {
                return;
            }

            uintptr_t targetEntity = 0;
            if (!Memory::SafeRead(listEntry + Constants::EntityList::ENTRY_SIZE * (idEntIndex & Constants::EntityList::INDEX_MASK), 
                                  targetEntity) || !Memory::IsValidPtr(targetEntity)) {
                return;
            }

            // Skip if target is local player
            if (targetEntity == localPawn) {
                return;
            }

            // Check if entity is a player pawn by checking health
            int targetHealth = 0;
            uint8_t targetLifeState = 1;
            Memory::SafeRead(targetEntity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, targetHealth);
            Memory::SafeRead(targetEntity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, targetLifeState);

            if (targetHealth <= 0 || targetLifeState != Constants::Game::LIFE_ALIVE) {
                return; // Target is dead or not a player
            }

            // Team check
            if (g_bTeamCheck.load() && !Hooks::g_bFFAEnabled.load()) {
                uint8_t targetTeam = 0;
                Memory::SafeRead(targetEntity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, targetTeam);
                
                if (targetTeam == localTeam || targetTeam == 0) {
                    return; // Don't shoot teammates or spectators
                }
            }

            // Tapping mode: rapid single shots (click-release each frame)
            if (g_nFireMode.load() == MODE_TAPPING) {
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                Sleep(Constants::Timing::TRIGGER_DELAY_MS);
                mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
                s_shotsFired++;
                s_wasTriggering = true;
            } else {
                // Lazer mode: hold mouse down while on target
                mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
                s_wasTriggering = true;
            }

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (s_wasTriggering) mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
            s_wasTriggering = false;
        }
    }

    void Reset() {
        if (s_wasTriggering) mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
        s_wasTriggering = false;
        s_shotsFired = 0;
        s_lastEntityId = 0;
    }
} // namespace Triggerbot
