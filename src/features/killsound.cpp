#include "killsound.hpp"
#include "killsound.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>

#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <vector>

namespace KillSound {
    static std::mutex g_mutex;
    static std::vector<std::string> g_files;
    static int g_selectedIndex = -1;
    static int g_appliedIndex = -1;
    static int g_lastKillCount = -1;
    static uintptr_t g_lastLocalController = 0;

    static std::string ToLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static bool HasSupportedExtension(const std::string& path) {
        std::string lower = ToLower(path);
        return lower.size() >= 4 && (lower.ends_with(".mp3") || lower.ends_with(".wav"));
    }

    static void StopPlayback() {
        mciSendStringA("stop killsound", nullptr, 0, nullptr);
        mciSendStringA("close killsound", nullptr, 0, nullptr);
    }

    static void PlayCurrentSound() {
        std::string path;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_appliedIndex < 0 || g_appliedIndex >= static_cast<int>(g_files.size())) return;
            path = g_files[g_appliedIndex];
        }

        StopPlayback();

        std::string openCmd = "open \"" + path + "\" alias killsound";
        if (mciSendStringA(openCmd.c_str(), nullptr, 0, nullptr) == 0) {
            mciSendStringA("play killsound from 0", nullptr, 0, nullptr);
        }
    }

    void Run() {
        __try {
            uintptr_t clientBase = Memory::GetModuleBase("client.dll");
            if (!clientBase) {
                g_lastKillCount = -1;
                g_lastLocalController = 0;
                return;
            }

            uintptr_t localController = 0;
            if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) ||
                !Memory::IsValidPtr(localController)) {
                g_lastKillCount = -1;
                g_lastLocalController = 0;
                return;
            }

            if (localController != g_lastLocalController) {
                g_lastLocalController = localController;
                g_lastKillCount = -1;
            }

            uintptr_t actionTracking = 0;
            if (!Memory::SafeRead(localController + cs2_dumper::schemas::client_dll::CCSPlayerController::m_pActionTrackingServices, actionTracking) ||
                !Memory::IsValidPtr(actionTracking)) {
                g_lastKillCount = -1;
                return;
            }

            int killCount = 0;
            if (!Memory::SafeRead(actionTracking + cs2_dumper::schemas::client_dll::CCSPlayerController_ActionTrackingServices::m_iNumRoundKills, killCount)) {
                g_lastKillCount = -1;
                return;
            }

            if (g_lastKillCount < 0) {
                g_lastKillCount = killCount;
                return;
            }

            if (killCount < g_lastKillCount) {
                g_lastKillCount = killCount;
                return;
            }

            if (killCount > g_lastKillCount) {
                g_lastKillCount = killCount;
                PlayCurrentSound();
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_lastKillCount = -1;
        }
    }

    void Reset() {
        g_lastKillCount = -1;
        g_lastLocalController = 0;
        StopPlayback();
    }

    bool BrowseAndAddFiles(HWND owner) {
        char buffer[32768] = {};
        OPENFILENAMEA ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = owner;
        ofn.lpstrFilter = "Audio Files (*.mp3;*.wav)\0*.mp3;*.wav\0MP3 Files (*.mp3)\0*.mp3\0WAV Files (*.wav)\0*.wav\0\0";
        ofn.lpstrFile = buffer;
        ofn.nMaxFile = sizeof(buffer);
        ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_ALLOWMULTISELECT;
        ofn.lpstrTitle = "Select kill sound files";

        if (!GetOpenFileNameA(&ofn)) {
            return false;
        }

        std::vector<std::string> toAdd;
        const char* first = buffer;
        const char* second = first + strlen(first) + 1;

        if (*second == '\0') {
            if (HasSupportedExtension(first)) {
                toAdd.emplace_back(first);
            }
        } else {
            std::string directory = first;
            const char* current = second;
            while (*current) {
                std::string fullPath = directory + "\\" + current;
                if (HasSupportedExtension(fullPath)) {
                    toAdd.push_back(fullPath);
                }
                current += strlen(current) + 1;
            }
        }

        if (toAdd.empty()) return false;

        std::lock_guard<std::mutex> lock(g_mutex);
        bool changed = false;
        for (const auto& path : toAdd) {
            if (std::find(g_files.begin(), g_files.end(), path) == g_files.end()) {
                g_files.push_back(path);
                changed = true;
            }
        }

        if (g_selectedIndex < 0 && !g_files.empty()) {
            g_selectedIndex = 0;
        }

        return changed;
    }

    std::vector<std::string> GetFileList() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_files;
    }

    int GetSelectedIndex() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_selectedIndex;
    }

    void SetSelectedIndex(int index) {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (index >= 0 && index < static_cast<int>(g_files.size())) {
            g_selectedIndex = index;
        }
    }

    int GetAppliedIndex() {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_appliedIndex;
    }

    bool ApplySelected() {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_selectedIndex < 0 || g_selectedIndex >= static_cast<int>(g_files.size())) {
            return false;
        }
        g_appliedIndex = g_selectedIndex;
        return true;
    }
}
