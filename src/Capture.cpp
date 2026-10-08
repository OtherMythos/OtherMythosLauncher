#include "Capture.h"

#include <SDL3/SDL.h>
#include <imgui.h>

namespace OML{
    static const int kFirstStepFrame = 10;
    static const int kFramesPerStep = 8;
    static const int kSettleFrames = 10;

    bool CaptureScript::parse(const std::string& keys, std::string& error){
        //Keyboard equivalents, since a headless capture has no gamepad for ImGui to read.
        struct Name{
            const char* name;
            ImGuiKey key;
        };
        static const Name kNames[] = {
            {"up", ImGuiKey_UpArrow}, {"down", ImGuiKey_DownArrow}, {"left", ImGuiKey_LeftArrow}, {"right", ImGuiKey_RightArrow},
            {"a", ImGuiKey_Enter}, {"b", ImGuiKey_Escape}, {"x", ImGuiKey_D}, {"y", ImGuiKey_Delete},
            {"start", ImGuiKey_F1}, {"view", ImGuiKey_F5}, {"lb", ImGuiKey_PageUp}, {"rb", ImGuiKey_PageDown},
        };
        size_t start = 0;
        while(start < keys.size()){
            size_t end = keys.find(',', start);
            std::string name = keys.substr(start, end == std::string::npos ? std::string::npos : end - start);
            bool found = false;
            for(const Name& n : kNames){
                if(name == n.name){
                    mKeys.push_back(n.key);
                    found = true;
                }
            }
            if(!found){
                error = "unknown key '" + name + "' in --input";
                return false;
            }
            if(end == std::string::npos) break;
            start = end + 1;
        }
        return true;
    }

    bool CaptureScript::apply(int frame){
        ImGuiIO& io = ImGui::GetIO();
        int step = (frame - kFirstStepFrame) / kFramesPerStep;
        int within = (frame - kFirstStepFrame) % kFramesPerStep;
        //Released on the very next frame: a key held across a slow frame (a window the compositor
        //is throttling runs at 1 fps) would otherwise trip ImGui's key repeat and move twice.
        if(frame >= kFirstStepFrame && step < int(mKeys.size())){
            if(within == 0) io.AddKeyEvent(ImGuiKey(mKeys[step]), true);
            if(within == 1) io.AddKeyEvent(ImGuiKey(mKeys[step]), false);
        }
        return frame >= kFirstStepFrame + int(mKeys.size()) * kFramesPerStep + kSettleFrames;
    }

    bool saveCapture(SDL_Renderer* renderer, const std::string& path, std::string& error){
        SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
        if(!surface){
            error = SDL_GetError();
            return false;
        }
        bool ok = SDL_SaveBMP(surface, path.c_str());
        if(!ok) error = SDL_GetError();
        SDL_DestroySurface(surface);
        return ok;
    }
}
