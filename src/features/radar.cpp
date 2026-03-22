#include "radar.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/math.hpp"
#include "imgui.h"
#include "../vendor/imgui/IconsFontAwesome6.h"

#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <cmath>
#include <cstdio>

// =====================================================================
// Radar Configuration Constants
// =====================================================================

static constexpr float PI = 3.14159265358979323846f;
static constexpr float DEG2RAD = PI / 180.0f;

// =====================================================================
// Accent Colors (matching the main menu theme)
// =====================================================================

static constexpr ImVec4 kRadarAccent     = ImVec4(0.44f, 0.31f, 0.86f, 1.00f);
static constexpr ImVec4 kRadarBg         = ImVec4(0.067f, 0.067f, 0.082f, 0.92f);
static constexpr ImVec4 kRadarGridColor  = ImVec4(0.18f, 0.16f, 0.25f, 0.40f);
static constexpr ImVec4 kRadarTextDim    = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
static constexpr ImVec4 kRadarTextBright = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);

static ImU32 ToU32(ImVec4 c) {
    return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(c.w * 255));
}

// =====================================================================
// Radar Drawing Helpers
// =====================================================================

// Rotate a 2D point around origin by angle (radians)
static ImVec2 RotatePoint(float x, float y, float angle) {
    float cosA = cosf(angle);
    float sinA = sinf(angle);
    return ImVec2(x * cosA - y * sinA, x * sinA + y * cosA);
}

// Draw a filled diamond (player marker) with optional outline
static void DrawPlayerMarker(ImDrawList* dl, ImVec2 center, float size, ImU32 fillColor, ImU32 outlineColor, float rotation) {
    // Create a rotated diamond shape
    ImVec2 points[4];
    float halfSize = size * 0.5f;

    // Diamond points relative to center
    float pts[4][2] = {
        {  0.0f, -halfSize * 1.3f },  // Top (pointed, direction indicator)
        {  halfSize,  0.0f },          // Right
        {  0.0f,  halfSize * 0.8f },   // Bottom
        { -halfSize,  0.0f }           // Left
    };

    for (int i = 0; i < 4; i++) {
        ImVec2 rotated = RotatePoint(pts[i][0], pts[i][1], rotation);
        points[i] = ImVec2(center.x + rotated.x, center.y + rotated.y);
    }

    // Fill
    dl->AddConvexPolyFilled(points, 4, fillColor);
    // Outline
    for (int i = 0; i < 4; i++) {
        int next = (i + 1) % 4;
        dl->AddLine(points[i], points[next], outlineColor, 1.0f);
    }
}

// Draw the local player arrow (larger, accent-colored)
static void DrawLocalPlayerArrow(ImDrawList* dl, ImVec2 center, float size, float rotation) {
    ImVec2 points[3];
    float halfSize = size * 0.5f;

    // Triangle pointing up (direction indicator)
    float pts[3][2] = {
        {  0.0f, -halfSize * 1.5f },   // Top point (forward direction)
        {  halfSize * 0.8f,  halfSize * 0.6f },  // Bottom right
        { -halfSize * 0.8f,  halfSize * 0.6f }   // Bottom left
    };

    for (int i = 0; i < 3; i++) {
        ImVec2 rotated = RotatePoint(pts[i][0], pts[i][1], rotation);
        points[i] = ImVec2(center.x + rotated.x, center.y + rotated.y);
    }

    // Glow effect
    ImU32 glowColor = IM_COL32(112, 80, 219, 40);
    dl->AddTriangleFilled(
        ImVec2(center.x, center.y - halfSize * 2.0f),
        ImVec2(center.x + halfSize * 1.2f, center.y + halfSize * 1.0f),
        ImVec2(center.x - halfSize * 1.2f, center.y + halfSize * 1.0f),
        glowColor);

    // Main fill
    dl->AddConvexPolyFilled(points, 3, ToU32(kRadarAccent));
    // Outline
    for (int i = 0; i < 3; i++) {
        int next = (i + 1) % 3;
        dl->AddLine(points[i], points[next], IM_COL32(200, 180, 255, 220), 1.5f);
    }
}

// =====================================================================
// Radar Rendering
// =====================================================================

namespace Radar {
    void Render() {
        // Load radar settings
        if (!Hooks::g_bRadarEnabled.load()) return;

        float radarSize   = Hooks::g_fRadarSize.load();
        float radarRange  = Hooks::g_fRadarRange.load();
        float radarZoom   = Hooks::g_fRadarZoom.load();
        bool  showNames   = Hooks::g_bRadarShowNames.load();
        bool  showHealth  = Hooks::g_bRadarShowHealth.load();
        int   radarStyle  = Hooks::g_nRadarStyle.load();
        float radarAlpha  = Hooks::g_fRadarAlpha.load();

        // Apply zoom to range
        float effectiveRange = radarRange / radarZoom;

        // ============ Get game state ============
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

        // Get local player team
        uint8_t localTeam = 0;
        Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, localTeam);

