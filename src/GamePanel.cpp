#include "GamePanel.h"

#include "Catalog.h"
#include "Theme.h"
#include "TimeFormat.h"
#include "Widgets.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#ifdef __linux__
    #include <cerrno>
    #include <dlfcn.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <spawn.h>
    #include <sys/wait.h>
    #include <unistd.h>

    extern char** environ;
#endif

namespace OML{
    //How long Quit waits before offering Force quit.
    static const int64_t kForceQuitAfterMs = 5000;
    //The panel's share of the screen's width.
    static const float kPanelWidth = 0.34f;

    static bool underGamescope(){
        const char* desktop = getenv("XDG_CURRENT_DESKTOP");
        return getenv("GAMESCOPE_WAYLAND_DISPLAY") || (desktop && strstr(desktop, "gamescope"));
    }

#ifdef __linux__
    static bool onGamescopeRootServer(){
        const char* display = getenv("DISPLAY");
        return display && strcmp(display, ":0") == 0;
    }
#endif

    //Gamescope only ever shows the focused game's window, so a window of the launcher's own never
    //appears over it, however it's stacked. What gamescope does draw over the game are "external
    //overlay" windows, which is how MangoHud's figures appear: an X11 window with the
    //GAMESCOPE_EXTERNAL_OVERLAY property set. It gets no input, which the panel doesn't need; it
    //reads the pads in the background. libX11 is loaded the way SDL loads it, so the launcher
    //doesn't link it.
    static bool markGamescopeOverlay(SDL_Window* window){
#ifdef __linux__
        SDL_PropertiesID props = SDL_GetWindowProperties(window);
        void* display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        unsigned long xWindow = (unsigned long)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
        if(!display || !xWindow || !x11) return false;
        typedef unsigned long (*InternAtom)(void*, const char*, int);
        typedef int (*ChangeProperty)(void*, unsigned long, unsigned long, unsigned long, int, int, const unsigned char*, int);
        typedef int (*Flush)(void*);
        InternAtom internAtom = reinterpret_cast<InternAtom>(dlsym(x11, "XInternAtom"));
        ChangeProperty changeProperty = reinterpret_cast<ChangeProperty>(dlsym(x11, "XChangeProperty"));
        Flush flush = reinterpret_cast<Flush>(dlsym(x11, "XFlush"));
        if(!internAtom || !changeProperty || !flush) return false;
        const unsigned long kCardinal = 6;
        const int kReplace = 0;
        //Xlib wants format-32 property data as longs.
        long on = 1;
        changeProperty(display, xWindow, internAtom(display, "GAMESCOPE_EXTERNAL_OVERLAY", 0), kCardinal, 32, kReplace,
            reinterpret_cast<const unsigned char*>(&on), 1);
        flush(display);
        return true;
#else
        (void)window;
        return false;
#endif
    }

    GamePanelUi::GamePanelUi()
        : mFocusResume(true) {
    }

