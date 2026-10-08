#pragma once

#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

namespace OML{
    //Drawing and input helpers shared by the launcher's screen and the in-game panel. Sizes are
    //design pixels (1280x800) scaled by the current theme; see Theme.h.

    float px(float v);

    //Pressed this frame on the gamepad or either key. repeat follows ImGui's key repeat.
    bool pressed(ImGuiKey gamepad, ImGuiKey key, ImGuiKey otherKey = ImGuiKey_None, bool repeat = false);
    bool pressedBack();
    bool pressedUp();
    bool pressedDown();
    bool pressedLeft();
    bool pressedRight();

    //Moves the gamepad focus to the item just submitted. Not SetKeyboardFocusHere(): that also
    //activates the item, so focusing the Play button pressed it and started the game. Nor
    //FocusItem(): a d-pad move that found nothing is still pending and overrides it.
    void focusLastItem();

    ImVec2 textSize(float size, const std::string& s);
    void drawText(ImDrawList* dl, ImVec2 pos, float size, ImU32 colour, const std::string& s);
    //The box the letters actually cover, relative to where the text is drawn from. Centring on
    //the font's line box instead left the "A" in its circle visibly low and to the left.
    ImRect inkBounds(float size, const std::string& s);
    void drawCentredText(ImDrawList* dl, ImVec2 centre, float size, ImU32 colour, const std::string& s);

    //Wide enough for the label (after the icon's room), never narrower than minWidth.
    ImVec2 buttonSize(const std::string& label, float minWidth, float height);

    void drawPlayIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 colour);
    void drawDownloadIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 colour);
    void drawPowerIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 colour);

    //A controller button and what it does, for the hint row along the bottom.
    struct Hint{
        const char* button;
        std::string label;
    };
    //Draws the hints from x along the line centred on cy; returns where they end.
    float drawHints(ImDrawList* dl, float x, float cy, const std::vector<Hint>& hints);
}
