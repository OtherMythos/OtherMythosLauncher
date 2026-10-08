#pragma once

#include <cstdint>
#include <string>

namespace OML{
    //"2026-10-07T19:48:30Z"
    std::string isoTimeUtc(int64_t unixSeconds);
    bool parseIsoUtc(const std::string& iso, int64_t& unixSeconds);
    //"20261007-194830", for directory names that sort by time.
    std::string compactTimeUtc(int64_t unixSeconds);

    //"7 Oct 19:48" in local time.
    std::string formatLocalShort(int64_t unixSeconds);
    //"just now", "5 min ago", "3 h ago", "2 days ago"
    std::string formatAge(int64_t seconds);
    //"215 MB", "15.7 MB", "820 KB"
    std::string formatSize(uint64_t bytes);
    //"45 s", "12 min", "1 h 20 min"
    std::string formatDuration(int64_t seconds);

    //Milliseconds on a clock that only goes forwards, for timeouts.
    int64_t steadyMs();
}
