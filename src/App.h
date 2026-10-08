#pragma once

#include "Catalog.h"
#include "Config.h"
#include "Installer.h"
#include "Runner.h"
#include "TaskQueue.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace OML{
    struct AppOptions{
        std::filesystem::path dataDirectory;
        //Added in front of the configured sources for this run.
        std::vector<std::string> extraSources;
        //Lists another platform's builds, e.g. to preview the Steam Deck's list from a Mac.
        std::string platform;
        //Use cached indexes only.
        bool offline = false;
    };

    struct DownloadView{
        std::string project;
        std::string buildId;
        std::string job;
        bool running = false;
        uint64_t done = 0;
        uint64_t total = 0;
        double bytesPerSecond = 0.0;
    };

    //The game that's running, for the in-game panel.
    struct GameView{
        bool running = false;
        std::string project;
        std::string title;
        std::string commit;
        std::string committed;
        std::string job;
        std::string source;
        int64_t startedAt = 0;
        //steadyMs() when Quit was chosen; 0 if it hasn't been.
        int64_t quitRequestedMs = 0;
    };

    //Everything the UI draws, copied out under the lock so drawing never holds it.
    struct AppView{
        std::vector<CatalogProject> projects;
        std::vector<SourceStatus> sources;
        std::vector<DownloadView> downloads;
        std::map<std::string, RunRecord> lastRuns;
        std::map<std::string, std::string> titles;
        bool refreshing = false;
        bool gameRunning = false;
        GameView game;
        std::string message;
        bool messageIsError = false;
        std::string configError;
        std::string httpProblem;
        std::filesystem::path dataDirectory;
        std::string platform;
    };

    //Owns the launcher's state and its two background threads: one fetching indexes, one
    //downloading and deleting builds. The UI thread calls in; the threads call wake() whenever
    //there's something new to draw.
    class App{
    public:
        App(const AppOptions& options, std::function<void()> wake);
        ~App();

        //Loads config, what's installed and the cached indexes, then starts a refresh.
        void start();
        void refresh();

        AppView view();
        const Config& config() const { return mConfig; }

        void download(const std::string& project, const std::string& buildId, const std::string& job);
        void cancelDownload(const std::string& project, const std::string& buildId, const std::string& job);
        void deleteBuild(const std::string& project, const std::string& buildId, const std::string& job);
        //Starts the build. Returns false, with the reason in the message, if it couldn't.
        bool play(const std::string& project, const std::string& buildId, const std::string& job);

        //Asks the running game to close (SIGTERM, or WM_CLOSE on Windows).
        void quitGame();
        //Ends it at once, for a game that doesn't close when asked.
        void forceQuitGame();

        //True once after a game started by play() has exited.
        bool takeGameExited();
        bool gameRunning();
        bool refreshing();

    private:
        struct Download{
            std::string project;
            std::string buildId;
            std::string job;
            std::shared_ptr<InstallProgress> progress;
            bool running = false;
            std::chrono::steady_clock::time_point started;
        };

        const CatalogJob* findJobLocked(const std::string& project, const std::string& buildId, const std::string& job,
            const CatalogBuild** buildOut = nullptr) const;
        void rebuildCatalogLocked();
        void rescanInstalledLocked();
        void setMessageLocked(const std::string& message, bool isError);
        void wakeThrottled();

        AppOptions mOptions;
        std::function<void()> mWake;
        std::filesystem::path mProjectsDirectory;
        std::filesystem::path mCacheDirectory;
        std::filesystem::path mRunsDirectory;
        Config mConfig;
        std::string mConfigError;
        std::string mPlatform;

        std::mutex mMutex;
        std::vector<SourceSnapshot> mSnapshots;
        std::vector<InstalledBuild> mInstalled;
        std::vector<CatalogProject> mProjects;
        std::vector<Download> mDownloads;
        std::map<std::string, RunRecord> mLastRuns;
        std::string mMessage;
        bool mMessageIsError;
        bool mRefreshing;
        bool mGameRunning;
        std::shared_ptr<ChildProcess> mGame;
        GameView mGameView;
        bool mGameExited;
        std::atomic<int64_t> mLastWakeMs;
        //Stops a refresh waiting on an unreachable source from holding up quitting.
        std::atomic<bool> mStopping;

        //Declared last so they're destroyed first, while everything their tasks touch still exists.
        std::unique_ptr<TaskQueue> mRefreshQueue;
        std::unique_ptr<TaskQueue> mInstallQueue;
    };
}
