#pragma once

#include <atomic>
#include <filesystem>
#include <string>

namespace OML{
    //Extracts a zip into destination. An entry that would land outside it fails the whole
    //extraction, symlinks are skipped, and exec bits from zips made on unix are kept.
    bool extractZip(const std::filesystem::path& zipPath, const std::filesystem::path& destination, const std::atomic<bool>* cancel, std::string& error);
}
