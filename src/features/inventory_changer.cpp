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
#include <map>
#include <vector>
#include <mutex>
#include <atomic>
#include <sstream>
#include <windows.h>
#include <Psapi.h>
#include <cstdio>

namespace {

    // =========================================================================
    // Debug Console (matches Epstein pattern)
    // =========================================================================
    static bool g_consoleInit = false;
    static void EnsureConsole() {
        if (g_consoleInit) return;
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        g_consoleInit = true;
    }

    // =========================================================================
    // Direct Read/Write — exact clone of Epstein Game::Read / Game::Write
    // =========================================================================
    template<typename T>
    T GameRead(uintptr_t address) {
        if (!address) return T{};
        __try {
            return *reinterpret_cast<T*>(address);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return T{};
        }
    }

    template<typename T>
    void GameWrite(uintptr_t address, const T& value) {
        if (!address) return;
        __try {
            *reinterpret_cast<T*>(address) = value;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    // Safe string write for HUD name (char-by-char with SEH via GameWrite)
    static void GameWriteStr(uintptr_t address, const char* str, size_t maxLen) {
        if (!address || !str) return;
        size_t len = strlen(str);
        if (len > maxLen) len = maxLen;
        for (size_t i = 0; i < len; i++)
            GameWrite<char>(address + i, str[i]);
        GameWrite<char>(address + len, '\0');
    }

    // =========================================================================
    // Entity Offsets (from dumped schemas — values match Epstein hardcoded)
    // =========================================================================
    namespace Off {
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

        constexpr uintptr_t m_AttributeList = C_EconItemView::m_AttributeList;
        constexpr uintptr_t m_Attributes    = CAttributeList::m_Attributes;

        constexpr uintptr_t m_pClippingWeapon = C_CSPlayerPawn::m_pClippingWeapon;

        constexpr uintptr_t m_szCustomName = C_EconItemView::m_szCustomName; // char[161]
        constexpr uintptr_t m_szCustomNameOverride = C_EconItemView::m_szCustomNameOverride; // char[161]

        constexpr uintptr_t m_bNeedToReApplyGloves = C_CSPlayerPawn::m_bNeedToReApplyGloves;
        constexpr uintptr_t m_EconGloves           = C_CSPlayerPawn::m_EconGloves;
        constexpr uintptr_t m_hMyWearables         = C_BaseCombatCharacter::m_hMyWearables;
    }

    // =========================================================================
    // Skin Configuration — exact clone of Epstein SkinChanger::SkinConfig
    // =========================================================================
    struct SkinConfig {
        int paintKit    = 0;
        float wear      = 0.001f;
        int seed        = 0;
        int statTrak    = -1;   // -1 = off, >=0 = enabled with this value
        bool enabled    = false;
    };

    // --- Global State (exact Epstein layout) ---
    static std::map<int, SkinConfig> weaponSkins;
    static std::atomic<bool> forceUpdate{false};
    static std::mutex configMutex;
    static int tickCounter = 0;

    // Track what's been applied to avoid re-applying
    static uintptr_t lastAppliedWeapon = 0;
    static int lastAppliedKit = 0;

    // Debug counters (matching Epstein)
    static int applyCount = 0;
    static int ticksSinceApply = 0;

    // UI status
    static std::string g_statusMessage = "Idle";
    static uint64_t g_framesApplied = 0;
    static uint64_t g_weaponsPatched = 0;
    static std::string g_lastError = "none";
    static std::string g_lastHudName = "none";

    // =========================================================================
    // CEconItemAttribute / CPtrGameVector — exact Epstein structs
    // =========================================================================
    #pragma pack(push, 1)
    struct CEconItemAttribute {
        uintptr_t vtable;               // 0x00
        uintptr_t owner;                // 0x08
        char pad_0010[32];              // 0x10
        uint16_t defIndex;              // 0x30
        char pad_0032[2];               // 0x32
        float value;                    // 0x34
        float initValue;                // 0x38
        int32_t refundableCurrency;     // 0x3C
        bool setBonus;                  // 0x40
        char pad_0041[7];               // 0x41
    }; // 0x48

    struct CPtrGameVector {
        uint64_t size;
        uintptr_t ptr;
    };
    #pragma pack(pop)

    static CEconItemAttribute MakeAttribute(uint16_t def, float val) {
        CEconItemAttribute attr{};
        attr.defIndex = def;
        attr.value = val;
        attr.initValue = val;
        return attr;
    }

    // Static attribute buffer — allocated ONCE, reused every apply (exact Epstein)
    static CEconItemAttribute* g_attrBuffer = nullptr;

    // RegenerateWeaponSkins state
    static uintptr_t regenAddr = 0;
    static bool regenPatched = false;

    // Weapon constants
    enum WeaponDefIndex : uint16_t {
        WEAPON_KNIFE_CT = 42,
        WEAPON_KNIFE_T = 59,
    };

    // =========================================================================
    // SigScan — exact clone of Epstein Game::SigScan
    // =========================================================================
    static uintptr_t SigScan(uintptr_t moduleBase, size_t moduleSize, const char* pattern) {
        std::vector<std::pair<uint8_t, bool>> sig;
        const char* start = pattern;
        const char* end = start + strlen(pattern);

        while (start < end) {
            if (*start == '?') {
                start++;
                if (*start == '?') start++;
                sig.emplace_back(0, true);
            } else {
                sig.emplace_back(static_cast<uint8_t>(strtoul(start, const_cast<char**>(&start), 16)), false);
            }
            while (*start == ' ') start++;
        }

        size_t sigSize = sig.size();
        uint8_t* scanBytes = reinterpret_cast<uint8_t*>(moduleBase);

        for (size_t i = 0; i < moduleSize - sigSize; i++) {
            bool found = true;
            for (size_t j = 0; j < sigSize; j++) {
                if (!sig[j].second && scanBytes[i + j] != sig[j].first) {
                    found = false;
                    break;
                }
            }
            if (found) return moduleBase + i;
        }
        return 0;
    }

    // =========================================================================
    // CreateAttributes — exact Epstein clone
    // =========================================================================
    static void CreateAttributes(uintptr_t item, int paintKit, int seed, float wear) {
        if (paintKit <= 0) return;

        uintptr_t attrListAddr = item + Off::m_AttributeList + Off::m_Attributes;
        CPtrGameVector existing = GameRead<CPtrGameVector>(attrListAddr);
        if (existing.size > 0 || existing.ptr != 0) return;

        if (!g_attrBuffer) {
            g_attrBuffer = reinterpret_cast<CEconItemAttribute*>(
                VirtualAlloc(nullptr, sizeof(CEconItemAttribute) * 3,
                    MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!g_attrBuffer) return;
            printf("[DBG] Weapon attr buffer at 0x%llX\n", (uintptr_t)g_attrBuffer);
        }

        g_attrBuffer[0] = MakeAttribute(6, static_cast<float>(paintKit));
        g_attrBuffer[1] = MakeAttribute(7, static_cast<float>(seed));
        g_attrBuffer[2] = MakeAttribute(8, wear);

        CPtrGameVector newList;
        newList.size = 3;
        newList.ptr = reinterpret_cast<uintptr_t>(g_attrBuffer);
        GameWrite<CPtrGameVector>(attrListAddr, newList);
    }

    // =========================================================================
    // RemoveAttributes — exact Epstein clone
    // =========================================================================
    static void RemoveAttributes(uintptr_t item) {
        uintptr_t attrListAddr = item + Off::m_AttributeList + Off::m_Attributes;
        CPtrGameVector existing = GameRead<CPtrGameVector>(attrListAddr);
        if (existing.size == 0) return;
        CPtrGameVector empty{};
        GameWrite<CPtrGameVector>(attrListAddr, empty);
    }

    // =========================================================================
    // InitRegen — exact Epstein clone
    // =========================================================================
    static void ResetRegenState() {
        regenAddr = 0;
        regenPatched = false;
    }

    static void InitRegen() {
        if (regenAddr != 0) return;

        HMODULE clientModule = GetModuleHandleW(L"client.dll");
        if (!clientModule) return;
        MODULEINFO modInfo{};
        if (!GetModuleInformation(GetCurrentProcess(), clientModule, &modInfo, sizeof(modInfo))) return;

        regenAddr = SigScan(
            reinterpret_cast<uintptr_t>(clientModule), modInfo.SizeOfImage,
            "48 83 EC ? E8 ? ? ? ? 48 85 C0 0F 84 ? ? ? ? 48 8B 10"
        );

        if (regenAddr) {
            printf("[+] RegenerateWeaponSkins at 0x%llX\n", regenAddr);

            uint16_t combinedOffset = static_cast<uint16_t>(
                Off::m_AttributeManager + Off::m_Item +
                Off::m_AttributeList + Off::m_Attributes
            );

            DWORD oldProtect;
            if (VirtualProtect(reinterpret_cast<void*>(regenAddr + 0x52), 2, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                *reinterpret_cast<uint16_t*>(regenAddr + 0x52) = combinedOffset;
                VirtualProtect(reinterpret_cast<void*>(regenAddr + 0x52), 2, oldProtect, &oldProtect);
                regenPatched = true;
                printf("[+] Patched +0x52 = 0x%X (VirtualProtect OK)\n", combinedOffset);
            } else {
                printf("[-] VirtualProtect FAILED\n");
            }
        } else {
            printf("[-] RegenerateWeaponSkins NOT FOUND\n");
        }
    }

    // =========================================================================
    // CallRegen — exact Epstein clone (direct call on game thread)
    // =========================================================================
    static void CallRegen() {
        if (!regenAddr || !regenPatched) return;

        printf("[DBG] CallRegen: direct call on game thread at 0x%llX\n", regenAddr);
        __try {
            typedef void(__fastcall* RegenFn)();
            auto fn = reinterpret_cast<RegenFn>(regenAddr);
            fn();
            printf("[DBG] Regen call returned OK\n");
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("[!] RegenerateWeaponSkins CRASHED (caught by SEH)\n");
        }
    }

    // =========================================================================
    // ApplyAndRegen — exact Epstein clone
    // =========================================================================
    static void ApplyAndRegen(uintptr_t weapon, const SkinConfig& skin, uint16_t defIndex) {
        uintptr_t item = weapon + Off::m_AttributeManager + Off::m_Item;

        printf("[SKIN] === APPLY START === weapon=0x%llX def=%d kit=%d\n", weapon, defIndex, skin.paintKit);

        // Save original values so we can restore
        uint32_t origItemIDHigh = GameRead<uint32_t>(item + Off::m_iItemIDHigh);

        GameWrite<uint32_t>(item + Off::m_iItemIDHigh, 0xFFFFFFFF);

        GameWrite<int32_t>(weapon + Off::m_nFallbackPaintKit, skin.paintKit);
        GameWrite<float>(weapon + Off::m_flFallbackWear, skin.wear);
        GameWrite<int32_t>(weapon + Off::m_nFallbackSeed, skin.seed);
        GameWrite<int32_t>(weapon + Off::m_nFallbackStatTrak, skin.statTrak);

        CreateAttributes(item, skin.paintKit, skin.seed, skin.wear);
        CallRegen();

        // Remove attributes after regen (buffer stays allocated)
        RemoveAttributes(item);

        // Full reset — make weapon look untouched
        GameWrite<uint32_t>(item + Off::m_iItemIDHigh, origItemIDHigh);
        GameWrite<int32_t>(weapon + Off::m_nFallbackPaintKit, 0);
        GameWrite<float>(weapon + Off::m_flFallbackWear, 0.0f);
        GameWrite<int32_t>(weapon + Off::m_nFallbackSeed, 0);
        GameWrite<int32_t>(weapon + Off::m_nFallbackStatTrak, -1);
        printf("[SKIN] === APPLY DONE (weapon reset to clean state) ===\n");
    }

    // =========================================================================
    // HUD Name — write skin name into m_szCustomName so HUD shows it
    // =========================================================================
    static void WriteHudName(uintptr_t item, uint16_t defIndex, const SkinConfig& cfg) {
        const char* skinName = nullptr;
        for (int w = 0; w < SkinDB::WEAPON_COUNT; w++) {
            if (SkinDB::g_weapons[w].defIndex == defIndex) {
                for (int s = 0; s < SkinDB::g_weapons[w].skinCount; s++) {
                    if (SkinDB::g_weapons[w].skins[s].paintKit == cfg.paintKit) {
                        skinName = SkinDB::g_weapons[w].skins[s].name;
                        break;
                    }
                }
                break;
            }
        }
        if (!skinName) {
            g_lastHudName = "lookup_failed(def=" + std::to_string(defIndex) + " kit=" + std::to_string(cfg.paintKit) + ")";
            printf("[HUD] Name lookup FAILED: def=%d kit=%d\n", defIndex, cfg.paintKit);
            return;
        }
        const char* weaponName = nullptr;
        for (int w2 = 0; w2 < SkinDB::WEAPON_COUNT; w2++) {
            if (SkinDB::g_weapons[w2].defIndex == defIndex) {
                weaponName = SkinDB::g_weapons[w2].name;
                break;
            }
        }
        if (!weaponName) weaponName = "Unknown";
        char fullName[161];
        if (cfg.statTrak >= 0)
            snprintf(fullName, sizeof(fullName), "StatTrak\xe2\x84\xa2 %s | %s", weaponName, skinName);
        else
            snprintf(fullName, sizeof(fullName), "%s | %s", weaponName, skinName);
        GameWriteStr(item + Off::m_szCustomName, fullName, 160);
        GameWriteStr(item + Off::m_szCustomNameOverride, fullName, 160);
        // Mark item as "custom" (non-zero, non-FFFFFFFF) so game shows the nametag
        // Value 1 = custom item without triggering fallback paint kit resolution
        uint32_t curIDHigh = GameRead<uint32_t>(item + Off::m_iItemIDHigh);
        if (curIDHigh == 0) {
            GameWrite<uint32_t>(item + Off::m_iItemIDHigh, 1);
        }
        // Log on first write or when name changes
        if (g_lastHudName != fullName) {
            printf("[HUD] Wrote name: %s (def=%d kit=%d) itemIDHigh=%u -> item+0x%X & +0x%X\n",
                   fullName, defIndex, cfg.paintKit, curIDHigh,
                   (unsigned)Off::m_szCustomName, (unsigned)Off::m_szCustomNameOverride);
        }
        g_lastHudName = fullName;
    }

    static void ClearHudName(uintptr_t item) {
        GameWrite<char>(item + Off::m_szCustomName, '\0');
        GameWrite<char>(item + Off::m_szCustomNameOverride, '\0');
    }

    // =========================================================================
    // UI State
    // =========================================================================
    static int g_selectedCategory = 0;
    static int g_selectedWeaponIdx = -1;
    static float g_editWear = 0.001f;
    static int   g_editSeed = 0;
    static bool  g_editStatTrak = false;

} // anonymous namespace

// =========================================================================
// Public API
// =========================================================================
namespace InventoryUI {

    // =========================================================================
    // TickInner — exact clone of Epstein SkinChanger::TickInner
    // =========================================================================
    static void TickInner() {
        EnsureConsole();

        tickCounter++;
        ticksSinceApply++;

        uintptr_t clientBase = Memory::GetModuleBase("client.dll");
        if (!clientBase) return;

        // ===== Resolve local pawn via Controller → EntityList (same as ESP for other players) =====
        uintptr_t entityList = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwEntityList, entityList) ||
            !Memory::IsValidPtr(entityList)) return;

        uintptr_t localController = 0;
        if (!Memory::SafeRead(clientBase + cs2_dumper::offsets::client_dll::dwLocalPlayerController, localController) ||
            !Memory::IsValidPtr(localController)) return;

        uint32_t pawnHandle = 0;
        if (!Memory::SafeRead(localController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn, pawnHandle) ||
            !pawnHandle) return;

        uintptr_t pawnEntry = 0;
        if (!Memory::SafeRead(entityList + Constants::EntityList::OFFSET_BASE +
            sizeof(uintptr_t) * ((pawnHandle & Constants::EntityList::HANDLE_MASK) >>
            Constants::EntityList::ENTRY_SHIFT), pawnEntry) ||
            !Memory::IsValidPtr(pawnEntry)) return;

        uintptr_t localPawn = 0;
        if (!Memory::SafeRead(pawnEntry + Constants::EntityList::ENTRY_SIZE * (pawnHandle & Constants::EntityList::INDEX_MASK),
            localPawn) || !Memory::IsValidPtr(localPawn)) {
            if (lastAppliedWeapon != 0)
                printf("[DBG] Pawn gone, resetting tracking\n");
            lastAppliedWeapon = 0;
            lastAppliedKit = 0;
            return;
        }

        // Check if player is ALIVE
        uint8_t lifeState = 255;
        int32_t health = 0;
        Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, lifeState);
        Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health);

        // Detailed periodic logging
        if (tickCounter % 200 == 1)
            printf("[DBG] tick=%d base=0x%llX ctrl=0x%llX pawn=0x%llX life=%d hp=%d wep=0x%llX kit=%d\n",
                tickCounter, clientBase, localController, localPawn, lifeState, health, lastAppliedWeapon, lastAppliedKit);

        if (lifeState != 0 || health <= 0) {
            if (lastAppliedWeapon != 0)
                printf("[DBG] DEAD: lifeState=%d hp=%d — resetting tracking\n", lifeState, health);
            lastAppliedWeapon = 0;
            lastAppliedKit = 0;
            return;
        }

        InitRegen();

        bool force = forceUpdate.load();

        std::lock_guard<std::mutex> lock(configMutex);

        // ===== WEAPON SKINS (guns only, no knives/gloves) =====
        uintptr_t activeWeapon = 0;
        Memory::SafeRead(localPawn + Off::m_pClippingWeapon, activeWeapon);
        if (activeWeapon && Memory::IsValidPtr(activeWeapon)) {
            uintptr_t item = activeWeapon + Off::m_AttributeManager + Off::m_Item;
            uint16_t defIndex = 0;
            Memory::SafeRead(item + Off::m_iItemDefinitionIndex, defIndex);

            // Only process actual guns (defIndex 1-69, skip knife 31)
            bool isWeapon = (defIndex > 0 && defIndex < 70);
            if (isWeapon && defIndex != 31 && defIndex != WEAPON_KNIFE_CT && defIndex != WEAPON_KNIFE_T) {
                auto it = weaponSkins.find(defIndex);
                if (it != weaponSkins.end() && it->second.enabled && it->second.paintKit > 0) {
                    const SkinConfig& skin = it->second;
                    bool needsApply = force
                        || (activeWeapon != lastAppliedWeapon)
                        || (skin.paintKit != lastAppliedKit);

                    if (needsApply) {
                        Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_lifeState, lifeState);
                        Memory::SafeRead(localPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth, health);
                        if (lifeState == 0 && health > 0) {
                            applyCount++;
                            printf("[DBG] Apply #%d: life=%d hp=%d weapon=0x%llX def=%d kit=%d\n",
                                applyCount, lifeState, health, activeWeapon, defIndex, skin.paintKit);

                            GameWrite<uint32_t>(item + Off::m_iItemIDHigh, 0);
                            ApplyAndRegen(activeWeapon, skin, defIndex);
                            lastAppliedWeapon = activeWeapon;
                            lastAppliedKit = skin.paintKit;
                            ticksSinceApply = 0;
                            g_weaponsPatched++;
                        } else {
                            printf("[DBG] Skipped apply — died between checks! life=%d hp=%d\n", lifeState, health);
                        }
                    }
                    // Write HUD name every tick to keep it in sync
                    WriteHudName(item, defIndex, skin);
                }
            }
        }

