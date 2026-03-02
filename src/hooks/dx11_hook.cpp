#include "hooks.hpp"
#include "../features/esp.hpp"
#include "../features/aimbot.hpp"
#include "../features/player_fov.hpp"
#include "../features/spectator_list.hpp"
#include "../features/triggerbot.hpp"
#include "../features/radar.hpp"
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

    // Aimbot Prediction
    bool g_bAimbotPrediction = false;
    float g_fAimbotPredictStrength = 1.0f;

    LRESULT CALLBACK hkWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        if (msg == WM_KEYDOWN && wParam == VK_INSERT) {
            g_bMenuOpen = !g_bMenuOpen;
            return 0;
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

    // Include FontAwesome header
    #include "../vendor/imgui/IconsFontAwesome6.h"

    static float g_animTime = 0.0f;
    static float g_tabAnim[4] = { 1.0f, 0.0f, 0.0f, 0.0f };

    static constexpr ImVec4 kAccent       = ImVec4(0.44f, 0.31f, 0.86f, 1.00f);
    static constexpr ImVec4 kAccentGlow   = ImVec4(0.44f, 0.31f, 0.86f, 0.25f);
    static constexpr ImVec4 kTextBright   = ImVec4(0.95f, 0.95f, 0.97f, 1.00f);
    static constexpr ImVec4 kTextDim      = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    static constexpr ImVec4 kBgMain       = ImVec4(0.067f, 0.067f, 0.082f, 0.97f);
    static constexpr ImVec4 kBgPanel      = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
    static constexpr ImVec4 kBgWidget     = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);

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
    // RenderMenu — Premium Level-100 GUI
    // =====================================================================

    void RenderMenu() {
        g_animTime += ImGui::GetIO().DeltaTime;

        static int activeTab = 0;
        for (int i = 0; i < 4; i++) {
            float target = (i == activeTab) ? 1.0f : 0.0f;
            g_tabAnim[i] += (target - g_tabAnim[i]) * ImGui::GetIO().DeltaTime * 8.0f;
        }

        ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_FirstUseEver); // Slightly larger
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, kBgMain);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.22f, 0.18f, 0.35f, 0.6f));

        ImGui::Begin("##mindcheat_main", &g_bMenuOpen,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();

        // ============ HEADER ============
        float headerH = 60.0f; // Taller header
        ImVec2 headerEnd = ImVec2(winPos.x + winSize.x, winPos.y + headerH);

        dl->AddRectFilled(winPos, headerEnd, IM_COL32(18, 16, 26, 255), 12.0f, ImDrawFlags_RoundCornersTop);

        // Animated accent line
        float pulseW = 140.0f + sinf(g_animTime * 1.5f) * 40.0f;
        float pulseX = winPos.x + (winSize.x - pulseW) * 0.5f + sinf(g_animTime * 0.8f) * 80.0f;
        dl->AddRectFilled(ImVec2(pulseX, headerEnd.y - 2), ImVec2(pulseX + pulseW, headerEnd.y),
                          ToU32(kAccent), 1.0f);
        dl->AddRectFilled(ImVec2(pulseX - 15, headerEnd.y - 5), ImVec2(pulseX + pulseW + 15, headerEnd.y + 3),
                          IM_COL32(112, 80, 219, 35), 2.0f);

        // Title
        ImGui::SetCursorPos(ImVec2(24, 18));
        ImGui::SetWindowFontScale(1.1f); // Make title slightly larger
        ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
        ImGui::Text(ICON_FA_SHIELD_HALVED " MIND");
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 0);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextBright);
        ImGui::Text("CHEAT");
        ImGui::PopStyleColor();
        ImGui::SetWindowFontScale(1.0f);

        ImGui::SameLine(winSize.x - 90);
        ImGui::SetCursorPosY(20);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::Text("v3.0 PRO");
        ImGui::PopStyleColor();

        // ============ SIDEBAR + CONTENT ============
        float contentY = headerH;
        float sidebarWidth = 140.0f;
        
        ImVec2 sidebarStart = ImVec2(winPos.x, winPos.y + contentY);
        ImVec2 sidebarEnd = ImVec2(winPos.x + sidebarWidth, winPos.y + winSize.y - 22);
        
        // Add left border/background
        dl->AddRectFilled(sidebarStart, sidebarEnd, IM_COL32(14, 13, 21, 255), 10.0f, ImDrawFlags_RoundCornersBottomLeft);

        // Sidebar Tabs
        const char* tabLabels[] = { "LEGIT", "ESP", "VISUALS", "MISC" };
        const char* tabIcons[] = { ICON_FA_CROSSHAIRS, ICON_FA_EYE, ICON_FA_PALETTE, ICON_FA_GEARS };
        float tabH = 45.0f;

        for (int i = 0; i < 4; i++) {
            ImVec2 tabStart = ImVec2(winPos.x, winPos.y + contentY + i * tabH + 10.0f);
            ImVec2 tabEnd = ImVec2(tabStart.x + sidebarWidth, tabStart.y + tabH);

            ImGui::SetCursorScreenPos(tabStart);
            char tabId[32];
            snprintf(tabId, sizeof(tabId), "##tab%d", i);
            ImGui::InvisibleButton(tabId, ImVec2(sidebarWidth, tabH));
            bool hovered = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) activeTab = i;

            if (g_tabAnim[i] > 0.01f) {
                // Left glowing accent line instead of bottom
                dl->AddRectFilled(ImVec2(tabStart.x, tabStart.y + tabH * 0.15f),
                                  ImVec2(tabStart.x + 3.0f, tabStart.y + tabH * 0.85f),
                                  IM_COL32(112, 80, 219, (int)(g_tabAnim[i] * 255)), 2.0f);
            }

            char fullLabel[64];
            snprintf(fullLabel, sizeof(fullLabel), "%s  %s", tabIcons[i], tabLabels[i]);
            
            float scale = 1.0f + (g_tabAnim[i] * 0.05f);
            ImVec2 baseTextSz = ImGui::CalcTextSize(fullLabel);
            ImVec2 textSz = ImVec2(baseTextSz.x * scale, baseTextSz.y * scale);
            
            ImVec4 textCol = Lerp4(kTextDim, kTextBright, g_tabAnim[i]);
            if (hovered && i != activeTab) textCol = ImVec4(0.75f, 0.75f, 0.80f, 1.0f);
            
            dl->AddText(NULL, ImGui::GetFontSize() * scale, 
                        ImVec2(tabStart.x + 20.0f + 1, tabStart.y + (tabH - textSz.y) * 0.5f + 1),
                        IM_COL32(0,0,0,150), fullLabel);
                        
            dl->AddText(NULL, ImGui::GetFontSize() * scale, 
                        ImVec2(tabStart.x + 20.0f, tabStart.y + (tabH - textSz.y) * 0.5f),
                        ToU32(textCol), fullLabel);
        }

        // ============ CONTENT ============
        ImGui::SetCursorPos(ImVec2(sidebarWidth + 16, contentY + 16));

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kBgPanel);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.18f, 0.16f, 0.25f, 0.4f));

        ImGui::BeginChild("##content", ImVec2(winSize.x - sidebarWidth - 32, winSize.y - contentY - 32 - 22), true);

        // ============ TAB 0: LEGIT (Aimbot + Triggerbot) ============
        if (activeTab == 0) {
            static int subTab = 0;
            
            // Sub-tabs styling
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.16f, 0.25f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.28f, 0.22f, 0.45f, 1.0f));
            
            ImVec2 buttonSize = ImVec2(120, 30);
            
            if (subTab == 0) {
                ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.16f, 0.25f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            }
            if (ImGui::Button("AIMBOT", buttonSize)) subTab = 0;
            if (subTab == 0) ImGui::PopStyleColor();
            ImGui::PopStyleColor();
            
            ImGui::SameLine();
            
            if (subTab == 1) {
                ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.16f, 0.25f, 1.0f));
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            }
            if (ImGui::Button("TRIGGERBOT", buttonSize)) subTab = 1;
            if (subTab == 1) ImGui::PopStyleColor();
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
            } else if (subTab == 1) {
                // TRIGGERBOT Section
                SectionHeader("TRIGGERBOT");
                ToggleSwitch("Enable Triggerbot", &Triggerbot::g_bEnabled);
                ToggleSwitch("Trigger Team Check", &Triggerbot::g_bTeamCheck);
                ImGui::Spacing();
                StyledSlider("Delay", (float*)&Triggerbot::g_nDelayMs, 0.0f, 100.0f, "%.0f ms");
                StyledSlider("Burst", (float*)&Triggerbot::g_nBurstAmount, -1.0f, 10.0f, "%.0f");
                
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                ImGui::TextWrapped("Key: MOUSE4 (side button)");
                ImGui::TextWrapped("Burst: -1 = Auto, 0 = Single");
                ImGui::PopStyleColor();
            }
        }
        // ============ TAB 1: ESP ============
        else if (activeTab == 1) {
            ImGui::Columns(2, "esp_cols", false);
            ImGui::SetColumnWidth(0, ImGui::GetWindowWidth() * 0.5f);
            
            // Box ESP
            SectionHeader("BOX ESP");
            ToggleSwitch("ESP Master", &Hooks::g_bEspEnabled);
            if (Hooks::g_bEspEnabled) {
                ToggleSwitch("Bounding Box", &Hooks::g_bEspBoxes);
                if (Hooks::g_bEspBoxes) {
                    const char* boxStyles[] = { "Full", "Corner", "Rounded" };
                    StyledCombo("Box Style", &Hooks::g_nEspBoxStyle, boxStyles, 3);
                }
                ImGui::Spacing();
                ToggleSwitch("Health Bar", &Hooks::g_bEspHealth);
                ToggleSwitch("Armor Bar", &Hooks::g_bEspArmor);
                ToggleSwitch("Player Names", &Hooks::g_bEspNames);
                ToggleSwitch("Show Distance", &Hooks::g_bEspDistance);
            }
            
            ImGui::NextColumn();
            
            // Advanced ESP
            SectionHeader("ADVANCED");
            ToggleSwitch("Skeleton", &Hooks::g_bEspSkeleton);
            ToggleSwitch("Snaplines", &Hooks::g_bEspSnaplines);
            ToggleSwitch("Head Dot", &Hooks::g_bEspHeadDot);
            
            ImGui::Spacing();
            SectionHeader("GLOW");
            ToggleSwitch("Glow Enable", &Hooks::g_bGlowEnabled);
            if (Hooks::g_bGlowEnabled) {
                ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
                ImGui::ColorEdit4("Glow Enemy", Hooks::g_fGlowEnemyColor,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                ImGui::PopStyleColor();
                ToggleSwitch("Glow Teammates", &Hooks::g_bGlowTeamEnabled);
                if (Hooks::g_bGlowTeamEnabled) {
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
                    ImGui::ColorEdit4("Glow Team", Hooks::g_fGlowTeamColor,
                                      ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
                    ImGui::PopStyleColor();
                }
            }
            
            ImGui::Columns(1);
            
            // Colors at bottom
            ImGui::Spacing();
            SectionHeader("ESP COLORS");
            ImGui::PushStyleColor(ImGuiCol_FrameBg, kBgWidget);
            ImGui::ColorEdit4("ESP Enemy Color", Hooks::g_fEspEnemyColor,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::SameLine();
            ImGui::ColorEdit4("ESP Team Color", Hooks::g_fEspTeamColor,
                              ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar);
            ImGui::PopStyleColor();
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
            
            ImGui::NextColumn();
            
            SectionHeader("INFORMATION");
            ToggleSwitch("Spectator List", &Hooks::g_bSpectatorListEnabled);
            
            ImGui::Columns(1);
        }

        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);

        // ============ STATUS BAR ============
        ImVec2 statusStart = ImVec2(winPos.x, winPos.y + winSize.y - 22);
        dl->AddRectFilled(statusStart, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y),
                          IM_COL32(14, 13, 21, 255), 10.0f, ImDrawFlags_RoundCornersBottom);

        float dotY = statusStart.y + 11;
        dl->AddCircleFilled(ImVec2(winPos.x + 14, dotY), 4.0f, IM_COL32(80, 220, 100, 255));
        dl->AddText(ImVec2(winPos.x + 24, statusStart.y + 4), ToU32(kTextDim),
                    "Active  |  Press INSERT to toggle");

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

        colors[ImGuiCol_Text]                   = ImVec4(0.92f, 0.92f, 0.94f, 1.00f);
        colors[ImGuiCol_TextDisabled]           = ImVec4(0.40f, 0.40f, 0.45f, 1.00f);
        colors[ImGuiCol_WindowBg]               = ImVec4(0.067f, 0.067f, 0.082f, 0.97f);
        colors[ImGuiCol_ChildBg]                = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
        colors[ImGuiCol_PopupBg]                = ImVec4(0.10f, 0.10f, 0.13f, 0.98f);
        colors[ImGuiCol_Border]                 = ImVec4(0.22f, 0.18f, 0.35f, 0.50f);
        colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        colors[ImGuiCol_FrameBg]                = ImVec4(0.12f, 0.12f, 0.15f, 1.00f);
        colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.18f, 0.16f, 0.25f, 1.00f);
        colors[ImGuiCol_FrameBgActive]          = ImVec4(0.28f, 0.22f, 0.45f, 1.00f);
        colors[ImGuiCol_TitleBg]                = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
        colors[ImGuiCol_TitleBgActive]          = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.05f, 0.05f, 0.07f, 1.00f);
        colors[ImGuiCol_MenuBarBg]              = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
        colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.30f, 0.25f, 0.50f, 0.60f);
        colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.40f, 0.32f, 0.65f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.44f, 0.31f, 0.86f, 1.00f);
        colors[ImGuiCol_CheckMark]              = ImVec4(0.44f, 0.31f, 0.86f, 1.00f);
        colors[ImGuiCol_SliderGrab]             = ImVec4(0.44f, 0.31f, 0.86f, 0.80f);
        colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.55f, 0.40f, 0.95f, 1.00f);
        colors[ImGuiCol_Button]                 = ImVec4(0.14f, 0.13f, 0.18f, 1.00f);
        colors[ImGuiCol_ButtonHovered]          = ImVec4(0.24f, 0.20f, 0.38f, 1.00f);
        colors[ImGuiCol_ButtonActive]           = ImVec4(0.35f, 0.28f, 0.55f, 1.00f);
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
    // DX11 Hook — Present / ResizeBuffers / Init / Shutdown
    // =====================================================================

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

                // Load custom fonts
                std::string robotoPath = "C:\\Users\\navea\\OneDrive\\\xD7\xA9\xD7\x95\xD7\x9C\xD7\x97\xD7\x9F \xD7\x94\xD7\xA2\xD7\x91\xD7\x95\xD7\x93\xD7\x94\\project1\\mindcheat\\vendor\\imgui\\misc\\fonts\\Roboto-Medium.ttf";
                std::string faPath = "C:\\Users\\navea\\OneDrive\\\xD7\xA9\xD7\x95\xD7\x9C\xD7\x97\xD7\x9F \xD7\x94\xD7\xA2\xD7\x91\xD7\x95\xD7\x93\xD7\x94\\project1\\mindcheat\\vendor\\imgui\\misc\\fonts\\fa-solid-900.ttf";
                
                io.Fonts->AddFontFromFileTTF(robotoPath.c_str(), 16.0f);

                static const ImWchar icons_ranges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
                ImFontConfig icons_config;
                icons_config.MergeMode = true;
                icons_config.PixelSnapH = true;
                icons_config.FontDataOwnedByAtlas = false;
                
                io.Fonts->AddFontFromFileTTF(faPath.c_str(), 16.0f, &icons_config, icons_ranges);

                ApplyCustomStyle();
                ImGui_ImplWin32_Init(g_hWnd);
                ImGui_ImplDX11_Init(g_pDevice, g_pContext);

                g_bInitialized = true;
            }
        }

        if (g_bInitialized && g_pRenderTargetView) {
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            ImGuiIO& io2 = ImGui::GetIO();
            Aimbot::UpdateScreenSize((int)io2.DisplaySize.x, (int)io2.DisplaySize.y);

            if (g_bMenuOpen) {
                RenderMenu();
            }

            // Only render ESP, Radar, and SpectatorList when game is in a valid state
            // This prevents crashes during level transitions and loading screens
            if (Hooks::IsGameReady()) {
                if (Hooks::g_bAimbotEnabled.load()) {
                    Aimbot::Run();
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

            ImGui::Render();
            g_pContext->OMSetRenderTargets(1, &g_pRenderTargetView, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        return oPresent(pSwapChain, SyncInterval, Flags);
    }

    HRESULT __stdcall hkResizeBuffers(IDXGISwapChain* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
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
