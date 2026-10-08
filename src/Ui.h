#pragma once

#include "App.h"

#include <imgui.h>

#include <string>

namespace OML{
    struct UiResult{
        bool quit = false;
        bool toggleFullscreen = false;
        //A game was started; the window should get out of its way.
        bool launched = false;
    };

    //The one screen: projects on the left, the selected project's builds on the right, and a
    //footer saying what each controller button does for whatever has focus.
    class Ui{
    public:
        explicit Ui(App& app);

        UiResult draw(const AppView& view, bool fullscreen);
        //The OtherMythos cat, drawn beside the title. 0 draws nothing.
        void setLogo(ImTextureID logo){ mLogo = logo; }

    private:
        enum class FocusKind{
            NONE,
            PROJECT,
            PLAY,
            UPDATE,
            DOWNLOAD,
            CANCEL,
            ROW,
            QUIT,
            POPUP
        };

        struct Focus{
            FocusKind kind = FocusKind::NONE;
            std::string buildId;
            std::string job;
            bool installed = false;
            bool downloading = false;
            bool updateAvailable = false;
            //Nothing to its left in the detail pane, so d-pad left goes back to the projects.
            bool leftmost = false;
        };

        void drawHeader(const AppView& view, Focus& focus, UiResult& result);
        void drawProjects(const AppView& view, Focus& focus);
        void drawDetail(const AppView& view, const CatalogProject& project, Focus& focus, UiResult& result);
        void drawFooter(const AppView& view, const Focus& focus);
        void drawMenu(const AppView& view, bool fullscreen, UiResult& result);
        void drawDeleteConfirm(const AppView& view);
        void drawQuitConfirm(UiResult& result);
        void handleButtons(const AppView& view, const Focus& focus);
        void activate(const AppView& view, const std::string& buildId, const std::string& job, UiResult& result);
        std::string title(const AppView& view, const std::string& project) const;

        App& mApp;
        std::string mSelectedProject;
        FocusKind mLastFocusKind;
        bool mFocusProjectRequest;
        bool mFocusPrimaryRequest;
        bool mOpenMenuRequest;
        bool mOpenDeleteRequest;
        bool mOpenQuitRequest;
        bool mFocusQuitRequest;
        ImTextureID mLogo;
        //The press that opens a popup is still down in the frame it opens; it mustn't also close it.
        int mPopupOpenedFrame;
        std::string mDeleteBuildId;
        std::string mDeleteJob;
    };
}
