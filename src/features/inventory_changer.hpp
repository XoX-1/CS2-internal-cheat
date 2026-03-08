#pragma once

#include <string>
#include <vector>

namespace InventoryUI {
    // Called every frame from the cheat thread to apply skins to weapons
    void Run();
    
    // Renders the main content for the Inventory Changer tab
    void RenderInventoryChangerTab();

    // Real-time diagnostics used by Misc tab debug panel.
    std::vector<std::string> GetInventoryDebugLines();


}
