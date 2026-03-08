#include "inventory_changer.hpp"
#include "skin_database.hpp"
#include "texture_manager.hpp"
#include "../sdk/memory.hpp"
#include "../sdk/constants.hpp"
#include <offsets.hpp>
#include <client_dll.hpp>
#include <imgui.h>
#include "../../vendor/imgui/IconsFontAwesome6.h"
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <sstream>
#include <windows.h>

namespace {

    // =========================================================================
    // Entity Offsets (from dumped schemas)
    // =========================================================================
    namespace Offsets {
        using namespace cs2_dumper::schemas::client_dll;

        constexpr uintptr_t m_AttributeManager = C_EconEntity::m_AttributeManager;
        constexpr uintptr_t m_Item             = C_AttributeContainer::m_Item;

        constexpr uintptr_t m_iItemDefinitionIndex = C_EconItemView::m_iItemDefinitionIndex;
        constexpr uintptr_t m_iItemIDHigh          = C_EconItemView::m_iItemIDHigh;
        constexpr uintptr_t m_iItemIDLow           = C_EconItemView::m_iItemIDLow;
        constexpr uintptr_t m_iAccountID           = C_EconItemView::m_iAccountID;
        constexpr uintptr_t m_iEntityQuality       = C_EconItemView::m_iEntityQuality;
        constexpr uintptr_t m_bInitialized         = C_EconItemView::m_bInitialized;

        constexpr uintptr_t m_nFallbackPaintKit  = C_EconEntity::m_nFallbackPaintKit;
        constexpr uintptr_t m_nFallbackSeed      = C_EconEntity::m_nFallbackSeed;
        constexpr uintptr_t m_flFallbackWear     = C_EconEntity::m_flFallbackWear;
        constexpr uintptr_t m_nFallbackStatTrak  = C_EconEntity::m_nFallbackStatTrak;

        constexpr uintptr_t m_pWeaponServices = C_BasePlayerPawn::m_pWeaponServices;

        constexpr uintptr_t m_hMyWeapons   = CPlayer_WeaponServices::m_hMyWeapons;
        constexpr uintptr_t m_hActiveWeapon = CPlayer_WeaponServices::m_hActiveWeapon;
    }

    // =========================================================================
    // Skin Configuration
    // =========================================================================
    struct SkinConfig {
        int paintKit    = 0;
        float wear      = 0.000001f;
        int seed        = 1;
        bool statTrak   = false;
        int statTrakVal = 0;
        bool enabled    = false;
    };

    static std::unordered_map<uint16_t, SkinConfig> g_skinConfigs;
    static std::mutex g_configMutex;

    // Debug/Status
    static std::string g_statusMessage = "Idle";
    static ImVec4 g_statusColor = ImVec4(0.55f, 0.55f, 0.62f, 1.0f);
    static uint64_t g_framesApplied = 0;
    static uint64_t g_weaponsPatched = 0;
    static std::string g_lastError = "none";
    static uint32_t g_localAccountID = 0;

    // Game function
    using FnProcessFallbackAttribs = void(__fastcall*)(uintptr_t econEntity);
    static FnProcessFallbackAttribs s_fnProcessFallback = nullptr;
    static uintptr_t s_clientBase = 0;

    static void InitGameFunctions(uintptr_t clientBase) {
        if (s_fnProcessFallback) return;
        s_fnProcessFallback = reinterpret_cast<FnProcessFallbackAttribs>(clientBase + 0x1078820);
        s_clientBase = clientBase;
    }

    static void ForceUpdateAttributes(uintptr_t econEntity) {
        if (!s_fnProcessFallback) return;
        __try {
            s_fnProcessFallback(econEntity);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            g_lastError = "SEH in ForceUpdateAttributes";
        }
    }

    static bool IsKnife(uint16_t defIndex) {
        return defIndex == 42 || defIndex == 59 || (defIndex >= 500 && defIndex <= 527);
    }

