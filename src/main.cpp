#include "App.h"
#include "Capture.h"
#include "CatImage.h"
#include "GamePanel.h"
#include "Paths.h"
#include "Theme.h"
#include "TimeFormat.h"
#include "Ui.h"
#include "Version.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <signal.h>
#endif
#ifdef __linux__
    #include <sys/prctl.h>
#endif

using namespace OML;

namespace{
    struct Options{
        AppOptions app;
        bool fullscreen = false;
        bool windowed = false;
        std::string renderer;
        std::string capturePath;
        std::string input;
        std::string previewPanel;
        bool showPanel = false;
        bool panelHelper = false;
        GameView panelFor;
        int captureWidth = 1280;
        int captureHeight = 800;
    };

    const char* const kUsage =
        "OtherMythos Launcher %s\n"
        "Downloads OtherMythos builds from the build indexes and launches them.\n\n"
        "  --source <url>         Use this index first, for this run (repeatable)\n"
        "  --data-dir <path>      Keep config, builds and runs here\n"
        "  --offline              Don't fetch indexes; use the cached copies\n"
        "  --fullscreen           Start fullscreen\n"
        "  --windowed             Start in a window\n"
        "  --renderer <name>      SDL render driver, e.g. software, opengl, direct3d11\n"
        "  --platform <name>      List another platform's builds (linux, windows)\n"
        "  --input <keys>         Press these once the indexes are loaded, e.g. down,a,a\n"
        "  --capture <file.bmp>   Render headless (after any --input), save a screenshot, exit\n"
        "  --capture-size <WxH>   Size of the capture (default 1280x800)\n"
        "  --preview-panel <state> Draw the in-game panel for a pretend game: idle, quitting, force\n"
        "  --show-panel           Open just the in-game panel, for a pretend game, over whatever is on screen\n"
        "  --panel-for <title> <commit> <committed> <job> <source> <started>\n"
        "                         Internal: draw the in-game panel for the launcher under gamescope\n"
        "  --version, --help\n";

#ifdef _WIN32
    //A WIN32-subsystem exe has no console, so --help would print nowhere when run from a terminal.
    //Output already sent to a file or a pipe (2> log.txt) stays there.
    bool redirected(DWORD stdHandle){
        DWORD type = GetFileType(GetStdHandle(stdHandle));
        return type == FILE_TYPE_DISK || type == FILE_TYPE_PIPE;
    }

    void attachParentConsole(){
        bool out = redirected(STD_OUTPUT_HANDLE);
        bool err = redirected(STD_ERROR_HANDLE);
        if((out && err) || !AttachConsole(ATTACH_PARENT_PROCESS)) return;
        FILE* f = nullptr;
        if(!out) freopen_s(&f, "CONOUT$", "w", stdout);
        if(!err) freopen_s(&f, "CONOUT$", "w", stderr);
    }
#endif

    //Returns -1 to carry on, otherwise the exit code.
    int parseOptions(int argc, char** argv, Options& o){
        for(int i = 1; i < argc; i++){
            std::string arg = argv[i];
            bool hasValue = i + 1 < argc;
            if(arg == "--help" || arg == "-h"){
                printf(kUsage, kVersion);
                return 0;
            }else if(arg == "--version"){
                printf("%s\n", kVersion);
                return 0;
            }else if(arg == "--source" && hasValue){
                o.app.extraSources.push_back(argv[++i]);
            }else if(arg == "--data-dir" && hasValue){
                o.app.dataDirectory = utf8ToPath(argv[++i]);
            }else if(arg == "--offline"){
                o.app.offline = true;
            }else if(arg == "--fullscreen"){
                o.fullscreen = true;
            }else if(arg == "--windowed"){
                o.windowed = true;
            }else if(arg == "--renderer" && hasValue){
                o.renderer = argv[++i];
            }else if(arg == "--platform" && hasValue){
                o.app.platform = argv[++i];
            }else if(arg == "--capture" && hasValue){
                o.capturePath = argv[++i];
            }else if(arg == "--capture-size" && hasValue){
                if(sscanf(argv[++i], "%dx%d", &o.captureWidth, &o.captureHeight) != 2){
                    fprintf(stderr, "--capture-size wants WIDTHxHEIGHT\n");
                    return 2;
                }
            }else if(arg == "--input" && hasValue){
                o.input = argv[++i];
            }else if(arg == "--preview-panel" && hasValue){
                o.previewPanel = argv[++i];
            }else if(arg == "--show-panel"){
                o.showPanel = true;
            }else if(arg == "--panel-for" && i + 6 < argc){
                o.panelHelper = true;
                o.panelFor.running = true;
                o.panelFor.title = argv[++i];
                o.panelFor.commit = argv[++i];
                o.panelFor.committed = argv[++i];
                o.panelFor.job = argv[++i];
                o.panelFor.source = argv[++i];
                o.panelFor.startedAt = strtoll(argv[++i], nullptr, 10);
            }else{
                //Steam and desktop launchers sometimes pass their own arguments; don't refuse to start.
                fprintf(stderr, "ignoring unknown argument: %s\n", arg.c_str());
            }
        }
        if(o.app.dataDirectory.empty()) o.app.dataDirectory = defaultDataDirectory();
        return -1;
    }

