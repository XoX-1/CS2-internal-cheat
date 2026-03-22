#include "esp.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/math.hpp"
#include "aimbot.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include "imgui.h"

#include <windows.h>
#include <cmath>
#include <stdio.h>
#include <string.h>

// Drawing Helpers

static void DrawCornerBox(ImDrawList* dl, float x1, float y1, float x2, float y2, ImU32 col, float thickness = 2.0f) {
    float w = x2 - x1;
    float h = y2 - y1;
    float cornerW = w * 0.22f;
    float cornerH = h * 0.18f;

    // Top-left
    dl->AddLine(ImVec2(x1, y1), ImVec2(x1 + cornerW, y1), col, thickness);
    dl->AddLine(ImVec2(x1, y1), ImVec2(x1, y1 + cornerH), col, thickness);
    // Top-right
    dl->AddLine(ImVec2(x2, y1), ImVec2(x2 - cornerW, y1), col, thickness);
    dl->AddLine(ImVec2(x2, y1), ImVec2(x2, y1 + cornerH), col, thickness);
    // Bottom-left
    dl->AddLine(ImVec2(x1, y2), ImVec2(x1 + cornerW, y2), col, thickness);
    dl->AddLine(ImVec2(x1, y2), ImVec2(x1, y2 - cornerH), col, thickness);
    // Bottom-right
    dl->AddLine(ImVec2(x2, y2), ImVec2(x2 - cornerW, y2), col, thickness);
    dl->AddLine(ImVec2(x2, y2), ImVec2(x2, y2 - cornerH), col, thickness);
}

static bool SafeReadString(uintptr_t addr, char* out, size_t size) {
    __try {
        if (!addr || !out || size == 0) return false;
        SIZE_T bytesRead = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(addr), out, size - 1, &bytesRead) || bytesRead == 0)
            return false;
        out[size - 1] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static const char* TrimWeaponClassName(const char* className) {
    if (!className || !className[0]) return className;
    if (strncmp(className, "weapon_", 7) == 0) return className + 7;
    if (strncmp(className, "item_", 5) == 0) return className + 5;
    return className;
}

static const char* GetWeaponPrettyName(const char* className) {
    const char* w = TrimWeaponClassName(className);
    if (!w || !w[0]) return "Unknown";

    if (!strcmp(w, "ak47")) return "AK-47";
    if (!strcmp(w, "m4a1")) return "M4A4";
    if (!strcmp(w, "m4a1_silencer")) return "M4A1-S";
    if (!strcmp(w, "awp")) return "AWP";
    if (!strcmp(w, "ssg08")) return "SSG 08";
    if (!strcmp(w, "aug")) return "AUG";
    if (!strcmp(w, "sg556")) return "SG 553";
    if (!strcmp(w, "famas")) return "FAMAS";
    if (!strcmp(w, "galilar")) return "Galil AR";
    if (!strcmp(w, "glock")) return "Glock-18";
    if (!strcmp(w, "hkp2000")) return "P2000";
    if (!strcmp(w, "usp_silencer")) return "USP-S";
    if (!strcmp(w, "deagle")) return "Desert Eagle";
    if (!strcmp(w, "revolver")) return "R8 Revolver";
    if (!strcmp(w, "elite")) return "Dual Berettas";
    if (!strcmp(w, "p250")) return "P250";
    if (!strcmp(w, "fiveseven")) return "Five-SeveN";
    if (!strcmp(w, "tec9")) return "Tec-9";
    if (!strcmp(w, "cz75a")) return "CZ75-Auto";
    if (!strcmp(w, "mac10")) return "MAC-10";
    if (!strcmp(w, "mp9")) return "MP9";
    if (!strcmp(w, "mp7")) return "MP7";
    if (!strcmp(w, "ump45")) return "UMP-45";
    if (!strcmp(w, "p90")) return "P90";
    if (!strcmp(w, "bizon")) return "PP-Bizon";
    if (!strcmp(w, "nova")) return "Nova";
    if (!strcmp(w, "xm1014")) return "XM1014";
    if (!strcmp(w, "mag7")) return "MAG-7";
    if (!strcmp(w, "sawedoff")) return "Sawed-Off";
    if (!strcmp(w, "m249")) return "M249";
    if (!strcmp(w, "negev")) return "Negev";
    if (!strcmp(w, "knife") || strstr(w, "knife")) return "Knife";
    if (!strcmp(w, "hegrenade")) return "HE Grenade";
    if (!strcmp(w, "flashbang")) return "Flashbang";
    if (!strcmp(w, "smokegrenade")) return "Smoke";
    if (!strcmp(w, "molotov")) return "Molotov";
    if (!strcmp(w, "incgrenade")) return "Incendiary";
    if (!strcmp(w, "decoy")) return "Decoy";
    if (!strcmp(w, "taser")) return "Zeus x27";
    if (!strcmp(w, "c4")) return "C4";

    return w;
}

