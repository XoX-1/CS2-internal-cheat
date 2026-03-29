#include "silent_aim.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include <buttons.hpp>
#include <windows.h>
#include "MinHook.h"
#include <cmath>
#include <cfloat>
#include <algorithm>

// ============================================================================
// CS2 Silent Aim — Frame History Patch (matches Epstein Rage approach)
//
// KEY INSIGHT (from Epstein Rage aimbot.h reverse engineering):
//   The game builds CUserCmd from a frame history array inside CCSGOInput (a1):
//     a1 + 0xBC8  = entryCount  (int)
//     a1 + 0xBD0  = entryArray  (uintptr_t)
//   Each entry is 0x60 (96) bytes:
//     entry + 0x10 = pitch  (float)
//     entry + 0x14 = yaw    (float)
//
//   SAVE all entries  ->  PATCH to aim angle  ->  Call original CreateMove
//   -> RESTORE all entries  ->  Camera is visually unchanged, bullet hits target.
//
//   Additionally patch dwViewAngles + m_angEyeAngles for belt-and-suspenders.
// ============================================================================

namespace SilentAim {

    Config config;
    State  state;

    // =========================================================================
    // Offsets
    // =========================================================================
    static constexpr ptrdiff_t OFF_EYE_ANGLES = 0x3DD0;
    static constexpr ptrdiff_t OFF_AIMPUNCH   = 0x16CC;
    static constexpr ptrdiff_t OFF_MODELSTATE = 0x160;
    static constexpr ptrdiff_t OFF_BONEARRAY  = Constants::Offsets::BONE_ARRAY_OFFSET;

    // Frame history layout inside CCSGOInput (a1 in CreateMove)
    static constexpr ptrdiff_t FRAME_HISTORY_COUNT = 0xBC8;
    static constexpr ptrdiff_t FRAME_HISTORY_ARRAY = 0xBD0;
    static constexpr int       FRAME_ENTRY_SIZE    = 0x60;  // 96 bytes
    static constexpr ptrdiff_t ENTRY_PITCH_OFF     = 0x10;
    static constexpr ptrdiff_t ENTRY_YAW_OFF       = 0x14;

    // FOV modes: 0=90deg  1=180deg  2=360deg(no filter, closest by 3D distance)
    static constexpr float FOV_TABLE[3] = { 90.f, 180.f, 360.f };

    // CreateMove hook
    using CreateMoveFn = double(__fastcall*)(__int64, unsigned int, __int64);
    static CreateMoveFn oCreateMove = nullptr;
    static const char*  CREATEMOVE_SIG =
        "48 8B C4 4C 89 40 18 48 89 48 08 55 53 41 54 41 55";
    static uintptr_t    s_hookAddr = 0;

    // SDK aliases
    using namespace cs2_dumper::offsets::client_dll;
    namespace CBE   = cs2_dumper::schemas::client_dll::C_BaseEntity;
    namespace CGSN  = cs2_dumper::schemas::client_dll::CGameSceneNode;
    namespace CBME  = cs2_dumper::schemas::client_dll::C_BaseModelEntity;
    namespace CCSPC = cs2_dumper::schemas::client_dll::CCSPlayerController;
    using namespace Constants::EntityList;

    // =========================================================================
    // Math
    // =========================================================================
    static constexpr float PI      = 3.14159265358979323846f;
    static constexpr float RAD2DEG = 180.0f / PI;

    struct Vec3   { float x=0,y=0,z=0; bool IsZero() const { return !x&&!y&&!z; } };
    struct QAngle { float pitch=0,yaw=0,roll=0; };

    template<typename T>
    static T Read(uintptr_t addr) { T v{}; Memory::SafeRead(addr, v); return v; }

    static void WriteF(uintptr_t addr, float v) { Memory::SafeWrite<float>(addr, v); }

    static void Clamp(QAngle& a) {
        if (a.pitch >  89.f) a.pitch =  89.f;
        if (a.pitch < -89.f) a.pitch = -89.f;
        while (a.yaw >  180.f) a.yaw -= 360.f;
        while (a.yaw < -180.f) a.yaw += 360.f;
        a.roll = 0.f;
    }

