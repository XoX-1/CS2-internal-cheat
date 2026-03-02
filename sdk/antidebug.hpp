#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <intrin.h>
#include <cstdint>

namespace AntiDebug {

    // Check if a debugger is attached via IsDebuggerPresent
    inline bool CheckDebuggerAPI() {
        return IsDebuggerPresent() != FALSE;
    }

    // Check if remote debugger is attached
    inline bool CheckRemoteDebugger() {
        BOOL debuggerAttached = FALSE;
        CheckRemoteDebuggerPresent(GetCurrentProcess(), &debuggerAttached);
        return debuggerAttached != FALSE;
    }

    // Check debug registers (hardware breakpoints)
    inline bool CheckDebugRegisters() {
        CONTEXT ctx = {};
        ctx.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        
        if (GetThreadContext(GetCurrentThread(), &ctx)) {
            // Dr0-Dr3 contain breakpoint addresses
            // If any are set, a hardware breakpoint is active
            return (ctx.Dr0 != 0 || ctx.Dr1 != 0 || ctx.Dr2 != 0 || ctx.Dr3 != 0);
        }
        
        return false;
    }

    // Check for debugger via heap validation
    inline bool CheckHeapValidation() {
        // GetProcessHeaps and check for debugger artifacts
        HANDLE heaps[100];
        DWORD heapCount = GetProcessHeaps(100, heaps);
        
        for (DWORD i = 0; i < heapCount; i++) {
            ULONG heapInfo = 0;
            // Check heap flags for debugging
            if (HeapQueryInformation(heaps[i], HeapCompatibilityInformation, 
                                     &heapInfo, sizeof(heapInfo), nullptr)) {
                // In a debugged process, heaps often have different flags
            }
        }
        
        return false; // This check is passive
    }

    // Timing check for debugger single-stepping
    inline bool CheckTiming() {
        LARGE_INTEGER freq, start, end;
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        
        // Small operation that should be fast
        volatile int x = 0;
        for (int i = 0; i < 100; i++) {
            x += i;
        }
        
        QueryPerformanceCounter(&end);
        
        // Calculate elapsed time in microseconds
        double elapsed = (end.QuadPart - start.QuadPart) * 1000000.0 / freq.QuadPart;
        
        // If took too long, might be single-stepping
        return elapsed > 1000.0; // More than 1ms for simple loop
    }

    // Check for common debugger windows
    inline bool CheckDebuggerWindows() {
        const wchar_t* debuggerWindows[] = {
            L"OllyDbg",
            L"IDA",
            L"x64dbg",
            L"x32dbg",
            L"WinDbg",
            L"Immunity",
            L"Ghidra",
            L"Cheat Engine",
            L"cheatengine",
        };
        
        for (const auto* windowName : debuggerWindows) {
            if (FindWindowW(nullptr, windowName) != nullptr) {
                return true;
            }
        }
        
        return false;
    }

    // Check if running in VM using CPUID
    inline bool CheckVirtualMachine() {
        int cpuInfo[4] = {};
        
        // Check hypervisor presence
        __cpuid(cpuInfo, 1);
        bool hypervisorBit = (cpuInfo[2] >> 31) & 1;
        
        if (hypervisorBit) {
            return true;
        }
        
        // Check for known VM signatures
        char vendor[13] = {};
        __cpuid(cpuInfo, 0x40000000);
        memcpy(vendor, &cpuInfo[1], 4);
        memcpy(vendor + 4, &cpuInfo[2], 4);
        memcpy(vendor + 8, &cpuInfo[3], 4);
        
        const char* vmVendors[] = {
            "VMwareVMware",
            "Microsoft Hv",
            "KVMKVMKVM",
            "XenVMMXenVMM",
            "prl hyperv",
            "VBoxVBoxVBox",
        };
        
        for (const auto* vmVendor : vmVendors) {
            if (memcmp(vendor, vmVendor, 12) == 0) {
                return true;
            }
        }
        
        return false;
    }

    // Check for analysis tools
    inline bool CheckAnalysisTools() {
        const wchar_t* toolProcesses[] = {
            L"ProcessHacker.exe",
            L"procmon.exe",
            L"procmon64.exe",
            L"wireshark.exe",
            L"fiddler.exe",
            L"httpdebugger.exe",
            L"pe-bear.exe",
            L"DetectItEasy.exe",
            L"pe-sieve.exe",
        };
        
        // Create toolhelp snapshot
        HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (hSnapshot == INVALID_HANDLE_VALUE) {
            return false;
        }
        
        PROCESSENTRY32W pe;
        pe.dwSize = sizeof(pe);
        
        if (Process32FirstW(hSnapshot, &pe)) {
            do {
                for (const auto* toolName : toolProcesses) {
                    if (_wcsicmp(pe.szExeFile, toolName) == 0) {
                        CloseHandle(hSnapshot);
                        return true;
                    }
                }
            } while (Process32NextW(hSnapshot, &pe));
        }
        
        CloseHandle(hSnapshot);
        return false;
    }

    // Combined check - returns true if any debug/analysis detected
    enum CheckFlags {
        CHECK_DEBUGGER_API = 1 << 0,
        CHECK_REMOTE_DEBUGGER = 1 << 1,
        CHECK_DEBUG_REGISTERS = 1 << 2,
        CHECK_TIMING = 1 << 3,
        CHECK_DEBUGGER_WINDOWS = 1 << 4,
        CHECK_VIRTUAL_MACHINE = 1 << 5,
        CHECK_ANALYSIS_TOOLS = 1 << 6,
        CHECK_ALL = 0xFFFFFFFF
    };

    inline bool IsBeingDebugged(uint32_t flags = CHECK_ALL) {
        if ((flags & CHECK_DEBUGGER_API) && CheckDebuggerAPI()) return true;
        if ((flags & CHECK_REMOTE_DEBUGGER) && CheckRemoteDebugger()) return true;
        if ((flags & CHECK_DEBUG_REGISTERS) && CheckDebugRegisters()) return true;
        if ((flags & CHECK_TIMING) && CheckTiming()) return true;
        if ((flags & CHECK_DEBUGGER_WINDOWS) && CheckDebuggerWindows()) return true;
        if ((flags & CHECK_VIRTUAL_MACHINE) && CheckVirtualMachine()) return true;
        if ((flags & CHECK_ANALYSIS_TOOLS) && CheckAnalysisTools()) return true;
        
        return false;
    }

    // Action to take if debugger detected (configurable)
    enum class Action {
        NONE,           // Do nothing
        EXIT,           // Exit process
        CRASH,          // Cause crash
        INFINITE_LOOP,  // Hang
        FAKE_CRASH,     // Fake error message
    };

    inline void OnDebuggerDetected(Action action = Action::NONE) {
        switch (action) {
        case Action::EXIT:
            ExitProcess(0);
            break;
        case Action::CRASH:
            *reinterpret_cast<volatile int*>(0) = 0; // Access violation
            break;
        case Action::INFINITE_LOOP:
            while (true) { Sleep(1000); }
            break;
        case Action::FAKE_CRASH:
            MessageBoxA(nullptr, "Runtime Error: Division by zero", "Error", MB_ICONERROR);
            ExitProcess(1);
            break;
        case Action::NONE:
        default:
            break;
        }
    }

} // namespace AntiDebug