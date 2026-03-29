#pragma once

#include <string>
#include <vector>

namespace ConfigManager {

    // Config entry: name + file path
    struct Config {
        std::string name;
        std::string filePath;
    };

    // Initialize — creates configs directory, scans for existing configs
    void Init();

    // Save current settings to a config by name (creates or overwrites)
    bool Save(const std::string& name);

    // Load settings from a config by name
    bool Load(const std::string& name);

    // Delete a config by name
    bool Delete(const std::string& name);

    // Rename a config
    bool Rename(const std::string& oldName, const std::string& newName);

    // Refresh the in-memory list from disk
    void Refresh();

    // Returns a copy of the current config list
    const std::vector<Config>& GetConfigs();

    // Currently selected config index in the list (-1 = none)
    extern int g_nSelectedConfig;

    // Status message shown in UI after save/load/delete
    extern std::string g_sStatusMessage;
    extern float       g_fStatusTimer;   // seconds remaining to show status

} // namespace ConfigManager