    PanelChoice GamePanelUi::draw(const GameView& game, ImTextureID logo, ImVec2 min, ImVec2 max){
        PanelChoice choice = PanelChoice::NONE;
        ImGui::SetNextWindowPos(min);
        ImGui::SetNextWindowSize(ImVec2(max.x - min.x, max.y - min.y));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(Colours::kPanel));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(28), px(26)));
        ImGui::Begin("##gamePanel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(min, ImVec2(min.x + px(4), max.y), Colours::kAccent);

        float logoSize = px(36);
        ImVec2 cursor = ImGui::GetCursorScreenPos();
        if(logo){
            dl->AddImageRounded(ImTextureRef(logo), cursor, ImVec2(cursor.x + logoSize, cursor.y + logoSize), ImVec2(0, 0), ImVec2(1, 1), IM_COL32_WHITE, px(6));
        }
        float textX = cursor.x + (logo ? logoSize + px(12) : 0.0f);
        drawText(dl, ImVec2(textX, cursor.y + (logoSize - px(22)) * 0.5f), px(22), Colours::kAccent, "OtherMythos");
        ImGui::Dummy(ImVec2(logoSize, logoSize));
        ImGui::Dummy(ImVec2(0, px(14)));

        //What's running.
        ImGui::PushFont(nullptr, 30.0f);
        ImGui::TextWrapped("%s", game.title.c_str());
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
        int64_t committed = 0;
        std::string built = parseIsoUtc(game.committed, committed) ? "  ·  " + formatLocalShort(committed) : std::string();
        ImGui::TextWrapped("Build %s%s", game.commit.c_str(), built.c_str());
        std::string from = game.source.empty() ? std::string() : "  ·  from " + game.source;
        ImGui::TextWrapped("%s%s", jobBuildType(game.job).c_str(), from.c_str());
        int64_t played = game.startedAt ? int64_t(time(nullptr)) - game.startedAt : 0;
        ImGui::TextWrapped("Playing for %s", formatDuration(std::max<int64_t>(played, 0)).c_str());
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, px(18)));
        float width = ImGui::GetContentRegionAvail().x;
        float height = px(64);
        ImGui::PushFont(nullptr, 24.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.0f, 0.5f));
        if(ImGui::Button("    Resume###resume", ImVec2(width, height))) choice = PanelChoice::RESUME;
        drawPlayIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), Colours::kText);
        if(mFocusResume){
            focusLastItem();
            mFocusResume = false;
        }
        ImGui::Dummy(ImVec2(0, px(4)));
        bool quitting = game.quitRequestedMs != 0;
        ImGui::BeginDisabled(quitting);
        if(ImGui::Button(quitting ? "    Closing the game...###quit" : "    Quit game###quit", ImVec2(width, height))) choice = PanelChoice::QUIT;
        ImGui::EndDisabled();
        drawPowerIcon(dl, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), quitting ? Colours::kTextDim : Colours::kText);
        ImGui::PopStyleVar();
        ImGui::PopFont();

        if(quitting && steadyMs() - game.quitRequestedMs >= kForceQuitAfterMs){
            ImGui::Dummy(ImVec2(0, px(10)));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(Colours::kTextDim));
            ImGui::TextWrapped("It hasn't closed. Force quit ends it straight away, and anything it hasn't saved is lost.");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, px(4)));
            ImGui::PushFont(nullptr, 24.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(IM_COL32(150, 50, 45, 255)));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(IM_COL32(175, 60, 52, 255)));
            if(ImGui::Button("Force quit###force", ImVec2(width, height))) choice = PanelChoice::FORCE_QUIT;
            ImGui::PopStyleColor(2);
            ImGui::PopFont();
        }

        float footer = px(56);
        dl->AddRectFilled(ImVec2(min.x + px(4), max.y - footer), max, IM_COL32(16, 20, 28, 255));
        drawHints(dl, min.x + px(28), max.y - footer * 0.5f, {{"A", "Select"}, {"B", "Back to the game"}});

        if(pressedBack()) choice = PanelChoice::RESUME;
        ImGui::End();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        return choice;
    }

    GamePanel::GamePanel(SDL_Window* launcherWindow, SDL_Surface* logo, const std::string& renderer)
        : mLauncherWindow(launcherWindow),
          mLogoSurface(logo),
          mRendererName(renderer),
          mWindow(nullptr),
          mRenderer(nullptr),
          mLogo(nullptr),
          mContext(nullptr),
          mScale(0.0f),
          mOpen(false),
          mOverlay(underGamescope()),
