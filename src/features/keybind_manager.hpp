#pragma once

#include <atomic>
#include <string>
#include <windows.h>

namespace KeybindManager {
    // Configurable keys (atomic for thread safety)
    extern std::atomic<int> g_nAimbotKey;
    extern std::atomic<int> g_nTriggerbotKey;
    
    // Key listening state
    extern std::atomic<bool> g_bListeningForAimbotKey;
    extern std::atomic<bool> g_bListeningForTriggerbotKey;
    
    // Default key values
    constexpr int DEFAULT_AIMBOT_KEY = VK_MENU;      // ALT
    constexpr int DEFAULT_TRIGGERBOT_KEY = VK_XBUTTON1; // MOUSE4
    
    // Initialize keybind manager
    void Initialize();
    
    // Reset to defaults
    void ResetToDefaults();
    
    // Start listening for a key (returns true if started listening)
    bool StartListeningForAimbotKey();
    bool StartListeningForTriggerbotKey();
    
    // Stop listening
    void StopListening();
    
    // Process key/mouse input for keybind setting (call from WndProc)
    // Returns true if the input was consumed (should not be processed further)
    bool ProcessInputForKeybind(UINT msg, WPARAM wParam, LPARAM lParam);
    
    // Check if a specific key is currently pressed
    bool IsAimbotKeyPressed();
    bool IsTriggerbotKeyPressed();
    
    // Get display name for a key
    std::string GetKeyName(int keyCode);
    
    // Check if there's a conflict between aimbot and triggerbot keys
    bool HasKeyConflict();
    
    // Get the conflicting key code (returns 0 if no conflict)
    int GetConflictKey();
}
