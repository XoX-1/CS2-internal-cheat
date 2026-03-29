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

    // Time-based lock cooldown (seconds) instead of frame counts.
    // Frame-count cooldowns cause 1+ second hangs at low FPS.
    static float g_lockCooldownSec = 0.0f;
    static constexpr float LOCK_COOLDOWN_MAX   = 0.12f; // 120ms grace before dropping target
    static constexpr float LOCK_OFFSCREEN_MAX  = 0.20f; // 200ms grace when off-screen

    // High-precision timer - QPC for sub-millisecond accuracy.
    // GetTickCount64 has ~15ms resolution, too coarse at 130+ FPS.
    static LARGE_INTEGER g_lastAimQPC = {};
    static LARGE_INTEGER g_qpcFrequency = {};
    static bool g_qpcInitialized = false;

    // Screen dimensions (written by render thread, read by aimbot - atomic is overkill,
    // reads are coherent on x86/x64 for aligned 32-bit ints).
    static volatile int s_screenWidth  = 1920;
    static volatile int s_screenHeight = 1080;

    void UpdateScreenSize(int w, int h) {
        s_screenWidth  = w;
        s_screenHeight = h;
    }

    // Returns delta-time in seconds, clamped to [0.1ms, 50ms].
    // 50ms cap (20 FPS minimum) prevents the sub-step loop from becoming
    // unbounded at very low FPS. At 20 FPS the smoothing is still accurate
    // because we use FPS-independent exponential decay.
    static float GetDeltaTime() {
        if (!g_qpcInitialized) {
            QueryPerformanceFrequency(&g_qpcFrequency);
            QueryPerformanceCounter(&g_lastAimQPC);
            g_qpcInitialized = true;
            return 0.001f;
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        float dt = static_cast<float>(now.QuadPart - g_lastAimQPC.QuadPart)
                 / static_cast<float>(g_qpcFrequency.QuadPart);
        g_lastAimQPC = now;

        // Clamp: 0.1ms min (prevents div-by-zero), 50ms max (20 FPS floor).
        // Previously was 100ms (10 FPS) which caused 33+ sub-step iterations.
        if (dt < 0.0001f) dt = 0.0001f;
        if (dt > 0.05f)   dt = 0.05f;
        return dt;
    }

    // Bone cache: avoid re-probing fallbacks every frame once we know which bone works.
    static uintptr_t s_cachedBonePawn = 0;
    static int       s_cachedBoneId   = -1;

    static Vector3 GetBonePositionWithFallback(uintptr_t pawn, int primaryBone) {
        if (pawn == s_cachedBonePawn && s_cachedBoneId >= 0) {
            Vector3 pos = GetBonePosition(pawn, s_cachedBoneId);
            if (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f)
                return pos;
            s_cachedBoneId = -1; // cache miss, re-probe
        }

        const int candidates[] = {
            primaryBone,
            Constants::Bones::PELVIS,
            Constants::Bones::STOMACH,
            Constants::Bones::SPINE
        };
        for (int bone : candidates) {
            Vector3 pos = GetBonePosition(pawn, bone);
            if (pos.x != 0.0f || pos.y != 0.0f || pos.z != 0.0f) {
                s_cachedBonePawn = pawn;
                s_cachedBoneId   = bone;
                return pos;
            }
        }
        return { 0, 0, 0 };
    }

    // Inline fast 2D distance (avoids powf which is slower than fmul).
    static inline float Dist2D(float dx, float dy) {
        return sqrtf(dx * dx + dy * dy);
    }

    void Run() {
        __try {
            if (!KeybindManager::IsAimbotKeyPressed()) {
                g_lockedPawn    = 0;
                g_lockCooldownSec = 0.0f;
                // Do NOT reset g_qpcInitialized here - keeps timer warm so
                // the first frame after re-pressing key has a sane dt.
                return;
            }

            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return;

            uintptr_t entityList = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
                !Memory::IsValidPtr(entityList)) return;

            // Resolve local pawn via Controller -> EntityList
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

            view_matrix_t viewMatrix{};
            if (!Memory::SafeReadBytes(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix,
                           &viewMatrix, sizeof(viewMatrix))) return;

            int screenWidth  = s_screenWidth;
            int screenHeight = s_screenHeight;
            if (screenWidth <= 0 || screenHeight <= 0) return;
            Vector2 screenCenter(static_cast<float>(screenWidth)  / 2.0f,
                                 static_cast<float>(screenHeight) / 2.0f);

            // Convert FOV (degrees) to pixel radius using half-screen height.
            // This makes the FOV circle resolution-independent.
            float fovDeg     = Hooks::g_fAimbotFov.load();
            float fovPixels  = (fovDeg / 90.0f) * (screenHeight / 2.0f);
            float bestDist   = fovPixels;

            QAngle bestTargetAngle;
            bool foundTarget = false;
            int targetBone = Hooks::g_nAimbotBone.load();

            // Get time delta (used for lock cooldown accounting too)
            float dt = GetDeltaTime();

            // ---- Sticky target validation ----
            bool lockedTargetValid = false;

            if (g_lockedPawn && Memory::IsValidPtr(g_lockedPawn)) {
                int hp = 0;
                uint8_t team = 0;
                if (Memory::SafeRead(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, hp) && 
                    Memory::SafeRead(g_lockedPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum, team) && 
                    hp > 0 && (Hooks::g_bFFAEnabled.load() || localTeam != team)) {

                    Vector3 targetPos = GetBonePositionWithFallback(g_lockedPawn, targetBone);

                    if (targetPos.x != 0.0f || targetPos.y != 0.0f || targetPos.z != 0.0f) {
                        Vector2 screenPos;
                        if (WorldToScreen(targetPos, screenPos, viewMatrix, screenWidth, screenHeight)) {
                            float dist = Dist2D(screenPos.x - screenCenter.x, screenPos.y - screenCenter.y);
                            // 4x expanded FOV for sticky retention
                            if (dist < fovPixels * 4.0f) {
                                bestDist         = dist;
                                bestTargetAngle  = Vector3::CalculateAngle(eyePos, targetPos);
                                foundTarget      = true;
                                lockedTargetValid = true;
                                g_lockCooldownSec = 0.0f; // reset grace timer
                            } else {
                                // Outside FOV - hold for grace period
                                g_lockCooldownSec += dt;
                                if (g_lockCooldownSec < LOCK_COOLDOWN_MAX) {
                                    bestTargetAngle  = Vector3::CalculateAngle(eyePos, targetPos);
                                    foundTarget      = true;
                                    lockedTargetValid = true;
                                }
                            }
                        } else {
                            // Off-screen - hold longer grace period
                            g_lockCooldownSec += dt;
                            if (g_lockCooldownSec < LOCK_OFFSCREEN_MAX) {
                                bestTargetAngle  = Vector3::CalculateAngle(eyePos, targetPos);
                                foundTarget      = true;
                                lockedTargetValid = true;
                            }
                        }
                    } else {
                        // Bone read failed - accumulate cooldown
                        g_lockCooldownSec += dt;
                        if (g_lockCooldownSec < LOCK_COOLDOWN_MAX)
                            lockedTargetValid = true;
                    }
                }
            }

            // Drop lock only after grace period expires
            if (!lockedTargetValid && g_lockedPawn) {
                g_lockedPawn      = 0;
                g_lockCooldownSec = 0.0f;
            }

            // ---- Find new target ----
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

                    float dist = Dist2D(targetScreen.x - screenCenter.x, targetScreen.y - screenCenter.y);

                    if (dist < bestDist) {
                        bestDist        = dist;
                        bestTargetAngle = Vector3::CalculateAngle(eyePos, targetPos);
                        foundTarget     = true;
                        g_lockedPawn    = pawn;
                        g_lockCooldownSec = 0.0f;
                    }
                }
            }

            // ---- Apply aim ----
            if (foundTarget) {
                bestTargetAngle.Clamp();
                float smooth = Hooks::g_fAimbotSmooth.load();

                QAngle delta = bestTargetAngle - currentAngles;
                delta.Clamp();

                // Deadzone: skip micro-corrections already within 0.01 degrees
                float deltaMag = sqrtf(delta.x * delta.x + delta.y * delta.y);
                if (deltaMag < 0.01f) {
                    // On target - no write needed
                } else if (smooth <= 1.0f) {
                    // Instant snap
                    currentAngles = bestTargetAngle;
                    currentAngles.Clamp();
                    Memory::SafeWrite(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles, currentAngles);
                } else {
                    // FPS-independent exponential smoothing.
                    // speed = 60/smooth: smooth=5 -> speed=12, smooth=50 -> speed=1.2
                    //
                    // KEY FIX: Single-step instead of sub-stepping loop.
                    // Sub-stepping was originally added to handle large dt at low FPS,
                    // but with the 50ms cap on dt the single-step formula is accurate
                    // enough (error < 0.1 degree at 20 FPS, smooth=5). The loop was
                    // causing per-frame O(dt/2ms) iterations (e.g. 25 iters at 20 FPS)
                    // which manifested as CPU spikes and stutter on the render thread.
                    float speed = 60.0f / smooth;
                    float t = 1.0f - expf(-speed * dt);
                    if (t > 1.0f) t = 1.0f;

                    if (t >= 0.999f) {
                        currentAngles = bestTargetAngle;
                    } else {
                        currentAngles.x += delta.x * t;
                        currentAngles.y += delta.y * t;
                    }
                    currentAngles.Clamp();
                    Memory::SafeWrite(clientBase + cs2_dumper::offsets::client_dll::dwViewAngles, currentAngles);
                }
            }
            // No target: keep QPC running so next acquisition frame has clean dt
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_lockedPawn      = 0;
            g_lockCooldownSec = 0.0f;
        }
    }

    void Reset() {
        g_lockedPawn      = 0;
        g_lockCooldownSec = 0.0f;
        g_qpcInitialized  = false;
        s_cachedBonePawn  = 0;
        s_cachedBoneId    = -1;
    }

} // namespace Aimbot