    static uintptr_t ResolveEntityFromHandle(uintptr_t entityList, uint32_t handle) {
        if (handle == 0 || handle == 0xFFFFFFFF) return 0;
        uintptr_t listEntry = 0;
        if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE +
                              sizeof(uintptr_t) * ((handle & Constants::EntityList::HANDLE_MASK) >> Constants::EntityList::ENTRY_SHIFT),
                              listEntry) || !Memory::IsValidPtr(listEntry))
            return 0;
        uintptr_t entity = 0;
        if (!Memory::SafeRead(listEntry + Constants::EntityList::ENTRY_SIZE * (handle & Constants::EntityList::INDEX_MASK),
                              entity) || !Memory::IsValidPtr(entity))
            return 0;
        return entity;
    }

    static void ApplySkinToWeapon(uintptr_t weaponEntity, const SkinConfig& skin) {
        if (!skin.enabled || skin.paintKit <= 0) return;
        const uintptr_t itemView = weaponEntity + Offsets::m_AttributeManager + Offsets::m_Item;
        Memory::SafeWrite<uint32_t>(itemView + Offsets::m_iItemIDHigh, static_cast<uint32_t>(-1));
        Memory::SafeWrite<uint32_t>(itemView + Offsets::m_iAccountID, g_localAccountID ? g_localAccountID : 1u);
        int32_t currentPK = 0;
        Memory::SafeRead(weaponEntity + Offsets::m_nFallbackPaintKit, currentPK);
        bool needsUpdate = (currentPK != skin.paintKit);
        Memory::SafeWrite<int32_t>(weaponEntity + Offsets::m_nFallbackPaintKit, skin.paintKit);
        Memory::SafeWrite<float>(weaponEntity + Offsets::m_flFallbackWear, skin.wear);
        Memory::SafeWrite<int32_t>(weaponEntity + Offsets::m_nFallbackSeed, skin.seed);
        Memory::SafeWrite<int32_t>(weaponEntity + Offsets::m_nFallbackStatTrak, skin.statTrak ? skin.statTrakVal : -1);
        if (skin.statTrak) Memory::SafeWrite<int32_t>(itemView + Offsets::m_iEntityQuality, 9);
        if (needsUpdate) ForceUpdateAttributes(weaponEntity);
        g_weaponsPatched++;
    }

    // =========================================================================
    // UI State
    // =========================================================================
    static int g_selectedCategory = 0;   // Start showing first category immediately
    static int g_selectedWeaponIdx = -1;

    // Skin settings
    static float g_editWear = 0.000001f;
    static int   g_editSeed = 1;
    static bool  g_editStatTrak = false;

} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================
namespace InventoryUI {

    static void RunInternal();

