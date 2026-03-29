#include "config_manager.hpp"
#include "../hooks/hooks.hpp"
#include "triggerbot.hpp"
#include "keybind_manager.hpp"
#include "silent_aim.hpp"
#include "bullet_tracer.hpp"
#include "inventory_changer.hpp"

#include <windows.h>
#include <shlobj.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace ConfigManager {

    // =========================================================================
    // Globals
    // =========================================================================
    int         g_nSelectedConfig = -1;
    std::string g_sStatusMessage  = "";
    float       g_fStatusTimer    = 0.0f;

    static std::vector<Config> s_configs;
    static std::string         s_configDir;

    // =========================================================================
    // GetConfigDir
    // Uses %TEMP% (AppData\Local\Temp) — pure ASCII, immune to Unicode paths.
    // Result: C:\Users\<user>\AppData\Local\Temp\MindCheat\configs
    // =========================================================================
    static std::string GetConfigDir() {
        char tmp[MAX_PATH] = {};
        // TEMP env var always resolves to AppData\Local\Temp regardless of locale
        DWORD len = GetEnvironmentVariableA("TEMP", tmp, MAX_PATH);
        if (len == 0 || len >= MAX_PATH) {
            // Ultimate fallback: C:\Windows\Temp
            strncpy_s(tmp, "C:\\Windows\\Temp", MAX_PATH - 1);
        }
        return std::string(tmp) + "\\MindCheat\\configs";
    }

    // =========================================================================
    // EnsureDir — creates full path using SHCreateDirectoryExA
    // =========================================================================
    static void EnsureDir(const std::string& path) {
        int ret = SHCreateDirectoryExA(nullptr, path.c_str(), nullptr);
        if (ret != ERROR_SUCCESS && ret != ERROR_ALREADY_EXISTS) {
            // Fallback: segment-by-segment on a raw char buffer (no std::string nulls)
            char buf[MAX_PATH] = {};
            strncpy_s(buf, path.c_str(), MAX_PATH - 1);
            for (char* p = buf + 1; *p; p++) {
                if (*p == '\\' || *p == '/') {
                    char sv = *p; *p = '\0';
                    CreateDirectoryA(buf, nullptr);
                    *p = sv;
                }
            }
            CreateDirectoryA(buf, nullptr);
        }
    }

    // Strip characters illegal in Windows filenames
    static std::string SanitizeName(const std::string& name) {
        std::string out;
        out.reserve(name.size());
        for (char c : name) {
            if (c == '\\' || c == '/' || c == ':' || c == '*' ||
                c == '?'  || c == '"' || c == '<' || c == '>' || c == '|') {
                out += '_';
            } else {
                out += c;
            }
        }
        if (out.empty()) out = "config";
        return out;
    }

    // =========================================================================
    // Serialization helpers
    // =========================================================================
    static void WriteBool (std::ofstream& f, const char* k, bool v)           { f << k << '=' << (v ? 1 : 0) << '\n'; }
    static void WriteInt  (std::ofstream& f, const char* k, int v)            { f << k << '=' << v           << '\n'; }
    static void WriteF    (std::ofstream& f, const char* k, float v)          { f << k << '=' << v           << '\n'; }
    static void WriteColor(std::ofstream& f, const char* k, const float c[4]) {
        f << k << '=' << c[0] << ',' << c[1] << ',' << c[2] << ',' << c[3] << '\n';
    }

    // =========================================================================
    // Init
    // =========================================================================
    void Init() {
        s_configDir = GetConfigDir();
        EnsureDir(s_configDir);
        Refresh();
    }

    // =========================================================================
    // Refresh
    // =========================================================================
    void Refresh() {
        s_configs.clear();

        std::string searchPath = s_configDir + "\\*.cfg";
        WIN32_FIND_DATAA fd;
        HANDLE hFind = FindFirstFileA(searchPath.c_str(), &fd);
        if (hFind == INVALID_HANDLE_VALUE) return;

        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::string fname = fd.cFileName;
            std::string displayName = fname;
            if (displayName.size() > 4 &&
                displayName.substr(displayName.size() - 4) == ".cfg")
                displayName = displayName.substr(0, displayName.size() - 4);
            Config c;
            c.name     = displayName;
            c.filePath = s_configDir + "\\" + fname;
            s_configs.push_back(c);
        } while (FindNextFileA(hFind, &fd));

        FindClose(hFind);

        std::sort(s_configs.begin(), s_configs.end(),
            [](const Config& a, const Config& b) { return a.name < b.name; });

        if (g_nSelectedConfig >= (int)s_configs.size())
            g_nSelectedConfig = (int)s_configs.size() - 1;
    }

    // =========================================================================
    // Save
    // =========================================================================
    bool Save(const std::string& name) {
        if (name.empty()) {
            g_sStatusMessage = "Config name is empty!";
            g_fStatusTimer   = 3.0f;
            return false;
        }

        EnsureDir(s_configDir); // re-ensure every save

        std::string safeName = SanitizeName(name);
        std::string path     = s_configDir + "\\" + safeName + ".cfg";

        std::ofstream f(path, std::ios::out | std::ios::trunc);
        if (!f.is_open()) {
            g_sStatusMessage = "Save failed: " + path;
            g_fStatusTimer   = 3.0f;
            return false;
        }

        f << "# MindCheat Config v1\n";

        // ESP
        WriteBool(f, "esp_enabled",      Hooks::g_bEspEnabled.load());
        WriteBool(f, "esp_boxes",        Hooks::g_bEspBoxes.load());
        WriteInt (f, "esp_box_style",    Hooks::g_nEspBoxStyle.load());
        WriteBool(f, "esp_health",       Hooks::g_bEspHealth.load());
        WriteBool(f, "esp_armor",        Hooks::g_bEspArmor.load());
        WriteBool(f, "esp_names",        Hooks::g_bEspNames.load());
        WriteBool(f, "esp_weapon_name",  Hooks::g_bEspWeaponName.load());
        WriteBool(f, "esp_distance",     Hooks::g_bEspDistance.load());
        WriteBool(f, "esp_snaplines",    Hooks::g_bEspSnaplines.load());
        WriteBool(f, "esp_skeleton",     Hooks::g_bEspSkeleton.load());
        WriteBool(f, "esp_head_dot",     Hooks::g_bEspHeadDot.load());
        WriteColor(f, "esp_enemy_color", Hooks::g_fEspEnemyColor);
        WriteColor(f, "esp_team_color",  Hooks::g_fEspTeamColor);
        // Glow
        WriteBool(f, "glow_enabled",      Hooks::g_bGlowEnabled.load());
        WriteBool(f, "glow_team",         Hooks::g_bGlowTeamEnabled.load());
        WriteColor(f, "glow_enemy_color", Hooks::g_fGlowEnemyColor);
        WriteColor(f, "glow_team_color",  Hooks::g_fGlowTeamColor);
        // Aimbot
        WriteBool(f, "aim_enabled", Hooks::g_bAimbotEnabled.load());
        WriteF   (f, "aim_fov",     Hooks::g_fAimbotFov.load());
        WriteF   (f, "aim_smooth",  Hooks::g_fAimbotSmooth.load());
        WriteInt (f, "aim_bone",    Hooks::g_nAimbotBone.load());
        WriteBool(f, "aim_ffa",     Hooks::g_bFFAEnabled.load());
        WriteInt (f, "aim_key",     KeybindManager::g_nAimbotKey.load());
        // Triggerbot
        WriteBool(f, "trig_enabled",    Triggerbot::g_bEnabled.load());
        WriteBool(f, "trig_team_check", Triggerbot::g_bTeamCheck.load());
        WriteInt (f, "trig_fire_mode",  Triggerbot::g_nFireMode.load());
        WriteInt (f, "trig_key",        KeybindManager::g_nTriggerbotKey.load());
        // Visuals
        WriteBool(f, "noflash",     Hooks::g_bNoFlashEnabled.load());
        WriteBool(f, "nosmoke",     Hooks::g_bNoSmokeEnabled.load());
        WriteBool(f, "thirdperson", Hooks::g_bThirdPersonEnabled.load());
        WriteBool(f, "fov_enabled", Hooks::g_bFovChangerEnabled.load());
        WriteF   (f, "fov_value",   Hooks::g_fPlayerFov.load());
        // Radar
        WriteBool(f, "radar_enabled",      Hooks::g_bRadarEnabled.load());
        WriteInt (f, "radar_style",        Hooks::g_nRadarStyle.load());
        WriteF   (f, "radar_size",         Hooks::g_fRadarSize.load());
        WriteF   (f, "radar_range",        Hooks::g_fRadarRange.load());
        WriteF   (f, "radar_zoom",         Hooks::g_fRadarZoom.load());
        WriteF   (f, "radar_alpha",        Hooks::g_fRadarAlpha.load());
        WriteBool(f, "radar_names",        Hooks::g_bRadarShowNames.load());
        WriteBool(f, "radar_health",       Hooks::g_bRadarShowHealth.load());
        WriteColor(f, "radar_enemy_color", Hooks::g_fRadarEnemyColor);
        WriteColor(f, "radar_team_color",  Hooks::g_fRadarTeamColor);
        // Misc
        WriteBool(f, "bhop",           Hooks::g_bBhopEnabled.load());
        WriteBool(f, "spectator_list", Hooks::g_bSpectatorListEnabled.load());
        WriteBool(f, "bomb_timer",     Hooks::g_bBombTimerEnabled.load());
        WriteBool(f, "kill_sound",     Hooks::g_bKillSoundEnabled.load());
        // Silent Aim
        WriteBool(f, "sa_enabled",    SilentAim::config.enabled.load());
        WriteInt (f, "sa_fov_mode",   SilentAim::config.fovMode.load());
        WriteInt (f, "sa_bone",       SilentAim::config.targetBone.load());
        WriteBool(f, "sa_team_check", SilentAim::config.teamCheck.load());
        // Bullet Tracer
        WriteBool(f, "bullet_tracer", (bool)BulletTracer::config.enabled);

        // Skin Changer — one line per weapon: skin_<defIndex>=paintKit,wear,seed,statTrak
        auto skins = InventoryUI::GetAllSkinConfigs();
        for (const auto& s : skins) {
            std::string key = "skin_" + std::to_string(s.defIndex);
            f << key << '=' << s.paintKit << ','
                           << s.wear      << ','
                           << s.seed      << ','
                           << s.statTrak  << '\n';
        }

        f.close();

        g_sStatusMessage = "Saved: " + safeName;
        g_fStatusTimer   = 3.0f;
        Refresh();

        for (int i = 0; i < (int)s_configs.size(); i++) {
            if (s_configs[i].name == safeName) {
                g_nSelectedConfig = i;
                break;
            }
        }
        return true;
    }

    // =========================================================================
    // Load
    // =========================================================================
    bool Load(const std::string& name) {
        std::string safeName = SanitizeName(name);
        std::string path     = s_configDir + "\\" + safeName + ".cfg";

        std::ifstream f(path);
        if (!f.is_open()) {
            g_sStatusMessage = "Failed to load: file not found.";
            g_fStatusTimer   = 3.0f;
            return false;
        }

        auto parseBool  = [](const std::string& v) -> bool  { return v == "1"; };
        auto parseInt   = [](const std::string& v) -> int   {
            __try { return std::stoi(v); } __except(1) { return 0; }
        };
        auto parseFloat = [](const std::string& v) -> float {
            __try { return std::stof(v); } __except(1) { return 0.0f; }
        };
        auto parseColor = [](const std::string& v, float out[4]) {
            float r = 1, g = 1, b = 1, a = 1;
            if (sscanf_s(v.c_str(), "%f,%f,%f,%f", &r, &g, &b, &a) == 4) {
                out[0] = r; out[1] = g; out[2] = b; out[3] = a;
            }
        };

        std::string line;
        std::vector<InventoryUI::SkinEntry> skinEntries;
        while (std::getline(f, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string key = line.substr(0, eq);
            std::string val = line.substr(eq + 1);
            if (!val.empty() && val.back() == '\r') val.pop_back();

            if      (key == "esp_enabled")       Hooks::g_bEspEnabled.store(parseBool(val));
            else if (key == "esp_boxes")          Hooks::g_bEspBoxes.store(parseBool(val));
            else if (key == "esp_box_style")      Hooks::g_nEspBoxStyle.store(parseInt(val));
            else if (key == "esp_health")         Hooks::g_bEspHealth.store(parseBool(val));
            else if (key == "esp_armor")          Hooks::g_bEspArmor.store(parseBool(val));
            else if (key == "esp_names")          Hooks::g_bEspNames.store(parseBool(val));
            else if (key == "esp_weapon_name")    Hooks::g_bEspWeaponName.store(parseBool(val));
            else if (key == "esp_distance")       Hooks::g_bEspDistance.store(parseBool(val));
            else if (key == "esp_snaplines")      Hooks::g_bEspSnaplines.store(parseBool(val));
            else if (key == "esp_skeleton")       Hooks::g_bEspSkeleton.store(parseBool(val));
            else if (key == "esp_head_dot")       Hooks::g_bEspHeadDot.store(parseBool(val));
            else if (key == "esp_enemy_color")    parseColor(val, Hooks::g_fEspEnemyColor);
            else if (key == "esp_team_color")     parseColor(val, Hooks::g_fEspTeamColor);
            else if (key == "glow_enabled")       Hooks::g_bGlowEnabled.store(parseBool(val));
            else if (key == "glow_team")          Hooks::g_bGlowTeamEnabled.store(parseBool(val));
            else if (key == "glow_enemy_color")   parseColor(val, Hooks::g_fGlowEnemyColor);
            else if (key == "glow_team_color")    parseColor(val, Hooks::g_fGlowTeamColor);
            else if (key == "aim_enabled")        Hooks::g_bAimbotEnabled.store(parseBool(val));
            else if (key == "aim_fov")            Hooks::g_fAimbotFov.store(parseFloat(val));
            else if (key == "aim_smooth")         Hooks::g_fAimbotSmooth.store(parseFloat(val));
            else if (key == "aim_bone")           Hooks::g_nAimbotBone.store(parseInt(val));
            else if (key == "aim_ffa")            Hooks::g_bFFAEnabled.store(parseBool(val));
            else if (key == "aim_key")            KeybindManager::g_nAimbotKey.store(parseInt(val));
            else if (key == "trig_enabled")       Triggerbot::g_bEnabled.store(parseBool(val));
            else if (key == "trig_team_check")    Triggerbot::g_bTeamCheck.store(parseBool(val));
            else if (key == "trig_fire_mode")     Triggerbot::g_nFireMode.store(parseInt(val));
            else if (key == "trig_key")           KeybindManager::g_nTriggerbotKey.store(parseInt(val));
            else if (key == "noflash")            Hooks::g_bNoFlashEnabled.store(parseBool(val));
            else if (key == "nosmoke")            Hooks::g_bNoSmokeEnabled.store(parseBool(val));
            else if (key == "thirdperson")        Hooks::g_bThirdPersonEnabled.store(parseBool(val));
            else if (key == "fov_enabled")        Hooks::g_bFovChangerEnabled.store(parseBool(val));
            else if (key == "fov_value")          Hooks::g_fPlayerFov.store(parseFloat(val));
            else if (key == "radar_enabled")      Hooks::g_bRadarEnabled.store(parseBool(val));
            else if (key == "radar_style")        Hooks::g_nRadarStyle.store(parseInt(val));
            else if (key == "radar_size")         Hooks::g_fRadarSize.store(parseFloat(val));
            else if (key == "radar_range")        Hooks::g_fRadarRange.store(parseFloat(val));
            else if (key == "radar_zoom")         Hooks::g_fRadarZoom.store(parseFloat(val));
            else if (key == "radar_alpha")        Hooks::g_fRadarAlpha.store(parseFloat(val));
            else if (key == "radar_names")        Hooks::g_bRadarShowNames.store(parseBool(val));
            else if (key == "radar_health")       Hooks::g_bRadarShowHealth.store(parseBool(val));
            else if (key == "radar_enemy_color")  parseColor(val, Hooks::g_fRadarEnemyColor);
            else if (key == "radar_team_color")   parseColor(val, Hooks::g_fRadarTeamColor);
            else if (key == "bhop")               Hooks::g_bBhopEnabled.store(parseBool(val));
            else if (key == "spectator_list")     Hooks::g_bSpectatorListEnabled.store(parseBool(val));
            else if (key == "bomb_timer")         Hooks::g_bBombTimerEnabled.store(parseBool(val));
            else if (key == "kill_sound")         Hooks::g_bKillSoundEnabled.store(parseBool(val));
            else if (key == "sa_enabled")         SilentAim::config.enabled.store(parseBool(val));
            else if (key == "sa_fov_mode")        SilentAim::config.fovMode.store(parseInt(val));
            else if (key == "sa_bone")            SilentAim::config.targetBone.store(parseInt(val));
            else if (key == "sa_team_check")      SilentAim::config.teamCheck.store(parseBool(val));
            else if (key == "bullet_tracer")      BulletTracer::config.enabled = parseBool(val);
            else if (key.size() > 5 && key.substr(0, 5) == "skin_") {
                // skin_<defIndex>=paintKit,wear,seed,statTrak
                // NOTE: no __try here — Load() has C++ objects in scope which
                // makes __try illegal (C2712). Plain stoi/sscanf_s is safe;
                // invalid values are filtered by the range checks below.
                int defIndex = 0;
                try { defIndex = std::stoi(key.substr(5)); } catch (...) {}
                if (defIndex > 0) {
                    int   paintKit = 0;
                    float wear     = 0.001f;
                    int   seed     = 0;
                    int   statTrak = -1;
                    if (sscanf_s(val.c_str(), "%d,%f,%d,%d",
                                 &paintKit, &wear, &seed, &statTrak) >= 1
                        && paintKit > 0) {
                        skinEntries.push_back({ defIndex, paintKit, wear, seed, statTrak, true });
                    }
                }
            }
        }

        f.close();
        if (!skinEntries.empty())
            InventoryUI::SetAllSkinConfigs(skinEntries);

        g_sStatusMessage = "Loaded: " + safeName;
        g_fStatusTimer   = 3.0f;
        return true;
    }

    // =========================================================================
    // Delete
    // =========================================================================
    bool Delete(const std::string& name) {
        std::string safeName = SanitizeName(name);
        std::string path     = s_configDir + "\\" + safeName + ".cfg";
        if (DeleteFileA(path.c_str())) {
            g_sStatusMessage  = "Deleted: " + safeName;
            g_fStatusTimer    = 3.0f;
            g_nSelectedConfig = -1;
            Refresh();
            return true;
        }
        g_sStatusMessage = "Delete failed.";
        g_fStatusTimer   = 3.0f;
        return false;
    }

    // =========================================================================
    // Rename
    // =========================================================================
    bool Rename(const std::string& oldName, const std::string& newName) {
        std::string oldSafe = SanitizeName(oldName);
        std::string newSafe = SanitizeName(newName);
        std::string oldPath = s_configDir + "\\" + oldSafe + ".cfg";
        std::string newPath = s_configDir + "\\" + newSafe + ".cfg";
        if (MoveFileA(oldPath.c_str(), newPath.c_str())) {
            g_sStatusMessage = "Renamed to: " + newSafe;
            g_fStatusTimer   = 3.0f;
            Refresh();
            for (int i = 0; i < (int)s_configs.size(); i++) {
                if (s_configs[i].name == newSafe) { g_nSelectedConfig = i; break; }
            }
            return true;
        }
        g_sStatusMessage = "Rename failed.";
        g_fStatusTimer   = 3.0f;
        return false;
    }

    // =========================================================================
    // GetConfigs
    // =========================================================================
    const std::vector<Config>& GetConfigs() {
        return s_configs;
    }

} // namespace ConfigManager
