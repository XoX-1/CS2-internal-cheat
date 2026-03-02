#include "keybind_manager.hpp"
#include <unordered_map>

namespace KeybindManager {
    // Atomic key storage
    std::atomic<int> g_nAimbotKey{DEFAULT_AIMBOT_KEY};
    std::atomic<int> g_nTriggerbotKey{DEFAULT_TRIGGERBOT_KEY};
    
    // Listening state
    std::atomic<bool> g_bListeningForAimbotKey{false};
    std::atomic<bool> g_bListeningForTriggerbotKey{false};
    
    // Mouse button mapping
    static int GetMouseKeyFromMessage(UINT msg, WPARAM wParam) {
        switch (msg) {
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
                return VK_LBUTTON;
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
                return VK_RBUTTON;
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
                return VK_MBUTTON;
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
                // XBUTTON1 = 1, XBUTTON2 = 2
                // VK_XBUTTON1 = 0x05, VK_XBUTTON2 = 0x06
                if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1)
                    return VK_XBUTTON1;
                else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2)
                    return VK_XBUTTON2;
                break;
        }
        return 0;
    }
    
    void Initialize() {
        // Set defaults
        g_nAimbotKey.store(DEFAULT_AIMBOT_KEY);
        g_nTriggerbotKey.store(DEFAULT_TRIGGERBOT_KEY);
        g_bListeningForAimbotKey.store(false);
        g_bListeningForTriggerbotKey.store(false);
    }
    
    void ResetToDefaults() {
        g_nAimbotKey.store(DEFAULT_AIMBOT_KEY);
        g_nTriggerbotKey.store(DEFAULT_TRIGGERBOT_KEY);
    }
    
    bool StartListeningForAimbotKey() {
        // Stop any other listening first
        g_bListeningForTriggerbotKey.store(false);
        g_bListeningForAimbotKey.store(true);
        return true;
    }
    
    bool StartListeningForTriggerbotKey() {
        // Stop any other listening first
        g_bListeningForAimbotKey.store(false);
        g_bListeningForTriggerbotKey.store(true);
        return true;
    }
    
    void StopListening() {
        g_bListeningForAimbotKey.store(false);
        g_bListeningForTriggerbotKey.store(false);
    }
    
    bool ProcessInputForKeybind(UINT msg, WPARAM wParam, LPARAM lParam) {
        int keyCode = 0;
        bool isKeyDown = false;
        
        // Handle keyboard keys
        if (msg >= WM_KEYDOWN && msg <= WM_KEYUP) {
            keyCode = static_cast<int>(wParam);
            isKeyDown = (msg == WM_KEYDOWN);
        }
        // Handle system keys (like ALT)
        else if (msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
            keyCode = static_cast<int>(wParam);
            isKeyDown = (msg == WM_SYSKEYDOWN);
        }
        // Handle mouse buttons
        else if (msg >= WM_LBUTTONDOWN && msg <= WM_MBUTTONUP) {
            keyCode = GetMouseKeyFromMessage(msg, wParam);
            isKeyDown = (msg == WM_LBUTTONDOWN || msg == WM_RBUTTONDOWN || msg == WM_MBUTTONDOWN);
        }
        else if (msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP) {
            keyCode = GetMouseKeyFromMessage(msg, wParam);
            isKeyDown = (msg == WM_XBUTTONDOWN);
        }
        
        // If we have a valid key and it's being pressed down
        if (keyCode != 0 && isKeyDown) {
            // Ignore certain keys that shouldn't be used as binds
            if (keyCode == VK_INSERT || keyCode == VK_ESCAPE) {
                // Cancel listening
                StopListening();
                return true;
            }
            
            if (g_bListeningForAimbotKey.load()) {
                g_nAimbotKey.store(keyCode);
                g_bListeningForAimbotKey.store(false);
                return true;
            }
            else if (g_bListeningForTriggerbotKey.load()) {
                g_nTriggerbotKey.store(keyCode);
                g_bListeningForTriggerbotKey.store(false);
                return true;
            }
        }
        
        return false;
    }
    
    bool IsAimbotKeyPressed() {
        int key = g_nAimbotKey.load();
        if (key == 0) return false;
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    
    bool IsTriggerbotKeyPressed() {
        int key = g_nTriggerbotKey.load();
        if (key == 0) return false;
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    }
    
    std::string GetKeyName(int keyCode) {
        if (keyCode == 0) return "None";
        
        // Mouse buttons
        switch (keyCode) {
            case VK_LBUTTON:    return "LMB";
            case VK_RBUTTON:    return "RMB";
            case VK_MBUTTON:    return "MMB";
            case VK_XBUTTON1:   return "Mouse4";
            case VK_XBUTTON2:   return "Mouse5";
        }
        
        // Special keys
        switch (keyCode) {
            case VK_MENU:       return "Alt";
            case VK_LMENU:      return "LAlt";
            case VK_RMENU:      return "RAlt";
            case VK_CONTROL:    return "Ctrl";
            case VK_LCONTROL:   return "LCtrl";
            case VK_RCONTROL:   return "RCtrl";
            case VK_SHIFT:      return "Shift";
            case VK_LSHIFT:     return "LShift";
            case VK_RSHIFT:     return "RShift";
            case VK_TAB:        return "Tab";
            case VK_CAPITAL:    return "Caps";
            case VK_SPACE:      return "Space";
            case VK_RETURN:     return "Enter";
            case VK_BACK:       return "Backspace";
            case VK_DELETE:     return "Delete";
            case VK_INSERT:     return "Insert";
            case VK_HOME:       return "Home";
            case VK_END:        return "End";
            case VK_PRIOR:      return "PgUp";
            case VK_NEXT:       return "PgDn";
            case VK_UP:         return "Up";
            case VK_DOWN:       return "Down";
            case VK_LEFT:       return "Left";
            case VK_RIGHT:      return "Right";
        }
        
        // Function keys
        if (keyCode >= VK_F1 && keyCode <= VK_F24) {
            return "F" + std::to_string(keyCode - VK_F1 + 1);
        }
        
        // Number keys
        if (keyCode >= '0' && keyCode <= '9') {
            return std::string(1, static_cast<char>(keyCode));
        }
        
        // Letter keys
        if (keyCode >= 'A' && keyCode <= 'Z') {
            return std::string(1, static_cast<char>(keyCode));
        }
        
        // Try to get key name from Windows
        UINT scanCode = MapVirtualKeyA(keyCode, MAPVK_VK_TO_VSC);
        if (scanCode != 0) {
            char keyName[128] = {0};
            LONG lParam = static_cast<LONG>(scanCode) << 16;
            if (GetKeyNameTextA(lParam, keyName, sizeof(keyName)) > 0) {
                return std::string(keyName);
            }
        }
        
        // Fallback to hex code
        char hexCode[16];
        snprintf(hexCode, sizeof(hexCode), "0x%X", keyCode);
        return std::string(hexCode);
    }
    
    bool HasKeyConflict() {
        int aimbotKey = g_nAimbotKey.load();
        int triggerbotKey = g_nTriggerbotKey.load();
        
        // No conflict if either key is 0 (unbound)
        if (aimbotKey == 0 || triggerbotKey == 0)
            return false;
            
        return aimbotKey == triggerbotKey;
    }
    
    int GetConflictKey() {
        if (HasKeyConflict()) {
            return g_nAimbotKey.load();
        }
        return 0;
    }
}
