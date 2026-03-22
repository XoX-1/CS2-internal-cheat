#pragma once

#include <string>
#include <vector>

namespace SkyChanger {
    // Called every frame from the cheat thread to apply sky overrides
    void Run();

    // Reset cached state on map/game transitions
    void Reset();

    // Renders the main content for the Sky Changer tab (ImGui)
    void RenderSkyChangerTab();

    void RenderWeatherOverlay();
}
