#include "hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/obfuscation.hpp"
#include "../sdk/antidebug.hpp"
#include "../features/aimbot.hpp"
#include "../features/player_fov.hpp"
#include "../features/bhop.hpp"
#include "../features/noflash.hpp"
#include "../features/nosmoke.hpp"
#include "../features/glow.hpp"
#include "../features/triggerbot.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include <iostream>

// Forward declaration from dx11_hook.cpp
namespace DX11Hook {
    void Initialize();
    void Shutdown();
}

namespace Hooks {
    // ESP settings - atomic for thread safety
    std::atomic<bool> g_bEspEnabled{true};
    std::atomic<bool> g_bEspBoxes{true};
    std::atomic<int>  g_nEspBoxStyle{1};  // Corner box default
    std::atomic<bool> g_bEspHealth{true};
    std::atomic<bool> g_bEspArmor{false};
    std::atomic<bool> g_bEspNames{true};
    std::atomic<bool> g_bEspDistance{false};
    std::atomic<bool> g_bEspSnaplines{false};
    std::atomic<bool> g_bEspSkeleton{false};
    std::atomic<bool> g_bEspHeadDot{false};
    float g_fEspEnemyColor[4] = { 1.0f, 0.3f, 0.3f, 1.0f };
    float g_fEspTeamColor[4]  = { 0.3f, 0.5f, 1.0f, 1.0f };

    std::atomic<bool> g_bAimbotEnabled{false};
    std::atomic<float> g_fAimbotFov{5.0f};
    std::atomic<float> g_fAimbotSmooth{5.0f};
    std::atomic<int> g_nAimbotBone{6}; // Head
    std::atomic<bool> g_bFFAEnabled{false};

    std::atomic<bool> g_bFovChangerEnabled{false};
    std::atomic<float> g_fPlayerFov{90.0f}; // Default CS2 FOV

    std::atomic<bool> g_bSpectatorListEnabled{false};

    std::atomic<bool> g_bBhopEnabled{false}; // Bunnyhop
    
    std::atomic<bool> g_bNoFlashEnabled{false}; // NoFlash
    std::atomic<bool> g_bNoSmokeEnabled{false}; // NoSmoke

