#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace OML{
    //One model for both index generators: builds.othermythos.com's manageBuilds.php, where a
    //file's url is <project>/<buildId>/<name>, and the private archive's, where files sit a
    //directory deeper and carry an explicit 'path'.

    struct IndexFile{
        //Where the file goes inside the job's directory, '/' separated.
        std::string name;
        //Relative to the project's directory on the host.
        std::string urlPath;
        uint64_t size = 0;
        std::string sha256;
        bool executable = false;
    };

    struct IndexJob{
        std::string name;
        std::string status;
        //Optional entry point inside the job's directory. No generator writes it yet; it's
        //there for a build the launcher can't work out how to start by itself.
        std::string launch;
        std::vector<IndexFile> files;
    };

    struct IndexBuild{
        std::string id;
        std::string commit;
        std::string committed;
        std::vector<IndexJob> jobs;
    };

    struct ProjectIndex{
        std::string name;
        //The project's directory url, ending in '/'.
        std::string baseUrl;
        std::vector<IndexBuild> builds;
    };

    struct RootIndexEntry{
        std::string name;
        std::string indexUrl;
    };

    enum class IndexKind{
        INVALID,
        ROOT,
        PROJECT
    };

    IndexKind detectIndexKind(const std::string& json);
    bool parseRootIndex(const std::string& json, const std::string& url, std::vector<RootIndexEntry>& out, std::string& error);
    bool parseProjectIndex(const std::string& json, const std::string& url, ProjectIndex& out, std::string& error);

    //"http://host/a/index.json" -> "http://host/a/"
    std::string urlDirectory(const std::string& url);
    //"http://host/a/index.json" -> "a"
    std::string urlLastDirectoryName(const std::string& url);
    std::string urlHost(const std::string& url);
    std::string percentEncodePath(const std::string& path);
}
