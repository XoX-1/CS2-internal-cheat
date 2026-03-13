// mindcheat injector — Manual Map with in-process loader
// Best-practice approach: the loader function runs INSIDE the target process.
// This correctly handles forwarded exports, TLS callbacks, exception tables,
// API sets, and CRT initialization.

#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

// ============================================================================
// Console colors
// ============================================================================
static HANDLE g_hConsole = nullptr;

static void PrintColor(WORD color, const char* fmt, ...) {
    SetConsoleTextAttribute(g_hConsole, color);
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    SetConsoleTextAttribute(g_hConsole, 7);
}

#define LOG_INFO(...)  PrintColor(11, __VA_ARGS__)
#define LOG_OK(...)    PrintColor(10, __VA_ARGS__)
#define LOG_WARN(...)  PrintColor(14, __VA_ARGS__)
#define LOG_ERR(...)   PrintColor(12, __VA_ARGS__)

// ============================================================================
// Process helpers
// ============================================================================
static DWORD FindProcessId(const wchar_t* processName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, processName) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// ============================================================================
// NT function types for stealth injection
// ============================================================================
using NtCreateThreadEx_t = LONG(NTAPI*)(
    PHANDLE hThread, ACCESS_MASK desiredAccess, PVOID objectAttributes,
    HANDLE processHandle, PVOID startRoutine, PVOID argument,
    ULONG createFlags, SIZE_T zeroBits, SIZE_T stackSize,
    SIZE_T maximumStackSize, PVOID attributeList);

// ============================================================================
// Loader data struct — passed to the loader function inside the target process
// ============================================================================
struct LoaderData {
    BYTE*  pImageBase;         // Base address of mapped DLL image in target
    // Function pointers (resolved from kernel32 — same addr in all processes)
    HMODULE (WINAPI* fnLoadLibraryA)(LPCSTR);
    FARPROC (WINAPI* fnGetProcAddress)(HMODULE, LPCSTR);
    BOOL    (WINAPI* fnRtlAddFunctionTable)(PRUNTIME_FUNCTION, DWORD, DWORD64);
    BOOL    success;           // Set to TRUE by loader on success
};

// ============================================================================
// Loader function — this gets compiled and COPIED into the target process.
// It runs entirely inside cs2.exe. It must NOT use any CRT functions, globals,
// string literals, or anything that would create relocations outside itself.
// Only uses function pointers passed via LoaderData.
//
// Disable all runtime checks and optimizations that would break PIC.
// ============================================================================
#pragma runtime_checks("", off)
#pragma optimize("", off)

