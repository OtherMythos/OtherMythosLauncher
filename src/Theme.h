#pragma once

#include <imgui.h>

namespace OML{
    //Colours named for what they're used for, so the UI code doesn't carry hex values.
    namespace Colours{
        const ImU32 kBackground = IM_COL32(11, 14, 19, 255);
        const ImU32 kPanel = IM_COL32(18, 23, 34, 255);
        const ImU32 kText = IM_COL32(230, 237, 247, 255);
        const ImU32 kTextDim = IM_COL32(138, 151, 171, 255);
        const ImU32 kAccent = IM_COL32(242, 165, 65, 255);
        const ImU32 kGood = IM_COL32(110, 200, 140, 255);
        const ImU32 kBad = IM_COL32(235, 100, 90, 255);
        const ImU32 kButtonA = IM_COL32(96, 186, 96, 255);
        const ImU32 kButtonB = IM_COL32(220, 80, 70, 255);
        const ImU32 kButtonX = IM_COL32(70, 140, 230, 255);
        const ImU32 kButtonY = IM_COL32(235, 190, 60, 255);
    }

    //Everything is laid out for 1280x800 (the Steam Deck) and scaled from the window's height.
    const float kDesignHeight = 800.0f;
    const float kBaseFontSize = 20.0f;

    //Navigation settings and fonts for a new ImGui context; the launcher's and the panel's match.
    void setUpContext();
    //Rebuilds the style at the given scale; call when the window's height changes.
    void applyTheme(float scale);
    float themeScale();
}
