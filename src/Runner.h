#pragma once

#include "ChildProcess.h"
#include "Model.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>

namespace OML{
    //Every launch gets <data>/runs/<yyyymmdd-hhmmss>-<project>/ with output.log (the game's
    //stdout and stderr) and run.json. The game is told where through OTHERMYTHOS_RUN_DIR, which
    //is the hook for it to drop crash dumps or flight captures beside the log.
    struct RunRecord{
        std::string project;
        std::string buildId;
        std::string commit;
        std::string job;
        int64_t started = 0;
        int64_t ended = 0;
        int exitCode = 0;
        bool crashed = false;
        //Quit (or force quit) from the launcher's in-game panel, rather than by the game itself.
        bool stoppedByLauncher = false;
        std::string description;
        std::filesystem::path directory;
    };

    //Starts an installed build and returns straight away, with the running game in process so it
    //can be asked to stop. onExit is called from a background thread once the game exits.
    bool launchBuild(const std::filesystem::path& runsDirectory, const InstalledBuild& build, std::function<void(const RunRecord&)> onExit,
        std::shared_ptr<ChildProcess>& process, std::string& error);

    //The most recent finished run of each project, keyed by project.
    std::map<std::string, RunRecord> latestRuns(const std::filesystem::path& runsDirectory);
}
