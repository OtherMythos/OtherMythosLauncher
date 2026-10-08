#include "Installer.h"

#include "Archive.h"
#include "Catalog.h"
#include "Http.h"
#include "Json.h"
#include "Paths.h"
#include "SafePath.h"
#include "Sha256.h"
#include "TimeFormat.h"

#include <algorithm>
#include <cJSON.h>
#include <ctime>
#include <fstream>

namespace OML{
    static const char* const kManifestFile = "build.json";
    static const char* const kIncomingPrefix = ".incoming-";

    static bool endsWithNoCase(const std::string& s, const std::string& suffix){
        if(s.size() < suffix.size()) return false;
        return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b){
            return tolower((unsigned char)a) == tolower((unsigned char)b);
        });
    }

    static void addExecutePermission(const std::filesystem::path& path){
#ifndef _WIN32
        std::error_code ec;
        std::filesystem::permissions(path, std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec | std::filesystem::perms::others_exec,
            std::filesystem::perm_options::add, ec);
#else
        (void)path;
#endif
    }

    static bool isExecutable(const std::filesystem::path& path, const std::string& platform){
        if(platform == "windows") return endsWithNoCase(pathToUtf8(path.filename()), ".exe");
        std::error_code ec;
        std::filesystem::perms p = std::filesystem::status(path, ec).permissions();
        return !ec && (p & std::filesystem::perms::owner_exec) != std::filesystem::perms::none;
    }

    std::string findEntry(const std::filesystem::path& jobDirectory, const std::string& launch, const std::string& platform, std::string& error){
        std::error_code ec;
        if(!launch.empty()){
            if(isSafeRelativePath(launch) && std::filesystem::is_regular_file(jobDirectory / utf8ToPath(launch), ec)) return launch;
            error = "the index says to run " + launch + ", but it isn't there";
            return std::string();
        }
        std::filesystem::path dir = jobDirectory;
        std::string prefix;
        for(int depth = 0; depth < 2; depth++){
            std::vector<std::string> appImages;
            std::vector<std::string> executables;
            std::vector<std::string> directories;
            size_t fileCount = 0;
            for(const auto& entry : std::filesystem::directory_iterator(dir, ec)){
                std::string name = pathToUtf8(entry.path().filename());
                if(name.empty() || name[0] == '.' || name == kManifestFile) continue;
                if(entry.is_directory(ec)){
                    directories.push_back(name);
                    continue;
                }
                fileCount++;
                if(endsWithNoCase(name, ".AppImage")) appImages.push_back(prefix + name);
                else if(isExecutable(entry.path(), platform)) executables.push_back(prefix + name);
            }
            if(appImages.size() == 1) return appImages[0];
            if(appImages.empty() && executables.size() == 1) return executables[0];
            if(appImages.size() > 1 || executables.size() > 1){
                error = "more than one program in the build, and the index doesn't say which to run";
                return std::string();
            }
            if(fileCount > 0 || directories.size() != 1) break;
            prefix += directories[0] + "/";
            dir /= utf8ToPath(directories[0]);
        }
        error = "couldn't find a program to run in the build";
        return std::string();
    }

    static std::string manifestJson(const InstallTarget& target, const std::string& entry, const std::string& source, uint64_t size){
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "project", target.project.c_str());
        cJSON_AddStringToObject(root, "build", target.buildId.c_str());
        cJSON_AddStringToObject(root, "commit", target.commit.c_str());
        cJSON_AddStringToObject(root, "committed", target.committed.c_str());
        cJSON_AddStringToObject(root, "job", target.job.name.c_str());
        cJSON_AddStringToObject(root, "entry", entry.c_str());
        cJSON_AddStringToObject(root, "source", source.c_str());
        cJSON_AddStringToObject(root, "installed", isoTimeUtc(int64_t(time(nullptr))).c_str());
        cJSON_AddNumberToObject(root, "size", double(size));
        cJSON* files = cJSON_AddArrayToObject(root, "files");
        for(const IndexFile& f : target.job.files){
            cJSON* file = cJSON_CreateObject();
            cJSON_AddStringToObject(file, "name", f.name.c_str());
            cJSON_AddNumberToObject(file, "size", double(f.size));
            cJSON_AddStringToObject(file, "sha256", f.sha256.c_str());
            cJSON_AddItemToArray(files, file);
        }
        return jsonPrintAndDelete(root);
    }

    static bool readManifest(const std::filesystem::path& jobDirectory, InstalledBuild& out){
        std::string text;
        if(!readFile(jobDirectory / kManifestFile, text)) return false;
        JsonDocument doc(text);
        if(!doc.isObject()) return false;
        out.project = jsonString(doc.root(), "project");
        out.buildId = jsonString(doc.root(), "build");
        out.commit = jsonString(doc.root(), "commit");
        out.committed = jsonString(doc.root(), "committed");
        out.job = jsonString(doc.root(), "job");
        out.entry = jsonString(doc.root(), "entry");
        out.source = jsonString(doc.root(), "source");
        out.installedAt = jsonString(doc.root(), "installed");
        out.size = uint64_t(jsonNumber(doc.root(), "size"));
        out.directory = jobDirectory;
        return !out.project.empty() && !out.buildId.empty() && !out.job.empty() && isSafeRelativePath(out.entry);
    }

    //Streams one file to disk, hashing as it arrives so a 200 MB build is only read once.
    static bool downloadFile(const std::string& url, const IndexFile& file, const std::filesystem::path& out, InstallProgress& progress, std::string& error){
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);
        std::ofstream stream(out, std::ios::binary | std::ios::trunc);
        if(!stream){
            error = "can't write " + pathToUtf8(out);
            return false;
        }
        Sha256 hash;
        uint64_t received = 0;
        uint64_t startDone = progress.done.load();
        HttpOptions options;
        options.cancel = &progress.cancel;
        bool ok = httpGet(url, [&](const char* data, size_t size){
            received += size;
            if(received > file.size) return false;
            hash.update(data, size);
            stream.write(data, std::streamsize(size));
            if(!stream) return false;
            progress.done = startDone + received;
            if(progress.onProgress) progress.onProgress();
            return true;
        }, options, error);
        stream.close();
        if(ok && received != file.size){
            error = file.name + ": expected " + std::to_string(file.size) + " bytes, got " + std::to_string(received);
            ok = false;
        }else if(!ok && error == "aborted"){
            error = received > file.size ? file.name + ": larger than the index says" : "couldn't write " + file.name;
        }
        if(ok && hash.finish() != file.sha256){
            error = file.name + ": sha256 doesn't match the index";
            ok = false;
        }
        if(!ok) progress.done = startDone;
        return ok;
    }

    bool installBuild(const std::filesystem::path& projectsDirectory, const InstallTarget& target, InstallProgress& progress, std::string& error){
        if(!isSafeName(target.project) || !isSafeName(target.buildId) || !isSafeName(target.job.name)){
            error = "bad build name";
            return false;
        }
        if(target.job.sources.empty() || target.job.files.empty()){
            error = "no source has this build";
            return false;
        }
        std::error_code ec;
        std::filesystem::path projectDirectory = projectsDirectory / utf8ToPath(target.project);
        std::filesystem::path finalDirectory = projectDirectory / utf8ToPath(target.buildId) / utf8ToPath(target.job.name);
        std::filesystem::path incoming = projectDirectory / utf8ToPath(kIncomingPrefix + target.buildId + "-" + target.job.name);
        if(std::filesystem::exists(finalDirectory / kManifestFile, ec)) return true;

        std::filesystem::remove_all(incoming, ec);
        std::filesystem::create_directories(incoming, ec);
        if(ec){
            error = "can't create " + pathToUtf8(incoming) + ": " + ec.message();
            return false;
        }

        uint64_t total = target.job.totalSize();
        bool hasZip = std::any_of(target.job.files.begin(), target.job.files.end(), [](const IndexFile& f){ return endsWithNoCase(f.name, ".zip"); });
        //Room for the zip and what it unpacks to, plus a little slack.
        uint64_t needed = total * (hasZip ? 3 : 1) + 64ull * 1024 * 1024;
        std::filesystem::space_info space = std::filesystem::space(incoming, ec);
        if(!ec && space.available < needed){
            std::filesystem::remove_all(incoming, ec);
            error = "not enough disk space: needs " + formatSize(needed) + ", " + formatSize(space.available) + " free";
            return false;
        }

        progress.total = total;
        progress.done = 0;
        std::string usedSource;
        bool ok = true;
        for(size_t i = 0; i < target.job.files.size() && ok; i++){
            const IndexFile& file = target.job.files[i];
            std::filesystem::path out = incoming / utf8ToPath(file.name);
            ok = false;
            std::string firstError;
            for(const JobSource& source : target.job.sources){
                std::string attemptError;
                if(downloadFile(source.baseUrl + percentEncodePath(source.urlPaths[i]), file, out, progress, attemptError)){
                    ok = true;
                    if(usedSource.empty()) usedSource = source.sourceName;
                    break;
                }
                if(firstError.empty()) firstError = attemptError;
                if(progress.cancel) break;
            }
            if(!ok) error = progress.cancel ? "cancelled" : firstError;
            if(ok && (file.executable || endsWithNoCase(file.name, ".AppImage"))) addExecutePermission(out);
        }

        for(size_t i = 0; i < target.job.files.size() && ok; i++){
            const IndexFile& file = target.job.files[i];
            if(!endsWithNoCase(file.name, ".zip")) continue;
            std::filesystem::path zipPath = incoming / utf8ToPath(file.name);
            ok = extractZip(zipPath, zipPath.parent_path(), &progress.cancel, error);
            std::filesystem::remove(zipPath, ec);
        }

        std::string entry;
        if(ok){
            entry = findEntry(incoming, target.job.launch, jobPlatform(target.job.name), error);
            ok = !entry.empty();
        }
        if(ok){
            addExecutePermission(incoming / utf8ToPath(entry));
            ok = writeFileAtomically(incoming / kManifestFile, manifestJson(target, entry, usedSource, total));
            if(!ok) error = "couldn't write build.json";
        }
        if(ok){
            std::filesystem::create_directories(finalDirectory.parent_path(), ec);
            std::filesystem::rename(incoming, finalDirectory, ec);
            if(ec){
                error = "couldn't move the build into place: " + ec.message();
                ok = false;
            }
        }
        if(!ok) std::filesystem::remove_all(incoming, ec);
        return ok;
    }

    bool deleteInstalledBuild(const InstalledBuild& build, std::string& error){
        std::error_code ec;
        std::filesystem::remove_all(build.directory, ec);
        if(ec){
            error = "couldn't delete " + pathToUtf8(build.directory) + ": " + ec.message();
            return false;
        }
        std::filesystem::path buildDirectory = build.directory.parent_path();
        if(std::filesystem::is_empty(buildDirectory, ec)) std::filesystem::remove(buildDirectory, ec);
        return true;
    }

    std::vector<InstalledBuild> scanInstalled(const std::filesystem::path& projectsDirectory){
        std::vector<InstalledBuild> out;
        std::error_code ec;
        for(const auto& project : std::filesystem::directory_iterator(projectsDirectory, ec)){
            if(!project.is_directory(ec) || pathToUtf8(project.path().filename())[0] == '.') continue;
            for(const auto& build : std::filesystem::directory_iterator(project.path(), ec)){
                if(!build.is_directory(ec) || pathToUtf8(build.path().filename())[0] == '.') continue;
                for(const auto& job : std::filesystem::directory_iterator(build.path(), ec)){
                    InstalledBuild installed;
                    if(job.is_directory(ec) && readManifest(job.path(), installed)) out.push_back(installed);
                }
            }
        }
        return out;
    }

    void removeIncomingDirectories(const std::filesystem::path& projectsDirectory){
        std::error_code ec;
        for(const auto& project : std::filesystem::directory_iterator(projectsDirectory, ec)){
            if(!project.is_directory(ec)) continue;
            for(const auto& entry : std::filesystem::directory_iterator(project.path(), ec)){
                if(pathToUtf8(entry.path().filename()).rfind(kIncomingPrefix, 0) == 0) std::filesystem::remove_all(entry.path(), ec);
            }
        }
    }
}
