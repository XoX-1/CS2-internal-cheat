#pragma once

#include <atomic>

namespace Triggerbot {
    void Run();
    void Reset(); // Reset triggerbot state (call on match transition)
    
    // Firing modes
    enum FireMode : int { MODE_TAPPING = 0, MODE_LAZER = 1 };

    // Settings - atomic for thread safety
    extern std::atomic<bool> g_bEnabled;
    extern std::atomic<bool> g_bTeamCheck;
    extern std::atomic<int> g_nFireMode;       // 0 = Tapping (single shot), 1 = Lazer (full auto)
    extern std::atomic<float> g_fMaxDistance;  // Maximum target distance
}
