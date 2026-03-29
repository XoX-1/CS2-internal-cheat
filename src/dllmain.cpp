#include <windows.h>
#include <thread>
#include "hooks/hooks.hpp"
#include "../sdk/obfuscation.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/memory.hpp"

HMODULE g_hModule = nullptr;

DWORD WINAPI MainThread(LPVOID lpParam) {
    while (!Memory::GetModuleBase("client.dll") || !Memory::GetModuleBase("engine2.dll")) {
        Sleep(Constants::Timing::MODULE_WAIT_MS);
    }

    Sleep(Constants::Timing::INIT_DELAY_MS);

    Hooks::Initialize();

    // Wait for unload signal
    while (true) {
        if (GetAsyncKeyState(Constants::Keys::UNLOAD) & 1) {
            break;
        }
        Sleep(Constants::Timing::MODULE_WAIT_MS);
    }

    Hooks::Shutdown();
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        g_hModule = hModule;
        HANDLE hThread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    return TRUE;
}
