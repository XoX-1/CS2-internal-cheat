#include "bomb_timer.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include "imgui.h"
#include <windows.h>

using Memory::SafeRead;
using Memory::IsValidPtr;

namespace BombTimer {

    // Use offsets from the dumped client_dll.hpp
    using namespace cs2_dumper::schemas::client_dll::C_PlantedC4;

    // Auto-detect the correct curtime offset within GlobalVars
    // Different CS2 builds have curtime at different offsets
    static ptrdiff_t s_cachedCurtimeOffset = -1;

    static float ReadCurrentTime(uintptr_t clientBase, float c4BlowTime = -1.0f) {
        uintptr_t globalVars = 0;
        if (!SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwGlobalVars, globalVars) ||
            !IsValidPtr(globalVars))
            return -1.0f;

        // If we already found the working offset, use it
        if (s_cachedCurtimeOffset >= 0) {
            float t = 0.0f;
            if (SafeRead(globalVars + s_cachedCurtimeOffset, t) && t > 0.0f)
                return t;
            // Reset cache if it stops working
            s_cachedCurtimeOffset = -1;
        }

        // Try common GlobalVars curtime offsets
        // CS2 CGlobalVarsBase layout varies between builds
        const ptrdiff_t candidates[] = { 
            0x2C, 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44, 0x10, 0x14, 0x18, 0x48 
        };

        for (auto off : candidates) {
            float t = 0.0f;
            if (!SafeRead(globalVars + off, t) || t <= 0.0f)
                continue;

            // If we have a c4BlowTime reference, validate against it
            if (c4BlowTime > 0.0f) {
                float diff = c4BlowTime - t;
                if (diff >= 0.0f && diff <= 45.0f) {
                    s_cachedCurtimeOffset = off; // Cache the working offset
                    return t;
                }
            } else if (t > 1.0f && t < 999999999.0f) {
                // Without reference, just return a plausible game time
                return t;
            }
        }
        return -1.0f;
    }

    // Try multiple pointer chain strategies to find the C_PlantedC4 entity
    static uintptr_t FindPlantedC4(uintptr_t clientBase) {
        uintptr_t raw = 0;
        if (!SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwPlantedC4, raw) || !IsValidPtr(raw))
            return 0;

        // Strategy 1: raw is a C_PlantedC4* directly
        // Check if m_bBombTicking at this address reads as true
        bool ticking1 = false;
        if (SafeRead(raw + m_bBombTicking, ticking1) && ticking1) {
            // Verify blow time is also reasonable
            float blowTime = 0.0f;
            if (SafeRead(raw + m_flC4Blow, blowTime) && blowTime > 0.0f)
                return raw;
        }

        // Strategy 2: raw is a pointer-to-pointer (list)
        // Read first element from the list
        uintptr_t entity = 0;
        if (SafeRead(raw, entity) && IsValidPtr(entity)) {
            bool ticking2 = false;
            if (SafeRead(entity + m_bBombTicking, ticking2) && ticking2) {
                float blowTime = 0.0f;
                if (SafeRead(entity + m_flC4Blow, blowTime) && blowTime > 0.0f)
                    return entity;
            }
        }

        // Strategy 3: raw points to a CUtlVector — skip 8 bytes (size field) to get data ptr
        uintptr_t dataPtr = 0;
        if (SafeRead(raw + 0x8, dataPtr) && IsValidPtr(dataPtr)) {
            uintptr_t entity3 = 0;
            if (SafeRead(dataPtr, entity3) && IsValidPtr(entity3)) {
                bool ticking3 = false;
                if (SafeRead(entity3 + m_bBombTicking, ticking3) && ticking3) {
                    float blowTime = 0.0f;
                    if (SafeRead(entity3 + m_flC4Blow, blowTime) && blowTime > 0.0f)
                        return entity3;
                }
            }
        }

        return 0;
    }

    void Render() {
        if (!Hooks::g_bBombTimerEnabled.load()) return;

        // Always show the window (like spectator list)
        ImGui::SetNextWindowSize(ImVec2(200, 0), ImGuiCond_Always);
        ImGui::Begin("Bomb Timer", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize);

        if (!Hooks::IsGameReady()) {
            ImGui::TextDisabled("Waiting for game...");
            ImGui::End();
            return;
        }

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) {
            ImGui::TextDisabled("No client module");
            ImGui::End();
            return;
        }

        uintptr_t plantedC4 = FindPlantedC4(clientBase);
        if (!plantedC4) {
            ImGui::Text("No bomb planted");
            ImGui::End();
            return;
        }

        // Check if defused
        bool isDefused = false;
        SafeRead(plantedC4 + m_bBombDefused, isDefused);
        if (isDefused) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.2f, 1.0f), "Bomb Defused!");
            ImGui::End();
            return;
        }

        // Check if exploded (prevents stale timer carrying into next round)
        bool hasExploded = false;
        SafeRead(plantedC4 + m_bHasExploded, hasExploded);
        if (hasExploded) {
            ImGui::Text("No bomb planted");
            ImGui::End();
            return;
        }

        // Read blow time and curtime
        float c4BlowTime = 0.0f;
        SafeRead(plantedC4 + m_flC4Blow, c4BlowTime);

        float currentTime = ReadCurrentTime(clientBase, c4BlowTime);
        if (currentTime <= 0.0f || c4BlowTime <= 0.0f) {
            ImGui::TextDisabled("Reading bomb data...");
            ImGui::End();
            return;
        }

        float remaining = c4BlowTime - currentTime;

        if (remaining < 0.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "EXPLODED!");
            ImGui::End();
            return;
        }
        if (remaining > 45.0f) {
            ImGui::TextDisabled("Timer sync...");
            ImGui::End();
            return;
        }

        // Color coding
        ImVec4 timeColor;
        if (remaining <= 5.0f) {
            timeColor = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
        } else if (remaining <= 10.0f) {
            timeColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        } else {
            timeColor = ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
        }

        ImGui::Text("Bomb Planted");
        ImGui::Separator();
        ImGui::TextColored(timeColor, "%.1f s", remaining);

        if (remaining <= 5.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), "RUN!");
        } else if (remaining <= 10.0f) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "No-kit defuse impossible");
        }

        float progress = remaining / 40.0f;
        if (progress > 1.0f) progress = 1.0f;
        ImGui::ProgressBar(progress, ImVec2(-1, 4), "");

        ImGui::End();
    }
}
