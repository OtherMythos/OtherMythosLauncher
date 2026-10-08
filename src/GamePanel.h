#pragma once

#include "App.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <string>
#include <vector>

namespace OML{
    enum class PanelChoice{
        NONE,
        RESUME,
        QUIT,
        FORCE_QUIT
    };

    //What the in-game panel shows: the running game, and what can be done to it. Draws into any
    //rectangle of the current ImGui context, so --preview-panel can show it beside a stand-in
    //for the game.
    class GamePanelUi{
    public:
        GamePanelUi();

        PanelChoice draw(const GameView& game, ImTextureID logo, ImVec2 min, ImVec2 max);
        //Focus goes to Resume on the next frame.
        void resetFocus(){ mFocusResume = true; }

    private:
        bool mFocusResume;
    };

    //The panel over a running game, opened by holding LB + RB. A window of its own (borderless,
    //always on top, along the right edge of the screen) with its own renderer and ImGui context,
    //so the launcher's main window can stay hidden and the game stays visible beside it. Under
    //gamescope it's a transparent overlay covering the screen instead.
    class GamePanel{
    public:
        GamePanel(SDL_Window* launcherWindow, SDL_Surface* logo, const std::string& renderer);
        ~GamePanel();
        GamePanel(const GamePanel&) = delete;
        GamePanel& operator=(const GamePanel&) = delete;

        //game is only needed under gamescope, where the panel is drawn by a helper process.
        bool open(const GameView& game, std::string& error);
        void close();
        bool isOpen() const { return mOpen; }
        SDL_WindowID windowId() const;

        //An event for the panel: its own window's, or any gamepad's.
        void processEvent(const SDL_Event& event);
        PanelChoice frame(const GameView& game, const std::vector<SDL_Gamepad*>& pads);

    private:
        bool create(std::string& error);
        bool startHelper(const GameView& game, std::string& error);
        void stopHelper();
        PanelChoice readHelper();
        //Along the right edge of the screen the launcher (and so, most likely, the game) is on.
        SDL_Rect dockedRect() const;
        void dock();

        SDL_Window* mLauncherWindow;
        SDL_Surface* mLogoSurface;
        std::string mRendererName;
        SDL_Window* mWindow;
        SDL_Renderer* mRenderer;
        SDL_Texture* mLogo;
        ImGuiContext* mContext;
        GamePanelUi mUi;
        float mScale;
        bool mOpen;
        //Under gamescope (the Deck's Gaming mode) the panel is a full-screen transparent overlay
        //with the panel drawn on the right; see GamePanel.cpp.
        bool mOverlay;
        //gamescope only takes overlays from its root X server (":0"), and the launcher and its
        //game are on another (":1"). SDL talks to one X server per process, so there the panel is
        //drawn by a copy of the launcher run on ":0" (--panel-for), which reports what was chosen
        //on its stdout.
        bool mRemote;
#ifdef __linux__
        int mHelperPid;
        int mHelperOutput;
        std::string mHelperBuffer;
#endif
    };
}