static DWORD WINAPI LoaderFunction(LoaderData* pData) {
    if (!pData || !pData->pImageBase)
        return 1;

    BYTE* pBase = pData->pImageBase;

    // ---- Parse PE headers ----
    auto* dosHeader = reinterpret_cast<IMAGE_DOS_HEADER*>(pBase);
    if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
        return 1;

    auto* ntHeaders = reinterpret_cast<IMAGE_NT_HEADERS64*>(pBase + dosHeader->e_lfanew);
    if (ntHeaders->Signature != IMAGE_NT_SIGNATURE)
        return 1;

    auto* optHeader = &ntHeaders->OptionalHeader;

    // ---- Process relocations ----
    const UINT_PTR delta = reinterpret_cast<UINT_PTR>(pBase) - optHeader->ImageBase;
    if (delta != 0) {
        auto& relocDir = optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
        if (relocDir.Size > 0) {
            auto* relocBlock = reinterpret_cast<IMAGE_BASE_RELOCATION*>(pBase + relocDir.VirtualAddress);
            while (relocBlock->VirtualAddress) {
                DWORD count = (relocBlock->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);
                auto* entries = reinterpret_cast<WORD*>(reinterpret_cast<BYTE*>(relocBlock) + sizeof(IMAGE_BASE_RELOCATION));
                for (DWORD i = 0; i < count; i++) {
                    WORD type = entries[i] >> 12;
                    WORD offset = entries[i] & 0xFFF;
                    if (type == IMAGE_REL_BASED_DIR64) {
                        auto* patch = reinterpret_cast<UINT_PTR*>(pBase + relocBlock->VirtualAddress + offset);
                        *patch += delta;
                    } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                        auto* patch = reinterpret_cast<DWORD*>(pBase + relocBlock->VirtualAddress + offset);
                        *patch += static_cast<DWORD>(delta);
                    }
                }
                relocBlock = reinterpret_cast<IMAGE_BASE_RELOCATION*>(
                    reinterpret_cast<BYTE*>(relocBlock) + relocBlock->SizeOfBlock);
            }
        }
    }

    // ---- Resolve imports (runs INSIDE the target process) ----
    // LoadLibraryA / GetProcAddress called here correctly handle:
    // - Forwarded exports (kernel32!HeapAlloc → ntdll!RtlAllocateHeap)
    // - API sets (api-ms-win-crt-* → ucrtbase.dll)
    // - Module loader database registration
    {
        auto& importDir = optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
        if (importDir.Size > 0) {
            auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(pBase + importDir.VirtualAddress);
            while (desc->Name) {
                const char* modName = reinterpret_cast<const char*>(pBase + desc->Name);
                HMODULE hMod = pData->fnLoadLibraryA(modName);

                if (hMod) {
                    auto* origThunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(
                        pBase + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
                    auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA64*>(pBase + desc->FirstThunk);

                    while (origThunk->u1.AddressOfData) {
                        if (IMAGE_SNAP_BY_ORDINAL64(origThunk->u1.Ordinal)) {
                            thunk->u1.Function = reinterpret_cast<ULONGLONG>(
                                pData->fnGetProcAddress(hMod,
                                    reinterpret_cast<LPCSTR>(IMAGE_ORDINAL64(origThunk->u1.Ordinal))));
                        } else {
                            auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(pBase + origThunk->u1.AddressOfData);
                            thunk->u1.Function = reinterpret_cast<ULONGLONG>(
                                pData->fnGetProcAddress(hMod, ibn->Name));
                        }
                        origThunk++;
                        thunk++;
                    }
                }
                desc++;
            }
        }
    }

    // ---- Process TLS callbacks ----
    {
        auto& tlsDir = optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_TLS];
        if (tlsDir.Size > 0) {
            auto* tls = reinterpret_cast<IMAGE_TLS_DIRECTORY64*>(pBase + tlsDir.VirtualAddress);
            auto* callbacks = reinterpret_cast<PIMAGE_TLS_CALLBACK*>(tls->AddressOfCallBacks);
            if (callbacks) {
                while (*callbacks) {
                    (*callbacks)(reinterpret_cast<PVOID>(pBase), DLL_PROCESS_ATTACH, nullptr);
                    callbacks++;
                }
            }
        }
    }

    // ---- Register exception handlers (x64 table-based SEH) ----
    {
        auto& exceptDir = optHeader->DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        if (exceptDir.Size > 0 && pData->fnRtlAddFunctionTable) {
            pData->fnRtlAddFunctionTable(
                reinterpret_cast<PRUNTIME_FUNCTION>(pBase + exceptDir.VirtualAddress),
                exceptDir.Size / sizeof(RUNTIME_FUNCTION),
                reinterpret_cast<DWORD64>(pBase));
        }
    }

    // ---- Call DllMain ----
    using DllMain_t = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
    auto DllMain = reinterpret_cast<DllMain_t>(pBase + optHeader->AddressOfEntryPoint);
    DllMain(reinterpret_cast<HINSTANCE>(pBase), DLL_PROCESS_ATTACH, nullptr);

    pData->success = TRUE;
    return 0;
}

// Marker function — used to calculate LoaderFunction's size
static DWORD WINAPI LoaderFunctionEnd() { return 0; }

#pragma optimize("", on)
#pragma runtime_checks("", restore)

