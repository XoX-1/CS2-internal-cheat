#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace InventoryUI {
    // Called every frame from the cheat thread to apply skins to weapons
    void Run();
    
    // Reset cached regen state (call on map/game transitions)
    void ResetRegen();
    
    // Renders the main content for the Inventory Changer tab
    void RenderInventoryChangerTab();


    // --- Inventory Changer API (per guide implementation) ---
    
    // Fill a CEconItemView structure with proper fields
    // defIndex: Item definition index (e.g., 7 for AK-47)
    // accountID: Steam account ID of the player
    // quality: Item quality (4 = Unique, 3 = Genuine, etc.)
    // level: Item level (usually 1)
    void FillEconItemView(uintptr_t itemPtr, uint16_t defIndex, uint32_t accountID, 
                          uint8_t quality = 4, uint8_t level = 1);
    
    // Clear/reset a CEconItemView to default state
    void ClearEconItemView(uintptr_t itemPtr);
    
    // Apply cosmetic to weapon via CAttributeContainer::m_Item
    void ApplyWeaponCosmetic(uintptr_t weapon, uint16_t defIndex, int paintKit, float wear, int seed);
    
    // Write item to networked loadout (m_vecNetworkableLoadout)
    bool WriteToLoadout(uintptr_t invServices, uintptr_t itemPtr, uint16_t team, uint16_t loadoutSlot);
    
    // Get inventory services pointer from local controller
    uintptr_t GetInventoryServices(uintptr_t clientBase);
    
    // Get local player's account ID
    uint32_t GetLocalAccountID(uintptr_t clientBase);

    // -------------------------------------------------------------------------
    // Config persistence API
    // Used by ConfigManager to save/load skin selections.
    // -------------------------------------------------------------------------
    struct SkinEntry {
        int   defIndex  = 0;
        int   paintKit  = 0;
        float wear      = 0.001f;
        int   seed      = 0;
        int   statTrak  = -1;   // -1 = off
        bool  enabled   = true;
    };

    // Returns a snapshot of all currently configured skins (enabled only).
    std::vector<SkinEntry> GetAllSkinConfigs();

    // Replaces the entire skin map with the provided entries and triggers
    // a forced re-apply on the next tick.
    void SetAllSkinConfigs(const std::vector<SkinEntry>& entries);
}
