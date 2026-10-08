#include "Paths.h"

#include <atomic>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace OML{
    static const char* const kAppDirectory = "OtherMythosLauncher";

    std::filesystem::path defaultDataDirectory(){
#if defined(_WIN32)
        const wchar_t* local = _wgetenv(L"LOCALAPPDATA");
        std::filesystem::path base = local ? std::filesystem::path(local) : std::filesystem::temp_directory_path();
        return base / kAppDirectory;
#else
        const char* home = getenv("HOME");
        std::filesystem::path homePath = home ? home : "/tmp";
    #if defined(__APPLE__)
        return homePath / "Library" / "Application Support" / kAppDirectory;
    #else
        const char* xdg = getenv("XDG_DATA_HOME");
        if(xdg && xdg[0] == '/') return std::filesystem::path(xdg) / kAppDirectory;
        return homePath / ".local" / "share" / kAppDirectory;
    #endif
#endif
    }

    const char* hostPlatform(){
#if defined(_WIN32)
        return "windows";
#elif defined(__APPLE__)
        return "macos";
#else
        return "linux";
#endif
    }

    std::string pathToUtf8(const std::filesystem::path& path){
        return path.u8string();
    }

    std::filesystem::path utf8ToPath(const std::string& utf8){
        return std::filesystem::u8path(utf8);
    }

    bool readFile(const std::filesystem::path& path, std::string& out){
        std::ifstream file(path, std::ios::binary);
        if(!file) return false;
        std::ostringstream ss;
        ss << file.rdbuf();
        out = ss.str();
        return true;
    }

    bool writeFileAtomically(const std::filesystem::path& path, const std::string& contents){
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
#ifdef _WIN32
        unsigned long pid = GetCurrentProcessId();
#else
        unsigned long pid = (unsigned long)getpid();
#endif
        static std::atomic<unsigned> counter{0};
        std::filesystem::path tmp = path;
        tmp += ".tmp" + std::to_string(pid) + "-" + std::to_string(counter++);
        {
            std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
            if(!file) return false;
            file.write(contents.data(), std::streamsize(contents.size()));
            if(!file) return false;
        }
        std::filesystem::rename(tmp, path, ec);
        if(ec){
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }
}