    static QAngle CalcAngle(const Vec3& src, const Vec3& dst, uintptr_t localPawn) {
        Vec3 d{ dst.x-src.x, dst.y-src.y, dst.z-src.z };
        float hyp = sqrtf(d.x*d.x + d.y*d.y);
        QAngle a;
        a.pitch = -atan2f(d.z, hyp) * RAD2DEG;
        a.yaw   =  atan2f(d.y, d.x) * RAD2DEG;
        // Recoil compensation (m_aimPunchAngle * 2)
        QAngle punch = Read<QAngle>(localPawn + OFF_AIMPUNCH);
        a.pitch -= punch.pitch * 2.f;
        a.yaw   -= punch.yaw   * 2.f;
        Clamp(a);
        return a;
    }

    static float GetFOV(const QAngle& v, const QAngle& a) {
        float dp = a.pitch - v.pitch;
        float dy = a.yaw   - v.yaw;
        while (dy >  180.f) dy -= 360.f;
        while (dy < -180.f) dy += 360.f;
        return sqrtf(dp*dp + dy*dy);
    }

    // =========================================================================
    // Entity helpers
    // =========================================================================
    static uintptr_t GetEntityByHandle(uintptr_t entList, uint32_t handle) {
        if (!handle || handle == 0xFFFFFFFF || !entList) return 0;
        uint32_t idx   = handle & HANDLE_MASK;
        uint32_t chunk = idx >> ENTRY_SHIFT;
        uint32_t entry = idx & INDEX_MASK;
        uintptr_t le = Read<uintptr_t>(entList + OFFSET_BASE + sizeof(uintptr_t)*chunk);
        if (!le) return 0;
        return Read<uintptr_t>(le + ENTRY_SIZE * entry);
    }

    static Vec3 GetEntityOrigin(uintptr_t pawn) {
        uintptr_t node = Read<uintptr_t>(pawn + CBE::m_pGameSceneNode);
        if (!node) return {};
        return Read<Vec3>(node + CGSN::m_vecAbsOrigin);
    }

    static Vec3 GetEyePos(uintptr_t pawn) {
        Vec3 o = GetEntityOrigin(pawn);
        Vec3 v = Read<Vec3>(pawn + CBME::m_vecViewOffset);
        return { o.x+v.x, o.y+v.y, o.z+v.z };
    }

    static Vec3 GetBonePos(uintptr_t pawn, int bone) {
        uintptr_t node = Read<uintptr_t>(pawn + CBE::m_pGameSceneNode);
        if (!node) return {};
        uintptr_t boneArr = Read<uintptr_t>(node + OFF_MODELSTATE + OFF_BONEARRAY);
        if (!boneArr) return {};
        struct BoneEntry { float px,py,pz,pad,qx,qy,qz,qw; };
        BoneEntry b = Read<BoneEntry>(boneArr + bone * sizeof(BoneEntry));
        return { b.px, b.py, b.pz };
    }

    // =========================================================================
    // Target selection
    // =========================================================================
    struct AimTarget { uintptr_t pawn=0; QAngle angle{}; float fov=FLT_MAX; };

