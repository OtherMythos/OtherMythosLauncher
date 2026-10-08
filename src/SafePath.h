#pragma once

#include <string>

namespace OML{
    //Names and paths in an index or a zip decide where files land on disk, so anything that
    //could escape the build's directory, or that Windows can't represent, is refused.

    //One path component: no separators, not '.' or '..', nothing Windows forbids.
    bool isSafeName(const std::string& name);

    //A relative path of safe components separated by '/'.
    bool isSafeRelativePath(const std::string& path);
}
