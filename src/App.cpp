#include "App.h"

#include "Http.h"
#include "Paths.h"
#include "TimeFormat.h"

#include <algorithm>

namespace OML{
    App::App(const AppOptions& options, std::function<void()> wake)
        : mOptions(options),
          mWake(wake),
          mProjectsDirectory(options.dataDirectory / "projects"),
          mCacheDirectory(options.dataDirectory / "cache"),
          mRunsDirectory(options.dataDirectory / "runs"),
          mPlatform(options.platform.empty() ? hostPlatform() : options.platform),
          mMessageIsError(false),
          mRefreshing(false),
          mGameRunning(false),
          mGameExited(false),
          mLastWakeMs(0),
          mStopping(false),
          mRefreshQueue(new TaskQueue()),
          mInstallQueue(new TaskQueue()) {
    }

    App::~App(){
        mStopping = true;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            for(Download& d : mDownloads) d.progress->cancel = true;
        }
        mInstallQueue.reset();
        mRefreshQueue.reset();
    }

    void App::start(){
        std::error_code ec;
        std::filesystem::create_directories(mProjectsDirectory, ec);
        mConfig = loadOrCreateConfig(mOptions.dataDirectory / "config.json", mConfigError);
        for(auto it = mOptions.extraSources.rbegin(); it != mOptions.extraSources.rend(); ++it) addSourceFirst(mConfig, *it);

        removeIncomingDirectories(mProjectsDirectory);
        std::vector<SourceSnapshot> cached;
        for(const SourceConfig& s : mConfig.sources) cached.push_back(fetchSource(s, mCacheDirectory, false));
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mSnapshots = cached;
            mLastRuns = latestRuns(mRunsDirectory);
            rescanInstalledLocked();
        }
        if(!mOptions.offline) refresh();
    }

    void App::refresh(){
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if(mRefreshing) return;
            mRefreshing = true;
        }
        Config config = mConfig;
        mRefreshQueue->post([this, config]{
            std::vector<SourceSnapshot> fresh;
            for(const SourceConfig& s : config.sources) fresh.push_back(fetchSource(s, mCacheDirectory, true, &mStopping));
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mSnapshots = fresh;
                mRefreshing = false;
                rebuildCatalogLocked();
            }
            mWake();
        });
    }

    void App::rebuildCatalogLocked(){
        mProjects = buildCatalog(mSnapshots, mInstalled, mPlatform, mConfig.hiddenProjects);
    }

    void App::rescanInstalledLocked(){
        mInstalled = scanInstalled(mProjectsDirectory);
        rebuildCatalogLocked();
    }

    void App::setMessageLocked(const std::string& message, bool isError){
        mMessage = message;
        mMessageIsError = isError;
    }

    void App::wakeThrottled(){
        int64_t now = steadyMs();
        int64_t last = mLastWakeMs.load();
        if(now - last < 100) return;
        if(mLastWakeMs.compare_exchange_strong(last, now)) mWake();
    }

    AppView App::view(){
        std::lock_guard<std::mutex> lock(mMutex);
        AppView v;
        v.projects = mProjects;
        for(const SourceSnapshot& s : mSnapshots) v.sources.push_back(s.status);
        auto now = std::chrono::steady_clock::now();
        for(const Download& d : mDownloads){
            DownloadView dv;
            dv.project = d.project;
            dv.buildId = d.buildId;
            dv.job = d.job;
            dv.running = d.running;
            dv.done = d.progress->done;
            dv.total = d.progress->total;
            double seconds = std::chrono::duration<double>(now - d.started).count();
            if(d.running && seconds > 0.5) dv.bytesPerSecond = double(dv.done) / seconds;
            v.downloads.push_back(dv);
        }
        v.lastRuns = mLastRuns;
        v.titles = mConfig.titles;
        v.refreshing = mRefreshing;
        v.gameRunning = mGameRunning;
        v.game = mGameView;
        v.game.running = mGameRunning;
        v.message = mMessage;
        v.messageIsError = mMessageIsError;
        v.configError = mConfigError;
        httpAvailable(v.httpProblem);
        v.dataDirectory = mOptions.dataDirectory;
        v.platform = mPlatform;
        return v;
    }

    const CatalogJob* App::findJobLocked(const std::string& project, const std::string& buildId, const std::string& job, const CatalogBuild** buildOut) const{
        for(const CatalogProject& p : mProjects){
            if(p.name != project) continue;
            for(const CatalogBuild& b : p.builds){
                if(b.id != buildId) continue;
                for(const CatalogJob& j : b.jobs){
                    if(j.name != job) continue;
                    if(buildOut) *buildOut = &b;
                    return &j;
                }
            }
        }
        return nullptr;
    }

    void App::download(const std::string& project, const std::string& buildId, const std::string& job){
        InstallTarget target;
        auto progress = std::make_shared<InstallProgress>();
        {
            std::lock_guard<std::mutex> lock(mMutex);
            for(const Download& d : mDownloads){
                if(d.project == project && d.buildId == buildId && d.job == job) return;
            }
            const CatalogBuild* build = nullptr;
            const CatalogJob* found = findJobLocked(project, buildId, job, &build);
            if(!found || found->installed) return;
            if(found->sources.empty()){
                setMessageLocked("No source has " + project + " " + build->commit + " any more", true);
                return;
            }
            target.project = project;
            target.buildId = buildId;
            target.commit = build->commit;
            target.committed = build->committed;
            target.job = *found;
            Download d;
            d.project = project;
            d.buildId = buildId;
            d.job = job;
            d.progress = progress;
            mDownloads.push_back(d);
            setMessageLocked("", false);
        }
        progress->onProgress = [this]{ wakeThrottled(); };
        mInstallQueue->post([this, target, progress]{
            {
                std::lock_guard<std::mutex> lock(mMutex);
                for(Download& d : mDownloads){
                    if(d.progress == progress){
                        d.running = true;
                        d.started = std::chrono::steady_clock::now();
                    }
                }
            }
            mWake();
            std::string error;
            bool ok = !progress->cancel && installBuild(mProjectsDirectory, target, *progress, error);
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mDownloads.erase(std::remove_if(mDownloads.begin(), mDownloads.end(), [&](const Download& d){ return d.progress == progress; }), mDownloads.end());
                rescanInstalledLocked();
                std::string what = target.project + " " + target.commit + " " + jobBuildType(target.job.name);
                if(ok) setMessageLocked("Downloaded " + what, false);
                else if(progress->cancel) setMessageLocked("Cancelled " + what, false);
                else setMessageLocked("Couldn't download " + what + ": " + error, true);
            }
            mWake();
        });
    }

    void App::cancelDownload(const std::string& project, const std::string& buildId, const std::string& job){
        std::lock_guard<std::mutex> lock(mMutex);
        for(Download& d : mDownloads){
            if(d.project == project && d.buildId == buildId && d.job == job) d.progress->cancel = true;
        }
    }

    void App::deleteBuild(const std::string& project, const std::string& buildId, const std::string& job){
        InstalledBuild install;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            const CatalogJob* found = findJobLocked(project, buildId, job);
            if(!found || !found->installed || mGameRunning) return;
            install = found->install;
        }
        mInstallQueue->post([this, install]{
            std::string error;
            bool ok = deleteInstalledBuild(install, error);
            {
                std::lock_guard<std::mutex> lock(mMutex);
                rescanInstalledLocked();
                std::string what = install.project + " " + install.commit + " " + jobBuildType(install.job);
                if(ok) setMessageLocked("Deleted " + what, false);
                else setMessageLocked(error, true);
            }
            mWake();
        });
    }

    bool App::play(const std::string& project, const std::string& buildId, const std::string& job){
        InstalledBuild install;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            const CatalogJob* found = findJobLocked(project, buildId, job);
            if(!found || !found->installed || mGameRunning) return false;
            install = found->install;
            mGameRunning = true;
            mGameView = GameView();
            mGameView.project = install.project;
            auto title = mConfig.titles.find(install.project);
            mGameView.title = title == mConfig.titles.end() ? install.project : title->second;
            mGameView.commit = install.commit;
            mGameView.committed = install.committed;
            mGameView.job = install.job;
            mGameView.source = install.source;
            mGameView.startedAt = int64_t(time(nullptr));
            setMessageLocked("", false);
        }
        std::string error;
        std::shared_ptr<ChildProcess> process;
        bool ok = launchBuild(mRunsDirectory, install, [this](const RunRecord& record){
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mGameRunning = false;
                mGame.reset();
                mGameExited = true;
                mLastRuns[record.project] = record;
                if(record.crashed) setMessageLocked(record.project + " " + record.description + ". The log is in " + pathToUtf8(record.directory), true);
            }
            mWake();
        }, process, error);
        std::lock_guard<std::mutex> lock(mMutex);
        if(!ok){
            mGameRunning = false;
            mLastRuns = latestRuns(mRunsDirectory);
            setMessageLocked(error, true);
        }else if(mGameRunning){
            //Unless it has already exited, in which case the exit callback has run.
            mGame = process;
        }
        return ok;
    }

    void App::quitGame(){
        std::lock_guard<std::mutex> lock(mMutex);
        if(!mGame) return;
        mGame->requestStop();
        if(!mGameView.quitRequestedMs) mGameView.quitRequestedMs = steadyMs();
    }

    void App::forceQuitGame(){
        std::lock_guard<std::mutex> lock(mMutex);
        if(mGame) mGame->forceStop();
    }

    bool App::takeGameExited(){
        std::lock_guard<std::mutex> lock(mMutex);
        bool exited = mGameExited;
        mGameExited = false;
        return exited;
    }

    bool App::gameRunning(){
        std::lock_guard<std::mutex> lock(mMutex);
        return mGameRunning;
    }

    bool App::refreshing(){
        std::lock_guard<std::mutex> lock(mMutex);
        return mRefreshing;
    }
}