    static AimTarget FindBestTarget(
        uintptr_t clientBase,
        uintptr_t localPawn,
        const Vec3& eye,
        const QAngle& viewAngle)
    {
        AimTarget best{};
        uintptr_t entList = Read<uintptr_t>(clientBase + dwEntityList);
        if (!entList) return best;

        int   localTeam = Read<int>(localPawn + CBE::m_iTeamNum);
        int   fovMode   = std::clamp(config.fovMode.load(), 0, 2);
        float maxFov    = FOV_TABLE[fovMode];

        for (int i = 1; i <= 64; ++i) {
            uint32_t chunkIdx = (uint32_t)i >> 9;
            uint32_t entryIdx = (uint32_t)i & 0x1FF;
            uintptr_t listEntry = Read<uintptr_t>(entList + 0x10 + sizeof(uintptr_t)*chunkIdx);
            if (!listEntry) continue;
            uintptr_t ctrl = Read<uintptr_t>(listEntry + 0x70 * entryIdx);
            if (!ctrl) continue;

            uint32_t pH = Read<uint32_t>(ctrl + CCSPC::m_hPlayerPawn);
            if (!pH || pH == 0xFFFFFFFF) continue;

            uintptr_t pawn = GetEntityByHandle(entList, pH);
            if (!pawn || pawn == localPawn) continue;

            int     hp = Read<int>(pawn + CBE::m_iHealth);
            uint8_t ls = Read<uint8_t>(pawn + CBE::m_lifeState);
            if (hp <= 0 || ls != 0) continue;

            if (config.teamCheck.load())
                if (Read<int>(pawn + CBE::m_iTeamNum) == localTeam) continue;

            Vec3 bonePos = GetBonePos(pawn, config.targetBone.load());
            if (bonePos.IsZero()) bonePos = GetEntityOrigin(pawn);
            if (bonePos.IsZero()) continue;

            QAngle aim = CalcAngle(eye, bonePos, localPawn);

            float score;
            if (fovMode == 2) {
                // 360 mode: no angle filter, pick closest by 3D world distance
                float dx = eye.x - bonePos.x;
                float dy = eye.y - bonePos.y;
                float dz = eye.z - bonePos.z;
                score = sqrtf(dx*dx + dy*dy + dz*dz);
            } else {
                // 90/180 mode: score = FOV angle, skip if outside cone
                score = GetFOV(viewAngle, aim);
                if (score > maxFov) continue;
            }

            if (score < best.fov) { best.pawn=pawn; best.angle=aim; best.fov=score; }
        }
        return best;
    }

    // =========================================================================
    // Frame history patch helpers
    // =========================================================================
    struct SavedEntry { float pitch; float yaw; };

    static int PatchFrameHistory(__int64 a1, float newPitch, float newYaw, SavedEntry* saved, int maxSaved) {
        int count = *reinterpret_cast<int*>(a1 + FRAME_HISTORY_COUNT);
        if (count <= 0 || count > maxSaved) return 0;
        uintptr_t arr = *reinterpret_cast<uintptr_t*>(a1 + FRAME_HISTORY_ARRAY);
        if (!arr || !Memory::IsValidPtr(arr)) return 0;

        for (int i = 0; i < count; ++i) {
            uintptr_t entry = arr + FRAME_ENTRY_SIZE * i;
            saved[i].pitch = Read<float>(entry + ENTRY_PITCH_OFF);
            saved[i].yaw   = Read<float>(entry + ENTRY_YAW_OFF);
            WriteF(entry + ENTRY_PITCH_OFF, newPitch);
            WriteF(entry + ENTRY_YAW_OFF,   newYaw);
        }
        return count;
    }

    static void RestoreFrameHistory(__int64 a1, const SavedEntry* saved, int count) {
        uintptr_t arr = *reinterpret_cast<uintptr_t*>(a1 + FRAME_HISTORY_ARRAY);
        if (!arr || !Memory::IsValidPtr(arr)) return;
        for (int i = 0; i < count; ++i) {
            uintptr_t entry = arr + FRAME_ENTRY_SIZE * i;
            WriteF(entry + ENTRY_PITCH_OFF, saved[i].pitch);
            WriteF(entry + ENTRY_YAW_OFF,   saved[i].yaw);
        }
    }