    bool envIs(const char* name, const char* value){
        const char* v = getenv(name);
        return v && strstr(v, value) != nullptr;
    }

    bool startFullscreen(const Options& o, const Config& config){
        if(o.fullscreen) return true;
        if(o.windowed) return false;
        if(config.fullscreen != FullscreenMode::AUTO) return config.fullscreen == FullscreenMode::ON;
        //Steam sets SteamDeck=1 on the Deck, and gaming mode runs everything under gamescope.
        return envIs("SteamDeck", "1") || envIs("XDG_CURRENT_DESKTOP", "gamescope") || getenv("GAMESCOPE_WAYLAND_DISPLAY") != nullptr;
    }

    //The launcher owns its gamepads rather than letting the ImGui backend open them, so it can
    //hand ImGui none while input is blocked or a game is running, and give them to the in-game
    //panel instead.
    class Gamepads{
    public:
        void add(SDL_JoystickID id){
            for(SDL_Gamepad* g : mPads) if(SDL_GetGamepadID(g) == id) return;
            if(SDL_Gamepad* g = SDL_OpenGamepad(id)){
                mPads.push_back(g);
                //Which pads the launcher can see is the first question when one doesn't respond;
                //under Steam it may be Steam's virtual pad rather than the hardware.
                fprintf(stderr, "gamepad connected: %s (%04x:%04x)\n", SDL_GetGamepadName(g), SDL_GetGamepadVendor(g), SDL_GetGamepadProduct(g));
            }
            sync();
        }

        void remove(SDL_JoystickID id){
            for(size_t i = 0; i < mPads.size(); i++){
                if(SDL_GetGamepadID(mPads[i]) != id) continue;
                SDL_CloseGamepad(mPads[i]);
                mPads.erase(mPads.begin() + long(i));
                break;
            }
            sync();
        }

        void setBlocked(bool blocked){
            mBlocked = blocked;
            sync();
        }

        bool blocked() const { return mBlocked; }
        const std::vector<SDL_Gamepad*>& pads() const { return mPads; }

        //Both shoulder buttons (LB + RB, L1 + R1) held on one pad: what opens the in-game panel.
        //Not View + Start: on the Deck, Steam keeps a long press of the menu button for switching
        //its own action sets, so Start never arrives while it's held.
        bool panelComboHeld() const{
            for(SDL_Gamepad* g : mPads){
                if(SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) return true;
            }
            return false;
        }

        bool hasSteamDeck() const{
            for(SDL_Gamepad* g : mPads){
                if(SDL_GetGamepadVendor(g) == 0x28de && SDL_GetGamepadProduct(g) == 0x1205) return true;
            }
            return false;
        }

        //Any button down or stick pushed, on any pad, or any key held.
        bool anyHeld() const{
            for(SDL_Gamepad* g : mPads){
                for(int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++){
                    if(SDL_GetGamepadButton(g, SDL_GamepadButton(b))) return true;
                }
                for(int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++){
                    if(abs(SDL_GetGamepadAxis(g, SDL_GamepadAxis(a))) > 16000) return true;
                }
            }
            int count = 0;
            const bool* keys = SDL_GetKeyboardState(&count);
            for(int k = 0; k < count; k++) if(keys[k]) return true;
            return false;
        }