static const char* GetWeaponViewTag(const char* className) {
    const char* w = TrimWeaponClassName(className);
    if (!w || !w[0]) return "WEAPON";

    if (strstr(w, "knife")) return "KNIFE";
    if (strstr(w, "grenade") || !strcmp(w, "flashbang") || !strcmp(w, "decoy") || !strcmp(w, "molotov") || !strcmp(w, "incgrenade")) return "GRENADE";
    if (!strcmp(w, "awp") || !strcmp(w, "ssg08") || !strcmp(w, "scar20") || !strcmp(w, "g3sg1")) return "SNIPER";
    if (!strcmp(w, "deagle") || !strcmp(w, "usp_silencer") || !strcmp(w, "hkp2000") || !strcmp(w, "glock") || !strcmp(w, "p250") || !strcmp(w, "fiveseven") || !strcmp(w, "elite") || !strcmp(w, "cz75a") || !strcmp(w, "tec9") || !strcmp(w, "revolver")) return "PISTOL";
    if (!strcmp(w, "mac10") || !strcmp(w, "mp9") || !strcmp(w, "mp7") || !strcmp(w, "ump45") || !strcmp(w, "p90") || !strcmp(w, "bizon")) return "SMG";
    if (!strcmp(w, "nova") || !strcmp(w, "xm1014") || !strcmp(w, "mag7") || !strcmp(w, "sawedoff") || !strcmp(w, "m249") || !strcmp(w, "negev")) return "HEAVY";
    if (!strcmp(w, "c4")) return "C4";
    return "RIFLE";
}

static void DrawFilledBar(ImDrawList* dl, float x, float y, float w, float h, float percent, ImU32 fillColor, ImU32 bgColor, bool leftSide) {
    if (percent > 1.0f) percent = 1.0f;
    if (percent < 0.0f) percent = 0.0f;

    dl->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), bgColor);
    float filled = h * percent;
    dl->AddRectFilled(ImVec2(x, y + h - filled), ImVec2(x + w, y + h), fillColor);
    dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(0, 0, 0, 180));
}

static Vector3 GetBonePos(uintptr_t pawn, int boneId) {
    uintptr_t sceneNode = 0;
    if (!Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, sceneNode) || 
        !Memory::IsValidPtr(sceneNode))
        return { 0, 0, 0 };

    uintptr_t boneArray = 0;
    if (!Memory::SafeRead(sceneNode + cs2_dumper::schemas::client_dll::CSkeletonInstance::m_modelState + 
                          Constants::Offsets::BONE_ARRAY_OFFSET, boneArray) || 
        !Memory::IsValidPtr(boneArray))
        return { 0, 0, 0 };

    Vector3 pos;
    if (!Memory::SafeRead(boneArray + boneId * Constants::Bones::BONE_SIZE, pos))
        return { 0, 0, 0 };

    return pos;
}