        // Get local player position
        Vector3 localOrigin = { 0, 0, 0 };
        uintptr_t localSceneNode = 0;
        if (Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, localSceneNode) &&
            Memory::IsValidPtr(localSceneNode)) {
            Memory::SafeRead(localSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin, localOrigin);
        }

        // Get local player view angles (for rotating the radar)
        Vector3 localAngles = { 0, 0, 0 };
        Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles, localAngles);
        float localYawRad = localAngles.y * DEG2RAD;

        // ============ Draw radar window ============
        ImGuiIO& io = ImGui::GetIO();
        float padding = 15.0f;

        // Position: configurable via dragging
        ImGui::SetNextWindowSize(ImVec2(radarSize + 30, radarSize + 55), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(radarAlpha);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(kRadarBg.x, kRadarBg.y, kRadarBg.z, radarAlpha));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.18f, 0.35f, 0.6f));
        ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.05f, 0.05f, 0.07f, radarAlpha));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.06f, 0.06f, 0.08f, radarAlpha));

        ImGui::Begin("##radar_window", nullptr,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoTitleBar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        // ============ Radar header ============
        float headerH = 28.0f;
        ImVec2 headerStart = winPos;
        ImVec2 headerEnd = ImVec2(winPos.x + winSize.x, winPos.y + headerH);

        dl->AddRectFilled(headerStart, headerEnd, IM_COL32(14, 13, 21, (int)(radarAlpha * 255)),
                          10.0f, ImDrawFlags_RoundCornersTop);

        // Title text
        ImGui::SetCursorPos(ImVec2(10, 5));
        ImGui::PushStyleColor(ImGuiCol_Text, kRadarAccent);
        ImGui::Text(ICON_FA_SATELLITE_DISH);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 5);
        ImGui::PushStyleColor(ImGuiCol_Text, kRadarTextBright);
        ImGui::Text("RADAR");
        ImGui::PopStyleColor();

        // Range indicator
        char rangeText[32];
        snprintf(rangeText, sizeof(rangeText), "%.0fm", effectiveRange / 100.0f);
        ImVec2 rangeSz = ImGui::CalcTextSize(rangeText);
        ImGui::SameLine(winSize.x - rangeSz.x - 15);
        ImGui::PushStyleColor(ImGuiCol_Text, kRadarTextDim);
        ImGui::Text("%s", rangeText);
        ImGui::PopStyleColor();

        // Accent line under header
        dl->AddRectFilled(ImVec2(winPos.x, headerEnd.y - 2), ImVec2(winPos.x + winSize.x, headerEnd.y),
                          ToU32(kRadarAccent), 1.0f);

        // ============ Radar canvas ============
        float canvasStartY = headerH + 8;
        float canvasSize = (winSize.x < winSize.y - canvasStartY - 8) ?
                           winSize.x - 20 : winSize.y - canvasStartY - 16;
        if (canvasSize < 50.0f) canvasSize = 50.0f;

        float radarRadius = canvasSize * 0.5f;
        ImVec2 radarCenter = ImVec2(
            winPos.x + winSize.x * 0.5f,
            winPos.y + canvasStartY + canvasSize * 0.5f + 4
        );

        // ============ Background ============
        if (radarStyle == 0) {
            // Circular radar
            dl->AddCircleFilled(radarCenter, radarRadius, IM_COL32(10, 9, 16, (int)(radarAlpha * 240)), 64);
            dl->AddCircle(radarCenter, radarRadius, ToU32(kRadarGridColor), 64, 1.5f);
        } else {
            // Square radar
            dl->AddRectFilled(
                ImVec2(radarCenter.x - radarRadius, radarCenter.y - radarRadius),
                ImVec2(radarCenter.x + radarRadius, radarCenter.y + radarRadius),
                IM_COL32(10, 9, 16, (int)(radarAlpha * 240)), 4.0f);
            dl->AddRect(
                ImVec2(radarCenter.x - radarRadius, radarCenter.y - radarRadius),
                ImVec2(radarCenter.x + radarRadius, radarCenter.y + radarRadius),
                ToU32(kRadarGridColor), 4.0f, 0, 1.5f);
        }

        // ============ Grid lines ============
        ImU32 gridColor = IM_COL32(30, 25, 50, (int)(radarAlpha * 180));
        ImU32 gridColorFaint = IM_COL32(25, 20, 40, (int)(radarAlpha * 100));

        if (radarStyle == 0) {
            // Circular grid: concentric rings
            for (int ring = 1; ring <= 3; ring++) {
                float ringRadius = radarRadius * (ring / 4.0f);
                dl->AddCircle(radarCenter, ringRadius, gridColorFaint, 48, 1.0f);
            }
            // Cross lines
            dl->AddLine(ImVec2(radarCenter.x - radarRadius, radarCenter.y),
                        ImVec2(radarCenter.x + radarRadius, radarCenter.y), gridColor, 0.8f);
            dl->AddLine(ImVec2(radarCenter.x, radarCenter.y - radarRadius),
                        ImVec2(radarCenter.x, radarCenter.y + radarRadius), gridColor, 0.8f);
        } else {
            // Square grid
            int gridLines = 4;
            float gridStep = (radarRadius * 2.0f) / gridLines;
            for (int g = 1; g < gridLines; g++) {
                float offset = -radarRadius + g * gridStep;
                dl->AddLine(ImVec2(radarCenter.x - radarRadius, radarCenter.y + offset),
                            ImVec2(radarCenter.x + radarRadius, radarCenter.y + offset),
                            gridColorFaint, 0.8f);
                dl->AddLine(ImVec2(radarCenter.x + offset, radarCenter.y - radarRadius),
                            ImVec2(radarCenter.x + offset, radarCenter.y + radarRadius),
                            gridColorFaint, 0.8f);
            }
            // Center cross (brighter)
            dl->AddLine(ImVec2(radarCenter.x - radarRadius, radarCenter.y),
                        ImVec2(radarCenter.x + radarRadius, radarCenter.y), gridColor, 0.8f);
            dl->AddLine(ImVec2(radarCenter.x, radarCenter.y - radarRadius),
                        ImVec2(radarCenter.x, radarCenter.y + radarRadius), gridColor, 0.8f);
        }

        // ============ Cardinal direction labels ============
        float labelOffset = radarRadius + 10.0f;
        ImU32 labelColor = IM_COL32(130, 120, 170, (int)(radarAlpha * 200));

        // When rotating radar, the labels stay fixed so we rotate the labels opposite
        const char* dirs[] = { "N", "E", "S", "W" };
        float dirAngles[] = { 0, PI * 0.5f, PI, PI * 1.5f };

        for (int i = 0; i < 4; i++) {
            float angle = dirAngles[i] - localYawRad + PI * 0.5f; // Adjust for game coord system
            ImVec2 labelPos = ImVec2(
                radarCenter.x + sinf(angle) * (radarRadius - 12.0f),
                radarCenter.y - cosf(angle) * (radarRadius - 12.0f)
            );
            ImVec2 textSz = ImGui::CalcTextSize(dirs[i]);
            dl->AddText(ImVec2(labelPos.x - textSz.x * 0.5f, labelPos.y - textSz.y * 0.5f),
                        labelColor, dirs[i]);
        }

        // ============ Draw entities ============
        float scale = radarRadius / effectiveRange;

        for (int i = 1; i <= Constants::EntityList::MAX_PLAYERS; i++) {
            __try {
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
                uint8_t team = 0;
                uint8_t lifeState = 1;
                Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health);
                Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, team);
                Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, lifeState);

                if (health <= 0 || lifeState != Constants::Game::LIFE_ALIVE) continue;

                // Get entity position
                uintptr_t gameSceneNode = 0;
                if (!Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, gameSceneNode) ||
                    !Memory::IsValidPtr(gameSceneNode)) continue;

                Vector3 entityOrigin;
                if (!Memory::SafeRead(gameSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin, entityOrigin))
                    continue;

                // Calculate relative position (game uses X=forward, Y=left in Source engine)
                float dx = entityOrigin.x - localOrigin.x;
                float dy = entityOrigin.y - localOrigin.y;

                // Rotate relative to local player's yaw so radar is always oriented correctly
                float rotatedX = dx * cosf(-localYawRad) - dy * sinf(-localYawRad);
                float rotatedY = dx * sinf(-localYawRad) + dy * cosf(-localYawRad);

                // Scale to radar pixels
                float radarX = rotatedY * scale;  // Y maps to horizontal on radar
                float radarY = -rotatedX * scale;  // X maps to vertical (inverted for screen coords)

                // Entity direction: use the relative angle from local player to this entity
                // since per-entity eye angles aren't exposed in this SDK
                float angleToDot = atan2f(radarX, -radarY);

                // Distance check — clamp to edge if outside range
                float dist = sqrtf(radarX * radarX + radarY * radarY);
                bool outsideRange = false;

                if (radarStyle == 0) {
                    // Circular: clamp to circle edge
                    if (dist > radarRadius - 6.0f) {
                        float clampDist = radarRadius - 6.0f;
                        radarX = radarX / dist * clampDist;
                        radarY = radarY / dist * clampDist;
                        outsideRange = true;
                    }
                } else {
                    // Square: clamp to box edge
                    float maxCoord = radarRadius - 6.0f;
                    if (fabsf(radarX) > maxCoord || fabsf(radarY) > maxCoord) {
                        float scaleDown = fminf(maxCoord / fabsf(radarX), maxCoord / fabsf(radarY));
                        radarX *= scaleDown;
                        radarY *= scaleDown;
                        outsideRange = true;
                    }
                }

                ImVec2 dotPos = ImVec2(radarCenter.x + radarX, radarCenter.y + radarY);

                // Determine colors
                bool isEnemy = (team != localTeam) || Hooks::g_bFFAEnabled.load();
                ImU32 dotColor, outlineColor;
                float dotSize = outsideRange ? 4.0f : 5.0f;

                if (isEnemy) {
                    float* ec = Hooks::g_fRadarEnemyColor;
                    dotColor = IM_COL32((int)(ec[0]*255), (int)(ec[1]*255), (int)(ec[2]*255), outsideRange ? 150 : 255);
                    outlineColor = IM_COL32(0, 0, 0, outsideRange ? 100 : 200);
                } else {
                    float* tc = Hooks::g_fRadarTeamColor;
                    dotColor = IM_COL32((int)(tc[0]*255), (int)(tc[1]*255), (int)(tc[2]*255), outsideRange ? 100 : 200);
                    outlineColor = IM_COL32(0, 0, 0, outsideRange ? 80 : 150);
                }

                // Draw player marker with direction
                DrawPlayerMarker(dl, dotPos, dotSize, dotColor, outlineColor, angleToDot);

                // ============ Health indicator (small bar or dot color) ============
                if (showHealth && !outsideRange) {
                    float hp = health / (float)Constants::Game::MAX_HEALTH;
                    if (hp > 1.0f) hp = 1.0f;
                    
                    float barW = dotSize * 2.0f;
                    float barH = 2.5f;
                    float barX = dotPos.x - barW * 0.5f;
                    float barY = dotPos.y + dotSize * 0.8f + 2.0f;

                    // Background
                    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW, barY + barH),
                                      IM_COL32(0, 0, 0, 150), 1.0f);
                    // Health fill (red to green gradient)
                    ImU32 hpColor = IM_COL32((int)(255 * (1.0f - hp)), (int)(255 * hp), 0, 220);
                    dl->AddRectFilled(ImVec2(barX, barY), ImVec2(barX + barW * hp, barY + barH),
                                      hpColor, 1.0f);
                }

                // ============ Player name ============
                if (showNames && !outsideRange) {
                    char shortName[12] = { 0 };
                    char nameBuf[128] = { 0 };
                    uintptr_t nameAddr = controller + cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName;
                    SIZE_T bytesRead = 0;
                    if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(nameAddr), nameBuf, sizeof(nameBuf) - 1, &bytesRead) &&
                        bytesRead > 0 && nameBuf[0] > 0x20 && nameBuf[0] < 0x7F) {
                        nameBuf[sizeof(nameBuf) - 1] = '\0';
                        snprintf(shortName, sizeof(shortName), "%.10s", nameBuf);

                        ImVec2 nameSz = ImGui::CalcTextSize(shortName);
                        float nameX = dotPos.x - nameSz.x * 0.5f;
                        float nameY = dotPos.y - dotSize - nameSz.y - 1.0f;

                        // Shadow + text
                        dl->AddText(ImVec2(nameX + 1, nameY + 1), IM_COL32(0, 0, 0, 180), shortName);
                        dl->AddText(ImVec2(nameX, nameY), IM_COL32(220, 220, 230, 200), shortName);
                    }
                }

            } __except (EXCEPTION_EXECUTE_HANDLER) {
                // Silently recover from any access violation
            }
        }

        // ============ Draw local player (always center) ============
        DrawLocalPlayerArrow(dl, radarCenter, 8.0f, 0.0f); // Always pointing up since we rotate the map

        // ============ Bottom info bar ============
        float bottomY = radarCenter.y + radarRadius + 8.0f;
        if (bottomY < winPos.y + winSize.y - 18) {
            char infoText[64];
            snprintf(infoText, sizeof(infoText), "Zoom: %.1fx  |  Range: %.0fm",
                     radarZoom, effectiveRange / 100.0f);
            ImVec2 infoSz = ImGui::CalcTextSize(infoText);
            dl->AddText(ImVec2(radarCenter.x - infoSz.x * 0.5f, bottomY),
                        IM_COL32(100, 95, 140, (int)(radarAlpha * 180)), infoText);
        }

        ImGui::End();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(3);
    }
} // namespace Radar