    private:
        void sync(){
            if(mBlocked) ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, nullptr, 0);
            else ImGui_ImplSDL3_SetGamepadMode(ImGui_ImplSDL3_GamepadMode_Manual, mPads.data(), int(mPads.size()));
        }

        std::vector<SDL_Gamepad*> mPads;
        bool mBlocked = false;
    };

    //What Steam's Desktop-mode layout for the Deck sends for its buttons: A Return, B Escape,
    //Y Space, the d-pad arrows, the bumpers Ctrl and Alt, the menu button Tab, the back grips
    //Shift, Windows, Page Up and Page Down.
    bool isSteamDesktopKey(SDL_Keycode key){
        switch(key){
            case SDLK_RETURN: case SDLK_ESCAPE: case SDLK_SPACE: case SDLK_TAB:
            case SDLK_UP: case SDLK_DOWN: case SDLK_LEFT: case SDLK_RIGHT:
            case SDLK_LCTRL: case SDLK_LALT: case SDLK_LSHIFT: case SDLK_LGUI:
            case SDLK_PAGEUP: case SDLK_PAGEDOWN:
                return true;
            default:
                return false;
        }
    }

    //Keyboard, mouse, joystick, gamepad and touch events sit in one block of event numbers; pens in another.
    bool isInputEvent(Uint32 type){
        return (type >= SDL_EVENT_KEY_DOWN && type < SDL_EVENT_CLIPBOARD_UPDATE) || (type >= SDL_EVENT_PEN_PROXIMITY_IN && type < SDL_EVENT_CAMERA_DEVICE_ADDED);
    }

    //Something the person did: a key, a click, a button, a stick pushed past the dead zone. Not
    //the rest of what pads report, which on a Steam Deck never stops: SDL's raw joystick view of
    //the same pads, stick jitter, touchpads and motion sensors. Redrawing for those kept the
    //launcher rendering at 60 fps while nobody touched it.
    bool isUserInput(const SDL_Event& event){
        switch(event.type){
            case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                return abs(event.gaxis.value) >= 8000;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            case SDL_EVENT_GAMEPAD_BUTTON_UP:
                return true;
            default:
                break;
        }
        if(event.type >= SDL_EVENT_JOYSTICK_AXIS_MOTION && event.type < SDL_EVENT_FINGER_DOWN) return false;
        return isInputEvent(event.type);
    }

    SDL_Surface* loadCat(){
        return SDL_LoadPNG_IO(SDL_IOFromConstMem(kCatPng, sizeof(kCatPng)), true);
    }

    const Uint64 kPanelHoldMs = 1000;

    //While a game runs, sticks moving in play shouldn't fill the queue with events nobody reads.
    //Back on whenever the launcher has a UI up. Joystick button and hat events stay on whatever:
    //SDL makes the gamepad button events from them.
    void setNoisyPadEvents(bool on){
        const Uint32 types[] = {
            SDL_EVENT_GAMEPAD_AXIS_MOTION, SDL_EVENT_GAMEPAD_SENSOR_UPDATE,
            SDL_EVENT_GAMEPAD_TOUCHPAD_DOWN, SDL_EVENT_GAMEPAD_TOUCHPAD_MOTION, SDL_EVENT_GAMEPAD_TOUCHPAD_UP,
            SDL_EVENT_JOYSTICK_AXIS_MOTION, SDL_EVENT_JOYSTICK_BALL_MOTION,
        };
        for(Uint32 type : types) SDL_SetEventEnabled(type, on);
    }

    //How often to poll the pads. SDL's own polling (SDL_AUTO_UPDATE_JOYSTICKS) runs every
    //millisecond while one is open: 4.5% of a Steam Deck core for a launcher doing nothing.
    Sint32 padPollMs(bool havePads, bool gameRunning, bool panelOpen, Uint64 sinceInput){
        if(!havePads) return 2000;
        if(gameRunning && !panelOpen) return 50;
        return sinceInput < 3000 ? 16 : 50;
    }

    GameView previewGame(const std::string& state){
        GameView game;
        game.running = true;
        game.project = "forestAtNight";
        game.title = "The Forest At Night";
        game.commit = "143694b";
        game.committed = "2026-10-07T19:48:30Z";
        game.job = "linux-Release";
        game.source = "archive";
        game.startedAt = int64_t(time(nullptr)) - 754;
        if(state == "quitting") game.quitRequestedMs = steadyMs() - 1000;
        if(state == "force") game.quitRequestedMs = steadyMs() - 6000;
        return game;
    }

    //The panel on its own: --show-panel, to see how it appears over whatever is on screen without
    //starting a game, and --panel-for, the copy of the launcher that draws it under gamescope
    //(see GamePanel.h), which says what was chosen on stdout. Runs before App, so neither touches
    //the data directory.
    int runPanelAlone(const Options& options){
        bool helper = options.panelHelper;
#ifdef __linux__
        //Gone with the launcher, rather than left over the screen.
        if(helper) prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        SDL_Window* window = SDL_CreateWindow("OtherMythos Launcher", 1280, 800, SDL_WINDOW_HIDDEN);
        if(!window){
            fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        SDL_Surface* cat = loadCat();
        GameView game = helper ? options.panelFor : previewGame("idle");
        auto say = [helper](const char* what){
            if(!helper) return;
            printf("%s\n", what);
            fflush(stdout);
        };
        std::string error;
        std::vector<SDL_Gamepad*> pads;
        bool open = false;
        {
            GamePanel panel(window, cat, options.renderer);
            open = panel.open(game, error);
            if(!open) fprintf(stderr, "couldn't open the game panel: %s\n", error.c_str());
            while(open){
                SDL_Event event;
                bool haveEvent = SDL_WaitEventTimeout(&event, 16);
                SDL_UpdateJoysticks();
                if(!haveEvent) haveEvent = SDL_PollEvent(&event);
                while(haveEvent){
                    if(event.type == SDL_EVENT_QUIT) open = false;
                    if(event.type == SDL_EVENT_GAMEPAD_ADDED){
                        if(SDL_Gamepad* g = SDL_OpenGamepad(event.gdevice.which)) pads.push_back(g);
                    }
                    panel.processEvent(event);
                    haveEvent = SDL_PollEvent(&event);
                }
                switch(panel.frame(game, pads)){
                    case PanelChoice::RESUME:
                        say("resume");
                        open = false;
                        break;
                    case PanelChoice::QUIT:
                        //The helper stays up, saying the game is closing, until the launcher
                        //sees the game go and closes it.
                        say("quit");
                        if(!game.quitRequestedMs) game.quitRequestedMs = steadyMs();
                        if(!helper) open = false;
                        break;
                    case PanelChoice::FORCE_QUIT:
                        say("force");
                        if(!helper) open = false;
                        break;
                    case PanelChoice::NONE:
                        break;
                }
            }
        }
        for(SDL_Gamepad* g : pads) SDL_CloseGamepad(g);
        if(cat) SDL_DestroySurface(cat);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 0;
    }

#ifndef _WIN32
    //kill -USR1 <launcher> opens the panel over a running game: for testing over SSH, and a way
    //out when no controller is to hand.
    volatile sig_atomic_t gPanelSignal = 0;
    void onPanelSignal(int){
        gPanelSignal = 1;
    }
#endif
}

