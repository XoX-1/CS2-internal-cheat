#pragma once
#include <cstdint>

namespace BulletTracer {
    struct TracerConfig {
        bool  enabled     = false;
        float trailLife   = 2.5f;     // seconds before full fade
        float bulletSpeed = 8000.0f;  // visual bullet travel speed (units/sec)
        float thickness   = 2.0f;
        float rayLength   = 8000.0f;  // how far the ray extends
    };

    extern TracerConfig config;

    // Call from CreateMove hook when a shot is detected
    void AddTrace(float eyeX, float eyeY, float eyeZ, float pitch, float yaw);

    // Detect shot via m_iShotsFired change
    bool DetectShot(uintptr_t localPawn);

    // Independent tick for shot detection (call from Present if CreateMove not hooked)
    void Tick();

    // Render all active traces (call from Present hook)
    void Render();
}
