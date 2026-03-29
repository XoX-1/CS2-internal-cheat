#include "bullet_tracer.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/math.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include "imgui.h"
#include <vector>
#include <mutex>
#include <cmath>
#include <algorithm>
#include <windows.h>

namespace BulletTracer {

    TracerConfig config;

    // ===== Math =====
    static constexpr float PI = 3.14159265358979323846f;
    static constexpr float DEG2RAD = PI / 180.0f;

    static void AngleToDir(float pitch, float yaw, float out[3]) {
        float cp = cosf(pitch * DEG2RAD);
        float sp = sinf(pitch * DEG2RAD);
        float cy = cosf(yaw * DEG2RAD);
        float sy = sinf(yaw * DEG2RAD);
        out[0] = cp * cy;
        out[1] = cp * sy;
        out[2] = -sp;
    }

    // ===== Trace storage =====
    struct Trace {
        float startPos[3];
        float endPos[3];
        float spawnTime;
        float totalDist;
    };

    static std::vector<Trace> traces;
    static std::mutex traceMutex;
    static int lastShotsFired = 0;

    // Shot detection via attack button state (independent of CreateMove)
    static bool g_prevAttackButton = false;
    static float g_lastShotTime = 0.0f;
    static constexpr float SHOT_COOLDOWN = 0.1f; // Minimum time between shots

    static float GetTime() {
        return static_cast<float>(GetTickCount64()) / 1000.0f;
    }

    // ===== API =====
    void AddTrace(float eyeX, float eyeY, float eyeZ, float pitch, float yaw) {
        if (!config.enabled) return;

        float dir[3];
        AngleToDir(pitch, yaw, dir);

        Trace t;
        t.startPos[0] = eyeX;
        t.startPos[1] = eyeY;
        t.startPos[2] = eyeZ;
        t.endPos[0] = eyeX + dir[0] * config.rayLength;
        t.endPos[1] = eyeY + dir[1] * config.rayLength;
        t.endPos[2] = eyeZ + dir[2] * config.rayLength;
        t.spawnTime = GetTime();

        float dx = t.endPos[0] - eyeX;
        float dy = t.endPos[1] - eyeY;
        float dz = t.endPos[2] - eyeZ;
        t.totalDist = sqrtf(dx * dx + dy * dy + dz * dz);

        std::lock_guard<std::mutex> lock(traceMutex);
        traces.push_back(t);
    }

    bool DetectShot(uintptr_t localPawn) {
        if (!localPawn) return false;

        // Use the dumper-confirmed offset for m_iShotsFired
        constexpr std::ptrdiff_t OFF_SHOTS_FIRED = 0x270C; // C_CSPlayerPawn::m_iShotsFired
        int currentShots = 0;
        Memory::SafeRead(localPawn + OFF_SHOTS_FIRED, currentShots);

        if (currentShots > lastShotsFired && lastShotsFired >= 0) {
            lastShotsFired = currentShots;
            return true;
        }
        lastShotsFired = currentShots;
        return false;
    }

    // ===== Independent shot detection (works without CreateMove hook) =====
    // Detects shots by checking attack button state transitions
    static bool DetectShotViaButton() {
        bool currentAttack = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        float now = GetTime();

        bool shot = false;
        if (currentAttack && !g_prevAttackButton) {
            // Button just pressed
            if ((now - g_lastShotTime) > SHOT_COOLDOWN) {
                shot = true;
                g_lastShotTime = now;
            }
        }
        g_prevAttackButton = currentAttack;
        return shot;
    }

    // ===== Tick function for independent shot detection =====
    // Call this from the Present hook to detect shots without CreateMove
    void Tick() {
        if (!config.enabled) return;

        // Detect shot via button state
        if (!DetectShotViaButton()) return;

        // Get local pawn and eye position for the trace
        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        using namespace cs2_dumper::offsets::client_dll;
        namespace CBE = cs2_dumper::schemas::client_dll::C_BaseEntity;
        namespace CBPP = cs2_dumper::schemas::client_dll::C_CSPlayerPawn;

        uintptr_t localPawn = 0;
        Memory::SafeRead(clientBase + dwLocalPlayerPawn, localPawn);
        if (!localPawn || !Memory::IsValidPtr(localPawn)) return;

        // Read health to make sure we're alive
        int health = 0;
        Memory::SafeRead(localPawn + CBE::m_iHealth, health);
        if (health <= 0) return;

        // Get eye position
        uintptr_t gameSceneNode = 0;
        Memory::SafeRead(localPawn + CBE::m_pGameSceneNode, gameSceneNode);
        if (!gameSceneNode) return;

        struct Vec3 { float x, y, z; };
        Vec3 origin = {};
        namespace CGSN = cs2_dumper::schemas::client_dll::CGameSceneNode;
        Memory::SafeRead(gameSceneNode + CGSN::m_vecAbsOrigin, origin);

        Vec3 viewOffset = {};
        // m_vecViewOffset is in C_BaseModelEntity (parent of C_BasePlayerPawn)
        constexpr std::ptrdiff_t OFF_VIEW_OFFSET = 0xD58; // Confirmed by dumper
        Memory::SafeRead(localPawn + OFF_VIEW_OFFSET, viewOffset);

        Vec3 eyePos = { origin.x + viewOffset.x, origin.y + viewOffset.y, origin.z + viewOffset.z };

        // Read view angles
        struct QAngle { float pitch, yaw, roll; };
        QAngle viewAngles = {};
        Memory::SafeRead(clientBase + dwViewAngles, viewAngles);

        AddTrace(eyePos.x, eyePos.y, eyePos.z, viewAngles.pitch, viewAngles.yaw);
    }