// ============================================================================
// Manual Map Injector
// ============================================================================
class ManualMapper {
public:
    ManualMapper() = default;
    ~ManualMapper() { if (m_hProcess) CloseHandle(m_hProcess); }

    bool Inject(DWORD pid, const std::vector<uint8_t>& dllData) {
        m_hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!m_hProcess) {
            LOG_ERR("[!] Failed to open process (error %u). Run as Administrator.\n", GetLastError());
            return false;
        }

        // ---- Parse PE ----
        auto* dosHeader = reinterpret_cast<const IMAGE_DOS_HEADER*>(dllData.data());
        if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE) {
            LOG_ERR("[!] Invalid DOS signature.\n");
            return false;
        }

        auto* ntHeaders = reinterpret_cast<const IMAGE_NT_HEADERS64*>(dllData.data() + dosHeader->e_lfanew);
        if (ntHeaders->Signature != IMAGE_NT_SIGNATURE) {
            LOG_ERR("[!] Invalid NT signature.\n");
            return false;
        }

        if (ntHeaders->FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64) {
            LOG_ERR("[!] DLL is not x64.\n");
            return false;
        }

        const SIZE_T imageSize = ntHeaders->OptionalHeader.SizeOfImage;
        LOG_INFO("[*] Image size: 0x%llX\n", (uint64_t)imageSize);

        // ---- Allocate memory in target for the DLL image ----
        m_remoteBase = reinterpret_cast<uint8_t*>(
            VirtualAllocEx(m_hProcess, nullptr, imageSize, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
        if (!m_remoteBase) {
            LOG_ERR("[!] VirtualAllocEx failed (error %u).\n", GetLastError());
            return false;
        }
        LOG_OK("[+] Allocated image at 0x%p\n", m_remoteBase);

        // ---- Build local image (headers + sections only, NO relocations/imports) ----
        std::vector<uint8_t> localImage(imageSize, 0);

        // Copy headers
        memcpy(localImage.data(), dllData.data(), ntHeaders->OptionalHeader.SizeOfHeaders);

        // Copy sections
        auto* sectionHeader = IMAGE_FIRST_SECTION(ntHeaders);
        for (WORD i = 0; i < ntHeaders->FileHeader.NumberOfSections; i++) {
            if (sectionHeader[i].SizeOfRawData == 0) continue;
            memcpy(
                localImage.data() + sectionHeader[i].VirtualAddress,
                dllData.data() + sectionHeader[i].PointerToRawData,
                sectionHeader[i].SizeOfRawData);
        }

        // ---- Write raw mapped image to target ----
        if (!WriteProcessMemory(m_hProcess, m_remoteBase, localImage.data(), imageSize, nullptr)) {
            LOG_ERR("[!] WriteProcessMemory failed (error %u).\n", GetLastError());
            Cleanup();
            return false;
        }
        LOG_OK("[+] Image sections written to target.\n");

        // ---- Prepare LoaderData ----
        LoaderData data{};
        data.pImageBase = m_remoteBase;
        data.fnLoadLibraryA = LoadLibraryA;          // Same address in all processes (kernel32)
        data.fnGetProcAddress = GetProcAddress;      // Same address in all processes (kernel32)
        data.fnRtlAddFunctionTable = reinterpret_cast<decltype(data.fnRtlAddFunctionTable)>(
            ::GetProcAddress(GetModuleHandleA("kernel32.dll"), "RtlAddFunctionTable"));
        if (!data.fnRtlAddFunctionTable) {
            data.fnRtlAddFunctionTable = reinterpret_cast<decltype(data.fnRtlAddFunctionTable)>(
                ::GetProcAddress(GetModuleHandleA("ntdll.dll"), "RtlAddFunctionTable"));
        }
        data.success = FALSE;

        // Allocate LoaderData in target
        void* pRemoteData = VirtualAllocEx(m_hProcess, nullptr, sizeof(LoaderData),
            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pRemoteData) {
            LOG_ERR("[!] Failed to allocate loader data.\n");
            Cleanup();
            return false;
        }
        WriteProcessMemory(m_hProcess, pRemoteData, &data, sizeof(data), nullptr);

        // ---- Write loader function to target ----
        const SIZE_T loaderSize = reinterpret_cast<BYTE*>(LoaderFunctionEnd) - reinterpret_cast<BYTE*>(LoaderFunction);
        LOG_INFO("[*] Loader function size: %zu bytes\n", loaderSize);

        void* pRemoteLoader = VirtualAllocEx(m_hProcess, nullptr, loaderSize,
            MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!pRemoteLoader) {
            LOG_ERR("[!] Failed to allocate loader code.\n");
            VirtualFreeEx(m_hProcess, pRemoteData, 0, MEM_RELEASE);
            Cleanup();
            return false;
        }

        if (!WriteProcessMemory(m_hProcess, pRemoteLoader, reinterpret_cast<void*>(LoaderFunction), loaderSize, nullptr)) {
            LOG_ERR("[!] Failed to write loader function.\n");
            VirtualFreeEx(m_hProcess, pRemoteLoader, 0, MEM_RELEASE);
            VirtualFreeEx(m_hProcess, pRemoteData, 0, MEM_RELEASE);
            Cleanup();
            return false;
        }
        LOG_OK("[+] Loader function written to target.\n");

        // ---- Execute the loader via NtCreateThreadEx (stealth) ----
        HANDLE hThread = nullptr;
        auto NtCreateThreadEx = reinterpret_cast<NtCreateThreadEx_t>(
            ::GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx"));

        if (NtCreateThreadEx) {
            LONG status = NtCreateThreadEx(
                &hThread, THREAD_ALL_ACCESS, nullptr, m_hProcess,
                pRemoteLoader, pRemoteData,
                0x40,  // THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER
                0, 0, 0, nullptr);

            if (status < 0 || !hThread) {
                LOG_WARN("[!] NtCreateThreadEx failed (0x%08X), falling back.\n", status);
                hThread = nullptr;
            }
        }

        if (!hThread) {
            hThread = CreateRemoteThread(m_hProcess, nullptr, 0,
                reinterpret_cast<LPTHREAD_START_ROUTINE>(pRemoteLoader), pRemoteData, 0, nullptr);
        }

        if (!hThread) {
            LOG_ERR("[!] Failed to create remote thread (error %u).\n", GetLastError());
            VirtualFreeEx(m_hProcess, pRemoteLoader, 0, MEM_RELEASE);
            VirtualFreeEx(m_hProcess, pRemoteData, 0, MEM_RELEASE);
            Cleanup();
            return false;
        }

        // Wait for loader to finish
        WaitForSingleObject(hThread, 15000);

        // Check exit code
        DWORD exitCode = 0;
        GetExitCodeThread(hThread, &exitCode);
        CloseHandle(hThread);

        // Read back LoaderData to check success flag
        LoaderData result{};
        ReadProcessMemory(m_hProcess, pRemoteData, &result, sizeof(result), nullptr);

        // Clean up loader code + data (image stays mapped)
        VirtualFreeEx(m_hProcess, pRemoteLoader, 0, MEM_RELEASE);
        VirtualFreeEx(m_hProcess, pRemoteData, 0, MEM_RELEASE);

        if (!result.success || exitCode != 0) {
            LOG_ERR("[!] Loader returned failure (exit=%u, success=%d).\n", exitCode, result.success);
            Cleanup();
            return false;
        }

        LOG_OK("[+] DllMain executed successfully.\n");

        // ---- Stealth: Erase PE header ----
        ErasePEHeader(ntHeaders);
        LOG_OK("[+] PE header erased.\n");

        return true;
    }

private:
    HANDLE m_hProcess = nullptr;
    uint8_t* m_remoteBase = nullptr;

    void ErasePEHeader(const IMAGE_NT_HEADERS64* ntHeaders) {
        const SIZE_T headerSize = ntHeaders->OptionalHeader.SizeOfHeaders;
        std::vector<uint8_t> zeros(headerSize, 0);

        DWORD oldProtect = 0;
        VirtualProtectEx(m_hProcess, m_remoteBase, headerSize, PAGE_READWRITE, &oldProtect);
        WriteProcessMemory(m_hProcess, m_remoteBase, zeros.data(), headerSize, nullptr);
        VirtualProtectEx(m_hProcess, m_remoteBase, headerSize, PAGE_READONLY, &oldProtect);
    }

    void Cleanup() {
        if (m_remoteBase) {
            VirtualFreeEx(m_hProcess, m_remoteBase, 0, MEM_RELEASE);
            m_remoteBase = nullptr;
        }
    }
};

// ============================================================================
// Find the DLL path next to the exe
// ============================================================================
static std::wstring GetDllPath() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    std::filesystem::path p(exePath);
    auto dllPath = p.parent_path() / L"mindcheat.dll";
    if (std::filesystem::exists(dllPath)) return dllPath.wstring();

    dllPath = p.parent_path() / L"Release" / L"mindcheat.dll";
    if (std::filesystem::exists(dllPath)) return dllPath.wstring();

    return L"";
}