    void Run() {
        __try { RunInternal(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_lastError = "SEH exception in Run()"; }
    }

    void RunInternal() {
        const uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;
        InitGameFunctions(clientBase);

        uintptr_t localController = 0;
        if (Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) &&
            Memory::IsValidPtr(localController)) {
            uint64_t steamID = 0;
            if (Memory::SafeRead(localController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_steamID, steamID) && steamID != 0)
                g_localAccountID = static_cast<uint32_t>(steamID & 0xFFFFFFFF);
        }

        uintptr_t localPawn = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn, localPawn) || !Memory::IsValidPtr(localPawn))
            return;

        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) || !Memory::IsValidPtr(entityList))
            return;

        uintptr_t weaponServices = 0;
        if (!Memory::SafeRead(localPawn + Offsets::m_pWeaponServices, weaponServices) || !Memory::IsValidPtr(weaponServices))
            return;

        uint32_t activeHandle = 0;
        Memory::SafeRead(weaponServices + Offsets::m_hActiveWeapon, activeHandle);

        uintptr_t vecData = 0;
        int vecSize = 0;
        Memory::SafeRead(weaponServices + Offsets::m_hMyWeapons, vecData);          // +0x00 = T* m_pData
        Memory::SafeRead(weaponServices + Offsets::m_hMyWeapons + 0x10, vecSize);   // +0x10 = int m_nSize

        if (vecSize <= 0 || vecSize > 64 || !Memory::IsValidPtr(vecData)) {
            if (activeHandle && activeHandle != 0xFFFFFFFF) {
                uintptr_t weapon = ResolveEntityFromHandle(entityList, activeHandle);
                if (weapon) {
                    const uintptr_t itemView = weapon + Offsets::m_AttributeManager + Offsets::m_Item;
                    uint16_t defIndex = 0;
                    Memory::SafeRead(itemView + Offsets::m_iItemDefinitionIndex, defIndex);
                    if (!IsKnife(defIndex)) {
                        std::lock_guard<std::mutex> lock(g_configMutex);
                        auto it = g_skinConfigs.find(defIndex);
                        if (it != g_skinConfigs.end()) ApplySkinToWeapon(weapon, it->second);
                    }
                }
            }
            g_framesApplied++;
            return;
        }

        for (int i = 0; i < vecSize; i++) {
            uint32_t handle = 0;
            if (!Memory::SafeRead(vecData + i * sizeof(uint32_t), handle)) continue;
            if (handle == 0 || handle == 0xFFFFFFFF) continue;
            uintptr_t weapon = ResolveEntityFromHandle(entityList, handle);
            if (!weapon) continue;
            const uintptr_t itemView = weapon + Offsets::m_AttributeManager + Offsets::m_Item;
            uint16_t defIndex = 0;
            if (!Memory::SafeRead(itemView + Offsets::m_iItemDefinitionIndex, defIndex)) continue;
            if (IsKnife(defIndex)) continue;
            std::lock_guard<std::mutex> lock(g_configMutex);
            auto it = g_skinConfigs.find(defIndex);
            if (it != g_skinConfigs.end()) ApplySkinToWeapon(weapon, it->second);
        }
        g_framesApplied++;
    }

    std::vector<std::string> GetInventoryDebugLines() {
        std::vector<std::string> lines;
        lines.push_back(std::string("mode: weapon_skin_changer"));
        lines.push_back(std::string("frames_applied: ") + std::to_string(g_framesApplied));
        lines.push_back(std::string("weapons_patched: ") + std::to_string(g_weaponsPatched));
        lines.push_back(std::string("last_error: ") + g_lastError);
        { std::lock_guard<std::mutex> lock(g_configMutex); lines.push_back(std::string("active_configs: ") + std::to_string(g_skinConfigs.size())); }
        lines.push_back(std::string("status: ") + g_statusMessage);
        return lines;
    }

    // =========================================================================
    // UI Helpers
    // =========================================================================
    static ImVec4 GetRarityColor(SkinDB::Rarity r) {
        const auto& c = SkinDB::g_rarityColors[r];
        return ImVec4(c.r, c.g, c.b, c.a);
    }

    static bool HasActiveSkin(uint16_t defIndex) {
        std::lock_guard<std::mutex> lock(g_configMutex);
        auto it = g_skinConfigs.find(defIndex);
        return it != g_skinConfigs.end() && it->second.enabled;
    }

    static std::string GetActiveSkinName(uint16_t defIndex) {
        std::lock_guard<std::mutex> lock(g_configMutex);
        auto it = g_skinConfigs.find(defIndex);
        if (it == g_skinConfigs.end() || !it->second.enabled) return "";
        int pk = it->second.paintKit;
        for (int w = 0; w < SkinDB::WEAPON_COUNT; w++) {
            if (SkinDB::g_weapons[w].defIndex == defIndex) {
                for (int s = 0; s < SkinDB::g_weapons[w].skinCount; s++) {
                    if (SkinDB::g_weapons[w].skins[s].paintKit == pk)
                        return SkinDB::g_weapons[w].skins[s].name;
                }
                break;
            }
        }
        return "Custom #" + std::to_string(pk);
    }

    // =========================================================================
    // Card dimensions (wider cards)
    // =========================================================================
    static constexpr float CARD_W = 165.0f;
    static constexpr float CARD_H = 140.0f;
    static constexpr float CARD_IMG_H = 95.0f;
    static constexpr float CARD_PAD = 8.0f;

    // =========================================================================
    // DrawCard — renders a single image card.
    // Uses InvisibleButton FIRST to properly extend the scrollable region,
    // then draws everything via DrawList on top. No SetCursorScreenPos.
    // Returns true if clicked.
    // =========================================================================
    static const char* GetWeaponCardIcon(const SkinDB::WeaponDef& weapon) {
        if (weapon.defIndex == 9 || weapon.defIndex == 40) return ICON_FA_BULLSEYE;
        if (weapon.category == SkinDB::CAT_SMGS) return ICON_FA_BURST;
        if (weapon.category == SkinDB::CAT_HEAVY) return ICON_FA_SHIELD_HALVED;
        if (weapon.category == SkinDB::CAT_PISTOLS) return ICON_FA_CROSSHAIRS;
        return ICON_FA_GUN;
    }

    static const char* GetWeaponCardImageUrl(const SkinDB::WeaponDef& weapon) {
        switch (weapon.defIndex) {
            case 1:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnk-CNc4_fgOfA8cfGRDTfGku13seI4Fyq2wUQm5DjWzo38IH3BO1N2W5MmTe5etw74zINmLLSKdA";
            case 2:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnl8StP6ryvOqJpJqjACjbBkb93srg-Fn7ilBhysWXSyNarJSqUZlIpCMclTbMCrFDmxYRwJ9Kk";
            case 3:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnm9DRe_Pe4baojePHHWz7GwL4jsbVvTHnilE1w5WrVzo39JH6QOFUnC8RxROMN4ESwlMqnab24YkBbtQ";
            case 4:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnn8S1Y5Lz9O_M5d_LHDz6WmOp04-U8THmwzU0l6ziDyd__IC3DO1IgXJJwE7NfrFDmxU9v5GXb";
            case 7:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnh9nYMoaCvMfxudKGVC2bIwLku5bFsHn2xzU1w4W_Tm9-ucn2eaQZxWcYmR-IU8k7vea-fOvM";
            case 8:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnh6CUV7ff8PP08eanED2LHlLh06ec-TnjmkUUmsGXRn4n8cimTPVB0XsR1RPlK7Ee4GsImgw";
            case 9:  return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnh6jIVtqr4PqBoI_ORVjXFkeguseMwGXGwwUV_4GmHyd2qdH-WbFAmApsiQPlK7EcMn7y-CQ";
            case 10: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_famas_png.png";
            case 11: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_g3sg1_png.png";
            case 13: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnn_C5S4_O8JvZrIaPKV2ORx7d3trg7Gnjmlxl04WTTyoyqeXKUPFVzCsN1FuMJuxam0oqwr0aqT-w";
            case 14: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKntr3YCoaarbfc_IvHDWWLClb8g5OA7F3y1l0xw6juBzdeoJX6fZ1IoWcciQ-UU8k7vtOr9c-I";
            case 16: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKntqSMKofH_O_Y-JfSVV2XBkLolsbZvTCqxx0sk4DjUnNipdSiQagcgXJQlRLEU8k7vIDSSpqI";
            case 17: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_mac10_png.png";
            case 19: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnwpHIVvfOsPfI9dqDCWDDGkb4j5OU_Fy_kx0l1tj6DnoqseC2TP1cpAsF2QPlK7EcMYXqtDg";
            case 23: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_mp5sd_png.png";
            case 24: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKn18DIPurz6MPducqDGCDPIw7l05LA7SXyylkh25z-Dm9agJHKSZgciDZt3F7JerFDmxeILsNGi";
            case 25: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_xm1014_png.png";
            case 26: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKni9DhU4bz-PKZocPTBW2GWlbZw4bM7SS2xwER1smSEnIv6dS_GbFBxDJd0RLFbrFDmxaLGO5J-";
            case 27: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_mag7_png.png";
            case 28: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnu-CVe-bz3P6I1caTGDzTFk7gh5rg6Tn3rkBwm4zjSwo3_Ii-fZgIjA8dwELFZrFDmxWhBRARX";
            case 29: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnz_DVe6_2obuo_JqHACzaSk-1wtrMwTi3nkUly6m-ByYr4Jy2fP1cgA8dxFLRfu0LqjJS5YC4vtNQk";
            case 30: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKn0-CECofb2MKY9IvPGWjPAkrwi5Lk4Tn3nzUwlsGnVzI6pdymQbVAjW8d0F-IU8k7vdMx23Ho";
            case 32: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKno9jIJv6L-Jqc_cKjEWDDDlLx3trVrH3qykEtz4TjQno6td3uVbVRyWZR2EbEKtRCm0oqwKXhPxN4";
            case 33: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnt7XUVvfOoPqA1eKHEVjXFlr0ituM_SnqwwR9w4mXVyIn6dnKRblV1D5AhTPlK7EelyO1yEg";
            case 34: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnt7XsVv6T9OvE9dKLKCD_Ex74h5bY6Tnrgl0hzt2rXm4qseXyWaAMgWJJ2EflK7Ec0i602wg";
            case 35: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_nova_png.png";
            case 36: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnwr3cLoaf4avNvJqKXXmKUlrp0s-U9HCvgw0V05WSDw96pIC6VOw92X8QkROAU8k7vNwsvFQ4";
            case 38: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_scar20_png.png";
            case 39: return "https://raw.githubusercontent.com/ByMykel/counter-strike-image-tracker/main/static/panorama/images/econ/weapons/base_weapons/weapon_sg556_png.png";
            case 40: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnz7iULt7z2MPY1eaWVCDHGlrgksuQ_HS3lxhkh4m-Gm9b6ICjCPQ4hDMF3EOJerFDmxXJ24aAg";
            case 60: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKntqSMK0OGnZKFjI_WBQD_Cleh0teA_F37qkERy52rWm9yhdynGblMgD5AkQrZeuhXtkt3iMOv8p1uJZpwq8Vo";
            case 61: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKn17jJk_PuibapuJeLdWGLFwL8i4eVsFiqxxUt34jmHnoysJ3qVOAYgCJZwQrRb5EPul4XlYvSiuVIHgy4Xvg";
            case 63: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKnj53UO7ryvaac0dKiVW2XBlrwmsuA6GH3hkE9062qEz9aoeCmVawchW8dwEe4MrFDmxWPDR_Ga";
            case 64: return "https://community.akamai.steamstatic.com/economy/image/i0CoZ81Ui0m-9KwlBY1L_18myuGuq1wfhWSaZgMttyVfPaERSR0Wqmu7LAocGJKz2lu_XuWbwcuyMESA4Fdl-4nnpU7iQA3-kKny-DRU4-Sreuo8cvTLCzKRmbkk4ONtGijilk8k4znWy9v8JCiUaQIiWZpyQ-AJtEa7jJS5YM17OTN5";
            default: return "";
        }
    }

    static bool DrawCard(const char* label, const char* sublabel,
                         const char* imageUrl, const char* defaultIcon,
                         ImVec4 rarityCol, bool isActive,
                         float cardW = CARD_W) {
        // Save position, then create the invisible hit-area (advances cursor properly)
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##card", ImVec2(cardW, CARD_H));
        bool hovered = ImGui::IsItemHovered();
        bool clicked = ImGui::IsItemClicked(0);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Card background
        ImVec4 bgCol = isActive ? ImVec4(0.12f, 0.28f, 0.12f, 0.9f)
                                : ImVec4(0.08f, 0.08f, 0.12f, 0.9f);
        dl->AddRectFilled(pos, ImVec2(pos.x + cardW, pos.y + CARD_H),
                          ImGui::ColorConvertFloat4ToU32(bgCol), 6.0f);

        // Active glow border
        if (isActive) {
            dl->AddRect(pos, ImVec2(pos.x + cardW, pos.y + CARD_H),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(0.3f, 1.0f, 0.3f, 0.7f)),
                        6.0f, 0, 2.0f);
        }

        // Image area
        float imgX = pos.x + 8.0f;
        float imgY = pos.y + 4.0f;
        float imgW = cardW - 16.0f;

        ID3D11ShaderResourceView* tex = nullptr;
        if (imageUrl && imageUrl[0] != '\0')
            tex = TextureMgr::Get(imageUrl);

        if (tex) {
            dl->AddImageRounded((ImTextureID)tex,
                ImVec2(imgX, imgY), ImVec2(imgX + imgW, imgY + CARD_IMG_H),
                    ImVec2(1, 1), ImVec2(0, 0),
                IM_COL32(255, 255, 255, 255), 4.0f);
        } else {
            dl->AddRectFilled(ImVec2(imgX, imgY), ImVec2(imgX + imgW, imgY + CARD_IMG_H),
                              IM_COL32(20, 20, 30, 200), 4.0f);
            if (defaultIcon && defaultIcon[0] != '\0') {
                ImVec2 iconSize = ImGui::CalcTextSize(defaultIcon);
                dl->AddText(ImVec2(imgX + imgW / 2 - iconSize.x / 2, imgY + CARD_IMG_H / 2 - iconSize.y / 2 - 8.0f),
                            IM_COL32(180, 186, 210, 230), defaultIcon);
            }
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(imgX + imgW / 2 - ts.x / 2, imgY + CARD_IMG_H / 2 - ts.y / 2 + 16.0f),
                        IM_COL32(120, 120, 140, 220), label);
        }

        // Rarity color bar
        dl->AddRectFilled(
            ImVec2(pos.x, pos.y + CARD_IMG_H + 4.0f),
            ImVec2(pos.x + cardW, pos.y + CARD_IMG_H + 7.0f),
            ImGui::ColorConvertFloat4ToU32(rarityCol));

        // Label text (clipped)
        float textY = pos.y + CARD_IMG_H + 10.0f;
        ImGui::PushClipRect(ImVec2(pos.x + 4, textY), ImVec2(pos.x + cardW - 4, textY + 14), true);
        dl->AddText(ImVec2(pos.x + 6, textY), IM_COL32(230, 230, 230, 255), label);
        ImGui::PopClipRect();

        // Sublabel text
        if (sublabel && sublabel[0]) {
            float subY = textY + 14.0f;
            ImGui::PushClipRect(ImVec2(pos.x + 4, subY), ImVec2(pos.x + cardW - 4, subY + 14), true);
            dl->AddText(ImVec2(pos.x + 6, subY), IM_COL32(140, 140, 160, 200), sublabel);
            ImGui::PopClipRect();
        }

        // Hover glow
        if (hovered) {
            dl->AddRect(pos, ImVec2(pos.x + cardW, pos.y + CARD_H),
                        ImGui::ColorConvertFloat4ToU32(
                            ImVec4(rarityCol.x, rarityCol.y, rarityCol.z, 0.6f)),
                        6.0f, 0, 2.0f);
        }

        return clicked;
    }

    // =========================================================================
    // UI: Main render
    // =========================================================================
    void RenderInventoryChangerTab() {
        // ---- Category tab bar (always visible at top) ----
        if (ImGui::BeginTabBar("##SkinCategories")) {
            for (int c = 0; c < static_cast<int>(SkinDB::CAT_COUNT); c++) {
                int activeCount = 0;
                for (int w = 0; w < SkinDB::WEAPON_COUNT; w++) {
                    if (SkinDB::g_weapons[w].category == c && HasActiveSkin(SkinDB::g_weapons[w].defIndex))
                        activeCount++;
                }

                char tabLabel[64];
                if (activeCount > 0)
                    snprintf(tabLabel, sizeof(tabLabel), "%s (%d)###cat%d",
                             SkinDB::g_categoryNames[c], activeCount, c);
                else
                    snprintf(tabLabel, sizeof(tabLabel), "%s###cat%d",
                             SkinDB::g_categoryNames[c], c);

                if (ImGui::BeginTabItem(tabLabel)) {
                    if (c != g_selectedCategory) {
                        g_selectedCategory = c;
                        g_selectedWeaponIdx = -1;
                    }
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }

        ImGui::Spacing();

        // ---- Back button when viewing skins ----
        if (g_selectedWeaponIdx >= 0) {
            // Find weapon name for breadcrumb
            int wIdx = 0;
            const char* weaponName = "";
            for (int wi = 0; wi < SkinDB::WEAPON_COUNT; wi++) {
                if (SkinDB::g_weapons[wi].category != static_cast<SkinDB::Category>(g_selectedCategory))
                    continue;
                if (wIdx == g_selectedWeaponIdx) {
                    weaponName = SkinDB::g_weapons[wi].name;
                    break;
                }
                wIdx++;
            }

            if (ImGui::Button("<< Back to Weapons")) {
                g_selectedWeaponIdx = -1;
                return; // Don't render stale content this frame
            }
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "%s", weaponName);
            ImGui::Separator();
            ImGui::Spacing();
        }

        // ---- Content area (scrollable) ----
        ImGui::BeginChild("##skinContent", ImVec2(0, 0), false);

        if (g_selectedWeaponIdx < 0) {
            // =================================================================
            // Weapon Grid — show weapons for the selected category with images
            // =================================================================
            float regionW = ImGui::GetContentRegionAvail().x;
            int cols = (int)(regionW / (CARD_W + CARD_PAD));
            if (cols < 1) cols = 1;

            int wIdx = 0;
            int itemIdx = 0;
            for (int w = 0; w < SkinDB::WEAPON_COUNT; w++) {
                if (SkinDB::g_weapons[w].category != static_cast<SkinDB::Category>(g_selectedCategory))
                    continue;

                const auto& wd = SkinDB::g_weapons[w];
                bool hasActive = HasActiveSkin(wd.defIndex);

                const char* previewUrl = GetWeaponCardImageUrl(wd);
                ImVec4 rarityCol = GetRarityColor(SkinDB::RARITY_CONSUMER);

                // Subtitle
                char subtitle[64];
                if (hasActive) {
                    std::string activeName = GetActiveSkinName(wd.defIndex);
                    snprintf(subtitle, sizeof(subtitle), "%s", activeName.c_str());
                } else {
                    snprintf(subtitle, sizeof(subtitle), "%d skins", wd.skinCount);
                }

                ImGui::PushID(w);
                if (itemIdx % cols != 0) ImGui::SameLine(0, CARD_PAD);

                if (DrawCard(wd.name, subtitle, previewUrl, GetWeaponCardIcon(wd), rarityCol, hasActive)) {
                    g_selectedWeaponIdx = wIdx;
                }

                // Tooltip
                if (ImGui::IsItemHovered()) {
                    if (hasActive) {
                        std::string activeName = GetActiveSkinName(wd.defIndex);
                        ImGui::SetTooltip("%s\n%d skins\nActive: %s", wd.name, wd.skinCount, activeName.c_str());
                    } else {
                        ImGui::SetTooltip("%s\n%d skins", wd.name, wd.skinCount);
                    }
                }

                ImGui::PopID();
                itemIdx++;
                wIdx++;
            }
        } else {
            // =================================================================
            // Skin Grid — show skins for the selected weapon
            // =================================================================
            int realWeaponIndex = -1;
            {
                int wIdx = 0;
                for (int w = 0; w < SkinDB::WEAPON_COUNT; w++) {
                    if (SkinDB::g_weapons[w].category != static_cast<SkinDB::Category>(g_selectedCategory))
                        continue;
                    if (wIdx == g_selectedWeaponIdx) {
                        realWeaponIndex = w;
                        break;
                    }
                    wIdx++;
                }
            }

            if (realWeaponIndex >= 0) {
                const auto& weapon = SkinDB::g_weapons[realWeaponIndex];

                // Current active config
                int activePaintKit = 0;
                {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    auto it = g_skinConfigs.find(weapon.defIndex);
                    if (it != g_skinConfigs.end() && it->second.enabled)
                        activePaintKit = it->second.paintKit;
                }

                // Remove skin button
                if (activePaintKit > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.18f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.25f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Remove Active Skin", ImVec2(-1, 28))) {
                        std::lock_guard<std::mutex> lock(g_configMutex);
                        g_skinConfigs.erase(weapon.defIndex);
                        g_statusMessage = std::string("Removed skin from ") + weapon.name;
                        activePaintKit = 0;
                    }
                    ImGui::PopStyleColor(3);
                }

                // Wear / Seed / StatTrak controls
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.4f);
                ImGui::SliderFloat("Wear", &g_editWear, 0.000001f, 1.0f, "%.6f");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(70);
                ImGui::InputInt("Seed", &g_editSeed, 1, 10);
                if (g_editSeed < 1) g_editSeed = 1;
                if (g_editSeed > 1000) g_editSeed = 1000;
                ImGui::SameLine();
                ImGui::Checkbox("ST", &g_editStatTrak);

                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                // Skin cards
                float regionW = ImGui::GetContentRegionAvail().x;
                int cols = (int)(regionW / (CARD_W + CARD_PAD));
                if (cols < 1) cols = 1;

                for (int s = 0; s < weapon.skinCount; s++) {
                    const auto& skin = weapon.skins[s];
                    bool isActive = (skin.paintKit == activePaintKit);
                    ImVec4 rarityCol = GetRarityColor(skin.rarity);

                    ImGui::PushID(s);
                    if (s % cols != 0) ImGui::SameLine(0, CARD_PAD);

                    if (DrawCard(skin.name,
                                 skin.hasStatTrak ? "StatTrak" : "",
                                 skin.imageUrl, nullptr, rarityCol, isActive)) {
                        // Apply this skin
                        SkinConfig cfg;
                        cfg.paintKit = skin.paintKit;
                        cfg.wear = g_editWear;
                        cfg.seed = g_editSeed;
                        cfg.statTrak = g_editStatTrak;
                        cfg.enabled = true;
                        { std::lock_guard<std::mutex> lock(g_configMutex); g_skinConfigs[weapon.defIndex] = cfg; }
                        g_statusMessage = std::string("Applied ") + skin.name + " to " + weapon.name;
                        g_statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                    }

                    // Tooltip
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n%s%s", skin.name,
                                          SkinDB::g_rarityColors[skin.rarity].name,
                                          skin.hasStatTrak ? "\nStatTrak Available" : "");
                    }

                    ImGui::PopID();
                }
            }
        }

        ImGui::EndChild();
    }

} // namespace InventoryUI