// CS2 bone indices for skeleton
struct BoneConnection { int from; int to; };
static const BoneConnection kSkeleton[] = {
    {Constants::Bones::HEAD, Constants::Bones::NECK},
    {Constants::Bones::NECK, Constants::Bones::SPINE},
    {Constants::Bones::SPINE, Constants::Bones::LEFT_SHOULDER},
    {Constants::Bones::LEFT_SHOULDER, Constants::Bones::LEFT_ELBOW},
    {Constants::Bones::LEFT_ELBOW, Constants::Bones::LEFT_HAND},
    {Constants::Bones::SPINE, Constants::Bones::RIGHT_SHOULDER},
    {Constants::Bones::RIGHT_SHOULDER, Constants::Bones::RIGHT_ELBOW},
    {Constants::Bones::RIGHT_ELBOW, Constants::Bones::RIGHT_HAND},
    {Constants::Bones::SPINE, Constants::Bones::PELVIS},
    {Constants::Bones::PELVIS, Constants::Bones::LEFT_HIP},
    {Constants::Bones::LEFT_HIP, Constants::Bones::LEFT_KNEE},
    {Constants::Bones::LEFT_KNEE, Constants::Bones::LEFT_FOOT},
    {Constants::Bones::PELVIS, Constants::Bones::RIGHT_HIP},
    {Constants::Bones::RIGHT_HIP, Constants::Bones::RIGHT_KNEE},
    {Constants::Bones::RIGHT_KNEE, Constants::Bones::RIGHT_FOOT},
};

namespace ESP {
    void Render() {
        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
            !Memory::IsValidPtr(entityList)) return;

