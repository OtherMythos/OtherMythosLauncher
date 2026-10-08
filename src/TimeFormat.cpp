#include "TimeFormat.h"

#include <chrono>
#include <cstdio>
#include <ctime>

namespace OML{
    static bool utcTime(int64_t unixSeconds, tm& out){
        time_t t = time_t(unixSeconds);
#ifdef _WIN32
        return gmtime_s(&out, &t) == 0;
#else
        return gmtime_r(&t, &out) != nullptr;
#endif
    }

    static bool localTime(int64_t unixSeconds, tm& out){
        time_t t = time_t(unixSeconds);
#ifdef _WIN32
        return localtime_s(&out, &t) == 0;
#else
        return localtime_r(&t, &out) != nullptr;
#endif
    }

    std::string isoTimeUtc(int64_t unixSeconds){
        tm t = {};
        if(!utcTime(unixSeconds, t)) return std::string();
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &t);
        return buffer;
    }

    std::string compactTimeUtc(int64_t unixSeconds){
        tm t = {};
        if(!utcTime(unixSeconds, t)) return std::string();
        char buffer[32];
        strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &t);
        return buffer;
    }

    bool parseIsoUtc(const std::string& iso, int64_t& unixSeconds){
        tm t = {};
        if(sscanf(iso.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return false;
        t.tm_year -= 1900;
        t.tm_mon -= 1;
#ifdef _WIN32
        time_t value = _mkgmtime(&t);
#else
        time_t value = timegm(&t);
#endif
        if(value == time_t(-1)) return false;
        unixSeconds = int64_t(value);
        return true;
    }

    std::string formatLocalShort(int64_t unixSeconds){
        static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        tm t = {};
        if(!localTime(unixSeconds, t)) return std::string();
        char buffer[32];
        snprintf(buffer, sizeof(buffer), "%d %s %02d:%02d", t.tm_mday, kMonths[t.tm_mon % 12], t.tm_hour, t.tm_min);
        return buffer;
    }

    std::string formatAge(int64_t seconds){
        if(seconds < 60) return "just now";
        if(seconds < 3600) return std::to_string(seconds / 60) + " min ago";
        if(seconds < 48 * 3600) return std::to_string(seconds / 3600) + " h ago";
        return std::to_string(seconds / 86400) + " days ago";
    }

    std::string formatSize(uint64_t bytes){
        char buffer[32];
        double mb = double(bytes) / (1024.0 * 1024.0);
        if(bytes < 1024 * 1024) snprintf(buffer, sizeof(buffer), "%llu KB", (unsigned long long)((bytes + 1023) / 1024));
        else if(mb < 100.0) snprintf(buffer, sizeof(buffer), "%.1f MB", mb);
        else if(mb < 10240.0) snprintf(buffer, sizeof(buffer), "%.0f MB", mb);
        else snprintf(buffer, sizeof(buffer), "%.1f GB", mb / 1024.0);
        return buffer;
    }

    int64_t steadyMs(){
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::string formatDuration(int64_t seconds){
        if(seconds < 60) return std::to_string(seconds) + " s";
        if(seconds < 3600) return std::to_string(seconds / 60) + " min";
        int64_t minutes = (seconds % 3600) / 60;
        return std::to_string(seconds / 3600) + " h" + (minutes ? " " + std::to_string(minutes) + " min" : std::string());
    }
}
