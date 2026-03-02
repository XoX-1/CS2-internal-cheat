#pragma once

#include <atomic>

namespace Triggerbot {
    void Run();
    void Reset(); // Reset triggerbot state (call on match transition)
    
    // Settings - atomic for thread safety
    extern std::atomic<bool> g_bEnabled;
    extern std::atomic<bool> g_bTeamCheck;
    extern std::atomic<int> g_nDelayMs;        // Delay before firing (ms)
    extern std::atomic<int> g_nBurstAmount;    // Shots to fire (0 = single, -1 = auto)
    extern std::atomic<float> g_fMaxDistance;  // Maximum target distance
}