#ifdef __linux__
          mRemote(mOverlay && !onGamescopeRootServer()),
          mHelperPid(-1),
          mHelperOutput(-1) {
#else
          mRemote(false) {
#endif
    }

    GamePanel::~GamePanel(){
        stopHelper();
        if(mContext){
            ImGuiContext* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(mContext);
            ImGui_ImplSDLRenderer3_Shutdown();
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(mContext);
            ImGui::SetCurrentContext(previous);
        }
        if(mLogo) SDL_DestroyTexture(mLogo);
        if(mRenderer) SDL_DestroyRenderer(mRenderer);
        if(mWindow) SDL_DestroyWindow(mWindow);
    }

    SDL_Rect GamePanel::dockedRect() const{
        SDL_DisplayID display = SDL_GetDisplayForWindow(mLauncherWindow);
        if(!display) display = SDL_GetPrimaryDisplay();
        SDL_Rect bounds = {0, 0, 1280, 800};
        SDL_GetDisplayBounds(display, &bounds);
        if(mOverlay) return bounds;
        int width = std::min(bounds.w, std::max(380, int(float(bounds.w) * kPanelWidth)));
        return {bounds.x + bounds.w - width, bounds.y, width, bounds.h};
    }

    //Back to the right edge if anything has moved it. On the Deck in Desktop mode, Steam maps a
    //bumper to Alt and the trackpad to the mouse, so Alt-dragging it across the screen is easy.
    void GamePanel::dock(){
        SDL_Rect want = dockedRect();
        int x = 0;
        int y = 0;
        SDL_GetWindowPosition(mWindow, &x, &y);
        if(x != want.x || y != want.y) SDL_SetWindowPosition(mWindow, want.x, want.y);
    }

    bool GamePanel::create(std::string& error){
        SDL_Rect rect = dockedRect();
        SDL_WindowFlags flags = mOverlay ? SDL_WINDOW_BORDERLESS | SDL_WINDOW_TRANSPARENT | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY
            : SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_UTILITY | SDL_WINDOW_HIDDEN | SDL_WINDOW_HIGH_PIXEL_DENSITY;
        mWindow = SDL_CreateWindow("OtherMythos", rect.w, rect.h, flags);
        if(!mWindow){
            error = SDL_GetError();
            return false;
        }
        SDL_SetWindowPosition(mWindow, rect.x, rect.y);
        mRenderer = SDL_CreateRenderer(mWindow, mRendererName.empty() ? nullptr : mRendererName.c_str());
        if(!mRenderer){
            error = SDL_GetError();
            return false;
        }
        SDL_SetRenderVSync(mRenderer, 1);
        //After the renderer: creating an OpenGL one replaces the X window underneath, and the
        //property would be lost with the old one.
        if(mOverlay && !markGamescopeOverlay(mWindow)) fprintf(stderr, "couldn't make the game panel a gamescope overlay\n");
        if(mLogoSurface){
            mLogo = SDL_CreateTextureFromSurface(mRenderer, mLogoSurface);
            if(mLogo) SDL_SetTextureScaleMode(mLogo, SDL_SCALEMODE_LINEAR);
        }

        ImGuiContext* previous = ImGui::GetCurrentContext();
        mContext = ImGui::CreateContext();
        ImGui::SetCurrentContext(mContext);
        setUpContext();
        ImGui_ImplSDL3_InitForSDLRenderer(mWindow, mRenderer);
        ImGui_ImplSDLRenderer3_Init(mRenderer);
        ImGui::SetCurrentContext(previous);
        return true;
    }

#ifdef __linux__
    bool GamePanel::startHelper(const GameView& game, std::string& error){
        if(mHelperPid > 0) return true;
        char exe[4096];
        ssize_t length = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if(length <= 0){
            error = "can't find the launcher's own binary";
            return false;
        }
        exe[length] = '\0';
        std::vector<std::string> args = {exe, "--panel-for", game.title, game.commit, game.committed, game.job, game.source, std::to_string(game.startedAt)};
        if(!mRendererName.empty()){
            args.push_back("--renderer");
            args.push_back(mRendererName);
        }
        std::vector<char*> argv;
        for(std::string& a : args) argv.push_back(&a[0]);
        argv.push_back(nullptr);
        //Without Steam's overlay hook (LD_PRELOAD): the panel isn't part of the game.
        std::vector<std::string> environment;
        for(char** e = environ; *e; e++){
            if(strncmp(*e, "DISPLAY=", 8) != 0 && strncmp(*e, "LD_PRELOAD=", 11) != 0) environment.push_back(*e);
        }
        environment.push_back("DISPLAY=:0");
        std::vector<char*> envp;
        for(std::string& e : environment) envp.push_back(&e[0]);
        envp.push_back(nullptr);

        int fds[2];
        if(pipe2(fds, O_CLOEXEC) != 0){
            error = strerror(errno);
            return false;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
        pid_t pid = -1;
        int result = posix_spawn(&pid, exe, &actions, nullptr, argv.data(), envp.data());
        posix_spawn_file_actions_destroy(&actions);
        ::close(fds[1]);
        if(result != 0){
            ::close(fds[0]);
            error = std::string("couldn't start the panel helper: ") + strerror(result);
            return false;
        }
        fcntl(fds[0], F_SETFL, O_NONBLOCK);
        mHelperPid = pid;
        mHelperOutput = fds[0];
        mHelperBuffer.clear();
        return true;
    }

    void GamePanel::stopHelper(){
        if(mHelperPid > 0){
            kill(mHelperPid, SIGTERM);
            int status = 0;
            bool gone = false;
            for(int i = 0; i < 100 && !gone; i++){
                gone = waitpid(mHelperPid, &status, WNOHANG) == mHelperPid;
                if(!gone) usleep(20000);
            }
            if(!gone){
                kill(mHelperPid, SIGKILL);
                waitpid(mHelperPid, &status, 0);
            }
            mHelperPid = -1;
        }
        if(mHelperOutput >= 0){
            ::close(mHelperOutput);
            mHelperOutput = -1;
        }
    }

    //Reads what the helper has chosen since last time: a line each of "resume", "quit", "force".
    //It exits itself on resume; exiting any other way also counts as closing the panel.
    PanelChoice GamePanel::readHelper(){
        PanelChoice choice = PanelChoice::NONE;
        char buffer[256];
        ssize_t got = 0;
        while(mHelperOutput >= 0 && (got = read(mHelperOutput, buffer, sizeof(buffer))) > 0) mHelperBuffer.append(buffer, size_t(got));
        size_t end = 0;
        while((end = mHelperBuffer.find('\n')) != std::string::npos){
            std::string line = mHelperBuffer.substr(0, end);
            mHelperBuffer.erase(0, end + 1);
            if(line == "resume") choice = PanelChoice::RESUME;
            else if(line == "quit") choice = PanelChoice::QUIT;
            else if(line == "force") choice = PanelChoice::FORCE_QUIT;
        }
        int status = 0;
        if(choice == PanelChoice::NONE && mHelperPid > 0 && waitpid(mHelperPid, &status, WNOHANG) == mHelperPid){
            mHelperPid = -1;
            choice = PanelChoice::RESUME;
        }
        return choice;
    }
#else
    bool GamePanel::startHelper(const GameView&, std::string& error){
        error = "no panel helper on this platform";
        return false;
    }

    void GamePanel::stopHelper(){
    }

    PanelChoice GamePanel::readHelper(){
        return PanelChoice::NONE;
    }
#endif

    bool GamePanel::open(const GameView& game, std::string& error){
        if(mRemote){
            if(!startHelper(game, error)) return false;
            mOpen = true;
            return true;
        }
        if(!mWindow && !create(error)) return false;
        mUi.resetFocus();
        SDL_ShowWindow(mWindow);
        dock();
        SDL_RaiseWindow(mWindow);
        mOpen = true;
        return true;
    }

    void GamePanel::close(){
        if(mRemote) stopHelper();
        if(mWindow) SDL_HideWindow(mWindow);
        mOpen = false;
    }

    SDL_WindowID GamePanel::windowId() const{
        return mWindow ? SDL_GetWindowID(mWindow) : 0;
    }

    void GamePanel::processEvent(const SDL_Event& event){
        if(mRemote || !mContext) return;
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(mContext);
        ImGui_ImplSDL3_ProcessEvent(&event);
        ImGui::SetCurrentContext(previous);
    }

    PanelChoice GamePanel::frame(const GameView& game, const std::vector<SDL_Gamepad*>& pads){
        if(mRemote) return mOpen ? readHelper() : PanelChoice::NONE;
        if(!mOpen || !mContext) return PanelChoice::NONE;
        dock();
        ImGuiContext* previous = ImGui::GetCurrentContext();
        ImGui::SetCurrentContext(mContext);
        //The panel reads the pads whether or not its window has focus; the launcher turns on
        //background gamepad events while a game runs.
        ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, const_cast<SDL_Gamepad**>(pads.data()), int(pads.size()));
        int height = 0;
        SDL_GetWindowSize(mWindow, nullptr, &height);
        float scale = std::max(0.5f, float(height) / kDesignHeight);
        if(scale != mScale){
            mScale = scale;
            applyTheme(scale);
        }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
        //As an overlay the window covers the screen; the panel takes the right edge and the rest
        //stays clear so the game shows through.
        ImVec2 min = mOverlay ? ImVec2(max.x - std::max(380.0f, viewport->Size.x * kPanelWidth), viewport->Pos.y) : viewport->Pos;
        PanelChoice choice = mUi.draw(game, ImTextureID(intptr_t(mLogo)), min, max);
        ImGui::Render();
        ImGuiIO& io = ImGui::GetIO();
        SDL_SetRenderScale(mRenderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        if(mOverlay) SDL_SetRenderDrawColor(mRenderer, 0, 0, 0, 0);
        else SDL_SetRenderDrawColor(mRenderer, 18, 23, 34, 255);
        SDL_RenderClear(mRenderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), mRenderer);
        SDL_RenderPresent(mRenderer);
        ImGui::SetCurrentContext(previous);
        return choice;
    }
}
