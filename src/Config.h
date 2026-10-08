#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace OML{
    struct SourceConfig{
        //Also the name of the source's cache directory, so it's kept filesystem-safe.
        std::string name;
        std::string url;
    };

    enum class FullscreenMode{
        AUTO,
        ON,
        OFF
    };

    struct Config{
        //In order of preference: a build several sources carry downloads from the first.
        std::vector<SourceConfig> sources;
        std::vector<std::string> hiddenProjects;
        //Display names, keyed by project name.
        std::map<std::string, std::string> titles;
        FullscreenMode fullscreen = FullscreenMode::AUTO;
    };

    Config defaultConfig();
    std::string configToJson(const Config& config);
    bool configFromJson(const std::string& json, Config& config, std::string& error);

    //Reads config.json, writing the defaults there first if it doesn't exist. A file that
    //doesn't parse is left alone for the user to fix; the defaults are used meanwhile.
    Config loadOrCreateConfig(const std::filesystem::path& path, std::string& error);

    //Puts a source at the front of the list for this run, e.g. from --source.
    void addSourceFirst(Config& config, const std::string& url);

    std::string sanitizeName(const std::string& name);
}
