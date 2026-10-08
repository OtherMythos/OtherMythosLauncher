#pragma once

#include <string>
#include <vector>

struct SDL_Renderer;

namespace OML{
    //--input presses a scripted sequence of buttons; --capture renders the UI without a display
    //and saves it, so a UI change can be checked from any machine.
    class CaptureScript{
    public:
        //Comma separated: up, down, left, right, a, b, x, y, start, view, lb, rb.
        bool parse(const std::string& keys, std::string& error);
        //Feeds this frame's presses to ImGui. True once every step has been played and settled.
        bool apply(int frame);

    private:
        std::vector<int> mKeys;
    };

    bool saveCapture(SDL_Renderer* renderer, const std::string& path, std::string& error);
}
