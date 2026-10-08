#pragma once

#include "Model.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace OML{
    //Builds live at <data>/projects/<project>/<buildId>/<job>/, each with a build.json. A
    //download goes to a sibling .incoming-<buildId>-<job>/ and is renamed into place only once
    //every file is verified, so a job directory with a build.json is always complete.

    struct InstallTarget{
        std::string project;
        std::string buildId;
        std::string commit;
        std::string committed;
        CatalogJob job;
    };

    struct InstallProgress{
        std::atomic<uint64_t> done{0};
        std::atomic<uint64_t> total{0};
        std::atomic<bool> cancel{false};
        //Called from the installing thread as bytes arrive.
        std::function<void()> onProgress;
    };

    bool installBuild(const std::filesystem::path& projectsDirectory, const InstallTarget& target, InstallProgress& progress, std::string& error);
    bool deleteInstalledBuild(const InstalledBuild& build, std::string& error);

    std::vector<InstalledBuild> scanInstalled(const std::filesystem::path& projectsDirectory);
    //Leftovers from a download that was interrupted by the launcher closing.
    void removeIncomingDirectories(const std::filesystem::path& projectsDirectory);

    //The file to run inside an installed job: the index's 'launch' if it gave one, else the only
    //AppImage, else the only .exe (Windows) or executable (elsewhere) at the top level, looking
    //one directory down when the top level is a single directory. Empty if it's ambiguous.
    std::string findEntry(const std::filesystem::path& jobDirectory, const std::string& launch, const std::string& platform, std::string& error);
}
