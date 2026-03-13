#include "hooks.hpp"
#include "../features/esp.hpp"
#include "../features/aimbot.hpp"
#include "../features/player_fov.hpp"
#include "../features/spectator_list.hpp"
#include "../features/triggerbot.hpp"
#include "../features/radar.hpp"
#include "../features/bhop.hpp"
#include "../features/killsound.hpp"
#include "../features/keybind_manager.hpp"
#include <iostream>
#include <cmath>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace DX11Hook {
    typedef HRESULT(__stdcall* Present_t)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);
    typedef HRESULT(__stdcall* ResizeBuffers_t)(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
    typedef LRESULT(__stdcall* WndProc_t)(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    Present_t oPresent = nullptr;
    ResizeBuffers_t oResizeBuffers = nullptr;
    WndProc_t oWndProc = nullptr;

    ID3D11Device* g_pDevice = nullptr;
    ID3D11DeviceContext* g_pContext = nullptr;
    ID3D11RenderTargetView* g_pRenderTargetView = nullptr;
    HWND g_hWnd = nullptr;

    bool g_bInitialized = false;
    bool g_bMenuOpen = true;
    bool g_bStartupScreen = false;  // Disabled - go straight to menu

    // Aimbot Prediction
    bool g_bAimbotPrediction = false;
    float g_fAimbotPredictStrength = 1.0f;

    LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_KEYDOWN && wParam == VK_INSERT) {
            g_bMenuOpen = !g_bMenuOpen;
            return 0;
        }

        // Handle keybind listening - consume input when setting keys
        if (KeybindManager::g_bListeningForAimbotKey.load() || KeybindManager::g_bListeningForTriggerbotKey.load()) {
            if (KeybindManager::ProcessInputForKeybind(msg, wParam, lParam)) {
                return 0; // Input was consumed for keybind setting
            }
        }

        if (g_bMenuOpen) {
            if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) {
                return 0;
            }
        }

        return CallWindowProcA(oWndProc, hWnd, msg, wParam, lParam);
    }

    // =====================================================================
    // Premium GUI — Accent Colors & Helpers
    // =====================================================================

    #include "../vendor/imgui/IconsFontAwesome6.h"
    #include "../src/fonts/roboto_medium.h"
    #include "../src/fonts/fa_solid_900.h"

    static float g_animTime = 0.0f;
    static float g_tabAnim[5] = { 1.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    static float g_bgParticlePhase = 0.0f;

    // Premium Cyberpunk-Purple Color Palette
    static constexpr ImVec4 kAccent       = ImVec4(0.60f, 0.35f, 0.95f, 1.00f);
    static constexpr ImVec4 kAccentGlow   = ImVec4(0.60f, 0.35f, 0.95f, 0.40f);
    static constexpr ImVec4 kAccentDim    = ImVec4(0.40f, 0.25f, 0.70f, 1.00f);
    static constexpr ImVec4 kTextBright   = ImVec4(0.98f, 0.98f, 1.00f, 1.00f);
    static constexpr ImVec4 kTextDim      = ImVec4(0.55f, 0.55f, 0.62f, 1.00f);
    static constexpr ImVec4 kBgMain       = ImVec4(0.04f, 0.04f, 0.06f, 0.98f);
    static constexpr ImVec4 kBgPanel      = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
    static constexpr ImVec4 kBgWidget     = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
    static constexpr ImVec4 kGlassOverlay = ImVec4(0.08f, 0.08f, 0.12f, 0.60f);

    static ImU32 ToU32(ImVec4 c) {
        return IM_COL32((int)(c.x * 255), (int)(c.y * 255), (int)(c.z * 255), (int)(c.w * 255));
    }

    static ImVec4 Lerp4(ImVec4 a, ImVec4 b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t);
    }

    // Custom toggle switch for regular bool
    static bool ToggleSwitch(const char* label, bool* v) {
        ImGui::PushID(label);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        float height = ImGui::GetFrameHeight() * 0.65f;
        float width = height * 1.8f;
        float radius = height * 0.45f;

        ImGui::InvisibleButton(label, ImVec2(width + ImGui::CalcTextSize(label).x + 10, height));
        bool pressed = ImGui::IsItemClicked();
        if (pressed) *v = !*v;

        float t = *v ? 1.0f : 0.0f;
        // Make the switch background slightly lighter when off so it's less harsh
        ImU32 bgCol = *v ? ToU32(kAccent) : IM_COL32(45, 45, 55, 255);

        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), bgCol, height * 0.5f);

        if (*v) {
            dl->AddRectFilled(ImVec2(p.x - 2, p.y - 2), ImVec2(p.x + width + 2, p.y + height + 2),
                              ToU32(kAccentGlow), height * 0.5f + 2);
        }

        float knobX = p.x + radius + t * (width - 2 * radius);
        dl->AddCircleFilled(ImVec2(knobX, p.y + height * 0.5f), radius - 1.5f, IM_COL32(240, 240, 245, 255));

        dl->AddText(ImVec2(p.x + width + 10, p.y + (height - ImGui::GetTextLineHeight()) * 0.5f),
                    ToU32(*v ? kTextBright : kTextDim), label);

        ImGui::PopID();
        return pressed;
    }

    // Overload for atomic bool
    static bool ToggleSwitch(const char* label, std::atomic<bool>* v) {
        bool value = v->load();
        bool pressed = ToggleSwitch(label, &value);
        if (pressed) {
            v->store(value);
        }
        return pressed;
    }

    // Section header with accent bar
    static void SectionHeader(const char* label) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();

        dl->AddRectFilled(p, ImVec2(p.x + 3, p.y + ImGui::GetTextLineHeight()), ToU32(kAccent), 1.0f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 12);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBright);
        ImGui::Text("%s", label);
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::Spacing(); // extra padding under header
    }

    // Styled slider for regular float
    static bool StyledSlider(const char* label, float* v, float vmin, float vmax, const char* fmt = "%.1f") {
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, kAccent);
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.55f, 0.40f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.18f, 0.22f, 1.0f));
        bool changed = ImGui::SliderFloat(label, v, vmin, vmax, fmt);
        ImGui::PopStyleColor(5);
        return changed;
    }

    // Overload for atomic float
    static bool StyledSlider(const char* label, std::atomic<float>* v, float vmin, float vmax, const char* fmt = "%.1f") {
        float value = v->load();
        bool changed = StyledSlider(label, &value, vmin, vmax, fmt);
        if (changed) {
            v->store(value);
        }
        return changed;
    }

    // Styled combo for regular int
    static bool StyledCombo(const char* label, int* idx, const char* const items[], int count) {
        ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.15f, 0.15f, 0.19f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.10f, 0.10f, 0.13f, 0.98f));
        bool changed = ImGui::Combo(label, idx, items, count);
        ImGui::PopStyleColor(3);
        return changed;
    }

    // Overload for atomic int
    static bool StyledCombo(const char* label, std::atomic<int>* idx, const char* const items[], int count) {
        int value = idx->load();
        bool changed = StyledCombo(label, &value, items, count);
        if (changed) {
            idx->store(value);
        }
        return changed;
    }

    // =====================================================================
    // Keybind Button Helper
    // =====================================================================
    
    static bool KeybindButton(const char* label, int* keyCode, bool isListening, float width = 120.0f) {
        ImGui::PushID(label);
        
        std::string keyName = KeybindManager::GetKeyName(*keyCode);
        std::string buttonText = isListening ? "Press any key..." : keyName;
        
        // Style based on state
        if (isListening) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.60f, 0.35f, 0.95f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.45f, 1.00f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.50f, 0.30f, 0.85f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, kBgWidget);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.18f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.22f, 0.22f, 0.30f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, kTextBright);
        }
        
        bool clicked = ImGui::Button(buttonText.c_str(), ImVec2(width, 0));
        
        ImGui::PopStyleColor(4);
        ImGui::PopID();
        
        return clicked;
    }

    // =====================================================================
    // RenderStartupScreen — Animated startup with big "Mindcheat" title
    // =====================================================================
    
    void RenderStartupScreen() {
        g_animTime += ImGui::GetIO().DeltaTime;
        
        static float buttonAnim = 0.0f;
        static bool buttonHovered = false;
        
        // Center the window
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 center(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f);
        
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_Always);
        
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 20.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.05f, 0.05f, 0.08f, 0.98f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.3f, 0.2f, 0.5f, 0.5f));
        
        ImGui::Begin("##startup", nullptr, 
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | 
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar);
        
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        
        // Animated background gradient
        float gradientPhase = g_animTime * 0.3f;
        ImVec2 mousePos = io.MousePos;
        
        // Background glow effect
        ImVec2 glowCenter = ImVec2(winPos.x + winSize.x * 0.5f, winPos.y + winSize.y * 0.3f);
        float glowRadius = 250.0f + sinf(g_animTime * 0.8f) * 30.0f;
        dl->AddCircleFilled(glowCenter, glowRadius, IM_COL32(80, 50, 150, 30), 64);
        dl->AddCircleFilled(glowCenter, glowRadius * 0.6f, IM_COL32(100, 60, 180, 40), 64);
        
        // Big animated title
        float titleScale = 1.0f + sinf(g_animTime * 2.0f) * 0.03f;
        
        // Title shadow
        ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - 120 * titleScale, 60));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.1f, 0.08f, 0.15f, 1.0f));
        ImGui::SetWindowFontScale(3.0f * titleScale);
        ImGui::Text("MINDCHEAT");
        ImGui::PopStyleColor();
        
        // Main title
        ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - 122 * titleScale, 58));
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::SetWindowFontScale(3.0f * titleScale);
        ImGui::Text("MINDCHEAT");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);
        
        // Subtitle
        ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - 50, 115));
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("Premium CS2 Cheat");
        ImGui::PopStyleColor();
        
        // Animated line under title
        float lineWidth = 200.0f + sinf(g_animTime * 1.5f) * 30.0f;
        float lineX = winSize.x * 0.5f - lineWidth * 0.5f;
        dl->AddLine(ImVec2(lineX, 145), ImVec2(lineX + lineWidth, 145), 
                   IM_COL32(112, 80, 219, 150), 2.0f);
        
        // Version info
        ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - 25, 160));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.4f, 0.4f, 0.45f, 1.0f));
        ImGui::Text("v1.0");
        ImGui::PopStyleColor();
        
        // Animated Start Button - Using ImGui Button for reliability
        float buttonWidth = 200.0f;
        float buttonHeight = 55.0f;
        
        // Center the button
        ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - buttonWidth * 0.5f, winSize.y - 120.0f));
        
        // Style the button
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 15.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 15));
        
        // Animated button colors
        float pulse = (sinf(g_animTime * 3.0f) + 1.0f) * 0.5f; // 0 to 1
        ImVec4 buttonColor = ImVec4(0.35f + pulse * 0.1f, 0.25f + pulse * 0.08f, 0.55f + pulse * 0.12f, 1.0f);
        ImVec4 buttonHover = ImVec4(0.50f, 0.35f, 0.80f, 1.0f);
        ImVec4 buttonActive = ImVec4(0.60f, 0.45f, 0.90f, 1.0f);
        
        ImGui::PushStyleColor(ImGuiCol_Button, buttonColor);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, buttonHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, buttonActive);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        
        // Big bold START button - use window font scale
        ImGui::SetWindowFontScale(1.5f);
        
        if (ImGui::Button("START", ImVec2(buttonWidth, buttonHeight))) {
            g_bStartupScreen = false;
            g_bMenuOpen = true;
        }
        
        ImGui::SetWindowFontScale(1.0f);
        
        // Pop styles
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        
        // Add glow effect around button
        ImVec2 buttonMin = ImGui::GetItemRectMin();
        ImVec2 buttonMax = ImGui::GetItemRectMax();
        bool isHovered = ImGui::IsItemHovered();
        
        if (isHovered || pulse > 0.7f) {
            float glowAlpha = isHovered ? 100.0f : pulse * 60.0f;
            dl->AddRect(buttonMin, buttonMax, IM_COL32(150, 100, 255, (int)glowAlpha), 
                       15.0f, ImDrawFlags_RoundCornersAll, 3.0f);
            dl->AddRect(buttonMin, buttonMax, IM_COL32(150, 100, 255, (int)(glowAlpha * 0.5f)), 
                       15.0f, ImDrawFlags_RoundCornersAll, 6.0f);
        }
        
        // Footer hint
        ImGui::SetCursorPos(ImVec2(winSize.x * 0.5f - 60, winSize.y - 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.35f, 0.4f, 1.0f));
        ImGui::Text("Press INSERT to toggle");
        ImGui::PopStyleColor();
        
        ImGui::End();
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(2);
    }
    
    // =====================================================================
    // RenderMenu — Premium Level-100 GUI
    // =====================================================================

    void RenderMenu() {
        g_animTime += ImGui::GetIO().DeltaTime;
        g_bgParticlePhase += ImGui::GetIO().DeltaTime * 0.5f;

        static int activeTab = 0;
        for (int i = 0; i < 5; i++) {
            float target = (i == activeTab) ? 1.0f : 0.0f;
            g_tabAnim[i] += (target - g_tabAnim[i]) * ImGui::GetIO().DeltaTime * 12.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(720, 560), ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgMain);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.25f, 0.55f, 0.50f));

        ImGui::Begin("##mindcheat_main", &g_bMenuOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        // ============ BACKGROUND PARTICLE EFFECTS ============
        // Animated gradient orbs in background (softer, no hard edges)
        float time = g_animTime;
        for (int i = 0; i < 3; i++) {
            float phase = time * 0.2f + i * 2.0f;
            float x = winPos.x + winSize.x * (0.3f + 0.4f * sinf(phase * 0.5f));
            float y = winPos.y + winSize.y * (0.4f + 0.3f * sinf(phase * 0.3f + i));
            float radius = 120.0f + 30.0f * sinf(phase);
            int alpha = (int)(8 + 6 * sinf(phase * 1.5f));
            // Softer gradient with more segments
            dl->AddCircleFilled(ImVec2(x, y), radius, IM_COL32(90, 55, 160, alpha), 64);
        }

        // ============ HEADER ============
        float headerH = 70.0f;
        ImVec2 headerEnd = ImVec2(winPos.x + winSize.x, winPos.y + headerH);

        // Header with glassmorphism effect
        dl->AddRectFilled(winPos, headerEnd, IM_COL32(12, 10, 18, 240), 16.0f, ImDrawFlags_RoundCornersTop);
        dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + headerH * 0.7f), 
                         IM_COL32(20, 16, 30, 180), 16.0f, ImDrawFlags_RoundCornersTop);

        // Animated accent line with glow
        float pulseW = 180.0f + sinf(g_animTime * 2.0f) * 50.0f;
        float pulseX = winPos.x + (winSize.x - pulseW) * 0.5f + sinf(g_animTime * 0.6f) * 60.0f;
        
        // Outer glow
        dl->AddRectFilled(ImVec2(pulseX - 30, headerEnd.y - 8), ImVec2(pulseX + pulseW + 30, headerEnd.y + 6),
                          IM_COL32(140, 90, 240, 30), 4.0f);
        // Middle glow
        dl->AddRectFilled(ImVec2(pulseX - 15, headerEnd.y - 4), ImVec2(pulseX + pulseW + 15, headerEnd.y + 2),
                          IM_COL32(160, 100, 255, 60), 2.0f);
        // Core line
        dl->AddRectFilled(ImVec2(pulseX, headerEnd.y - 2), ImVec2(pulseX + pulseW, headerEnd.y),
                          ToU32(kAccent), 1.0f);

        // Title with glow effect
        ImGui::SetCursorPos(ImVec2(28, 22));
        ImGui::SetWindowFontScale(1.15f);
        
        // Title glow shadow
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.3f));
        ImGui::SetCursorPos(ImVec2(30, 24));
        ImGui::Text(ICON_FA_SHIELD_HALVED " MINDCHEAT");
        ImGui::PopStyleColor();
        
        // Main title
        ImGui::SetCursorPos(ImVec2(28, 22));
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::Text(ICON_FA_SHIELD_HALVED " MIND");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBright);
        ImGui::Text("CHEAT");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        // Version badge
        ImGui::SameLine(winSize.x - 100);
        ImGui::SetCursorPosY(24);
        ImGui::PushStyleColor(ImGuiCol_Text, kAccentDim);
        ImGui::Text("v1.0");
        ImGui::PopStyleColor();

        // ============ SIDEBAR + CONTENT ============
        float contentY = headerH;
        float sidebarWidth = 150.0f;
        
        ImVec2 sidebarStart = ImVec2(winPos.x, winPos.y + contentY);
        ImVec2 sidebarEnd = ImVec2(winPos.x + sidebarWidth, winPos.y + winSize.y - 28);
        
        // Glassmorphism sidebar background
        dl->AddRectFilled(sidebarStart, sidebarEnd, IM_COL32(10, 9, 14, 200), 16.0f, ImDrawFlags_RoundCornersBottomLeft);
        dl->AddRect(sidebarStart, sidebarEnd, IM_COL32(60, 45, 100, 80), 16.0f, ImDrawFlags_RoundCornersBottomLeft, 1.0f);

        // Sidebar Tabs
        const char* tabLabels[] = { "LEGIT", "ESP", "VISUALS", "MISC", "KILL SOUND", "SKINS" };
        const char* tabIcons[] = { ICON_FA_CROSSHAIRS, ICON_FA_EYE, ICON_FA_PALETTE, ICON_FA_GEARS, ICON_FA_MUSIC, ICON_FA_SHIRT };
        float tabH = 50.0f;
        float tabStartY = contentY + 15.0f;
        float tabAreaHeight = (winSize.y - 28.0f) - tabStartY - 10.0f;
        float requiredHeight = 6.0f * tabH;
        if (requiredHeight > tabAreaHeight && tabAreaHeight > 120.0f) {
            tabH = tabAreaHeight / 6.0f;
        }

        for (int i = 0; i < 6; i++) {
            ImVec2 tabStart = ImVec2(winPos.x + 8, winPos.y + tabStartY + i * tabH);
            ImVec2 tabEnd = ImVec2(tabStart.x + sidebarWidth - 16, tabStart.y + tabH);

            ImGui::SetCursorScreenPos(tabStart);
            char tabId[32];
            snprintf(tabId, sizeof(tabId), "##tab%d", i);
            ImGui::InvisibleButton(tabId, ImVec2(sidebarWidth - 16, tabH));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) activeTab = i;

            // Tab background with animation
            if (g_tabAnim[i] > 0.01f || hovered) {
                float alpha = g_tabAnim[i] * 180 + (hovered && i != activeTab ? 40 : 0);
                dl->AddRectFilled(tabStart, tabEnd, IM_COL32(80, 60, 140, (int)alpha), 10.0f);
                
                // Left accent glow line
                int glowAlpha = (int)(g_tabAnim[i] * 255);
                dl->AddRectFilled(ImVec2(tabStart.x, tabStart.y + 10),
                                  ImVec2(tabStart.x + 3, tabEnd.y - 10),
                                  IM_COL32(160, 100, 255, glowAlpha), 1.5f);
            }

            char fullLabel[64];
            snprintf(fullLabel, sizeof(fullLabel), "%s  %s", tabIcons[i], tabLabels[i]);
            
            float scale = 1.0f + (g_tabAnim[i] * 0.08f);
            ImVec2 baseTextSz = ImGui::CalcTextSize(fullLabel);
            ImVec2 textSz = ImVec2(baseTextSz.x * scale, baseTextSz.y * scale);
            
            ImVec4 textCol = Lerp4(kTextDim, kTextBright, g_tabAnim[i]);
            if (hovered && i != activeTab) textCol = ImVec4(0.85f, 0.85f, 0.90f, 1.0f);
            
            // Text shadow
            dl->AddText(NULL, ImGui::GetFontSize() * scale, 
                        ImVec2(tabStart.x + 16.0f + 1, tabStart.y + (tabH - textSz.y) * 0.5f + 1),
                        IM_COL32(0,0,0,100), fullLabel);
            // Main text
            dl->AddText(NULL, ImGui::GetFontSize() * scale, 
                        ImVec2(tabStart.x + 16.0f, tabStart.y + (tabH - textSz.y) * 0.5f),
                        ToU32(textCol), fullLabel);
        }

        // ============ CONTENT ============
        ImGui::SetCursorPos(ImVec2(sidebarWidth + 20, contentY + 20));

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kBgPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.20f, 0.40f, 0.50f));

        ImGui::BeginChild("##content", ImVec2(winSize.x - sidebarWidth - 40, winSize.y - contentY - 40 - 28), true);

        // ============ TAB 0: LEGIT (Aimbot + Triggerbot) ============
        if (activeTab == 0) {
            static int subTab = 0;
            bool aimbotSelected = (subTab == 0);
            bool triggerbotSelected = (subTab == 1);
            
            // Sub-tabs styling
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.16f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.22f, 0.45f, 1.0f));
            
            ImVec2 buttonSize = ImVec2(120, 30);
            
            if (aimbotSelected) {
                ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.16f, 0.25f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            }
            if (ImGui::Button("AIMBOT", buttonSize)) subTab = 0;
            if (aimbotSelected) ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            
            ImGui::SameLine();
            
            if (triggerbotSelected) {
                ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.16f, 0.25f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            }
            if (ImGui::Button("TRIGGERBOT", buttonSize)) subTab = 1;
            if (triggerbotSelected) ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            
            ImGui::PopStyleColor(3);
            
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            
            if (subTab == 0) {
                // AIMBOT Section
                SectionHeader("AIM ASSIST");
                ToggleSwitch("Enable", &Hooks::g_bAimbotEnabled);
                ToggleSwitch("Deathmatch Mode", &Hooks::g_bFFAEnabled);
                ImGui::Spacing();
                StyledSlider("FOV", &Hooks::g_fAimbotFov, 1.0f, 50.0f);
                StyledSlider("Smooth", &Hooks::g_fAimbotSmooth, 1.0f, 50.0f);
                
                ImGui::Spacing();
                const char* bones[] = { "Head", "Neck", "Chest", "Pelvis", "Stomach", "Left Shoulder", "Right Shoulder", "Left Hip", "Right Hip" };
                static int boneIdxAim = 0;
                if (StyledCombo("Target Bone", &boneIdxAim, bones, IM_ARRAYSIZE(bones))) {
                    switch (boneIdxAim) {
                        case 0: Hooks::g_nAimbotBone = 6; break;   // Head
                        case 1: Hooks::g_nAimbotBone = 5; break;   // Neck
                        case 2: Hooks::g_nAimbotBone = 4; break;   // Chest (Spine)
                        case 3: Hooks::g_nAimbotBone = 2; break;   // Pelvis
                        case 4: Hooks::g_nAimbotBone = 3; break;   // Stomach
                        case 5: Hooks::g_nAimbotBone = 8; break;   // Left Shoulder
                        case 6: Hooks::g_nAimbotBone = 13; break;  // Right Shoulder
                        case 7: Hooks::g_nAimbotBone = 22; break;  // Left Hip
                        case 8: Hooks::g_nAimbotBone = 25; break;  // Right Hip
                    }
                }
                
                // Aimbot Keybind
                ImGui::Spacing();
                SectionHeader("KEYBIND");
                ImGui::Text("Aimbot Key:");
                ImGui::SameLine();
                
                int aimbotKey = KeybindManager::g_nAimbotKey.load();
                bool listeningAimbot = KeybindManager::g_bListeningForAimbotKey.load();
                
                if (KeybindButton("##aimbot_key", &aimbotKey, listeningAimbot)) {
                    if (listeningAimbot) {
                        KeybindManager::StopListening();
                    } else {
                        KeybindManager::StartListeningForAimbotKey();
                    }
                }
                
                ImGui::SameLine();
                ImGui::TextColored(kTextDim, "(Click to change)");
                
                // Show conflict warning if aimbot key conflicts with triggerbot
                if (KeybindManager::HasKeyConflict()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Conflict: Same key as Triggerbot!");
                }
            } else if (subTab == 1) {
                // TRIGGERBOT Section
                SectionHeader("TRIGGERBOT");
                ToggleSwitch("Enable Triggerbot", &Triggerbot::g_bEnabled);
                ToggleSwitch("Trigger Team Check", &Triggerbot::g_bTeamCheck);
                ImGui::Spacing();
                {
                    const char* fireModes[] = { "Tapping", "Lazer" };
                    int mode = Triggerbot::g_nFireMode.load();
                    ImGui::Text("Fire Mode:");
                    ImGui::SetNextItemWidth(-1);
                    if (ImGui::Combo("##FireMode", &mode, fireModes, 2)) {
                        Triggerbot::g_nFireMode.store(mode);
                    }
                }
                
                // Triggerbot Keybind
                ImGui::Spacing();
                SectionHeader("KEYBIND");
                ImGui::Text("Triggerbot Key:");
                ImGui::SameLine();
                
                int triggerbotKey = KeybindManager::g_nTriggerbotKey.load();
                bool listeningTriggerbot = KeybindManager::g_bListeningForTriggerbotKey.load();
                
                if (KeybindButton("##triggerbot_key", &triggerbotKey, listeningTriggerbot)) {
                    if (listeningTriggerbot) {
                        KeybindManager::StopListening();
                    } else {
                        KeybindManager::StartListeningForTriggerbotKey();
                    }
                }
                
                ImGui::SameLine();
                ImGui::TextColored(kTextDim, "(Click to change)");
                
                // Show conflict warning if triggerbot key conflicts with aimbot
                if (KeybindManager::HasKeyConflict()) {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " Conflict: Same key as Aimbot!");
                }
                
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                ImGui::TextWrapped("Burst: -1 = Auto, 0 = Single");
                ImGui::PopStyleColor();
            }
        }
        // ============ TAB 1: ESP ============
        else if (activeTab == 1) {
            // Master Toggle
            ToggleSwitch("Enable ESP", &Hooks::g_bEspEnabled);
            ImGui::Spacing();
            
            if (Hooks::g_bEspEnabled) {
                ImGui::Columns(2, "esp_cols", false);
                ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f);
                
                // Visuals Column
                SectionHeader("VISUALS");
                ToggleSwitch("Bounding Box", &Hooks::g_bEspBoxes);
                if (Hooks::g_bEspBoxes) {
                    const char* boxStyles[] = { "Full", "Corner", "Rounded" };
                    StyledCombo("Style", &Hooks::g_nEspBoxStyle, boxStyles, 3);
                }
                ToggleSwitch("Skeleton", &Hooks::g_bEspSkeleton);
                ToggleSwitch("Snaplines", &Hooks::g_bEspSnaplines);
                ToggleSwitch("Head Dot", &Hooks::g_bEspHeadDot);
                
                ImGui::Spacing();
                SectionHeader("INFO");
                ToggleSwitch("Health Bar", &Hooks::g_bEspHealth);
                ToggleSwitch("Player Names", &Hooks::g_bEspNames);
                ToggleSwitch("Weapon Name", &Hooks::g_bEspWeaponName);
                ToggleSwitch("Distance", &Hooks::g_bEspDistance);
                
                ImGui::NextColumn();
                
                // Glow Column
                SectionHeader("GLOW");
                ToggleSwitch("Enable Glow", &Hooks::g_bGlowEnabled);
                if (Hooks::g_bGlowEnabled) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
                    ImGui::ColorEdit4("Enemy Glow", Hooks::g_fGlowEnemyColor,
                                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                    ImGui::PopStyleColor();
                    ToggleSwitch("Team Glow", &Hooks::g_bGlowTeamEnabled);
                }
                
                ImGui::Spacing();
                SectionHeader("COLORS");
                ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
                ImGui::ColorEdit4("Enemy", Hooks::g_fEspEnemyColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::ColorEdit4("Team", Hooks::g_fEspTeamColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::PopStyleColor();
                
                ImGui::Columns(1);
            }
        }
        // ============ TAB 2: VISUALS ============
        else if (activeTab == 2) {
            ImGui::Columns(2, "visual_cols", false);
            ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f);
            
            SectionHeader("REMOVALS");
            ToggleSwitch("No Flash", &Hooks::g_bNoFlashEnabled);
            ToggleSwitch("No Smoke", &Hooks::g_bNoSmokeEnabled);
            
            ImGui::Spacing();
            SectionHeader("FOV");
            ToggleSwitch("FOV Override", &Hooks::g_bFovChangerEnabled);
            if (Hooks::g_bFovChangerEnabled) {
                StyledSlider("FOV Value", &Hooks::g_fPlayerFov, 60.0f, 150.0f, "%.0f");
            }
            
            ImGui::NextColumn();
            
            SectionHeader("RADAR");
            ToggleSwitch("Radar Enable", &Hooks::g_bRadarEnabled);
            if (Hooks::g_bRadarEnabled.load()) {
                const char* radarStyles[] = { "Circular", "Square" };
                StyledCombo("Style", &Hooks::g_nRadarStyle, radarStyles, 2);
                StyledSlider("Size", &Hooks::g_fRadarSize, 120.0f, 400.0f, "%.0f px");
                StyledSlider("Range", &Hooks::g_fRadarRange, 500.0f, 8000.0f, "%.0f");
                StyledSlider("Zoom", &Hooks::g_fRadarZoom, 0.5f, 4.0f, "%.1fx");
                StyledSlider("Opacity", &Hooks::g_fRadarAlpha, 0.2f, 1.0f, "%.0f%%");
                ToggleSwitch("Show Names", &Hooks::g_bRadarShowNames);
                ToggleSwitch("Show Health", &Hooks::g_bRadarShowHealth);
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
                ImGui::ColorEdit4("Radar Enemy", Hooks::g_fRadarEnemyColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::SameLine();
                ImGui::ColorEdit4("Radar Team", Hooks::g_fRadarTeamColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::PopStyleColor();
            }
            
            ImGui::Columns(1);
        }
        // ============ TAB 3: MISC ============
        else if (activeTab == 3) {
            ImGui::Columns(2, "misc_cols", false);
            ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f);
            
            SectionHeader("MOVEMENT");
            ToggleSwitch("Bunnyhop", &Hooks::g_bBhopEnabled);
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::TextWrapped("Hold SPACE");
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::NextColumn();
            
            SectionHeader("INFORMATION");
            ToggleSwitch("Spectator List", &Hooks::g_bSpectatorListEnabled);

            ImGui::Columns(1);
        }
        // ============ TAB 4: KILL SOUND ============
        else if (activeTab == 4) {
            SectionHeader("KILL SOUND CHANGER");
            ToggleSwitch("Enable Kill Sound", &Hooks::g_bKillSoundEnabled);
            ImGui::Spacing();

            auto files = KillSound::GetFileList();
            int selectedIndex = KillSound::GetSelectedIndex();
            int appliedIndex = KillSound::GetAppliedIndex();

            ImGui::PushStyleColor(ImGuiCol_ChildBg, kBgWidget);
            ImGui::BeginChild("##killsound_list", ImVec2(0, ImGui::GetContentRegionAvail().y - 44.0f), true);

            if (files.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                ImGui::TextWrapped("No files added. Click Browse to add .mp3 or .wav files.");
                ImGui::PopStyleColor();
            } else {
                for (int i = 0; i < static_cast<int>(files.size()); ++i) {
                    std::string fileName = files[i];
                    size_t slashPos = fileName.find_last_of("\\/");
                    if (slashPos != std::string::npos && slashPos + 1 < fileName.size()) {
                        fileName = fileName.substr(slashPos + 1);
                    }

                    if (i == appliedIndex) {
                        fileName += "  [APPLIED]";
                    }

                    if (ImGui::Selectable(fileName.c_str(), selectedIndex == i)) {
                        KillSound::SetSelectedIndex(i);
                        selectedIndex = i;
                    }
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();

            float applyWidth = 90.0f;
            float browseWidth = 90.0f;
            float totalWidth = applyWidth + browseWidth + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - totalWidth);

            if (ImGui::Button("Apply", ImVec2(applyWidth, 0))) {
                KillSound::ApplySelected();
            }
            ImGui::SameLine();
            if (ImGui::Button("Browse", ImVec2(browseWidth, 0))) {
                KillSound::BrowseAndAddFiles(g_hWnd);
            }
        }
        // ============ TAB 5: SKINS (Inventory Changer) ============
        else if (activeTab == 5) {
            ImGui::TextWrapped("Coming Soon...");
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        // ============ STATUS BAR ============
        float statusH = 28.0f;
        ImVec2 statusStart = ImVec2(winPos.x, winPos.y + winSize.y - statusH);
        
        // Glassmorphism status bar
        dl->AddRectFilled(statusStart, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                          IM_COL32(12, 11, 18, 220), 16.0f, ImDrawFlags_RoundCornersBottom);
        
        // Animated status indicator
        float pulseDot = (sinf(g_animTime * 4.0f) + 1.0f) * 0.5f;
        float dotY = statusStart.y + statusH * 0.5f;
        
        // Outer glow
        dl->AddCircleFilled(ImVec2(winPos.x + 18, dotY), 6.0f + pulseDot * 2.0f, 
                           IM_COL32(100, 220, 120, (int)(40 + pulseDot * 40)), 16);
        // Inner dot
        dl->AddCircleFilled(ImVec2(winPos.x + 18, dotY), 4.0f, 
                           IM_COL32(100, 240, 130, 255), 12);
        
        // Status text with subtle glow
        dl->AddText(ImVec2(winPos.x + 32, statusStart.y + 6), ToU32(kTextDim), "System Active");
        
        // Key hint on right
        const char* hintText = "INSERT to toggle";
        ImVec2 hintSize = ImGui::CalcTextSize(hintText);
        dl->AddText(ImVec2(winPos.x + winSize.x - hintSize.x - 16, statusStart.y + 6), 
                   ToU32(kTextDim), hintText);

        ImGui::End();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
    }

    // =====================================================================
    // ApplyCustomStyle — Premium Dark-Purple Glassmorphism Theme
    // =====================================================================

    void ApplyCustomStyle() {
        ImGuiStyle* style = &ImGui::GetStyle();
        ImVec4* colors = style->Colors;

        style->WindowPadding     = ImVec2(12, 12);
        style->WindowRounding    = 10.0f;
        style->WindowBorderSize  = 1.0f;
        style->FramePadding      = ImVec2(8, 5);
        style->FrameRounding     = 6.0f;
        style->ItemSpacing       = ImVec2(10, 7);
        style->ItemInnerSpacing  = ImVec2(6, 4);
        style->IndentSpacing     = 20.0f;
        style->ScrollbarSize     = 12.0f;
        style->ScrollbarRounding = 10.0f;
        style->GrabMinSize       = 8.0f;
        style->GrabRounding      = 4.0f;
        style->TabRounding       = 6.0f;
        style->ChildRounding     = 8.0f;
        style->PopupRounding     = 6.0f;
        style->ChildBorderSize   = 1.0f;

        colors[ImGuiCol_Text]                   = ImVec4(0.96f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.04f, 0.04f, 0.06f, 0.98f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.06f, 0.06f, 0.09f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.08f, 0.08f, 0.12f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.35f, 0.25f, 0.55f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.60f, 0.35f, 0.95f, 0.10f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.18f, 0.16f, 0.28f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.30f, 0.22f, 0.50f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.04f, 0.04f, 0.06f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.07f, 0.07f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.05f, 0.05f, 0.08f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.40f, 0.28f, 0.65f, 0.60f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.50f, 0.35f, 0.80f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.60f, 0.35f, 0.95f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.60f, 0.35f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.60f, 0.35f, 0.95f, 0.80f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.70f, 0.40f, 1.00f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.14f, 0.12f, 0.20f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.26f, 0.18f, 0.45f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.38f, 0.25f, 0.65f, 1.00f);
        colors[ImGuiCol_Header]                 = ImVec4(0.14f, 0.13f, 0.18f, 1.00f);
        colors[ImGuiCol_HeaderHovered]          = ImVec4(0.24f, 0.20f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive]           = ImVec4(0.35f, 0.28f, 0.55f, 1.00f);
        colors[ImGuiCol_Separator]              = ImVec4(0.22f, 0.18f, 0.35f, 0.40f);
        colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.44f, 0.31f, 0.86f, 0.60f);
        colors[ImGuiCol_SeparatorActive]        = ImVec4(0.44f, 0.31f, 0.86f, 1.00f);
        colors[ImGuiCol_ResizeGrip]             = ImVec4(0.22f, 0.18f, 0.35f, 0.30f);
        colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.44f, 0.31f, 0.86f, 0.50f);
        colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.44f, 0.31f, 0.86f, 1.00f);
        colors[ImGuiCol_Tab]                    = ImVec4(0.10f, 0.09f, 0.14f, 1.00f);
        colors[ImGuiCol_TabHovered]             = ImVec4(0.28f, 0.22f, 0.45f, 0.80f);
        colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.44f, 0.31f, 0.86f, 0.35f);
        colors[ImGuiCol_PlotLines]              = ImVec4(0.44f, 0.31f, 0.86f, 0.63f);
        colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.55f, 0.40f, 0.95f, 1.00f);
        colors[ImGuiCol_PlotHistogram]          = ImVec4(0.44f, 0.31f, 0.86f, 0.63f);
        colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.55f, 0.40f, 0.95f, 1.00f);
    }

    // =====================================================================
    // Flag to skip rendering during ResizeBuffers (prevents crash on mid-transition Present)
    bool g_bResizing = false;

    HRESULT __stdcall hkPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags) {
        if (!g_bInitialized) {
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pDevice))) {
                g_pDevice->GetImmediateContext(&g_pContext);

                DXGI_SWAP_CHAIN_DESC desc;
                pSwapChain->GetDesc(&desc);
                g_hWnd = desc.OutputWindow;

                ID3D11Texture2D* pBackBuffer = nullptr;
                pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
                if (pBackBuffer) {
                    g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
                    pBackBuffer->Release();
                }

                oWndProc = (WndProc_t)SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)hkWndProc);

                ImGui::CreateContext();
                ImGuiIO& io = ImGui::GetIO();
                io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

                // Load fonts embedded in binary — no file paths needed at runtime
                ImFont* mainFont = io.Fonts->AddFontFromMemoryCompressedTTF(
                    RobotoMedium_compressed_data, RobotoMedium_compressed_size, 16.0f);
                if (!mainFont) {
                    io.Fonts->AddFontDefault();
                }

                // Merge FontAwesome icons into the main font
                static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
                ImFontConfig icons_config;
                icons_config.MergeMode = true;
                icons_config.PixelSnapH = true;
                io.Fonts->AddFontFromMemoryCompressedTTF(
                    FaSolid900_compressed_data, FaSolid900_compressed_size, 16.0f, &icons_config, icons_ranges);

                ApplyCustomStyle();
                ImGui_ImplWin32_Init(g_hWnd);
                ImGui_ImplDX11_Init(g_pDevice, g_pContext);

                g_bInitialized = true;
            }
        }

        // Skip rendering entirely during ResizeBuffers — the back buffer is invalid
        if (g_bResizing || !g_bInitialized || !g_pRenderTargetView) {
            return oPresent(pSwapChain, SyncInterval, Flags);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        ImGuiIO& io2 = ImGui::GetIO();
        Aimbot::UpdateScreenSize((int)io2.DisplaySize.x, (int)io2.DisplaySize.y);

        // Show main menu directly (startup screen disabled)
        if (g_bMenuOpen) {
            RenderMenu();
        }

        // Only render ESP, Radar, and SpectatorList when game is in a valid state.
        // Guard with SEH to prevent a transition-time memory fault from crashing the game.
        __try {
            if (Hooks::IsGameReady()) {
                if (Hooks::g_bAimbotEnabled.load()) {
                    Aimbot::Run();
                }

                // Bhop stamina/velocity patching only (game thread).
                // The actual jump logic runs in the main cheat thread for consistent timing.
                if (Hooks::g_bBhopEnabled.load()) {
                    Bunnyhop::RunGameThread();
                }

                if (Hooks::g_bEspEnabled) {
                    ESP::Render();
                }

                if (Hooks::g_bRadarEnabled) {
                    Radar::Render();
                }

                if (Hooks::g_bSpectatorListEnabled) {
                    SpectatorList::Render();
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Recover this frame; next frame will re-evaluate game readiness.
        }

        ImGui::Render();
        g_pContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
        // Block Present from rendering while we're rebuilding the pipeline
        g_bResizing = true;

        // Invalidate ImGui's internal DX11 objects (font texture, blend state, shaders, etc.)
        // before releasing the render target. Without this, ImGui_ImplDX11_RenderDrawData
        // will crash on the next Present call because it holds stale pipeline references.
        if (g_bInitialized) {
            ImGui_ImplDX11_InvalidateDeviceObjects();
        }

        if (g_pRenderTargetView) {
            g_pRenderTargetView->Release();
            g_pRenderTargetView = nullptr;
        }

        HRESULT hr = oResizeBuffers(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);

        ID3D11Texture2D* pBackBuffer = nullptr;
        pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBackBuffer);
        if (pBackBuffer) {
            g_pDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_pRenderTargetView);
            pBackBuffer->Release();
        }

        // Recreate ImGui's device objects for the new pipeline state
        if (g_bInitialized) {
            ImGui_ImplDX11_CreateDeviceObjects();
        }

        g_bResizing = false;

        return hr;
    }

    void Initialize() {
        WNDCLASSEXA wc = { sizeof(WNDCLASSEXA), CS_CLASSDC, DefWindowProcA, 0, 0, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, "DX11DummyMode", nullptr };
        RegisterClassExA(&wc);
        HWND hWnd = CreateWindowA("DX11DummyMode", "", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, wc.hInstance, nullptr);

        DXGI_SWAP_CHAIN_DESC sd = {};
        sd.BufferCount = 1;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        ID3D11Device* pDevice = nullptr;
        ID3D11DeviceContext* pContext = nullptr;
        IDXGISwapChain* pSwapChain = nullptr;
        D3D_FEATURE_LEVEL featureLevel;

        if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sd, &pSwapChain, &pDevice, &featureLevel, &pContext))) {
            DestroyWindow(hWnd);
            UnregisterClassA("DX11DummyMode", wc.hInstance);
            return;
        }

        void** pVTable = *(void***)pSwapChain;
        void* pPresent = pVTable[8];
        void* pResizeBuffers = pVTable[13];

        pContext->Release();
        pDevice->Release();
        pSwapChain->Release();
        DestroyWindow(hWnd);
        UnregisterClassA("DX11DummyMode", wc.hInstance);

        MH_CreateHook(pPresent, &hkPresent, (void**)&oPresent);
        MH_EnableHook(pPresent);

        MH_CreateHook(pResizeBuffers, &hkResizeBuffers, (void**)&oResizeBuffers);
        MH_EnableHook(pResizeBuffers);
    }

    void Shutdown() {
        MH_DisableHook(MH_ALL_HOOKS);

        if (oWndProc) {
            SetWindowLongPtrA(g_hWnd, GWLP_WNDPROC, (LONG_PTR)oWndProc);
        }

        if (g_bInitialized) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            g_bInitialized = false;
        }

        if (g_pRenderTargetView) g_pRenderTargetView->Release();
        if (g_pContext) g_pContext->Release();
        if (g_pDevice) g_pDevice->Release();
    }
}