    // =========================================================================
    // CreateMove hook
    // =========================================================================
    static double __fastcall hkCreateMove(__int64 a1, unsigned int a2, __int64 a3) {
        if (!oCreateMove) return 0.0;

        if (!config.enabled.load())
            return oCreateMove(a1, a2, a3);

        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000))
            return oCreateMove(a1, a2, a3);

        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) return oCreateMove(a1, a2, a3);

            uintptr_t localCtrl = Read<uintptr_t>(clientBase + dwLocalPlayerController);
            if (!localCtrl || !Memory::IsValidPtr(localCtrl))
                return oCreateMove(a1, a2, a3);

            uintptr_t entList = Read<uintptr_t>(clientBase + dwEntityList);
            if (!entList) return oCreateMove(a1, a2, a3);

            uint32_t  localHandle = Read<uint32_t>(localCtrl + CCSPC::m_hPlayerPawn);
            uintptr_t localPawn   = GetEntityByHandle(entList, localHandle);
            if (!localPawn) return oCreateMove(a1, a2, a3);

            if (Read<int>(localPawn + CBE::m_iHealth) <= 0)
                return oCreateMove(a1, a2, a3);

            uintptr_t pViewAngles = clientBase + dwViewAngles;
            QAngle    viewAngle   = Read<QAngle>(pViewAngles);
            Vec3      eyePos      = GetEyePos(localPawn);

            AimTarget target = FindBestTarget(clientBase, localPawn, eyePos, viewAngle);
            if (!target.pawn) return oCreateMove(a1, a2, a3);

            // ---- SAVE ----
            QAngle savedView = viewAngle;
            QAngle savedEye  = Read<QAngle>(localPawn + OFF_EYE_ANGLES);
            SavedEntry savedFrames[64];
            int frameCount = PatchFrameHistory(a1, target.angle.pitch, target.angle.yaw, savedFrames, 64);

            // ---- PATCH ----
            WriteF(pViewAngles + 0x0, target.angle.pitch);
            WriteF(pViewAngles + 0x4, target.angle.yaw);
            WriteF(pViewAngles + 0x8, 0.f);
            WriteF(localPawn + OFF_EYE_ANGLES + 0x0, target.angle.pitch);
            WriteF(localPawn + OFF_EYE_ANGLES + 0x4, target.angle.yaw);
            WriteF(localPawn + OFF_EYE_ANGLES + 0x8, 0.f);

            // ---- CALL ORIGINAL ----
            double result = oCreateMove(a1, a2, a3);

            // ---- RESTORE ----
            if (frameCount > 0)
                RestoreFrameHistory(a1, savedFrames, frameCount);
            WriteF(pViewAngles + 0x0, savedView.pitch);
            WriteF(pViewAngles + 0x4, savedView.yaw);
            WriteF(pViewAngles + 0x8, savedView.roll);
            WriteF(localPawn + OFF_EYE_ANGLES + 0x0, savedEye.pitch);
            WriteF(localPawn + OFF_EYE_ANGLES + 0x4, savedEye.yaw);
            WriteF(localPawn + OFF_EYE_ANGLES + 0x8, savedEye.roll);

            return result;

        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return oCreateMove(a1, a2, a3);
        }
    }

    // =========================================================================
    // Init / Shutdown
    // =========================================================================
    bool Init() {
        uintptr_t client = Memory::GetModuleBase("client.dll");
        if (!client) return false;

        size_t clientSize = Memory::GetModuleSize(client);
        if (!clientSize) return false;

        uintptr_t addr = Memory::StringPatternScan(client, clientSize, CREATEMOVE_SIG);
        if (!addr) return false;

        if (MH_CreateHook(
                reinterpret_cast<void*>(addr),
                reinterpret_cast<void*>(&hkCreateMove),
                reinterpret_cast<void**>(&oCreateMove)) != MH_OK)
            return false;

        if (MH_EnableHook(reinterpret_cast<void*>(addr)) != MH_OK)
            return false;

        s_hookAddr = addr;
        return true;
    }

    void Shutdown() {
        if (!oCreateMove) return;
        // SilentAim owns the enable/disable lifecycle for the CreateMove hook.
        // FakeBody shares the same hook address but only calls MH_RemoveHook.
        // We disable the hook here (which covers both chained handlers) then
        // remove it so MinHook's internal table is fully clean.
        if (s_hookAddr) {
            MH_DisableHook(reinterpret_cast<void*>(s_hookAddr));
            MH_RemoveHook(reinterpret_cast<void*>(s_hookAddr));
        }
        oCreateMove = nullptr;
        s_hookAddr  = 0;
    }

    void Run()       { /* hook-driven, nothing to poll */ }
    void RenderFOV() { /* implement ImGui FOV circle here if needed */ }

} // namespace SilentAim
