#pragma once

#include <filesystem>
#include <string>

namespace OML{
    //Where config, cached indexes, installed builds and run records live:
    //$XDG_DATA_HOME or ~/.local/share on Linux, %LOCALAPPDATA% on Windows.
    std::filesystem::path defaultDataDirectory();

    //The prefix of the job names this machine can run, e.g. "linux" for linux-Release.
    const char* hostPlatform();

    std::string pathToUtf8(const std::filesystem::path& path);
    std::filesystem::path utf8ToPath(const std::string& utf8);

    bool readFile(const std::filesystem::path& path, std::string& out);
    //Writes beside the target and renames over it, so a reader never sees half a file.
    bool writeFileAtomically(const std::filesystem::path& path, const std::string& contents);
}
