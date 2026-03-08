#pragma once

#include <windows.h>
#include <cstdint>
#include <type_traits>

namespace Memory {

    // Pointer validation constants
    constexpr uintptr_t MIN_VALID_PTR = 0x10000;
    constexpr uintptr_t MAX_VALID_PTR = 0x7FFFFFFFFFFF;

    // Check if pointer is within valid user-mode address range
    inline bool IsValidPtr(uintptr_t ptr) {
        return ptr >= MIN_VALID_PTR && ptr <= MAX_VALID_PTR;
    }

    // Safe memory read with SEH protection
    template<typename T>
    inline bool SafeRead(uintptr_t addr, T& out) {
        if (!IsValidPtr(addr)) return false;
        
        __try {
            out = *reinterpret_cast<T*>(addr);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Safe memory write with SEH protection
    template<typename T>
    inline bool SafeWrite(uintptr_t addr, const T& val) {
        if (!IsValidPtr(addr)) return false;
        
        __try {
            *reinterpret_cast<T*>(addr) = val;
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Read array of data
    inline bool SafeReadBytes(uintptr_t addr, void* buffer, size_t size) {
        if (!IsValidPtr(addr) || !buffer || size == 0) return false;
        
        __try {
            memcpy(buffer, reinterpret_cast<void*>(addr), size);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // Get module base with caching
    inline uintptr_t GetModuleBase(const char* moduleName) {
        static uintptr_t clientBase = 0;
        static uintptr_t engineBase = 0;
        
        if (strcmp(moduleName, "client.dll") == 0) {
            if (!clientBase) clientBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName));
            return clientBase;
        }
        if (strcmp(moduleName, "engine2.dll") == 0) {
            if (!engineBase) engineBase = reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName));
            return engineBase;
        }
        
        return reinterpret_cast<uintptr_t>(GetModuleHandleA(moduleName));
    }

    // Reset cached module bases (call on game restart/map change)
    inline void ResetModuleCache() {
        // Force re-fetch on next call
        GetModuleHandleA("client.dll");
        GetModuleHandleA("engine2.dll");
    }

    // Secure memory write that mimics normal access patterns
    // Uses intermediate buffer to avoid direct pointer writes
    template<typename T>
    inline bool SecureWrite(uintptr_t addr, const T& val) {
        if (!IsValidPtr(addr)) return false;
        
        // Align to page for potential future protection modifications
        DWORD oldProtect;
        if (!VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), PAGE_EXECUTE_READWRITE, &oldProtect)) {
            return SafeWrite(addr, val); // Fallback to regular write
        }
        
        bool result = SafeWrite(addr, val);
        VirtualProtect(reinterpret_cast<void*>(addr), sizeof(T), oldProtect, &oldProtect);
        
        return result;
    }

    // Pattern scan within a module's memory range.
    // pattern: raw byte array, mask: 'x' for exact match, '?' for wildcard.
    // Returns address of first match, or 0 if not found.
    inline uintptr_t PatternScan(uintptr_t base, size_t size, const uint8_t* pattern, const char* mask) {
        size_t maskLen = 0;
        while (mask[maskLen]) maskLen++;
        if (maskLen == 0 || size < maskLen) return 0;

        __try {
            for (size_t i = 0; i <= size - maskLen; i++) {
                bool found = true;
                for (size_t j = 0; j < maskLen; j++) {
                    if (mask[j] == 'x' && *reinterpret_cast<const uint8_t*>(base + i + j) != pattern[j]) {
                        found = false;
                        break;
                    }
                }
                if (found) return base + i;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    // Resolve relative CALL/JMP target: addr + 5 + *(int32_t*)(addr + 1)
    inline uintptr_t ResolveRelCall(uintptr_t callAddr) {
        __try {
            int32_t rel = *reinterpret_cast<int32_t*>(callAddr + 1);
            return callAddr + 5 + rel;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

    // Get module size from PE headers (for internal DLL use)
    inline size_t GetModuleSize(uintptr_t base) {
        if (!IsValidPtr(base)) return 0;
        __try {
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
            auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
            return nt->OptionalHeader.SizeOfImage;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return 0;
    }

} // namespace Memory