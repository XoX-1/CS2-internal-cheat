#include "glow.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>

namespace Glow {

    struct ColorRGBA {
        uint8_t r, g, b, a;
    };

    void Run() {
        if (!Hooks::g_bGlowEnabled.load()) return;

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t entityList = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
                !Memory::IsValidPtr(entityList)) return;

            uintptr_t localPawn = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
                !Memory::IsValidPtr(localPawn)) return;

            uint8_t localTeam = 0;
            Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, localTeam);

            for (int i = 1; i <= Constants::EntityList::MAX_PLAYERS; i++) {
                uintptr_t listEntry = 0;
                if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                      sizeof(uintptr_t) * (i >> Constants::EntityList::ENTRY_SHIFT), listEntry) || 
                    !Memory::IsValidPtr(listEntry)) continue;

                uintptr_t controller = 0;
                if (!Memory::SafeRead(listEntry + Constants::EntityList::ENTRY_SIZE * (i & Constants::EntityList::INDEX_MASK), 
                                      controller) || !Memory::IsValidPtr(controller)) continue;

                uint32_t pawnHandle = 0;
                if (!Memory::SafeRead(controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn, 
                                      pawnHandle) || !pawnHandle) continue;

                uintptr_t pawnEntry = 0;
                if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE + 
                                      sizeof(uintptr_t) * ((pawnHandle & Constants::EntityList::HANDLE_MASK) >> 
                                      Constants::EntityList::ENTRY_SHIFT), pawnEntry) || 
                    !Memory::IsValidPtr(pawnEntry)) continue;

                uintptr_t pawn = 0;
                if (!Memory::SafeRead(pawnEntry + Constants::EntityList::ENTRY_SIZE * (pawnHandle & Constants::EntityList::INDEX_MASK), 
                                      pawn) || !Memory::IsValidPtr(pawn) || pawn == localPawn) continue;

                int health = 0;
                uint8_t lifeState = 1;
                uint8_t team = 0;
                Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health);
                Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, lifeState);
                Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, team);

                if (health <= 0 || lifeState != Constants::Game::LIFE_ALIVE) continue;

                // Determine color based on team
                ColorRGBA color;
                if (!Hooks::g_bFFAEnabled.load() && team == localTeam) {
                    if (!Hooks::g_bGlowTeamEnabled.load()) continue; // Skip teammates if Team Glow is off
                    color.r = static_cast<uint8_t>(Hooks::g_fGlowTeamColor[0] * 255.0f);
                    color.g = static_cast<uint8_t>(Hooks::g_fGlowTeamColor[1] * 255.0f);
                    color.b = static_cast<uint8_t>(Hooks::g_fGlowTeamColor[2] * 255.0f);
                    color.a = static_cast<uint8_t>(Hooks::g_fGlowTeamColor[3] * 255.0f);
                } else {
                    color.r = static_cast<uint8_t>(Hooks::g_fGlowEnemyColor[0] * 255.0f);
                    color.g = static_cast<uint8_t>(Hooks::g_fGlowEnemyColor[1] * 255.0f);
                    color.b = static_cast<uint8_t>(Hooks::g_fGlowEnemyColor[2] * 255.0f);
                    color.a = static_cast<uint8_t>(Hooks::g_fGlowEnemyColor[3] * 255.0f);
                }

                // Apply glow
                uintptr_t glowOffset = pawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_Glow;
                Memory::SafeWrite<bool>(glowOffset + cs2_dumper::schemas::client_dll::CGlowProperty::m_bGlowing, true);
                Memory::SafeWrite<ColorRGBA>(glowOffset + cs2_dumper::schemas::client_dll::CGlowProperty::m_glowColorOverride, color);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }
}
