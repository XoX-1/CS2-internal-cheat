#include "skychanger.hpp"
#include "../hooks/hooks.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include "../sdk/math.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include <imgui.h>
#include "../../vendor/imgui/IconsFontAwesome6.h"
#include <cstdint>
#include <string>
#include <vector>
#include <atomic>
#include <windows.h>

namespace {

    // =========================================================================
    // Direct Read/Write (SEH protected)
    // =========================================================================
    template<typename T>
    T GameRead(uintptr_t address) {
        if (!address) return T{};
        __try { return *reinterpret_cast<T*>(address); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return T{}; }
    }

    template<typename T>
    void GameWrite(uintptr_t address, const T& value) {
        if (!address) return;
        __try { *reinterpret_cast<T*>(address) = value; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // =========================================================================
    // C_EnvSky Offsets (from output/client_dll.hpp)
    // =========================================================================
    namespace SkyOff {
        constexpr std::ptrdiff_t m_hSkyMaterial             = 0xE88;
        constexpr std::ptrdiff_t m_hSkyMaterialLightingOnly = 0xE90;
        constexpr std::ptrdiff_t m_bStartDisabled           = 0xE98;
        constexpr std::ptrdiff_t m_vTintColor               = 0xE99; // Color (RGBA)
        constexpr std::ptrdiff_t m_vTintColorLightingOnly   = 0xE9D;
        constexpr std::ptrdiff_t m_flBrightnessScale        = 0xEA4;
        constexpr std::ptrdiff_t m_nFogType                 = 0xEA8;
        constexpr std::ptrdiff_t m_flFogMinStart            = 0xEAC;
        constexpr std::ptrdiff_t m_flFogMinEnd              = 0xEB0;
        constexpr std::ptrdiff_t m_flFogMaxStart            = 0xEB4;
        constexpr std::ptrdiff_t m_flFogMaxEnd              = 0xEB8;
        constexpr std::ptrdiff_t m_bEnabled                 = 0xEBC;
    }

    // CMapInfo Offsets — weather/rain/wetness (from output/client_dll.hpp)
    namespace MapInfoOff {
        constexpr std::ptrdiff_t m_bRainTraceToSkyEnabled       = 0x621; // bool
        constexpr std::ptrdiff_t m_flEnvRainStrength            = 0x624; // float
        constexpr std::ptrdiff_t m_flEnvPuddleRippleStrength    = 0x628; // float
        constexpr std::ptrdiff_t m_flEnvPuddleRippleDirection   = 0x62C; // float
        constexpr std::ptrdiff_t m_flEnvWetnessCoverage         = 0x630; // float
        constexpr std::ptrdiff_t m_flEnvWetnessDryingAmount     = 0x634; // float
    }

    // C_Precipitation Offsets
    namespace PrecipOff {
        constexpr std::ptrdiff_t m_flDensity              = 0xF58; // float
        constexpr std::ptrdiff_t m_flParticleInnerDist    = 0xF68; // float
    }

    // C_EnvParticleGlow Offsets
    namespace ParticleGlowOff {
        constexpr std::ptrdiff_t m_flAlphaScale     = 0x1438;
        constexpr std::ptrdiff_t m_flRadiusScale    = 0x143C;
        constexpr std::ptrdiff_t m_flSelfIllumScale = 0x1440;
        constexpr std::ptrdiff_t m_ColorTint        = 0x1444; // Color (RGBA)
    }

    // CEntityIdentity offsets
    namespace EntityIdOff {
        constexpr std::ptrdiff_t m_pEntity      = 0x10;
        constexpr std::ptrdiff_t m_designerName = 0x20;
    }

    // =========================================================================
    // Color struct (4 bytes RGBA)
    // =========================================================================
    #pragma pack(push, 1)
    struct GameColor { uint8_t r, g, b, a; };
    #pragma pack(pop)

    // =========================================================================
    // Sky Preset Definition
    // =========================================================================
    struct SkyPreset {
        const char* name;
        const char* icon;
        GameColor   tintColor;
        GameColor   tintColorLighting;
        float       brightness;
        int         fogType;
        float       fogMinStart, fogMinEnd, fogMaxStart, fogMaxEnd;
        float       displayColor[4];
    };

    static constexpr int PRESET_COUNT = 8;
    static const SkyPreset g_presets[PRESET_COUNT] = {
        { "Default",       ICON_FA_ROTATE_LEFT,  {255, 255, 255, 255}, {255, 255, 255, 255}, 1.0f,  0, 0,0,0,0, {0.7f, 0.7f, 0.7f, 1.0f} },
        { "Blood Red",     ICON_FA_DROPLET,      {255,  50,  50, 255}, {255,  80,  80, 255}, 1.5f,  0, 0,0,0,0, {1.0f, 0.2f, 0.2f, 1.0f} },
        { "Arctic Blue",   ICON_FA_SNOWFLAKE,    {150, 200, 255, 255}, {180, 220, 255, 255}, 1.2f,  0, 0,0,0,0, {0.5f, 0.7f, 1.0f, 1.0f} },
        { "Neon Purple",   ICON_FA_BOLT,         {160,  80, 255, 255}, {180, 100, 255, 255}, 1.8f,  0, 0,0,0,0, {0.6f, 0.3f, 1.0f, 1.0f} },
        { "Toxic Green",   ICON_FA_BIOHAZARD,    { 80, 255,  80, 255}, {100, 255, 100, 255}, 1.3f,  0, 0,0,0,0, {0.3f, 1.0f, 0.3f, 1.0f} },
        { "Sunset Orange", ICON_FA_SUN,          {255, 150,  50, 255}, {255, 170,  80, 255}, 1.4f,  0, 0,0,0,0, {1.0f, 0.6f, 0.2f, 1.0f} },
        { "Midnight",      ICON_FA_MOON,         { 20,  20,  40, 255}, { 30,  30,  50, 255}, 0.3f,  0, 0,0,0,0, {0.1f, 0.1f, 0.2f, 1.0f} },
        { "Custom",        ICON_FA_SLIDERS,      {255, 255, 255, 255}, {255, 255, 255, 255}, 1.0f,  0, 0,0,0,0, {0.5f, 0.5f, 0.5f, 1.0f} },
    };

    // =========================================================================
    // Weather Preset Definition
    // =========================================================================
    struct WeatherPreset {
        const char* name;
        const char* icon;
        float rainStrength;
        float puddleRippleStrength;
        float wetnessCoverage;
        float wetnessDrying;
        bool  rainEnabled;
        float displayColor[4];
    };

    static constexpr int WEATHER_PRESET_COUNT = 7;
    static const WeatherPreset g_weatherPresets[WEATHER_PRESET_COUNT] = {
        { "Off",             ICON_FA_BAN,            0.0f,  0.0f, 0.0f, 1.0f, false, {0.5f, 0.5f, 0.5f, 1.0f} },
        { "Light Rain",      ICON_FA_CLOUD_RAIN,     0.3f,  0.2f, 0.3f, 0.5f, true,  {0.4f, 0.6f, 0.8f, 1.0f} },
        { "Heavy Rain",      ICON_FA_CLOUD_SHOWERS_HEAVY, 1.0f, 0.8f, 0.9f, 0.1f, true, {0.3f, 0.4f, 0.7f, 1.0f} },
        { "Storm",           ICON_FA_CLOUD_BOLT,     1.0f,  1.0f, 1.0f, 0.0f, true,  {0.2f, 0.2f, 0.5f, 1.0f} },
        { "Drizzle",         ICON_FA_DROPLET,        0.15f, 0.1f, 0.15f,0.7f, true,  {0.5f, 0.6f, 0.7f, 1.0f} },
        { "Wet Ground",      ICON_FA_WATER,          0.0f,  0.5f, 0.8f, 0.2f, false, {0.3f, 0.5f, 0.6f, 1.0f} },
        { "Custom",          ICON_FA_SLIDERS,        0.5f,  0.5f, 0.5f, 0.5f, true,  {0.5f, 0.5f, 0.5f, 1.0f} },
    };

    // =========================================================================
    // Global State
    // =========================================================================
    static std::atomic<int>  g_selectedPreset{0};
    static std::atomic<int>  g_selectedWeather{0};
    static std::atomic<bool> g_forceUpdate{false};
    static int               g_tickCounter = 0;

    // Custom sky values
    static float g_customColor[4]       = { 1.0f, 1.0f, 1.0f, 1.0f };
    static float g_customBrightness     = 1.0f;
    static int   g_customFogType        = 0;
    static float g_customFogMinStart    = 0.0f;
    static float g_customFogMinEnd      = 0.0f;
    static float g_customFogMaxStart    = 0.0f;
    static float g_customFogMaxEnd      = 0.0f;

    // Custom weather values
    static float g_customRainStrength   = 0.5f;
    static float g_customPuddleRipple   = 0.5f;
    static float g_customWetness        = 0.5f;
    static float g_customDrying         = 0.5f;
    static bool  g_customRainEnabled    = true;

    // Saved originals
    struct OriginalSkyState {
        GameColor tintColor, tintColorLighting;
        float brightness;
        int fogType;
        float fogMinStart, fogMinEnd, fogMaxStart, fogMaxEnd;
        bool saved;
    };
    struct OriginalWeatherState {
        float rainStrength, puddleRipple, wetnessCoverage, wetnessDrying;
        bool rainEnabled;
        bool saved;
    };

    static constexpr int MAX_SKY_ENTITIES     = 16;
    static uintptr_t     g_skyEntities[MAX_SKY_ENTITIES] = {};
    static OriginalSkyState g_originals[MAX_SKY_ENTITIES] = {};
    static int            g_skyEntityCount = 0;

    // Weather entities
    static constexpr int MAX_WEATHER_ENTITIES  = 8;
    static uintptr_t     g_mapInfoEntities[MAX_WEATHER_ENTITIES] = {};
    static OriginalWeatherState g_weatherOriginals[MAX_WEATHER_ENTITIES] = {};
    static int            g_mapInfoCount = 0;

    static uintptr_t     g_precipEntities[MAX_WEATHER_ENTITIES] = {};
    static int            g_precipCount = 0;

    // Tracking
    static int  g_lastAppliedPreset = -1;
    static int  g_lastAppliedWeather = -1;
    static bool g_hasSkyApplied = false;
    static bool g_hasWeatherApplied = false;

    // Transition state machine
    static int  g_transitionPhase  = 0;
    static int  g_transitionTick   = 0;
    static int  g_transitionTarget = -1;
    static constexpr int TRANSITION_DISABLE_TICKS = 5;

    // Status
    static std::string g_statusMessage = "Idle";

    // =========================================================================
    // Entity Identification
    // =========================================================================
    static bool MatchEntityName(uintptr_t entity, const char* targetName) {
        if (!Memory::IsValidPtr(entity)) return false;
        uintptr_t identity = GameRead<uintptr_t>(entity + EntityIdOff::m_pEntity);
        if (!Memory::IsValidPtr(identity)) return false;
        uintptr_t namePtr = GameRead<uintptr_t>(identity + EntityIdOff::m_designerName);
        if (!Memory::IsValidPtr(namePtr)) return false;
        char name[48] = {};
        __try {
            for (int i = 0; i < 47; i++) {
                name[i] = *reinterpret_cast<char*>(namePtr + i);
                if (name[i] == '\0') break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
        return (strcmp(name, targetName) == 0);
    }

    // =========================================================================
    static void SafeReadString(uintptr_t address, char* buffer, size_t maxLength) {
        if (!address || !buffer) return;
        __try {
            for (size_t i = 0; i < maxLength - 1; i++) {
                buffer[i] = *reinterpret_cast<char*>(address + i);
                if (buffer[i] == '\0') break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // =========================================================================
    // Entity Discovery
    // =========================================================================
    static void DiscoverEntities() {
        g_skyEntityCount = 0;
        g_mapInfoCount   = 0;
        g_precipCount    = 0;

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) ||
            !Memory::IsValidPtr(entityList)) return;

        for (int i = 0; i < 8192; i++) {
            uintptr_t listEntry = 0;
            if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE +
                sizeof(uintptr_t) * (i >> Constants::EntityList::ENTRY_SHIFT), listEntry) ||
                !Memory::IsValidPtr(listEntry)) continue;

            uintptr_t entity = 0;
            if (!Memory::SafeRead(listEntry + Constants::EntityList::ENTRY_SIZE * (i & Constants::EntityList::INDEX_MASK),
                entity) || !Memory::IsValidPtr(entity)) continue;

            // Read the designer name directly
            char name[64] = {};
            uintptr_t identity = GameRead<uintptr_t>(entity + EntityIdOff::m_pEntity);
            if (Memory::IsValidPtr(identity)) {
                uintptr_t namePtr = GameRead<uintptr_t>(identity + EntityIdOff::m_designerName);
                if (Memory::IsValidPtr(namePtr)) {
                    SafeReadString(namePtr, name, sizeof(name));
                }
            }

            if (name[0] != '\0') {
                if (g_skyEntityCount < MAX_SKY_ENTITIES && strcmp(name, "env_sky") == 0) {
                    g_skyEntities[g_skyEntityCount++] = entity;
                }
                if (g_mapInfoCount < MAX_WEATHER_ENTITIES && 
                   (strcmp(name, "info_map_parameters") == 0 || strcmp(name, "worldspawn") == 0 || strcmp(name, "env_wind") == 0)) {
                    g_mapInfoEntities[g_mapInfoCount++] = entity;
                }
                if (g_precipCount < MAX_WEATHER_ENTITIES &&
                   (strcmp(name, "func_precipitation") == 0 || strcmp(name, "env_precipitation") == 0)) {
                    g_precipEntities[g_precipCount++] = entity;
                }
            }
        }
    }

    // =========================================================================
    // Save/Restore Originals
    // =========================================================================
    static void SaveSkyOriginals() {
        for (int i = 0; i < g_skyEntityCount; i++) {
            if (g_originals[i].saved) continue;
            uintptr_t e = g_skyEntities[i];
            g_originals[i].tintColor       = GameRead<GameColor>(e + SkyOff::m_vTintColor);
            g_originals[i].tintColorLighting = GameRead<GameColor>(e + SkyOff::m_vTintColorLightingOnly);
            g_originals[i].brightness      = GameRead<float>(e + SkyOff::m_flBrightnessScale);
            g_originals[i].fogType         = GameRead<int>(e + SkyOff::m_nFogType);
            g_originals[i].fogMinStart     = GameRead<float>(e + SkyOff::m_flFogMinStart);
            g_originals[i].fogMinEnd       = GameRead<float>(e + SkyOff::m_flFogMinEnd);
            g_originals[i].fogMaxStart     = GameRead<float>(e + SkyOff::m_flFogMaxStart);
            g_originals[i].fogMaxEnd       = GameRead<float>(e + SkyOff::m_flFogMaxEnd);
            g_originals[i].saved = true;
        }
    }

    static void SaveWeatherOriginals() {
        for (int i = 0; i < g_mapInfoCount; i++) {
            if (g_weatherOriginals[i].saved) continue;
            uintptr_t e = g_mapInfoEntities[i];
            g_weatherOriginals[i].rainStrength   = GameRead<float>(e + MapInfoOff::m_flEnvRainStrength);
            g_weatherOriginals[i].puddleRipple   = GameRead<float>(e + MapInfoOff::m_flEnvPuddleRippleStrength);
            g_weatherOriginals[i].wetnessCoverage = GameRead<float>(e + MapInfoOff::m_flEnvWetnessCoverage);
            g_weatherOriginals[i].wetnessDrying  = GameRead<float>(e + MapInfoOff::m_flEnvWetnessDryingAmount);
            g_weatherOriginals[i].rainEnabled    = GameRead<bool>(e + MapInfoOff::m_bRainTraceToSkyEnabled);
            g_weatherOriginals[i].saved = true;
        }
    }

    static void RestoreSkyOriginals() {
        for (int i = 0; i < g_skyEntityCount; i++) {
            if (!g_originals[i].saved) continue;
            uintptr_t e = g_skyEntities[i];
            if (!Memory::IsValidPtr(e)) continue;
            GameWrite<bool>(e + SkyOff::m_bEnabled, false);
            GameWrite<GameColor>(e + SkyOff::m_vTintColor,             g_originals[i].tintColor);
            GameWrite<GameColor>(e + SkyOff::m_vTintColorLightingOnly, g_originals[i].tintColorLighting);
            GameWrite<float>(e + SkyOff::m_flBrightnessScale,          g_originals[i].brightness);
            GameWrite<int>(e + SkyOff::m_nFogType,                     g_originals[i].fogType);
            GameWrite<float>(e + SkyOff::m_flFogMinStart,              g_originals[i].fogMinStart);
            GameWrite<float>(e + SkyOff::m_flFogMinEnd,                g_originals[i].fogMinEnd);
            GameWrite<float>(e + SkyOff::m_flFogMaxStart,              g_originals[i].fogMaxStart);
            GameWrite<float>(e + SkyOff::m_flFogMaxEnd,                g_originals[i].fogMaxEnd);
            GameWrite<bool>(e + SkyOff::m_bEnabled, true);
        }
        g_statusMessage = "Sky restored";
    }

    static void RestoreWeatherOriginals() {
        for (int i = 0; i < g_mapInfoCount; i++) {
            if (!g_weatherOriginals[i].saved) continue;
            uintptr_t e = g_mapInfoEntities[i];
            if (!Memory::IsValidPtr(e)) continue;
            GameWrite<float>(e + MapInfoOff::m_flEnvRainStrength,          g_weatherOriginals[i].rainStrength);
            GameWrite<float>(e + MapInfoOff::m_flEnvPuddleRippleStrength,  g_weatherOriginals[i].puddleRipple);
            GameWrite<float>(e + MapInfoOff::m_flEnvWetnessCoverage,       g_weatherOriginals[i].wetnessCoverage);
            GameWrite<float>(e + MapInfoOff::m_flEnvWetnessDryingAmount,   g_weatherOriginals[i].wetnessDrying);
            GameWrite<bool>(e + MapInfoOff::m_bRainTraceToSkyEnabled,      g_weatherOriginals[i].rainEnabled);
        }
    }

    // =========================================================================
    // Apply Sky Preset
    // =========================================================================
    static void ApplySkyPreset(int idx) {
        if (idx < 0 || idx >= PRESET_COUNT) return;

        GameColor tint, tintL;
        float brightness;
        int fogType;
        float fogMS, fogME, fogXS, fogXE;

        if (idx == 7) {
            tint.r = (uint8_t)(g_customColor[0] * 255.0f);
            tint.g = (uint8_t)(g_customColor[1] * 255.0f);
            tint.b = (uint8_t)(g_customColor[2] * 255.0f);
            tint.a = (uint8_t)(g_customColor[3] * 255.0f);
            tintL = tint;
            brightness = g_customBrightness;
            fogType = g_customFogType;
            fogMS = g_customFogMinStart; fogME = g_customFogMinEnd;
            fogXS = g_customFogMaxStart; fogXE = g_customFogMaxEnd;
        } else {
            const auto& p = g_presets[idx];
            tint = p.tintColor; tintL = p.tintColorLighting;
            brightness = p.brightness; fogType = p.fogType;
            fogMS = p.fogMinStart; fogME = p.fogMinEnd;
            fogXS = p.fogMaxStart; fogXE = p.fogMaxEnd;
        }

        for (int i = 0; i < g_skyEntityCount; i++) {
            uintptr_t e = g_skyEntities[i];
            if (!Memory::IsValidPtr(e)) continue;

            GameWrite<bool>(e + SkyOff::m_bEnabled, false);
            GameWrite<bool>(e + SkyOff::m_bStartDisabled, false);
            GameWrite<GameColor>(e + SkyOff::m_vTintColor, tint);
            GameWrite<GameColor>(e + SkyOff::m_vTintColorLightingOnly, tintL);
            GameWrite<float>(e + SkyOff::m_flBrightnessScale, brightness);
            GameWrite<int>(e + SkyOff::m_nFogType, fogType);
            GameWrite<float>(e + SkyOff::m_flFogMinStart, fogMS);
            GameWrite<float>(e + SkyOff::m_flFogMinEnd, fogME);
            GameWrite<float>(e + SkyOff::m_flFogMaxStart, fogXS);
            GameWrite<float>(e + SkyOff::m_flFogMaxEnd, fogXE);
            GameWrite<bool>(e + SkyOff::m_bEnabled, true);
        }
        g_statusMessage = std::string("Sky: ") + g_presets[idx].name;
    }

    // =========================================================================
    // Apply Weather Preset
    // =========================================================================
    static void ApplyWeatherPreset(int idx) {
        if (idx < 0 || idx >= WEATHER_PRESET_COUNT) return;

        float rainStr, puddleStr, wetness, drying;
        bool  rainEnabled;

        if (idx == 6) {
            rainStr     = g_customRainStrength;
            puddleStr   = g_customPuddleRipple;
            wetness     = g_customWetness;
            drying      = g_customDrying;
            rainEnabled = g_customRainEnabled;
        } else {
            const auto& w = g_weatherPresets[idx];
            rainStr     = w.rainStrength;
            puddleStr   = w.puddleRippleStrength;
            wetness     = w.wetnessCoverage;
            drying      = w.wetnessDrying;
            rainEnabled = w.rainEnabled;
        }

        for (int i = 0; i < g_mapInfoCount; i++) {
            uintptr_t e = g_mapInfoEntities[i];
            if (!Memory::IsValidPtr(e)) continue;
            GameWrite<bool>(e + MapInfoOff::m_bRainTraceToSkyEnabled, rainEnabled);
            GameWrite<float>(e + MapInfoOff::m_flEnvRainStrength, rainStr);
            GameWrite<float>(e + MapInfoOff::m_flEnvPuddleRippleStrength, puddleStr);
            GameWrite<float>(e + MapInfoOff::m_flEnvWetnessCoverage, wetness);
            GameWrite<float>(e + MapInfoOff::m_flEnvWetnessDryingAmount, drying);
        }

        // Also adjust precipitation density if entities exist
        for (int i = 0; i < g_precipCount; i++) {
            uintptr_t e = g_precipEntities[i];
            if (!Memory::IsValidPtr(e)) continue;
            GameWrite<float>(e + PrecipOff::m_flDensity, rainStr);
        }
    }

    // =========================================================================
    // TickInner — main loop
    // =========================================================================
    static void TickInner() {
        g_tickCounter++;

        int preset  = g_selectedPreset.load();
        int weather = g_selectedWeather.load();
        bool force  = g_forceUpdate.load();

        // Discover entities periodically
        if (g_skyEntityCount == 0 || force || (g_tickCounter % 200 == 1)) {
            DiscoverEntities();
            if (g_skyEntityCount == 0) return;
            SaveSkyOriginals();
            SaveWeatherOriginals();
        }

        // --- Sky transition state machine ---
        bool skyChanged = force || (preset != g_lastAppliedPreset);
        if (skyChanged && g_transitionPhase == 0 && preset != 0) {
            g_transitionPhase = 1;
            g_transitionTick  = TRANSITION_DISABLE_TICKS;
            g_transitionTarget = preset;
            for (int i = 0; i < g_skyEntityCount; i++) {
                uintptr_t e = g_skyEntities[i];
                if (Memory::IsValidPtr(e)) GameWrite<bool>(e + SkyOff::m_bEnabled, false);
            }
        }

        if (g_transitionPhase == 1) {
            for (int i = 0; i < g_skyEntityCount; i++) {
                uintptr_t e = g_skyEntities[i];
                if (Memory::IsValidPtr(e)) GameWrite<bool>(e + SkyOff::m_bEnabled, false);
            }
            if (--g_transitionTick <= 0) g_transitionPhase = 2;
        } else if (g_transitionPhase == 2) {
            ApplySkyPreset(g_transitionTarget);
            g_lastAppliedPreset = g_transitionTarget;
            g_hasSkyApplied = true;
            g_transitionPhase = 0;
        } else {
            if (preset == 0) {
                if (g_hasSkyApplied) {
                    for (int i = 0; i < g_skyEntityCount; i++) {
                        uintptr_t e = g_skyEntities[i];
                        if (Memory::IsValidPtr(e)) GameWrite<bool>(e + SkyOff::m_bEnabled, false);
                    }
                    Sleep(1);
                    RestoreSkyOriginals();
                    g_hasSkyApplied = false;
                    g_lastAppliedPreset = 0;
                }
            } else {
                ApplySkyPreset(preset);
            }
        }

        // --- Weather (no transition needed — immediate overwrite) ---
        bool weatherChanged = force || (weather != g_lastAppliedWeather);
        if (weather == 0) {
            if (g_hasWeatherApplied) {
                RestoreWeatherOriginals();
                g_hasWeatherApplied = false;
                g_lastAppliedWeather = 0;
            }
        } else {
            ApplyWeatherPreset(weather);
            g_lastAppliedWeather = weather;
            g_hasWeatherApplied = true;
        }

        if (force) g_forceUpdate.store(false);
    }

} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================
namespace SkyChanger {

    void Run() {
        __try { TickInner(); }
        __except (EXCEPTION_EXECUTE_HANDLER) {
            g_skyEntityCount = 0;
            g_mapInfoCount   = 0;
            g_precipCount    = 0;
            g_hasSkyApplied = false;
            g_hasWeatherApplied = false;
            g_lastAppliedPreset = -1;
            g_lastAppliedWeather = -1;
            g_transitionPhase = 0;
        }
    }

    void Reset() {
        for (int i = 0; i < MAX_SKY_ENTITIES; i++) { g_skyEntities[i] = 0; g_originals[i] = {}; }
        for (int i = 0; i < MAX_WEATHER_ENTITIES; i++) {
            g_mapInfoEntities[i] = 0;  g_weatherOriginals[i] = {};
            g_precipEntities[i] = 0;
        }
        g_skyEntityCount = g_mapInfoCount = g_precipCount = 0;
        g_hasSkyApplied = g_hasWeatherApplied = false;
        g_lastAppliedPreset = g_lastAppliedWeather = -1;
        g_transitionPhase = g_transitionTick = 0;
        g_transitionTarget = -1;
        g_statusMessage = "Reset";
    }

    // =========================================================================
    // UI
    // =========================================================================
    static constexpr ImVec4 kAccent     = ImVec4(0.60f, 0.35f, 0.95f, 1.00f);
    static constexpr ImVec4 kTextBright = ImVec4(0.98f, 0.98f, 1.00f, 1.00f);
    static constexpr ImVec4 kTextDim    = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
    static constexpr ImVec4 kBgWidget   = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    static ImU32 ToU32(ImVec4 c) { return IM_COL32((int)(c.x*255),(int)(c.y*255),(int)(c.z*255),(int)(c.w*255)); }

    static void SectionHeader(const char* label) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x+3, p.y+ImGui::GetTextLineHeight()), ToU32(kAccent), 1.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBright);
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        ImGui::Spacing(); ImGui::Spacing();
    }

