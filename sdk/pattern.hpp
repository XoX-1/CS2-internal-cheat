#pragma once

#include <windows.h>
#include <cstdint>
#include <vector>
#include <string>

namespace Pattern {

    // Convert pattern string with wildcards to bytes and mask
    // Format: "48 8B 0D ? ? ? ? 48 89 7C 24"
    inline std::vector<std::pair<uint8_t, bool>> ParsePattern(const char* pattern) {
        std::vector<std::pair<uint8_t, bool>> result;
        
        while (*pattern) {
            // Skip whitespace
            if (*pattern == ' ') {
                ++pattern;
                continue;
            }
            
            // Wildcard
            if (*pattern == '?') {
                result.push_back({ 0, false }); // value, valid=false
                ++pattern;
                if (*pattern == '?') ++pattern; // Handle ??
                continue;
            }
            
            // Parse hex byte
            char byteStr[3] = { pattern[0], pattern[1], '\0' };
            uint8_t byte = static_cast<uint8_t>(strtol(byteStr, nullptr, 16));
            result.push_back({ byte, true });
            pattern += 2;
        }
        
        return result;
    }

    // Scan memory region for pattern
    inline uintptr_t ScanRegion(uintptr_t base, size_t size, const std::vector<std::pair<uint8_t, bool>>& pattern) {
        if (!base || !size || pattern.empty()) return 0;
        
        const uint8_t* data = reinterpret_cast<const uint8_t*>(base);
        const size_t patternLen = pattern.size();
        
        for (size_t i = 0; i <= size - patternLen; ++i) {
            bool found = true;
            
            for (size_t j = 0; j < patternLen; ++j) {
                if (pattern[j].second && data[i + j] != pattern[j].first) {
                    found = false;
                    break;
                }
            }
            
            if (found) {
                return base + i;
            }
        }
        
        return 0;
    }

    // Get module info
    inline bool GetModuleInfo(const char* moduleName, uintptr_t& base, size_t& size) {
        HMODULE hMod = GetModuleHandleA(moduleName);
        if (!hMod) return false;
        
        base = reinterpret_cast<uintptr_t>(hMod);
        
        // Get module size from PE headers
        PIMAGE_DOS_HEADER dosHeader = reinterpret_cast<PIMAGE_DOS_HEADER>(base);
        PIMAGE_NT_HEADERS ntHeaders = reinterpret_cast<PIMAGE_NT_HEADERS>(base + dosHeader->e_lfanew);
        size = ntHeaders->OptionalHeader.SizeOfImage;
        
        return true;
    }

    // Main pattern scan function
    // Returns address where pattern is found, or 0 if not found
    inline uintptr_t Scan(const char* moduleName, const char* patternStr) {
        uintptr_t base;
        size_t size;
        
        if (!GetModuleInfo(moduleName, base, size)) {
            return 0;
        }
        
        auto pattern = ParsePattern(patternStr);
        if (pattern.empty()) return 0;
        
        return ScanRegion(base, size, pattern);
    }

    // Scan with relative offset resolution (for "instruction + offset" patterns)
    // Pattern format: "48 8B 0D ? ? ? ?" (mov rcx, [rip+offset])
    // Returns the resolved address (where the pointer points to)
    inline uintptr_t ScanRelative(const char* moduleName, const char* patternStr, int offsetIndex = 3, int instructionSize = 7) {
        uintptr_t instructionAddr = Scan(moduleName, patternStr);
        if (!instructionAddr) return 0;
        
        // Read the relative offset from the instruction
        int32_t relativeOffset;
        memcpy(&relativeOffset, reinterpret_cast<void*>(instructionAddr + offsetIndex), sizeof(int32_t));
        
        // Calculate absolute address: instruction_addr + instruction_size + relative_offset
        return instructionAddr + instructionSize + relativeOffset;
    }

    // Common CS2 patterns for dynamic offset finding
    namespace CS2Patterns {
        // Entity list pattern
        constexpr const char* ENTITY_LIST = "48 8B 0D ? ? ? ? 48 89 7C 24 ? 8B FA";
        
        // Local player pattern
        constexpr const char* LOCAL_PLAYER = "48 8B 05 ? ? ? ? 48 85 C0 74 ? 8B 88";
        
        // View matrix pattern
        constexpr const char* VIEW_MATRIX = "48 8D ? ? ? ? ? 48 C1 E0 06 48 03 ? 48 8B";
        
        // View angles pattern
        constexpr const char* VIEW_ANGLES = "48 8B 05 ? ? ? ? 48 8D ? ? ? ? ? 48 85 C0";
        
        // Global vars pattern
        constexpr const char* GLOBAL_VARS = "48 89 0D ? ? ? ? 48 89 15 ? ? ? ? 48 8B";
    }

} // namespace Pattern