        if (force) forceUpdate.store(false);
        g_framesApplied++;
    }

    // SEH wrapper — exact clone of Epstein SkinChanger::Tick
    void Run() {
        __try {
            TickInner();
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            printf("[!] SEH CAUGHT crash in Tick! apply#=%d ticksSince=%d\n", applyCount, ticksSinceApply);
            lastAppliedWeapon = 0;
            lastAppliedKit = 0;
            ResetRegenState();
        }
    }

    void ResetRegen() {
        ResetRegenState();
        lastAppliedWeapon = 0;
        lastAppliedKit = 0;
        printf("[SKIN] Regen state reset (map/game transition)\n");
    }

    std::vector<std::string> GetInventoryDebugLines() {
        std::vector<std::string> lines;
        lines.push_back(std::string("mode: weapon_skin_changer"));
        lines.push_back(std::string("frames_applied: ") + std::to_string(g_framesApplied));
        lines.push_back(std::string("weapons_patched: ") + std::to_string(g_weaponsPatched));
        lines.push_back(std::string("last_error: ") + g_lastError);
        lines.push_back(std::string("regen_addr: ") + (regenAddr ? "found" : "not_found"));
        lines.push_back(std::string("regen_patched: ") + (regenPatched ? "yes" : "no"));
        { std::lock_guard<std::mutex> lock(configMutex); lines.push_back(std::string("active_configs: ") + std::to_string(weaponSkins.size())); }
        lines.push_back(std::string("status: ") + g_statusMessage);
        lines.push_back(std::string("hud_name: ") + g_lastHudName);
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
        std::lock_guard<std::mutex> lock(configMutex);
        auto it = weaponSkins.find(defIndex);
        return it != weaponSkins.end() && it->second.enabled;
    }

    static std::string GetActiveSkinName(uint16_t defIndex) {
        std::lock_guard<std::mutex> lock(configMutex);
        auto it = weaponSkins.find(defIndex);
        if (it == weaponSkins.end() || !it->second.enabled) return "";
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
                // Skip knives and gloves
                if (c == static_cast<int>(SkinDB::CAT_KNIVES) || c == static_cast<int>(SkinDB::CAT_GLOVES))
                    continue;
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
                    std::lock_guard<std::mutex> lock(configMutex);
                    auto it = weaponSkins.find(weapon.defIndex);
                    if (it != weaponSkins.end() && it->second.enabled)
                        activePaintKit = it->second.paintKit;
                }

                // Remove skin button
                if (activePaintKit > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.18f, 0.18f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.25f, 0.25f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.15f, 0.15f, 1.0f));
                    if (ImGui::Button("Remove Active Skin", ImVec2(-1, 28))) {
                        std::lock_guard<std::mutex> lock(configMutex);
                        weaponSkins.erase(weapon.defIndex);
                        forceUpdate.store(true);
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
                        // Apply this skin (matching Epstein SkinConfig)
                        SkinConfig cfg;
                        cfg.paintKit = skin.paintKit;
                        cfg.wear = g_editWear;
                        cfg.seed = g_editSeed;
                        cfg.statTrak = g_editStatTrak ? 0 : -1;
                        cfg.enabled = true;
                        { std::lock_guard<std::mutex> lock(configMutex); weaponSkins[weapon.defIndex] = cfg; }
                        forceUpdate.store(true);
                        g_statusMessage = std::string("Applied ") + skin.name + " to " + weapon.name;
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