int main(int argc, char** argv){
#ifdef _WIN32
    attachParentConsole();
#endif
    Options options;
    int exitCode = parseOptions(argc, argv, options);
    if(exitCode >= 0) return exitCode;
    bool capturing = !options.capturePath.empty();
    //Scripted input drives the UI with no controller attached: for captures, and for testing the
    //real windowed launcher on another machine over SSH.
    bool scripting = capturing || !options.input.empty();
    bool scriptDone = false;
    int scriptFrame = 0;
    CaptureScript script;
    std::string error;
    if(!script.parse(options.input, error)){
        fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }

    SDL_SetAppMetadata("OtherMythos Launcher", kVersion, "com.othermythos.launcher");
#ifdef __linux__
    //Without libdecor, a Wayland window on GNOME has no title bar, so take X11 (XWayland) when
    //there is one; the window manager decorates it. Gamescope on the Deck is X11 regardless.
    //SDL_VIDEO_DRIVER in the environment still overrides this. Only on Linux: anywhere else,
    //neither driver exists and SDL_Init fails.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11,wayland");
#endif
    SDL_SetHint(SDL_HINT_AUTO_UPDATE_JOYSTICKS, "0");
    if(capturing){
        SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
        options.renderer = "software";
    }
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)){
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetEventEnabled(SDL_EVENT_JOYSTICK_UPDATE_COMPLETE, false);
    SDL_SetEventEnabled(SDL_EVENT_GAMEPAD_UPDATE_COMPLETE, false);
    if(options.showPanel || options.panelHelper) return runPanelAlone(options);
    Uint32 wakeEvent = SDL_RegisterEvents(1);
    App app(options.app, [wakeEvent]{
        SDL_Event e = {};
        e.type = wakeEvent;
        SDL_PushEvent(&e);
    });
    app.start();

    bool fullscreen = !capturing && startFullscreen(options, app.config());
    int width = capturing ? options.captureWidth : 1280;
    int height = capturing ? options.captureHeight : 800;
    SDL_Window* window = SDL_CreateWindow("OtherMythos Launcher", width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
    if(!window){
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, options.renderer.empty() ? nullptr : options.renderer.c_str());
    if(!renderer){
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderVSync(renderer, 1);
    SDL_Texture* catTexture = nullptr;
    //Kept for the in-game panel, which has a renderer of its own.
    SDL_Surface* cat = loadCat();
    if(cat){
        SDL_SetWindowIcon(window, cat);
        catTexture = SDL_CreateTextureFromSurface(renderer, cat);
        SDL_SetTextureScaleMode(catTexture, SDL_SCALEMODE_LINEAR);
    }

    //One line to say what SDL picked, which is the first question on an unfamiliar machine.
    fprintf(stderr, "OtherMythos Launcher %s: video %s, renderer %s, data %s\n", kVersion, SDL_GetCurrentVideoDriver(),
        SDL_GetRendererName(renderer), pathToUtf8(options.app.dataDirectory).c_str());
    if(fullscreen) SDL_SetWindowFullscreen(window, true);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    setUpContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);
    Gamepads gamepads;
    gamepads.setBlocked(false);
    float scale = 0.0f;

    Ui ui(app);
    ui.setLogo(ImTextureID(intptr_t(catTexture)));
    GamePanelUi previewPanelUi;
    std::unique_ptr<GamePanel> panel;
    int panelFrames = 0;
    Uint64 comboSince = 0;
    //The hold has already acted; both buttons have to be let go before it can again.
    bool comboUsed = false;
    auto openPanel = [&](const char* why){
        if(!panel) panel.reset(new GamePanel(window, cat, options.renderer));
        if(panel->isOpen()) return;
        if(!panel->open(app.view().game, error)){
            fprintf(stderr, "couldn't open the game panel: %s\n", error.c_str());
            return;
        }
        fprintf(stderr, "game panel opened (%s)\n", why);
        setNoisyPadEvents(true);
        panelFrames = 3;
    };
    auto closePanel = [&]{
        if(!panel || !panel->isOpen()) return;
        panel->close();
        fprintf(stderr, "game panel closed\n");
        setNoisyPadEvents(false);
    };
#ifndef _WIN32
    signal(SIGUSR1, onPanelSignal);
#endif

    bool running = true;
    int framesToRender = 3;
    int frame = 0;
    Uint64 unblockAfter = 0;
    Uint64 lastInput = 0;
    while(running){
        bool gameRunning = app.gameRunning();
        bool panelOpen = panel && panel->isOpen();
        bool scriptActive = scripting && !scriptDone;
        Sint32 timeout = -1;
        if(gameRunning){
            //Asleep but for the pad poll, what's left of an LB + RB hold, and an open panel,
            //which redraws a few times a second for its playing time and Force quit offer.
            timeout = panelFrames > 0 ? 0 : panelOpen ? 250 : 500;
            if(comboSince && !comboUsed) timeout = std::min<Sint32>(timeout, Sint32(std::max<Sint64>(0, Sint64(comboSince + kPanelHoldMs) - Sint64(SDL_GetTicks()))));
        }else if(scriptActive || framesToRender > 0){
            timeout = 0;
        }else if(gamepads.blocked()){
            //Wake now and then to see if the buttons have been let go.
            timeout = 50;
        }
        Sint32 poll = padPollMs(!gamepads.pads().empty(), gameRunning, panelOpen, SDL_GetTicks() - lastInput);
        if(timeout < 0 || timeout > poll) timeout = poll;
        SDL_Event event;
        bool haveEvent = SDL_WaitEventTimeout(&event, timeout);
        //Reads the pads (queueing their events, read below) and notices ones plugged in.
        SDL_UpdateJoysticks();
        if(!haveEvent) haveEvent = SDL_PollEvent(&event);

        while(haveEvent){
            Uint32 type = event.type;
            //In Desktop mode, Steam turns the Deck's own buttons into key presses while SDL reads
            //the same controller directly, so each press would land twice: A would open a project
            //and then, as Return, press Play. With the Deck's controller open, those keys are
            //dropped. Launched from Steam, the app gets a virtual pad instead and this never applies.
            bool steamKey = (type == SDL_EVENT_KEY_DOWN || type == SDL_EVENT_KEY_UP) && isSteamDesktopKey(event.key.key) && gamepads.hasSteamDeck();
            bool forward = !(gamepads.blocked() && isInputEvent(type)) && !steamKey;
            if(type == SDL_EVENT_QUIT || type == SDL_EVENT_WINDOW_CLOSE_REQUESTED){
                running = false;
            }else if(type == SDL_EVENT_GAMEPAD_ADDED){
                gamepads.add(event.gdevice.which);
            }else if(type == SDL_EVENT_GAMEPAD_REMOVED){
                gamepads.remove(event.gdevice.which);
            }else if(type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F11 && forward && !gameRunning){
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(window, fullscreen);
            }
            bool userInput = isUserInput(event);
            //Window, wake-up and other non-input events all want a redraw; input only if it was the user.
            bool redraw = userInput || !isInputEvent(type);
            if(userInput) lastInput = SDL_GetTicks();
            if(gameRunning){
                SDL_Window* target = SDL_GetWindowFromEvent(&event);
                bool forPanel = panel && panel->isOpen() && ((target && SDL_GetWindowID(target) == panel->windowId()) || (type >= SDL_EVENT_GAMEPAD_AXIS_MOTION && type < SDL_EVENT_FINGER_DOWN));
                if(forPanel){
                    if(!steamKey) panel->processEvent(event);
                    if(redraw) panelFrames = std::max(panelFrames, 3);
                }
            }else{
                if(forward) ImGui_ImplSDL3_ProcessEvent(&event);
                if(redraw) framesToRender = std::max(framesToRender, 3);
            }
            haveEvent = SDL_PollEvent(&event);
        }

        if(app.takeGameExited()){
            panel.reset();
            comboSince = 0;
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");
            setNoisyPadEvents(true);
            //Whatever was pressed to quit the game is still queued or held; it mustn't land on
            //the Play button and start the game again. Input stays blocked until it's let go.
            SDL_FlushEvents(SDL_EVENT_KEY_DOWN, SDL_EVENT_CLIPBOARD_UPDATE - 1);
            gamepads.setBlocked(true);
            if(fullscreen) SDL_SetWindowFullscreen(window, true);
            SDL_ShowWindow(window);
            SDL_RaiseWindow(window);
            unblockAfter = SDL_GetTicks() + 300;
            framesToRender = 3;
            app.refresh();
            continue;
        }
        if(gameRunning){
            //Read from the pads' state rather than their events, so a press that arrived while
            //the launcher wasn't looking still counts. Held for a second, so a quick LB + RB in
            //a game doesn't open it.
            if(!gamepads.panelComboHeld()){
                comboSince = 0;
                comboUsed = false;
            }else if(!comboSince){
                comboSince = SDL_GetTicks();
            }
            if(comboSince && !comboUsed && SDL_GetTicks() >= comboSince + kPanelHoldMs){
                comboUsed = true;
                if(panel && panel->isOpen()) closePanel();
                else openPanel("LB + RB");
            }
#ifndef _WIN32
            if(gPanelSignal){
                gPanelSignal = 0;
                openPanel("SIGUSR1");
            }
#endif
            if(panel && panel->isOpen()){
                //Held d-pad or stick: keep drawing so ImGui's navigation repeats.
                if(gamepads.anyHeld()) panelFrames = std::max(panelFrames, 2);
                panelFrames = std::max(panelFrames, 1);
                panelFrames--;
                switch(panel->frame(app.view().game, gamepads.pads())){
                    case PanelChoice::RESUME:
                        closePanel();
                        break;
                    case PanelChoice::QUIT:
                        app.quitGame();
                        break;
                    case PanelChoice::FORCE_QUIT:
                        app.forceQuitGame();
                        break;
                    case PanelChoice::NONE:
                        break;
                }
            }
            continue;
        }

        if(gamepads.blocked() && SDL_GetTicks() > unblockAfter && !gamepads.anyHeld()) gamepads.setBlocked(false);
        //Held d-pad or stick: keep drawing so ImGui's navigation repeats.
        if(!gamepads.blocked() && gamepads.anyHeld()) framesToRender = std::max(framesToRender, 2);
        if(framesToRender <= 0 && !scriptActive) continue;
        framesToRender--;

        int windowHeight = 0;
        SDL_GetWindowSize(window, nullptr, &windowHeight);
        float wantScale = std::max(0.5f, float(windowHeight) / kDesignHeight);
        if(wantScale != scale){
            scale = wantScale;
            applyTheme(scale);
        }

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        //The script waits for the first index refresh, so it presses buttons on a populated list.
        if(scriptActive && !app.refreshing()) scriptDone = script.apply(scriptFrame++);
        bool captureDone = capturing && scriptDone && !app.refreshing();
        ImGui::NewFrame();
        UiResult result;
        if(options.previewPanel.empty()){
            result = ui.draw(app.view(), fullscreen);
        }else{
            //The panel as it sits over a game, with a plain stand-in for the game.
            ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImVec2 max(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
            ImGui::GetBackgroundDrawList()->AddRectFilledMultiColor(viewport->Pos, max, IM_COL32(20, 32, 28, 255), IM_COL32(8, 12, 20, 255),
                IM_COL32(4, 6, 10, 255), IM_COL32(16, 24, 22, 255));
            float panelWidth = std::max(380.0f, viewport->Size.x * 0.34f);
            previewPanelUi.draw(previewGame(options.previewPanel), ImTextureID(intptr_t(catTexture)), ImVec2(max.x - panelWidth, viewport->Pos.y), max);
        }
        ImGui::Render();
        SDL_SetRenderScale(renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
        SDL_SetRenderDrawColor(renderer, 11, 14, 19, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        frame++;

        if(captureDone || (capturing && frame > 2000)){
            if(!saveCapture(renderer, options.capturePath, error)){
                fprintf(stderr, "capture failed: %s\n", error.c_str());
                exitCode = 1;
            }else{
                printf("captured %s (%dx%d, %d frames)\n", options.capturePath.c_str(), options.captureWidth, options.captureHeight, frame);
                exitCode = frame > 2000 ? 1 : 0;
            }
            running = false;
        }
        SDL_RenderPresent(renderer);

        if(result.quit) running = false;
        if(result.toggleFullscreen){
            fullscreen = !fullscreen;
            SDL_SetWindowFullscreen(window, fullscreen);
        }
        if(result.launched){
            //Out of the game's way, but still watching the pads for LB + RB, which needs
            //their events while none of the launcher's windows has focus.
            SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
            setNoisyPadEvents(false);
            gamepads.setBlocked(true);
            comboSince = 0;
            comboUsed = false;
            SDL_HideWindow(window);
        }
    }

    panel.reset();
    if(catTexture) SDL_DestroyTexture(catTexture);
    if(cat) SDL_DestroySurface(cat);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return exitCode < 0 ? 0 : exitCode;
}
