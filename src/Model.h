#pragma once

#include "BuildIndex.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace OML{
    //Where one source has a job's files. The url paths line up with CatalogJob::files.
    struct JobSource{
        std::string sourceName;
        std::string baseUrl;
        std::vector<std::string> urlPaths;
    };

    //A job on disk, as described by its build.json.
    struct InstalledBuild{
        std::string project;
        std::string buildId;
        std::string commit;
        std::string committed;
        std::string job;
        std::string entry;
        std::string source;
        std::string installedAt;
        uint64_t size = 0;
        std::filesystem::path directory;
    };

    struct CatalogJob{
        std::string name;
        std::string launch;
        std::vector<IndexFile> files;
        //Empty when the job is only known from an installed copy.
        std::vector<JobSource> sources;
        bool installed = false;
        InstalledBuild install;

        uint64_t totalSize() const{
            if(installed && install.size) return install.size;
            uint64_t total = 0;
            for(const IndexFile& f : files) total += f.size;
            return total;
        }
    };

    struct CatalogBuild{
        std::string id;
        std::string commit;
        std::string committed;
        //Only this machine's jobs, Release first.
        std::vector<CatalogJob> jobs;
    };

    struct CatalogProject{
        std::string name;
        //Newest first.
        std::vector<CatalogBuild> builds;
    };

    enum class SourceState{
        UNKNOWN,
        OK,
        //The fetch failed, but an earlier copy of the index was on disk.
        CACHED,
        FAILED
    };

    struct SourceStatus{
        std::string name;
        std::string url;
        SourceState state = SourceState::UNKNOWN;
        std::string error;
        //Unix time the index (or the cached copy) was fetched.
        int64_t fetchedAt = 0;
    };
}
