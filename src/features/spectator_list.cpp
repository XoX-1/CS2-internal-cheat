#include "spectator_list.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include "imgui.h"

#include <windows.h>
#include <string>
#include <vector>

// Bring memory functions into namespace scope for SEH compatibility
using Memory::SafeRead;
using Memory::IsValidPtr;

namespace SpectatorList {

    // Helper function that does NOT create C++ objects (like std::string) directly inside __try
    void GetSpectators(uintptr_t entityList, uintptr_t localPawn, char outNames[64][128], int& outCount) {
        outCount = 0;
        for (int i = 1; i <= Constants::EntityList::MAX_PLAYERS; i++) {
            __try {
                uintptr_t listEntry = 0;
                if (!SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                      sizeof(uintptr_t) * (i >> Constants::EntityList::ENTRY_SHIFT), listEntry) || 
                    !IsValidPtr(listEntry)) continue;

                uintptr_t controller = 0;
                if (!SafeRead(listEntry + Constants::EntityList::ENTRY_SIZE * (i & Constants::EntityList::INDEX_MASK), 
                                      controller) || !IsValidPtr(controller)) continue;

                uint32_t pawnHandle = 0;
                if (!SafeRead(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn, 
                                      pawnHandle) || !pawnHandle) continue;

                uintptr_t pawnEntry = 0;
                if (!SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                      sizeof(uintptr_t) * ((pawnHandle & Constants::EntityList::HANDLE_MASK) >> 
                                      Constants::EntityList::ENTRY_SHIFT), pawnEntry) || 
                    !IsValidPtr(pawnEntry)) continue;

                uintptr_t pawn = 0;
                if (!SafeRead(pawnEntry + Constants::EntityList::ENTRY_SIZE * (pawnHandle & Constants::EntityList::INDEX_MASK), 
                                      pawn) || !IsValidPtr(pawn) || pawn == localPawn) continue;

                int health = 0;
                uint8_t lifeState = 0;
                SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health);
                SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, lifeState);

                // We only care about dead players / observers
                if (health > 0 && lifeState == Constants::Game::LIFE_ALIVE) continue;

                uintptr_t observerServices = 0;
                if (!SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_pObserverServices, 
                                      observerServices) || !IsValidPtr(observerServices)) continue;
                
                uint32_t targetHandle = 0;
                if (!SafeRead(observerServices + cs2_dumper::schemas::client_dll::CPlayer_ObserverServices::m_hObserverTarget, 
                                      targetHandle) || !targetHandle) continue;

                uintptr_t targetPawnEntry = 0;
                if (!SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                      sizeof(uintptr_t) * ((targetHandle & Constants::EntityList::HANDLE_MASK) >> 
                                      Constants::EntityList::ENTRY_SHIFT), targetPawnEntry) || 
                    !IsValidPtr(targetPawnEntry)) continue;
                
                uintptr_t targetPawn = 0;
                if (!SafeRead(targetPawnEntry + Constants::EntityList::ENTRY_SIZE * (targetHandle & Constants::EntityList::INDEX_MASK), 
                                      targetPawn) || !IsValidPtr(targetPawn)) continue;

                // Check if they are watching us
                if (targetPawn == localPawn) {
                    char nameBuf[128] = { 0 };
                    uintptr_t nameAddr = controller + 
                                     cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName;
                    SIZE_T bytesRead = 0;
                    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(nameAddr), nameBuf, sizeof(nameBuf) - 1, &bytesRead) &&
                        bytesRead > 0 && nameBuf[0] > 0x20 && nameBuf[0] < 0x7F) {
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        for (int c = 0; c < 127; c++) {
                            outNames[outCount][c] = nameBuf[c];
                            if (nameBuf[c] == '\0') break;
                        }
                        outNames[outCount][127] = '\0';
                        outCount++;
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                continue;
            }
        }
    }

    void Render() {
        if (!Hooks::g_bSpectatorListEnabled.load()) return;

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        uintptr_t entityList = 0;
        if (!SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) ||
            !IsValidPtr(entityList)) return;

        uintptr_t localController = 0;
        if (!SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) ||
            !IsValidPtr(localController)) return;

        uintptr_t localPawn = Memory::ResolvePawnFromController(entityList, localController);
        if (!localPawn) return;

        char spectatorNames[64][128];
        int spectatorCount = 0;
        GetSpectators(entityList, localPawn, spectatorNames, spectatorCount);

        // --- RENDER SPECTATOR LIST UI ---
        ImGui::SetNextWindowSize(ImVec2(200, 0), ImGuiCond_Always);
        ImGui::Begin("Spectators", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | 
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);
        
        ImGui::Text("Watching You: %d", spectatorCount);
        
        ImGui::Separator();
        
        for (int i = 0; i < spectatorCount; i++) {
            ImGui::Text("%s", spectatorNames[i]);
        }

        if (spectatorCount == 0) {
            ImGui::TextDisabled("No spectators");
        }
        
        ImGui::End();
    }
}
