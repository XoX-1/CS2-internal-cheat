#pragma once

#include <windows.h>
#include <d3d11.h>
#include <atomic>
#include "MinHook.h"
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

namespace Hooks {
    void Initialize();
    void Shutdown();
    
    // Game state validation - call this before accessing game memory
    // Returns true if we're in a valid game state (not loading, not in menu)
    bool IsGameReady();
    
    // Check if we just transitioned to a new game state (match/map change)
    // Returns true if state changed, resets internal caches
    bool CheckGameStateTransition();

    // Thread-safe global settings using std::atomic
    // ESP settings
    extern std::atomic<bool> g_bEspEnabled;
    extern std::atomic<bool> g_bEspBoxes;
    extern std::atomic<int>  g_nEspBoxStyle;   // 0=Full, 1=Corner, 2=Rounded
    extern std::atomic<bool> g_bEspHealth;
    extern std::atomic<bool> g_bEspArmor;
    extern std::atomic<bool> g_bEspNames;
    extern std::atomic<bool> g_bEspWeaponName;
    extern std::atomic<bool> g_bEspDistance;
    extern std::atomic<bool> g_bEspSnaplines;
    extern std::atomic<bool> g_bEspSkeleton;
    extern std::atomic<bool> g_bEspHeadDot;
    extern float g_fEspEnemyColor[4];
    extern float g_fEspTeamColor[4];

    // Aimbot settings
    extern std::atomic<bool> g_bAimbotEnabled;
    extern std::atomic<float> g_fAimbotFov;
    extern std::atomic<float> g_fAimbotSmooth;
    extern std::atomic<int> g_nAimbotBone; // 6=Head, 5=Neck, 4=Chest
    extern std::atomic<bool> g_bFFAEnabled; // Free-For-All mode for Deathmatch

    // Triggerbot settings


    // Player FOV settings
    extern std::atomic<bool> g_bFovChangerEnabled;
    extern std::atomic<float> g_fPlayerFov;

    extern std::atomic<bool> g_bSpectatorListEnabled;
    extern std::atomic<bool> g_bBombTimerEnabled;
    extern std::atomic<bool> g_bKillSoundEnabled;

    // Visuals extra
    extern std::atomic<bool> g_bNoFlashEnabled;
    extern std::atomic<bool> g_bNoSmokeEnabled;

    // Glow Options
    extern std::atomic<bool> g_bGlowEnabled;
    extern std::atomic<bool> g_bGlowTeamEnabled;
    extern float g_fGlowEnemyColor[4];
    extern float g_fGlowTeamColor[4];

    // Radar settings
    extern std::atomic<bool>  g_bRadarEnabled;
    extern std::atomic<float> g_fRadarSize;       // Radar window size in pixels
    extern std::atomic<float> g_fRadarRange;      // Range in game units
    extern std::atomic<float> g_fRadarZoom;       // Zoom multiplier
    extern std::atomic<float> g_fRadarAlpha;      // Background opacity
    extern std::atomic<int>   g_nRadarStyle;      // 0=Circular, 1=Square
    extern std::atomic<bool>  g_bRadarShowNames;  // Show player names on dots
    extern std::atomic<bool>  g_bRadarShowHealth; // Show health bars on dots
    extern float g_fRadarEnemyColor[4];
    extern float g_fRadarTeamColor[4];

    // Movement settings
    extern std::atomic<bool> g_bBhopEnabled;

    // Inventory Changer
    extern std::atomic<bool> g_bInventoryChangerEnabled;

    // Sky Changer
    extern std::atomic<bool> g_bSkyChangerEnabled;

    // Thread control
    extern std::atomic<bool> g_bRunning;
}

// Backward compatibility macros for non-atomic access
// Use .load() for reads and .store() for writes
#define GET_BOOL(var) (var).load()
#define SET_BOOL(var, val) (var).store(val)
#define GET_INT(var) (var).load()
#define SET_INT(var, val) (var).store(val)
#define GET_FLOAT(var) (var).load()
#define SET_FLOAT(var, val) (var).store(val)