        // Resolve local pawn via Controller → EntityList (reliable path)
        uintptr_t localController = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) ||
            !Memory::IsValidPtr(localController)) return;

        uint32_t localPawnHandle = 0;
        if (!Memory::SafeRead(localController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn, localPawnHandle) ||
            !localPawnHandle) return;

        uintptr_t localPawnEntry = 0;
        if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE +
            sizeof(uintptr_t) * ((localPawnHandle & Constants::EntityList::HANDLE_MASK) >>
            Constants::EntityList::ENTRY_SHIFT), localPawnEntry) ||
            !Memory::IsValidPtr(localPawnEntry)) return;

        uintptr_t localPawn = 0;
        if (!Memory::SafeRead(localPawnEntry + Constants::EntityList::ENTRY_SIZE * (localPawnHandle & Constants::EntityList::INDEX_MASK),
            localPawn) || !Memory::IsValidPtr(localPawn)) return;

        uint8_t localTeam = 0;
        Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, localTeam);

        Vector3 localOrigin = { 0, 0, 0 };
        uintptr_t localSceneNode = 0;
        if (Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, localSceneNode) && 
            Memory::IsValidPtr(localSceneNode)) {
            Memory::SafeRead(localSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin, localOrigin);
        }

        view_matrix_t viewMatrix{};
        if (!Memory::SafeReadBytes(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix,
                       &viewMatrix, sizeof(viewMatrix))) return;

        ImGuiIO& io = ImGui::GetIO();
        int screenW = static_cast<int>(io.DisplaySize.x);
        int screenH = static_cast<int>(io.DisplaySize.y);
        if (screenW <= 0 || screenH <= 0) return;

        ImDrawList* dl = ImGui::GetBackgroundDrawList();

        // Update aimbot screen size (thread-safe since aimbot reads these atomically)
        Aimbot::UpdateScreenSize(screenW, screenH);

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

                uintptr_t gameSceneNode = 0;
                if (!Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, gameSceneNode) || 
                    !Memory::IsValidPtr(gameSceneNode)) continue;

                Vector3 origin;
                if (!Memory::SafeRead(gameSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin, origin)) continue;

                Vector3 headPos = origin;
                headPos.z += Constants::Bones::HEAD_HEIGHT;

                Vector2 screenPos, headScreenPos;
                if (!WorldToScreen(origin, screenPos, viewMatrix, screenW, screenH)) continue;
                if (!WorldToScreen(headPos, headScreenPos, viewMatrix, screenW, screenH)) continue;

                float boxH = screenPos.y - headScreenPos.y;
                if (boxH <= 2.0f) continue;
                float boxW = boxH * 0.50f;

                bool isEnemy = (team != localTeam) || Hooks::g_bFFAEnabled.load();
                float* espCol = isEnemy ? Hooks::g_fEspEnemyColor : Hooks::g_fEspTeamColor;
                ImU32 color = IM_COL32(static_cast<int>(espCol[0]*255), static_cast<int>(espCol[1]*255), 
                                       static_cast<int>(espCol[2]*255), static_cast<int>(espCol[3]*255));

                float x1 = headScreenPos.x - boxW / 2;
                float y1 = headScreenPos.y;
                float x2 = headScreenPos.x + boxW / 2;
                float y2 = screenPos.y;

                // BOX ESP
                if (Hooks::g_bEspBoxes.load()) {
                    int boxStyle = Hooks::g_nEspBoxStyle.load();
                    if (boxStyle == 0) {
                        // Full box with outline
                        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0,0,0,180), 0.0f, 0, 3.0f);
                        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), color, 0.0f, 0, 1.5f);
                    }
                    else if (boxStyle == 1) {
                        // Corner box with outline
                        DrawCornerBox(dl, x1 - 1, y1 - 1, x2 + 1, y2 + 1, IM_COL32(0,0,0,150), 3.0f);
                        DrawCornerBox(dl, x1, y1, x2, y2, color, 1.5f);
                    }
                    else if (boxStyle == 2) {
                        // Rounded box
                        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(0,0,0,180), 4.0f, 0, 3.0f);
                        dl->AddRect(ImVec2(x1, y1), ImVec2(x2, y2), color, 4.0f, 0, 1.5f);
                    }
                }

                // HEALTH BAR
                if (Hooks::g_bEspHealth.load()) {
                    float hp = health / static_cast<float>(Constants::Game::MAX_HEALTH);
                    if (hp > 1.0f) hp = 1.0f;
                    ImU32 hpColor = IM_COL32(static_cast<int>(255 * (1.0f - hp)), static_cast<int>(255 * hp), 0, 255);
                    DrawFilledBar(dl, x1 - 6, y1, 3, boxH, hp, hpColor, IM_COL32(20, 20, 20, 200), true);
                }

                // ARMOR BAR
                if (Hooks::g_bEspArmor.load()) {
                    int armor = 0;
                    Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_ArmorValue, armor);
                    float armorPct = armor / static_cast<float>(Constants::Game::MAX_HEALTH);
                    if (armorPct > 1.0f) armorPct = 1.0f;
                    if (armor > 0) {
                        DrawFilledBar(dl, x2 + 3, y1, 3, boxH, armorPct, IM_COL32(80, 140, 255, 255), 
                                      IM_COL32(20, 20, 20, 200), false);
                    }
                }

                // HEAD DOT
                if (Hooks::g_bEspHeadDot.load()) {
                    Vector3 headBone = GetBonePos(pawn, Constants::Bones::HEAD);
                    Vector2 headScreen;
                    if (headBone.x != 0.0f && WorldToScreen(headBone, headScreen, viewMatrix, screenW, screenH)) {
                        dl->AddCircleFilled(ImVec2(headScreen.x, headScreen.y), 3.0f, color);
                        dl->AddCircle(ImVec2(headScreen.x, headScreen.y), 3.0f, IM_COL32(0, 0, 0, 200), 0, 1.5f);
                    }
                }

                // NAME
                if (Hooks::g_bEspNames.load()) {
                    char nameBuf[128] = { 0 };
                    if (SafeReadString(controller + 
                                     cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName,
                                     nameBuf, sizeof(nameBuf)) &&
                        nameBuf[0] > 0x20 && nameBuf[0] < 0x7F) {
                        ImVec2 tsz = ImGui::CalcTextSize(nameBuf);
                        float nx = headScreenPos.x - tsz.x / 2;
                        float ny = y1 - tsz.y - 3;
                        dl->AddText(ImVec2(nx + 1, ny + 1), IM_COL32(0, 0, 0, 200), nameBuf);
                        dl->AddText(ImVec2(nx, ny), IM_COL32(255, 255, 255, 255), nameBuf);
                    }
                }

                // DISTANCE
                float textBottomOffset = 3.0f;

                // WEAPON NAME
                uintptr_t weapon = 0;
                if (Hooks::g_bEspWeaponName.load() &&
                    Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_pClippingWeapon, weapon) &&
                    Memory::IsValidPtr(weapon)) {
                    uintptr_t entityIdentity = 0;
                    if (Memory::SafeRead(weapon + Constants::Offsets::ENTITY_IDENTITY, entityIdentity) &&
                        Memory::IsValidPtr(entityIdentity)) {
                        uintptr_t pDesignerName = 0;
                        if (Memory::SafeRead(entityIdentity + Constants::Offsets::DESIGNER_NAME_PTR, pDesignerName) &&
                            Memory::IsValidPtr(pDesignerName)) {
                            char weaponNameRaw[64] = { 0 };
                            if (SafeReadString(pDesignerName, weaponNameRaw, sizeof(weaponNameRaw))) {
                                const char* weaponName = GetWeaponPrettyName(weaponNameRaw);
                                const char* weaponTag = GetWeaponViewTag(weaponNameRaw);
                                if (weaponName && weaponName[0]) {
                                    char weaponLabel[96];
                                    snprintf(weaponLabel, sizeof(weaponLabel), "[%s] %s", weaponTag, weaponName);
                                    ImVec2 wsz = ImGui::CalcTextSize(weaponLabel);
                                    float wx = headScreenPos.x - wsz.x / 2;
                                    float wy = y2 + textBottomOffset;
                                    dl->AddText(ImVec2(wx + 1, wy + 1), IM_COL32(0, 0, 0, 200), weaponLabel);
                                    dl->AddText(ImVec2(wx, wy), IM_COL32(235, 235, 235, 255), weaponLabel);
                                    textBottomOffset += wsz.y + 2.0f;
                                }
                            }
                        }
                    }
                }

                // DISTANCE
                if (Hooks::g_bEspDistance.load()) {
                    float dx = origin.x - localOrigin.x;
                    float dy = origin.y - localOrigin.y;
                    float dz = origin.z - localOrigin.z;
                    float dist = sqrtf(dx*dx + dy*dy + dz*dz) / 100.0f; // Convert to meters
                    char distBuf[32];
                    snprintf(distBuf, sizeof(distBuf), "%.0fm", dist);
                    ImVec2 tsz = ImGui::CalcTextSize(distBuf);
                    float dTextX = headScreenPos.x - tsz.x / 2;
                    float dTextY = y2 + textBottomOffset;
                    dl->AddText(ImVec2(dTextX + 1, dTextY + 1), IM_COL32(0, 0, 0, 200), distBuf);
                    dl->AddText(ImVec2(dTextX, dTextY), IM_COL32(200, 200, 200, 255), distBuf);
                }

                // SNAPLINE
                if (Hooks::g_bEspSnaplines.load()) {
                    dl->AddLine(
                        ImVec2(static_cast<float>(screenW / 2), static_cast<float>(screenH)),
                        ImVec2(headScreenPos.x, y2),
                        IM_COL32(static_cast<int>(espCol[0]*255), static_cast<int>(espCol[1]*255), 
                                static_cast<int>(espCol[2]*255), 120), 1.0f);
                }

                // SKELETON
                if (Hooks::g_bEspSkeleton.load()) {
                    for (size_t b = 0; b < sizeof(kSkeleton) / sizeof(kSkeleton[0]); b++) {
                        Vector3 from = GetBonePos(pawn, kSkeleton[b].from);
                        Vector3 to = GetBonePos(pawn, kSkeleton[b].to);
                        if (from.x == 0.0f && from.y == 0.0f) continue;
                        if (to.x == 0.0f && to.y == 0.0f) continue;

                        Vector2 s1, s2;
                        if (WorldToScreen(from, s1, viewMatrix, screenW, screenH) &&
                            WorldToScreen(to, s2, viewMatrix, screenW, screenH)) {
                            dl->AddLine(ImVec2(s1.x, s1.y), ImVec2(s2.x, s2.y), color, 1.5f);
                        }
                    }
                }

            // ============ FOV CIRCLE ============
            if (Hooks::g_bAimbotEnabled.load()) {
                dl->AddCircle(
                    ImVec2(screenW / 2.0f, screenH / 2.0f),
                    Hooks::g_fAimbotFov.load() * 10.0f,
                    IM_COL32(255, 255, 255, 60),
                    64, 1.5f);
            }

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover
        }
    }
    } // end Render()
} // namespace ESP
