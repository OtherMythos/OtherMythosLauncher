#include "Theme.h"

#include <imgui_internal.h>

namespace OML{
    static ImVec4 colour(ImU32 c, float alpha = 1.0f){
        ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
        v.w *= alpha;
        return v;
    }

    void setUpContext(){
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
        //Controller-first: the focus highlight is always shown, and Escape/B never clears it.
        io.ConfigNavCursorVisibleAlways = true;
        io.ConfigNavEscapeClearFocusItem = false;
        //Holding X (the gamepad "menu" button to ImGui) would otherwise start ImGui's window
        //switcher, and Ctrl+Tab would too; there's only ever one window.
        ImGui::GetCurrentContext()->ConfigNavWindowingWithGamepad = false;
        ImGui::GetCurrentContext()->ConfigNavWindowingKeyNext = 0;
        ImGui::GetCurrentContext()->ConfigNavWindowingKeyPrev = 0;
        //ImGui's built-in vector font scales cleanly, so there's no font file to ship.
        io.Fonts->AddFontDefaultVector();
    }

    float themeScale(){
        //Kept in the style, so each ImGui context (the launcher's, the in-game panel's) has its own.
        return ImGui::GetStyle().FontScaleMain;
    }

    void applyTheme(float scale){
        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        style.WindowPadding = ImVec2(0, 0);
        style.WindowBorderSize = 0.0f;
        style.ChildBorderSize = 0.0f;
        style.PopupBorderSize = 1.0f;
        style.FramePadding = ImVec2(16, 10);
        style.ItemSpacing = ImVec2(10, 8);
        style.ItemInnerSpacing = ImVec2(8, 6);
        style.ScrollbarSize = 10.0f;
        style.FrameRounding = 8.0f;
        style.ChildRounding = 10.0f;
        style.PopupRounding = 10.0f;
        style.GrabRounding = 8.0f;
        style.ScrollbarRounding = 8.0f;
        style.SelectableTextAlign = ImVec2(0.0f, 0.5f);

        ImVec4* c = style.Colors;
        c[ImGuiCol_Text] = colour(Colours::kText);
        c[ImGuiCol_TextDisabled] = colour(Colours::kTextDim);
        c[ImGuiCol_WindowBg] = colour(Colours::kBackground);
        c[ImGuiCol_ChildBg] = colour(Colours::kPanel);
        c[ImGuiCol_PopupBg] = colour(Colours::kPanel);
        c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.6f);
        c[ImGuiCol_Border] = colour(Colours::kTextDim, 0.3f);
        c[ImGuiCol_Button] = ImVec4(0.16f, 0.20f, 0.28f, 1.0f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.26f, 0.36f, 1.0f);
        c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.32f, 0.44f, 1.0f);
        c[ImGuiCol_Header] = ImVec4(0.17f, 0.22f, 0.32f, 1.0f);
        c[ImGuiCol_HeaderHovered] = ImVec4(0.15f, 0.19f, 0.27f, 1.0f);
        c[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.26f, 0.38f, 1.0f);
        c[ImGuiCol_PlotHistogram] = colour(Colours::kAccent);
        c[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.13f, 0.19f, 1.0f);
        c[ImGuiCol_NavCursor] = colour(Colours::kAccent);
        c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
        c[ImGuiCol_Separator] = colour(Colours::kTextDim, 0.25f);

        style.ScaleAllSizes(scale);
        style.FontSizeBase = kBaseFontSize;
        style.FontScaleMain = scale;
        ImGui::GetStyle() = style;
    }
}
