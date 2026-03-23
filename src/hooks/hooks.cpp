#include "hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/obfuscation.hpp"
#include "../sdk/antidebug.hpp"
#include "../features/aimbot.hpp"
#include "../features/bhop.hpp"
#include "../features/player_fov.hpp"
#include "../features/noflash.hpp"
#include "../features/nosmoke.hpp"
#include "../features/killsound.hpp"
#include "../features/triggerbot.hpp"
#include "../features/keybind_manager.hpp"
#include "../features/inventory_changer.hpp"
#include "../features/skychanger.hpp"
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
    std::atomic<bool> g_bEspWeaponName{true};
    std::atomic<bool> g_bEspDistance{false};
    std::atomic<bool> g_bEspSnaplines{false};
    std::atomic<bool> g_bEspSkeleton{false};
    std::atomic<bool> g_bEspHeadDot{false};
    float g_fEspEnemyColor[4] = { 1.0f, 0.3f, 0.3f, 1.0f };
    float g_fEspTeamColor[4]  = { 0.3f, 0.5f, 1.0f, 1.0f };

    // Aimbot settings
    std::atomic<bool> g_bAimbotEnabled{false};
    std::atomic<float> g_fAimbotFov{5.0f};
    std::atomic<float> g_fAimbotSmooth{5.0f};
    std::atomic<int> g_nAimbotBone{6}; // Head
    std::atomic<bool> g_bFFAEnabled{false};

    // Player FOV settings
    std::atomic<bool> g_bFovChangerEnabled{false};
    std::atomic<float> g_fPlayerFov{90.0f};

    std::atomic<bool> g_bSpectatorListEnabled{false};
    std::atomic<bool> g_bBombTimerEnabled{false};
    std::atomic<bool> g_bKillSoundEnabled{false};

    // Visuals extra
    std::atomic<bool> g_bNoFlashEnabled{false};
    std::atomic<bool> g_bNoSmokeEnabled{false};

    // Glow Options
    std::atomic<bool> g_bGlowEnabled{false};
    std::atomic<bool> g_bGlowTeamEnabled{false};
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

    // Movement settings
    std::atomic<bool> g_bBhopEnabled{false};

    // Inventory Changer
    std::atomic<bool> g_bInventoryChangerEnabled{false};

    // Sky Changer
    std::atomic<bool> g_bSkyChangerEnabled{false};

    std::atomic<bool> g_bRunning{true};
    HANDLE g_hMainThread = nullptr;

    DWORD WINAPI MainCheatThread(LPVOID lpParam) {
        std::cout << XOR_STR("[+] Cheat logic thread started\n");
        
        while (g_bRunning.load()) {
            // Check for game state transitions (map/match changes)
            if (CheckGameStateTransition()) {
                Aimbot::Reset();
                Triggerbot::Reset();
                Bunnyhop::Reset();
                KillSound::Reset();
                SkyChanger::Reset();
            }
            
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
                if (g_bKillSoundEnabled.load()) {
                    KillSound::Run();
                }
                if (Triggerbot::g_bEnabled.load()) {
                    Triggerbot::Run();
                }
                SkyChanger::Run();
            } else {
                static int notReadyCounter = 0;
                notReadyCounter++;
                if (notReadyCounter > 500) {
                    Aimbot::Reset();
                    Triggerbot::Reset();
                    KillSound::Reset();
                    notReadyCounter = 0;
                }
            }
            
            Sleep(Constants::Timing::THREAD_SLEEP_MS); // ~1000Hz
        }
        
        std::cout << XOR_STR("[+] Cheat logic thread stopped\n");
        return 0;
    }

    void Initialize() {
        if (AntiDebug::IsBeingDebugged(AntiDebug::CHECK_DEBUGGER_WINDOWS | 
                                        AntiDebug::CHECK_ANALYSIS_TOOLS)) {
            std::cout << XOR_STR("[!] Warning: Analysis tool detected\n");
        }
        
        KeybindManager::Initialize();
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
        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return false;
        
        uintptr_t engineBase = Memory::GetModuleBase("engine2.dll");
        if (!engineBase) return false;

        uintptr_t networkClient = 0;
        if (!Memory::SafeRead(engineBase + cs2_dumper::offsets::engine2_dll::dwNetworkGameClient, networkClient) ||
            !Memory::IsValidPtr(networkClient)) {
            return false;
        }
        int signOnState = 0;
        if (!Memory::SafeRead(networkClient + cs2_dumper::offsets::engine2_dll::dwNetworkGameClient_signOnState, signOnState) ||
            signOnState != 6) {
            return false;
        }
        
        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || 
            !Memory::IsValidPtr(entityList)) {
            return false;
        }

        uintptr_t localController = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) ||
            !Memory::IsValidPtr(localController)) {
            return false;
        }

        uintptr_t localPawn = Memory::ResolvePawnFromController(entityList, localController);
        if (!localPawn) {
            return false;
        }
        
        int health = 0;
        if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health)) {
            return false;
        }
        
        return true;
    }

    static uintptr_t s_lastEntityList = 0;
    static uintptr_t s_lastLocalPawn = 0;
    static int s_gameStateCheckCounter = 0;
    static bool s_wasGameReady = false;
    
    bool CheckGameStateTransition() {
        s_gameStateCheckCounter++;
        if (s_gameStateCheckCounter < Constants::Timing::STATE_CHECK_INTERVAL) {
            return false;
        }
        s_gameStateCheckCounter = 0;
        
        bool isReady = IsGameReady();
        
        if (isReady && !s_wasGameReady) {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            uintptr_t entityList = 0;
            
            Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList);

            uintptr_t localController = 0;
            Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController);
            uintptr_t localPawn = Memory::ResolvePawnFromController(entityList, localController);
            
            bool stateChanged = (entityList != s_lastEntityList) || (localPawn != s_lastLocalPawn);
            
            s_lastEntityList = entityList;
            s_lastLocalPawn = localPawn;
            s_wasGameReady = true;
            
            if (stateChanged) {
                std::cout << XOR_STR("[+] Game state transition detected - resetting features\n");
                InventoryUI::ResetRegen();
                return true;
            }
        }
        
        s_wasGameReady = isReady;
        return false;
    }
} // namespace Hooks