    // ===== Helper: lerp along trace =====
    static void LerpPos(const Trace& t, float frac, float out[3]) {
        out[0] = t.startPos[0] + (t.endPos[0] - t.startPos[0]) * frac;
        out[1] = t.startPos[1] + (t.endPos[1] - t.startPos[1]) * frac;
        out[2] = t.startPos[2] + (t.endPos[2] - t.startPos[2]) * frac;
    }

    // ===== W2S adapter using our project's types =====
    static bool W2S(const float pos[3], float& sx, float& sy, const view_matrix_t& vm, float sw, float sh) {
        Vector3 w{ pos[0], pos[1], pos[2] };
        Vector2 s;
        if (!WorldToScreen(w, s, vm, (int)sw, (int)sh)) return false;
        sx = s.x;
        sy = s.y;
        return true;
    }

    // ===== Render =====
    void Render() {
        if (!config.enabled) return;

        // Run independent shot detection first
        Tick();

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        float now = GetTime();
        ImDrawList* draw = ImGui::GetBackgroundDrawList();
        if (!draw) return;

        // Read view matrix safely (SEH-guarded SafeReadBytes instead of raw memcpy)
        using namespace cs2_dumper::offsets::client_dll;
        view_matrix_t viewMatrix{};
        if (!Memory::SafeReadBytes(clientBase + dwViewMatrix, &viewMatrix, sizeof(view_matrix_t))) return;

        float screenW = ImGui::GetIO().DisplaySize.x;
        float screenH = ImGui::GetIO().DisplaySize.y;

        std::lock_guard<std::mutex> lock(traceMutex);

        // Cleanup expired
        float maxAge = config.trailLife + 2.0f;
        traces.erase(
            std::remove_if(traces.begin(), traces.end(),
                [now, maxAge](const Trace& t) { return (now - t.spawnTime) > maxAge; }),
            traces.end());

        for (const auto& t : traces) {
            float age = now - t.spawnTime;

            // Bullet travel progress
            float travelTime = t.totalDist / config.bulletSpeed;
            if (travelTime < 0.01f) travelTime = 0.01f;
            float bulletFrac = age / travelTime;
            if (bulletFrac > 1.0f) bulletFrac = 1.0f;

            // Trail fade
            float trailAge = age - travelTime;
            float trailAlpha = 1.0f;
            if (trailAge > 0.0f) {
                trailAlpha = 1.0f - (trailAge / config.trailLife);
                if (trailAlpha <= 0.0f) continue;
                trailAlpha *= trailAlpha;
            }

            // Segmented trail
            constexpr int SEGMENTS = 16;
            ImVec2 pts[SEGMENTS + 1];
            bool   ok[SEGMENTS + 1] = {};

            for (int s = 0; s <= SEGMENTS; s++) {
                float segFrac = (float)s / (float)SEGMENTS * bulletFrac;
                float pos3d[3];
                LerpPos(t, segFrac, pos3d);
                ok[s] = W2S(pos3d, pts[s].x, pts[s].y, viewMatrix, screenW, screenH);
            }

            for (int s = 0; s < SEGMENTS; s++) {
                if (!ok[s] || !ok[s + 1]) continue;

                float segFrac = (float)s / (float)SEGMENTS;
                float brightness = 0.2f + 0.8f * segFrac;
                int alpha = (int)(trailAlpha * brightness * 220.0f);
                if (alpha <= 0) continue;
                if (alpha > 255) alpha = 255;

                // White trace line
                draw->AddLine(pts[s], pts[s + 1], IM_COL32(255, 255, 255, alpha), config.thickness);

                // Glow
                int glowA = (int)(trailAlpha * brightness * 40.0f);
                if (glowA > 255) glowA = 255;
                draw->AddLine(pts[s], pts[s + 1], IM_COL32(180, 200, 255, glowA), config.thickness * 3.5f);
            }

            // Bullet head dot
            if (bulletFrac < 1.0f) {
                float headPos[3];
                LerpPos(t, bulletFrac, headPos);
                float hsx, hsy;
                if (W2S(headPos, hsx, hsy, viewMatrix, screenW, screenH)) {
                    int ha = (int)(trailAlpha * 255.0f);
                    draw->AddCircleFilled(ImVec2(hsx, hsy), 5.0f, IM_COL32(255, 255, 255, ha));
                    draw->AddCircleFilled(ImVec2(hsx, hsy), 2.5f, IM_COL32(255, 255, 200, ha));
                }
            }

            // Impact point
            if (bulletFrac >= 1.0f && trailAlpha > 0.05f) {
                float isx, isy;
                if (W2S(t.endPos, isx, isy, viewMatrix, screenW, screenH)) {
                    float sz = 4.0f * trailAlpha;
                    draw->AddCircleFilled(ImVec2(isx, isy), sz, IM_COL32(255, 200, 100, (int)(trailAlpha * 200.0f)));
                    draw->AddCircle(ImVec2(isx, isy), sz * 1.8f, IM_COL32(255, 255, 255, (int)(trailAlpha * 120.0f)), 0, 1.5f);
                }
            }
        }
    }
}
