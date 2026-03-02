#include <windows.h>
#include <iostream>
#include <thread>
#include "hooks/hooks.hpp"
#include "../sdk/obfuscation.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/memory.hpp"

HMODULE g_hModule = nullptr;

DWORD WINAPI MainThread(LPVOID lpParam) {
    // Note: Console is removed for production/stealth
    // Use OutputDebugString or file logging if needed

    std::cout << XOR_STR("[+] mindcheat loaded!\n");
    std::cout << XOR_STR("[+] Waiting for CS2 game modules...\n");

    while (!Memory::GetModuleBase("client.dll") || !Memory::GetModuleBase("engine2.dll")) {
        Sleep(Constants::Timing::MODULE_WAIT_MS);
    }

    std::cout << XOR_STR("[+] client.dll: 0x") << Memory::GetModuleBase("client.dll") << "\n";
    std::cout << XOR_STR("[+] engine2.dll: 0x") << Memory::GetModuleBase("engine2.dll") << "\n";

    Sleep(Constants::Timing::INIT_DELAY_MS); // Wait a bit for stability

    std::cout << XOR_STR("[+] Initializing hooks...\n");
    Hooks::Initialize();

    std::cout << XOR_STR("[+] mindcheat running! Press INSERT for Menu, END to Unload.\n");

    // Wait for unload signal
    while (true) {
        if (GetAsyncKeyState(Constants::Keys::UNLOAD) & 1) {
            break;
        }
        Sleep(Constants::Timing::MODULE_WAIT_MS);
    }

    std::cout << XOR_STR("[+] Unloading cheat...\n");

    Hooks::Shutdown();
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    if (ul_reason_for_call == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        g_hModule = hModule;
        HANDLE hThread = CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
        if (hThread) {
            CloseHandle(hThread);
        }
    }
    return TRUE;
}