    // Generic preset card grid renderer
    template<typename PresetT>
    static bool DrawPresetCard(const PresetT& preset, bool isActive, float cardW, float cardH) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
        bool clicked = ImGui::IsItemClicked(0);
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImVec4 bgCol = isActive ? ImVec4(0.12f, 0.10f, 0.28f, 0.95f) : ImVec4(0.08f, 0.08f, 0.12f, 0.9f);
        dl->AddRectFilled(pos, ImVec2(pos.x+cardW, pos.y+cardH), ImGui::ColorConvertFloat4ToU32(bgCol), 8.0f);

        if (isActive)
            dl->AddRect(pos, ImVec2(pos.x+cardW, pos.y+cardH),
                ImGui::ColorConvertFloat4ToU32(ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.8f)), 8.0f, 0, 2.5f);

        ImVec4 pc = ImVec4(preset.displayColor[0], preset.displayColor[1], preset.displayColor[2], preset.displayColor[3]);
        dl->AddRectFilled(ImVec2(pos.x, pos.y), ImVec2(pos.x+cardW, pos.y+5),
            ImGui::ColorConvertFloat4ToU32(pc), 8.0f, ImDrawFlags_RoundCornersTop);

        ImVec2 iconSz = ImGui::CalcTextSize(preset.icon);
        dl->AddText(ImVec2(pos.x+10, pos.y+12), isActive ? ToU32(kAccent) : IM_COL32(180,180,200,200), preset.icon);
        dl->AddText(ImVec2(pos.x+10+iconSz.x+6, pos.y+12), isActive ? ToU32(kTextBright) : IM_COL32(190,190,210,230), preset.name);

        if (isActive)
            dl->AddText(ImVec2(pos.x+10, pos.y+cardH-20), ToU32(kAccent), ICON_FA_CHECK " Active");

        if (hovered && !isActive)
            dl->AddRect(pos, ImVec2(pos.x+cardW, pos.y+cardH),
                ImGui::ColorConvertFloat4ToU32(ImVec4(pc.x, pc.y, pc.z, 0.5f)), 8.0f, 0, 2.0f);

        return clicked;
    }

    void RenderSkyChangerTab() {
        int curSky     = g_selectedPreset.load();
        int curWeather = g_selectedWeather.load();

        SectionHeader("SKY CHANGER");

        // Status + controls
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.9f, 0.4f, 1.0f));
        ImGui::Text(ICON_FA_CIRCLE_INFO " %s", g_statusMessage.c_str());
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.35f,0.25f,0.60f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.45f,0.35f,0.75f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f,0.20f,0.50f,1.0f));
        if (ImGui::Button(ICON_FA_ROTATE " Force Apply", ImVec2(140, 28)))
            g_forceUpdate.store(true);
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f,0.18f,0.18f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f,0.25f,0.25f,1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f,0.15f,0.15f,1.0f));
        if (ImGui::Button(ICON_FA_TRASH " Clear All", ImVec2(140, 28))) {
            g_selectedPreset.store(0);
            g_selectedWeather.store(0);
            g_forceUpdate.store(true);
            g_statusMessage = "Cleared";
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

        // ---- SKY PRESETS ----
        SectionHeader("SKY PRESETS");

        constexpr float CW = 140.0f, CH = 65.0f, CP = 7.0f;
        float rw = ImGui::GetContentRegionAvail().x;
        int cols = (int)(rw / (CW + CP));
        if (cols < 1) cols = 1;

        for (int p = 0; p < PRESET_COUNT; p++) {
            ImGui::PushID(p);
            if (p % cols != 0) ImGui::SameLine(0, CP);
            if (DrawPresetCard(g_presets[p], curSky == p, CW, CH)) {
                g_selectedPreset.store(p);
                g_forceUpdate.store(true);
            }
            ImGui::PopID();
        }

        // Custom sky controls
        if (curSky == 7) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
            ImGui::ColorEdit4("Sky Tint", g_customColor, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoInputs);
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, kAccent);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
            bool ch = ImGui::SliderFloat("Brightness", &g_customBrightness, 0.0f, 5.0f, "%.2f");
            const char* fogN[] = {"None","Linear","Exponential"};
            ch |= ImGui::Combo("Fog", &g_customFogType, fogN, 3);
            if (g_customFogType > 0) {
                ch |= ImGui::SliderFloat("Fog Min Start", &g_customFogMinStart, 0, 5000, "%.0f");
                ch |= ImGui::SliderFloat("Fog Min End",   &g_customFogMinEnd,   0, 10000, "%.0f");
                ch |= ImGui::SliderFloat("Fog Max Start", &g_customFogMaxStart, 0, 5000, "%.0f");
                ch |= ImGui::SliderFloat("Fog Max End",   &g_customFogMaxEnd,   0, 10000, "%.0f");
            }
            ImGui::PopStyleColor(2);
            if (ch) g_forceUpdate.store(true);
        }

        ImGui::Spacing(); ImGui::Spacing();

        // ---- WEATHER / PARTICLES ----
        SectionHeader("WEATHER & PARTICLES");

        for (int w = 0; w < WEATHER_PRESET_COUNT; w++) {
            ImGui::PushID(1000 + w);
            if (w % cols != 0) ImGui::SameLine(0, CP);
            if (DrawPresetCard(g_weatherPresets[w], curWeather == w, CW, CH)) {
                g_selectedWeather.store(w);
                g_forceUpdate.store(true);
            }
            ImGui::PopID();
        }

        // Custom weather controls
        if (curWeather == 6) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, kAccent);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
            bool ch = false;
            ch |= ImGui::SliderFloat("Rain Strength",   &g_customRainStrength, 0, 1, "%.2f");
            ch |= ImGui::SliderFloat("Puddle Ripples",   &g_customPuddleRipple, 0, 1, "%.2f");
            ch |= ImGui::SliderFloat("Wetness Coverage", &g_customWetness,      0, 1, "%.2f");
            ch |= ImGui::SliderFloat("Drying Amount",    &g_customDrying,       0, 1, "%.2f");
            ch |= ImGui::Checkbox("Rain Enabled", &g_customRainEnabled);
            ImGui::PopStyleColor(2);
            if (ch) g_forceUpdate.store(true);
        }
    }

    // =========================================================================
    // 3D Weather Particle Renderer
    // =========================================================================
    struct WeatherParticle {
        Vector3 pos;
        Vector3 velocity;
        float life;
        float length;
        float alpha;
    };
    
    static std::vector<WeatherParticle> g_particles;
    static float g_lastTime = 0.0f;

    void RenderWeatherOverlay() {
        int w = g_selectedWeather.load();
        if (w == 0 || w == 5) { // Off or Wet Ground (no particles)
            if (!g_particles.empty()) g_particles.clear();
            return;
        }

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        // Get view matrix
        view_matrix_t viewMatrix{};
        if (!Memory::SafeReadBytes(clientBase + cs2_dumper::offsets::client_dll::dwViewMatrix, &viewMatrix, sizeof(viewMatrix))) return;

        // Get Local Player position
        uintptr_t localController = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) || !localController) return;

        uint32_t localPawnHandle = 0;
        if (!Memory::SafeRead(localController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn, localPawnHandle) || !localPawnHandle) return;

        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || !entityList) return;

        uintptr_t localPawnEntry = 0;
        if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE + sizeof(uintptr_t) * ((localPawnHandle & Constants::EntityList::HANDLE_MASK) >> Constants::EntityList::ENTRY_SHIFT), localPawnEntry) || !localPawnEntry) return;

        uintptr_t localPawn = 0;
        if (!Memory::SafeRead(localPawnEntry + Constants::EntityList::ENTRY_SIZE * (localPawnHandle & Constants::EntityList::INDEX_MASK), localPawn) || !localPawn) return;

        uintptr_t localSceneNode = 0;
        if (!Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_pGameSceneNode, localSceneNode) || !localSceneNode) return;

        Vector3 localOrigin = {0,0,0};
        if (!Memory::SafeRead(localSceneNode + cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin, localOrigin)) return;

        // Particle configuration 
        int desiredCount = 0;
        float speed = 500.0f;
        float size = 1.0f;
        
        // 1=Light Rain, 2=Heavy Rain, 3=Storm, 4=Drizzle, 6=Custom
        if (w == 1)      { desiredCount = 600;  speed = 500.0f;  size = 1.0f; }
        else if (w == 2) { desiredCount = 1500; speed = 800.0f;  size = 1.5f; }
        else if (w == 3) { desiredCount = 3000; speed = 1200.0f; size = 2.0f; }
        else if (w == 4) { desiredCount = 300;  speed = 300.0f;  size = 0.5f; }
        else if (w == 6) { 
            desiredCount = (int)(g_customRainStrength * 2000.0f);
            speed = 600.0f * (g_customRainStrength + 0.5f);
            size = 1.0f;
        }

        if (desiredCount <= 0) {
            if (!g_particles.empty()) g_particles.clear();
            return;
        }

        // Initialize / Resize pool
        if ((int)g_particles.size() != desiredCount) {
            g_particles.resize(desiredCount);
            for (auto& p : g_particles) p.life = -1.0f; // mark for respawn
        }

        float currentTime = (float)ImGui::GetTime();
        float dt = currentTime - g_lastTime;
        if (dt > 0.1f) dt = 0.1f; // Clamp to avoid huge jumps on lag spikes
        g_lastTime = currentTime;
        
        ImDrawList* dl = ImGui::GetBackgroundDrawList();
        ImGuiIO& io = ImGui::GetIO();
        int screenW = (int)io.DisplaySize.x;
        int screenH = (int)io.DisplaySize.y;
        
        // Fast PRNG
        static uint32_t seed = 0x1337;
        auto fastRand = [&]() -> float {
            seed = (214013 * seed + 2531011);
            return (float)((seed >> 16) & 0x7FFF) / (float)0x7FFF;
        };

        for (auto& p : g_particles) {
            if (p.life < 0.0f) {
                // Respawn particle randomly in a cylinder above player
                float angle = fastRand() * 6.283185f;
                float radius = fastRand() * 1000.0f;
                p.pos.x = localOrigin.x + cosf(angle) * std::sqrt(radius) * 35.0f; 
                p.pos.y = localOrigin.y + sinf(angle) * std::sqrt(radius) * 35.0f;
                p.pos.z = localOrigin.z + 200.0f + fastRand() * 500.0f;
                
                p.velocity.x = (fastRand() - 0.5f) * 100.0f; // wind X
                p.velocity.y = (fastRand() - 0.5f) * 100.0f; // wind Y
                p.velocity.z = -speed * (0.8f + fastRand() * 0.4f); // falling speed
                
                p.life = 1.0f;
                p.length = size * (fastRand() * 0.5f + 0.5f);
                p.alpha = fastRand() * 0.4f + 0.3f; // 0.3 to 0.7 opacity
            }

            // Update Physics
            p.pos.x += p.velocity.x * dt;
            p.pos.y += p.velocity.y * dt;
            p.pos.z += p.velocity.z * dt;
            p.life -= dt;

            // Kill if passed ground level relative to player
            if (p.pos.z < localOrigin.z - 150.0f) {
                p.life = -1.0f;
                continue;
            }

            // Draw projected line
            Vector2 screenPos, screenPosTrail;
            // Trail is slightly behind current position based on velocity
            Vector3 trailPos = { p.pos.x, p.pos.y, p.pos.z - p.velocity.z * 0.02f }; 
            
            if (WorldToScreen(p.pos, screenPos, viewMatrix, screenW, screenH) &&
                WorldToScreen(trailPos, screenPosTrail, viewMatrix, screenW, screenH)) {
                
                ImU32 pCol = IM_COL32(210, 220, 255, (int)(255 * p.alpha));
                dl->AddLine(ImVec2(screenPos.x, screenPos.y), ImVec2(screenPosTrail.x, screenPosTrail.y), pCol, p.length);
            }
        }
    }

} // namespace SkyChanger