// ============================================================================
// Read file into buffer
// ============================================================================
static std::vector<uint8_t> ReadFileToBuffer(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);

    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize.QuadPart));
    DWORD bytesRead = 0;
    ReadFile(hFile, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
    CloseHandle(hFile);

    if (bytesRead != buffer.size()) return {};
    return buffer;
}

// ============================================================================
// Entry point
// ============================================================================
int main() {
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    SetConsoleTitleA("mindcheat injector");

    PrintColor(13, R"(
  __  __ _           _  ____ _                _
 |  \/  (_)_ __   __| |/ ___| |__   ___  __ _| |_
 | |\/| | | '_ \ / _` | |   | '_ \ / _ \/ _` | __|
 | |  | | | | | | (_| | |___| | | |  __/ (_| | |_
 |_|  |_|_|_| |_|\__,_|\____|_| |_|\___|\__,_|\__|
)");
    printf("\n");

    // ---- Find DLL ----
    std::wstring dllPath = GetDllPath();
    if (dllPath.empty()) {
        LOG_ERR("[!] mindcheat.dll not found next to injector exe.\n");
        LOG_ERR("    Place mindcheat.dll in the same folder as this exe.\n");
        printf("\nPress any key to exit...\n");
        getchar();
        return 1;
    }
    LOG_OK("[+] DLL found: %ls\n", dllPath.c_str());

    // ---- Read DLL ----
    auto dllData = ReadFileToBuffer(dllPath);
    if (dllData.empty()) {
        LOG_ERR("[!] Failed to read DLL file.\n");
        printf("\nPress any key to exit...\n");
        getchar();
        return 1;
    }
    LOG_OK("[+] DLL loaded (%zu bytes)\n", dllData.size());

    // ---- Wait for CS2 and F1 ----
    LOG_INFO("\n[*] Waiting for cs2.exe...\n");
    DWORD pid = 0;
    while (!(pid = FindProcessId(L"cs2.exe"))) {
        Sleep(500);
    }
    LOG_OK("[+] cs2.exe found (PID: %u)\n", pid);

    LOG_WARN("\n>>> Press F1 to inject <<<\n\n");

    while (!(GetAsyncKeyState(VK_F1) & 0x8000)) {
        Sleep(50);
    }
    Sleep(200);

    // ---- Inject ----
    LOG_INFO("[*] Injecting via Manual Map...\n");

    ManualMapper mapper;
    if (mapper.Inject(pid, dllData)) {
        Beep(750, 300);
        LOG_OK("\n=== Injection successful! ===\n");
        LOG_INFO("[*] You can close this window.\n");
    } else {
        LOG_ERR("\n=== Injection failed! ===\n");
        LOG_INFO("[*] Make sure you're running as Administrator.\n");
    }

    Sleep(3000);
    return 0;
}
