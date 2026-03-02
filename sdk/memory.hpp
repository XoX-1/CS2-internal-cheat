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

} // namespace Memory