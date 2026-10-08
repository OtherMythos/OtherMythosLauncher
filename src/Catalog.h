#pragma once

#include "Config.h"
#include "Model.h"

#include <atomic>
#include <filesystem>
#include <string>
#include <vector>

namespace OML{
    //What one source contributed: its projects and how the fetch went.
    struct SourceSnapshot{
        SourceStatus status;
        std::vector<ProjectIndex> projects;
    };

    //Fetches a source's index (and, for a root index, every project's), keeping a copy under
    //cacheDirectory. If the network fails, or useNetwork is false, the last copy is used.
    SourceSnapshot fetchSource(const SourceConfig& source, const std::filesystem::path& cacheDirectory, bool useNetwork,
        const std::atomic<bool>* cancel = nullptr);

    //Merges every source with what's installed into the list the UI shows. Projects are merged
    //by name and builds by id; a job that several sources carry with identical files records
    //each of them, in source order, as places to download from.
    std::vector<CatalogProject> buildCatalog(const std::vector<SourceSnapshot>& sources, const std::vector<InstalledBuild>& installed,
        const std::string& platform, const std::vector<std::string>& hiddenProjects);

    //"linux-Release" -> "linux"
    std::string jobPlatform(const std::string& job);
    //"linux-Release" -> "Release"
    std::string jobBuildType(const std::string& job);
}