    std::atomic<bool> g_bGlowEnabled{false}; // Enemy Glow
    std::atomic<bool> g_bGlowTeamEnabled{false}; // Team Glow
    float g_fGlowEnemyColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f }; // Red
    float g_fGlowTeamColor[4] = { 0.0f, 0.5f, 1.0f, 1.0f }; // Blue

    // Radar settings
    std::atomic<bool>  g_bRadarEnabled{false};
    std::atomic<float> g_fRadarSize{200.0f};       // 200px default
    std::atomic<float> g_fRadarRange{2500.0f};     // ~25m default range
    std::atomic<float> g_fRadarZoom{1.0f};         // 1x zoom
    std::atomic<float> g_fRadarAlpha{0.85f};       // 85% opacity
    std::atomic<int>   g_nRadarStyle{0};           // Circular
    std::atomic<bool>  g_bRadarShowNames{false};
    std::atomic<bool>  g_bRadarShowHealth{true};
    float g_fRadarEnemyColor[4] = { 1.0f, 0.3f, 0.3f, 1.0f }; // Red
    float g_fRadarTeamColor[4]  = { 0.3f, 0.5f, 1.0f, 1.0f }; // Blue

    std::atomic<bool> g_bRunning{true};
    HANDLE g_hMainThread = nullptr;

    DWORD WINAPI MainCheatThread(LPVOID lpParam) {
        std::cout << XOR_STR("[+] Cheat logic thread started\n");
        
        while (g_bRunning.load()) {
            // Check for game state transitions (map/match changes)
            // This resets feature states to prevent stale pointer issues
            if (CheckGameStateTransition()) {
                Aimbot::Reset();
                Triggerbot::Reset();
            }
            
            // Check if game is in a valid state before running features
            // This prevents crashes during level transitions and loading screens
            if (IsGameReady()) {
                if (g_bFovChangerEnabled.load()) {
                    PlayerFov::Run();
                }
                if (g_bBhopEnabled.load()) {
                    Bunnyhop::Run();
                }
                if (g_bNoFlashEnabled.load()) {
                    NoFlash::Run();
                }
                if (g_bNoSmokeEnabled.load()) {
                    NoSmoke::Run();
                }
                if (g_bGlowEnabled.load()) {
                    Glow::Run();
                }
                if (Triggerbot::g_bEnabled.load()) {
                    Triggerbot::Run();
                }
            } else {
                // Reset features when game is not ready (loading/menu)
                // This prevents stale pointers when entering new matches
                static int notReadyCounter = 0;
                notReadyCounter++;
                if (notReadyCounter > 500) { // Reset every ~500ms when not ready
                    Aimbot::Reset();
                    Triggerbot::Reset();
                    notReadyCounter = 0;
                }
            }
            
            Sleep(Constants::Timing::THREAD_SLEEP_MS); // ~1000Hz
        }
        
        std::cout << XOR_STR("[+] Cheat logic thread stopped\n");
        return 0;
    }

    void Initialize() {
        // Check for debuggers/analysis tools
        if (AntiDebug::IsBeingDebugged(AntiDebug::CHECK_DEBUGGER_WINDOWS | 
                                        AntiDebug::CHECK_ANALYSIS_TOOLS)) {
            // In production, you might want to take action here
            // For now, just log it
            std::cout << XOR_STR("[!] Warning: Analysis tool detected\n");
        }
        
        MH_Initialize();
        DX11Hook::Initialize();

        g_bRunning.store(true);
        g_hMainThread = CreateThread(nullptr, 0, MainCheatThread, nullptr, 0, nullptr);
    }

    void Shutdown() {
        g_bRunning.store(false);

        if (g_hMainThread) {
            WaitForSingleObject(g_hMainThread, 1000);
            CloseHandle(g_hMainThread);
            g_hMainThread = nullptr;
        }

        DX11Hook::Shutdown();
        MH_Uninitialize();
    }

    bool IsGameReady() {
        // Check if game modules are loaded
        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return false;
        
        uintptr_t engineBase = Memory::GetModuleBase("engine2.dll");
        if (!engineBase) return false;
        
        // Check if we have a valid local player
        uintptr_t localPawn = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || 
            !Memory::IsValidPtr(localPawn)) {
            return false;
        }
        
        // Check if local player is valid (has valid health/lifestate pointers would be checked)
        int health = 0;
        if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health)) {
            return false;
        }
        
        // Health can be 0 when dead, but the read should succeed
        // Additional check: make sure entity list is valid
        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
            !Memory::IsValidPtr(entityList)) {
            return false;
        }
        
        return true;
    }

    // Game state tracking for detecting match transitions
    static uintptr_t s_lastEntityList = 0;
    static uintptr_t s_lastLocalPawn = 0;
    static int s_gameStateCheckCounter = 0;
    static bool s_wasGameReady = false;
    
    bool CheckGameStateTransition() {
        // Only check every 100 frames (~100ms) to avoid overhead
        s_gameStateCheckCounter++;
        if (s_gameStateCheckCounter < Constants::Timing::STATE_CHECK_INTERVAL) {
            return false;
        }
        s_gameStateCheckCounter = 0;
        
        bool isReady = IsGameReady();
        
        // If game just became ready after not being ready, we might have transitioned
        if (isReady && !s_wasGameReady) {
            // Get current state
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            uintptr_t entityList = 0;
            uintptr_t localPawn = 0;
            
            Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList);
            Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn);
            
            // Check if entity list or local pawn changed (indicates map/match change)
            bool stateChanged = (entityList != s_lastEntityList) || (localPawn != s_lastLocalPawn);
            
            // Update stored state
            s_lastEntityList = entityList;
            s_lastLocalPawn = localPawn;
            s_wasGameReady = true;
            
            if (stateChanged) {
                std::cout << XOR_STR("[+] Game state transition detected - resetting features\n");
                return true;
            }
        }
        
        s_wasGameReady = isReady;
        return false;
    }
} // namespace Hooks
