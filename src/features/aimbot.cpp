#include "aimbot.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/math.hpp"
#include "keybind_manager.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <cmath>

namespace Aimbot {

    static Vector3 GetBonePosition(uintptr_t pawn, int boneId) {
        uintptr_t gameSceneNode = 0;
        if (!Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, gameSceneNode) || 
            !Memory::IsValidPtr(gameSceneNode))
            return { 0, 0, 0 };

        uintptr_t boneArray = 0;
        // Bone array is at m_modelState + BONE_ARRAY_OFFSET
        if (!Memory::SafeRead(gameSceneNode + cs2_dumper::schemas::client_dll::CSkeletonInstance::m_modelState + 
                              Constants::Offsets::BONE_ARRAY_OFFSET, boneArray) || 
            !Memory::IsValidPtr(boneArray)) {
            return { 0, 0, 0 };
        }

        Vector3 pos;
        if (!Memory::SafeRead(boneArray + boneId * Constants::Bones::BONE_SIZE, pos))
            return { 0, 0, 0 };

        return pos;
    }

    static uintptr_t g_lockedPawn = 0;
    static int g_targetSwitchCooldown = 0; // Frames to wait before switching targets

    // Cache screen dimensions from render thread (updated by ESP::Render via ImGui)
    // Aimbot thread should NOT call ImGui::GetIO() - it's not thread safe.
    static int s_screenWidth = 1920;
    static int s_screenHeight = 1080;

    void UpdateScreenSize(int w, int h) {
        s_screenWidth = w;
        s_screenHeight = h;
    }

    // Improved bone position retrieval with fallback bones
    static Vector3 GetBonePositionWithFallback(uintptr_t pawn, int primaryBone) {
        Vector3 pos = GetBonePosition(pawn, primaryBone);
        
        // If primary bone fails, try fallback bones (pelvis, chest, neck)
        if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) {
            // Try pelvis
            pos = GetBonePosition(pawn, Constants::Bones::PELVIS);
            if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) {
                // Try stomach
                pos = GetBonePosition(pawn, Constants::Bones::STOMACH);
                if (pos.x == 0.0f && pos.y == 0.0f && pos.z == 0.0f) {
                    // Try chest
                    pos = GetBonePosition(pawn, Constants::Bones::SPINE);
                }
            }
        }
        
        return pos;
    }

    void Run() {
        __try {
            // Only aim while AIMBOT key is held
            if (!KeybindManager::IsAimbotKeyPressed()) {
                g_lockedPawn = 0;
                return;
            }

            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t entityList = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
                !Memory::IsValidPtr(entityList)) return;

            uintptr_t localPawn = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
                !Memory::IsValidPtr(localPawn)) return;

            uintptr_t localSceneNode = 0;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, localSceneNode) || 
                !Memory::IsValidPtr(localSceneNode)) return;

            Vector3 localOrigin;
            if (!Memory::SafeRead(localSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin, localOrigin)) return;

            Vector3 viewOffset;
            if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset, viewOffset)) return;
            if (viewOffset.z < 10.0f) viewOffset.z = 64.0f;
            Vector3 eyePos = localOrigin + viewOffset;

            QAngle currentAngles;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles, currentAngles)) return;

            uint8_t localTeam = 0;
            Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, localTeam);

            // Read aim punch (commented out - caused issues with jumping/shooting)
            // Vector3 aimPunch;
            // Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_aimPunchAngle, aimPunch);

            view_matrix_t viewMatrix;
            memcpy(&viewMatrix, reinterpret_cast<void*>(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix), sizeof(view_matrix_t));

            int screenWidth = s_screenWidth;
            int screenHeight = s_screenHeight;
            if (screenWidth <= 0 || screenHeight <= 0) return;
            Vector2 screenCenter(static_cast<float>(screenWidth) / 2.0f, static_cast<float>(screenHeight) / 2.0f);

            float bestDist = Hooks::g_fAimbotFov.load() * 30.0f;
            QAngle bestTargetAngle;
            bool foundTarget = false;
            int targetBone = Hooks::g_nAimbotBone.load();

            // Sticky target validation - must use SafeRead because pawn can become stale
            // Issue 2 & 3 fix: Improved sticky targeting logic
            bool lockedTargetValid = false;
            float stickyFovMultiplier = 4.0f;
            
            if (g_lockedPawn && Memory::IsValidPtr(g_lockedPawn)) {
                int hp = 0;
                uint8_t team = 0;
                if (Memory::SafeRead(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, hp) && 
                    Memory::SafeRead(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, team) && 
                    hp > 0 && (Hooks::g_bFFAEnabled.load() || localTeam != team)) {
                    
                    // Use improved bone position retrieval with fallback
                    Vector3 targetPos = GetBonePositionWithFallback(g_lockedPawn, targetBone);
                    
                    if (targetPos.x != 0.0f || targetPos.y != 0.0f || targetPos.z != 0.0f) {
                        Vector2 screenPos;
                        if (WorldToScreen(targetPos, screenPos, viewMatrix, screenWidth, screenHeight)) {
                            float dist = sqrtf(powf(screenPos.x - screenCenter.x, 2) + powf(screenPos.y - screenCenter.y, 2));
                            // Increased FOV tolerance for sticky target (4x instead of 1.5x)
                            if (dist < bestDist * stickyFovMultiplier) {
                                bestDist = dist;
                                bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                                foundTarget = true;
                                lockedTargetValid = true;
                                // Reset cooldown when target is valid and in FOV
                                g_targetSwitchCooldown = 0;
                            } else {
                                // Target is outside expanded FOV - don't drop immediately
                                // Only keep valid if we have cooldown remaining
                                if (g_targetSwitchCooldown < 10) {
                                    bestDist = dist;
                                    bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                                    foundTarget = true;
                                    lockedTargetValid = true;
                                    g_targetSwitchCooldown++;
                                }
                            }
                        } else {
                            // Target not on screen - keep locked for a few frames with cooldown
                            if (g_targetSwitchCooldown < 15) {
                                bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                                foundTarget = true;
                                lockedTargetValid = true;
                                g_targetSwitchCooldown++;
                            }
                        }
                    } else {
                        // Bone position read failed - decrement cooldown but don't switch immediately
                        if (g_targetSwitchCooldown < 5) {
                            g_targetSwitchCooldown++;
                        }
                    }
                }
            }
            
            // Only clear locked pawn if it's completely invalid (dead, invalid pointer, etc.)
            // and we've exhausted our cooldown period
            if (!lockedTargetValid && g_lockedPawn && g_targetSwitchCooldown >= 15) {
                // Check if locked target is still valid before clearing
                int hp = 0;
                uint8_t team = 0;
                if (!(Memory::SafeRead(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, hp) && 
                      Memory::SafeRead(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, team) && 
                      hp > 0 && (Hooks::g_bFFAEnabled.load() || localTeam != team))) {
                    g_lockedPawn = 0;
                }
            }

            // Find new target
            if (!foundTarget) {
                for (int i = 0; i < Constants::EntityList::MAX_PLAYERS; i++) {
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
                    Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health);
                    Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, lifeState);
                    if (health <= 0 || lifeState != Constants::Game::LIFE_ALIVE) continue;

                    uint8_t enemyTeam = 0;
                    Memory::SafeRead(pawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, enemyTeam);
                    if (!Hooks::g_bFFAEnabled.load() && localTeam == enemyTeam) continue;

                    Vector3 targetPos = GetBonePositionWithFallback(pawn, targetBone);
                    if (targetPos.x == 0.0f && targetPos.y == 0.0f && targetPos.z == 0.0f) continue;

                    Vector2 targetScreen;
                    if (!WorldToScreen(targetPos, targetScreen, viewMatrix, screenWidth, screenHeight)) continue;

                    float dist = sqrtf(powf(targetScreen.x - screenCenter.x, 2) + powf(targetScreen.y - screenCenter.y, 2));

                    if (dist < bestDist) {
                        bestDist = dist;
                        bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                        foundTarget = true;
                        g_lockedPawn = pawn;
                    }
                }
            }

            // Apply aim
            if (foundTarget) {
                // Standard Smooth Aim - improved smoothing for smooth=1.0
                bestTargetAngle.Clamp();
                float smooth = Hooks::g_fAimbotSmooth.load();
                
                // Apply smoothing - even smooth=1.0 has minimal smoothing for smoothness
                // Use exponential smoothing for smoother feel at higher values
                if (smooth >= 1.0f) {
                    QAngle delta = bestTargetAngle - currentAngles;
                    delta.Clamp();
                    
                    // Minimum smoothing factor for smooth=1.0 (divides by 1.2)
                    // Higher smooth values scale up gradually for smoother movement
                    float smoothFactor = (smooth - 1.0f) * 0.3f + 1.2f;
                    
                    // Reduce smoothing when shooting for faster response
                    // Check if left mouse button is pressed (attack)
                    if (GetAsyncKeyState(0x01) & 0x8000) {
                        smoothFactor = 1.2f; // Very fast response when shooting
                    }
                    
                    currentAngles.x += delta.x / smoothFactor;
                    currentAngles.y += delta.y / smoothFactor;
                } else {
                    currentAngles = bestTargetAngle;
                }

                currentAngles.Clamp();
                Memory::SafeWrite(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles, currentAngles);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Silently recover from any access violation
            g_lockedPawn = 0;
        }
    }

    void Reset() {
        // Clear locked target on match transition
        g_lockedPawn = 0;
        g_targetSwitchCooldown = 0;
    }

} // namespace Aimbot
