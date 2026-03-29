#pragma once
#include <atomic>
#include <cstdint>

namespace SilentAim {
    // Configuration structure
    struct Config {
        std::atomic<bool> enabled{false};
        std::atomic<int>  targetBone{6};
        std::atomic<bool> teamCheck{true};
        std::atomic<int>  fovMode{0};
        std::atomic<int>  hitboxMode{0};
        std::atomic<int>  minDamage{0};
        std::atomic<bool> forceBodyAim{false};
        std::atomic<bool> forceHeadshot{false};
    };

    // Target data structure
    struct TargetData {
        uintptr_t pawn = 0;
        float point[3] = {0, 0, 0};
        float angle[2] = {0, 0};
        float damage = 0.f;
        float distance = 0.f;
        int hitboxIndex = -1;

        bool isValid() const {
            return pawn != 0 && damage >= 0.f;
        }
    };

    // State structure
    struct State {
        TargetData currentTarget;
        bool hasTarget = false;
        int shotSequence = 0;
    };

    // Global instances
    extern Config config;
    extern State state;

    // Core functions
    bool Init();
    void Shutdown();
    
    // Called from render loop (like Aimbot::Run)
    void Run();
    
    // FOV rendering
    void RenderFOV();
}
