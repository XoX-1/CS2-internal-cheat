#include "hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/obfuscation.hpp"
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
#include "../features/third_person.hpp"
#include "../features/silent_aim.hpp"

#include <offsets.hpp>
#include <client_dll.hpp>
#include <iostream>
#include <mutex>
#include <timeapi.h>     // timeBeginPeriod / timeEndPeriod
#pragma comment(lib, "winmm.lib")

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
    // CRASH FIX: These arrays are read on the render thread and written on the UI thread.
    // We protect them with a dedicated mutex to prevent torn writes causing
    // out-of-range alpha values that crash IM_COL32().
    std::mutex g_colorMutex;
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
    std::atomic<bool> g_bThirdPersonEnabled{false};

    // Glow Options
    std::atomic<bool> g_bGlowEnabled{false};
    std::atomic<bool> g_bGlowTeamEnabled{false};
    float g_fGlowEnemyColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    float g_fGlowTeamColor[4]  = { 0.0f, 0.5f, 1.0f, 1.0f };

    // Radar settings
    std::atomic<bool>  g_bRadarEnabled{false};
    std::atomic<float> g_fRadarSize{200.0f};
    std::atomic<float> g_fRadarRange{2500.0f};
    std::atomic<float> g_fRadarZoom{1.0f};
    std::atomic<float> g_fRadarAlpha{0.85f};
    std::atomic<int>   g_nRadarStyle{0};
    std::atomic<bool>  g_bRadarShowNames{false};
    std::atomic<bool>  g_bRadarShowHealth{true};
    float g_fRadarEnemyColor[4] = { 1.0f, 0.3f, 0.3f, 1.0f };
    float g_fRadarTeamColor[4]  = { 0.3f, 0.5f, 1.0f, 1.0f };

    // Movement settings
    std::atomic<bool> g_bBhopEnabled{false};

    // Inventory Changer
    std::atomic<bool> g_bInventoryChangerEnabled{false};

    // Sky Changer
    std::atomic<bool> g_bSkyChangerEnabled{false};

    // Silent Aim
    std::atomic<bool> g_bSilentAimEnabled{false};

    std::atomic<bool> g_bRunning{true};
    HANDLE g_hMainThread  = nullptr;
    HANDLE g_hBhopThread  = nullptr;  // <-- dedicated bhop thread

    // =========================================================================
    // DEDICATED BHOP THREAD
    // Runs at THREAD_PRIORITY_TIME_CRITICAL with a 1ms Windows timer period.
    // Completely isolated from all other features so their cost can't
    // delay a landing detection.
    // =========================================================================
    DWORD WINAPI BhopThread(LPVOID) {
        // Ask Windows for 1ms timer resolution (reduces Sleep(1) jitter from
        // ~15ms down to ~1ms on most systems). This is process-wide so it also
        // benefits every other Sleep in the cheat.
        timeBeginPeriod(1);

        // Boost this thread's priority so the OS schedules it immediately after
        // Sleep(1) expires, even when other threads are busy.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

        std::cout << XOR_STR("[+] Bhop thread started\n");

        static bool s_bhopWasReady = false;
        while (g_bRunning.load()) {
            bool bhopReady = IsGameReady();
            if (bhopReady) {
                Bunnyhop::Run();
                s_bhopWasReady = true;
            } else if (s_bhopWasReady) {
                // Game just went not-ready (disconnect/map change) - release jump & reset state
                Bunnyhop::Reset();
                s_bhopWasReady = false;
            }
            Sleep(1);
        }

        Bunnyhop::Reset();
        timeEndPeriod(1);
        std::cout << XOR_STR("[+] Bhop thread stopped\n");
        return 0;
    }

    // =========================================================================
    // MAIN CHEAT THREAD  (everything except bhop)
    // =========================================================================
    DWORD WINAPI MainCheatThread(LPVOID lpParam) {
        std::cout << XOR_STR("[+] Cheat logic thread started\n");
        
        while (g_bRunning.load()) {
            // CheckGameStateTransition() resets all features internally on transition.`r`n            // Do NOT duplicate Reset() calls here \u2014 double-reset corrupts mid-tick state.`r`n            CheckGameStateTransition();
            
            if (IsGameReady()) {
                if (g_bFovChangerEnabled.load()) {
                    PlayerFov::Run();
                }
                // NOTE: Bhop::Run() is intentionally NOT here anymore.
                //       It runs in its own dedicated BhopThread above.
                if (g_bNoFlashEnabled.load()) {
                    NoFlash::Run();
                }
                if (g_bNoSmokeEnabled.load()) {
                    NoSmoke::Run();
                }
                if (g_bThirdPersonEnabled.load()) {
                    ThirdPerson::Run();
                }
                if (g_bKillSoundEnabled.load()) {
                    KillSound::Run();
                }
                if (Triggerbot::g_bEnabled.load()) {
                    Triggerbot::Run();
                }
                if (Hooks::g_bAimbotEnabled.load()) {
                    Aimbot::Run();
                }
                // CRASH FIX: SilentAim::Run() was always called unconditionally.
                // Now gated by the enable flag so a crash inside it is also skipped when off.
                if (Hooks::g_bSilentAimEnabled.load()) {
                    SilentAim::Run();
                }
                if (Hooks::g_bSkyChangerEnabled.load()) {
                    SkyChanger::Run();
                }
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
        KeybindManager::Initialize();
        MH_Initialize();
        SilentAim::Init();
        DX11Hook::Initialize();

        g_bRunning.store(true);

        // Spawn dedicated bhop thread FIRST (highest priority)
        g_hBhopThread = CreateThread(nullptr, 0, BhopThread, nullptr, 0, nullptr);

        // Spawn the main cheat logic thread
        g_hMainThread = CreateThread(nullptr, 0, MainCheatThread, nullptr, 0, nullptr);
    }

    void Shutdown() {
        g_bRunning.store(false);

        if (g_hBhopThread) {
            WaitForSingleObject(g_hBhopThread, 2000);
            CloseHandle(g_hBhopThread);
            g_hBhopThread = nullptr;
        }

        if (g_hMainThread) {
            WaitForSingleObject(g_hMainThread, 1000);
            CloseHandle(g_hMainThread);
            g_hMainThread = nullptr;
        }

        DX11Hook::Shutdown();
        ThirdPerson::Shutdown();
        SilentAim::Shutdown();
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
        
        // Transition: ready → not-ready
        // This fires on BOTH warmup-end (brief not-ready blip) AND real disconnect.
        // We must NOT call ThirdPerson::Shutdown() or InventoryUI::ResetRegen() here
        // because during warmup-end the game is mid-reload and those SafeWrite/
        // VirtualProtect calls on stale addresses are the direct crash cause.
        // Instead we just reset the lightweight in-memory state trackers and let
        // the features guard themselves with IsGameReady() checks each tick.
        if (!isReady && s_wasGameReady) {
            std::cout << XOR_STR("[+] Game not-ready detected - soft resetting features\n");
            s_wasGameReady   = false;
            s_lastEntityList = 0;
            s_lastLocalPawn  = 0;
            Aimbot::Reset();
            Triggerbot::Reset();
            Bunnyhop::Reset();
            KillSound::Reset();
            SkyChanger::Reset();
            // NOTE: ThirdPerson::Shutdown() and InventoryUI::ResetRegen() are
            // intentionally NOT called here to avoid writing to stale pointers
            // during the warmup-end server reload. They clean up in Hooks::Shutdown()
            // or via their own IsGameReady() guards on the next tick.
            return true;
        }

        // Transition: not-ready → ready (new map/match joined)
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
