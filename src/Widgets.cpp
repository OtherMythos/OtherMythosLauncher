#include "Widgets.h"

#include "Theme.h"

#include <algorithm>
#include <cfloat>

namespace OML{
    float px(float v){
        return v * themeScale();
    }

    bool pressed(ImGuiKey gamepad, ImGuiKey key, ImGuiKey otherKey, bool repeat){
        return ImGui::IsKeyPressed(gamepad, repeat) || ImGui::IsKeyPressed(key, repeat) || (otherKey != ImGuiKey_None && ImGui::IsKeyPressed(otherKey, repeat));
    }

    bool pressedBack(){ return pressed(ImGuiKey_GamepadFaceRight, ImGuiKey_Escape, ImGuiKey_Backspace); }
    bool pressedUp(){ return pressed(ImGuiKey_GamepadDpadUp, ImGuiKey_UpArrow, ImGuiKey_GamepadLStickUp, true); }
    bool pressedDown(){ return pressed(ImGuiKey_GamepadDpadDown, ImGuiKey_DownArrow, ImGuiKey_GamepadLStickDown, true); }
    bool pressedLeft(){ return pressed(ImGuiKey_GamepadDpadLeft, ImGuiKey_LeftArrow, ImGuiKey_GamepadLStickLeft); }
    bool pressedRight(){ return pressed(ImGuiKey_GamepadDpadRight, ImGuiKey_RightArrow, ImGuiKey_GamepadLStickRight); }

    void focusLastItem(){
        ImGui::NavMoveRequestCancel();
        ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
        ImGui::ScrollToItem(ImGuiScrollFlags_KeepVisibleEdgeY);
    }

    ImVec2 textSize(float size, const std::string& s){
        return ImGui::GetFont()->CalcTextSizeA(size, FLT_MAX, 0.0f, s.c_str());
    }

    void drawText(ImDrawList* dl, ImVec2 pos, float size, ImU32 colour, const std::string& s){
        dl->AddText(ImGui::GetFont(), size, pos, colour, s.c_str());
    }

    ImRect inkBounds(float size, const std::string& s){
        ImFontBaked* baked = ImGui::GetFont()->GetFontBaked(size);
        float scale = size / baked->Size;
        ImRect bounds(FLT_MAX, FLT_MAX, -FLT_MAX, -FLT_MAX);
        float x = 0.0f;
        for(unsigned char c : s){
            const ImFontGlyph* g = baked->FindGlyph(ImWchar(c));
            if(!g) continue;
            if(g->Visible){
                bounds.Add(ImVec2(x + g->X0 * scale, g->Y0 * scale));
                bounds.Add(ImVec2(x + g->X1 * scale, g->Y1 * scale));
            }
            x += g->AdvanceX * scale;
        }
        return bounds.Min.x <= bounds.Max.x ? bounds : ImRect(0, 0, 0, 0);
    }

    void drawCentredText(ImDrawList* dl, ImVec2 centre, float size, ImU32 colour, const std::string& s){
        ImRect ink = inkBounds(size, s);
        ImVec2 pos(centre.x - (ink.Min.x + ink.Max.x) * 0.5f, centre.y - (ink.Min.y + ink.Max.y) * 0.5f);
        drawText(dl, ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y)), size, colour, s);
    }

    ImVec2 buttonSize(const std::string& label, float minWidth, float height){
        float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        return ImVec2(std::max(minWidth, w), height);
    }

    void drawPlayIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 colour){
        float h = max.y - min.y;
        float x = min.x + h * 0.42f;
        float cy = (min.y + max.y) * 0.5f;
        float r = h * 0.17f;
        dl->AddTriangleFilled(ImVec2(x, cy - r), ImVec2(x, cy + r), ImVec2(x + r * 1.6f, cy), colour);
    }

    void drawDownloadIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 colour){
        float h = max.y - min.y;
        float cx = min.x + h * 0.5f;
        float cy = (min.y + max.y) * 0.5f;
        float r = h * 0.16f;
        float t = px(2.5f);
        dl->AddLine(ImVec2(cx, cy - r * 1.2f), ImVec2(cx, cy + r * 0.5f), colour, t);
        dl->AddTriangleFilled(ImVec2(cx - r * 0.8f, cy), ImVec2(cx + r * 0.8f, cy), ImVec2(cx, cy + r), colour);
        dl->AddLine(ImVec2(cx - r, cy + r * 1.4f), ImVec2(cx + r, cy + r * 1.4f), colour, t);
    }

    void drawPowerIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 colour){
        float h = max.y - min.y;
        ImVec2 c(min.x + h * 0.55f, (min.y + max.y) * 0.5f);
        float r = h * 0.2f;
        float t = px(2.5f);
        //A ring open at the top, and a stroke through the gap.
        dl->PathArcTo(c, r, -IM_PI * 0.5f + 0.65f, IM_PI * 1.5f - 0.65f, 24);
        dl->PathStroke(colour, t);
        dl->AddLine(ImVec2(c.x, c.y - r * 1.3f), ImVec2(c.x, c.y - r * 0.15f), colour, t);
    }

    static void drawGlyph(ImDrawList* dl, ImVec2 centre, float radius, ImU32 colour, const char* label){
        dl->AddCircleFilled(centre, radius, colour, 24);
        drawCentredText(dl, centre, radius * 1.3f, IM_COL32(15, 18, 24, 255), label);
    }

    //A rounded pill, for shoulder buttons and Start/View, which have no letter colour.
    static float pillWidth(float height, const char* label){
        return inkBounds(height * 0.62f, label).GetWidth() + height * 0.9f;
    }

    static void drawPill(ImDrawList* dl, ImVec2 centre, float height, const char* label){
        float w = pillWidth(height, label);
        ImVec2 min(centre.x - w * 0.5f, centre.y - height * 0.5f);
        dl->AddRectFilled(min, ImVec2(min.x + w, min.y + height), IM_COL32(200, 208, 222, 255), height * 0.5f);
        drawCentredText(dl, centre, height * 0.62f, IM_COL32(15, 18, 24, 255), label);
    }

    float drawHints(ImDrawList* dl, float x, float cy, const std::vector<Hint>& hints){
        float labelSize = px(19);
        for(const Hint& h : hints){
            std::string button = h.button;
            if(button.size() == 1){
                ImU32 colour = button == "A" ? Colours::kButtonA : button == "B" ? Colours::kButtonB : button == "X" ? Colours::kButtonX : Colours::kButtonY;
                drawGlyph(dl, ImVec2(x + px(14), cy), px(14), colour, h.button);
                x += px(36);
            }else{
                float pillHeight = px(26);
                float w = pillWidth(pillHeight, h.button);
                drawPill(dl, ImVec2(x + w * 0.5f, cy), pillHeight, h.button);
                x += w + px(8);
            }
            ImVec2 ts = textSize(labelSize, h.label);
            drawText(dl, ImVec2(x, cy - ts.y * 0.5f), labelSize, Colours::kText, h.label);
            x += ts.x + px(28);
        }
        return x;
    }
}
