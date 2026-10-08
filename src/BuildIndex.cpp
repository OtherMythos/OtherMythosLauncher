#include "BuildIndex.h"

#include "Json.h"
#include "SafePath.h"

#include <cJSON.h>

namespace OML{
    static bool isSha256(const std::string& s){
        if(s.size() != 64) return false;
        for(char c : s){
            if(!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
        }
        return true;
    }

    static std::string stripQuery(const std::string& url){
        size_t q = url.find_first_of("?#");
        return q == std::string::npos ? url : url.substr(0, q);
    }

    std::string urlDirectory(const std::string& url){
        std::string u = stripQuery(url);
        size_t slash = u.rfind('/');
        size_t scheme = u.find("://");
        if(slash == std::string::npos || (scheme != std::string::npos && slash < scheme + 3)) return u + "/";
        return u.substr(0, slash + 1);
    }

    std::string urlLastDirectoryName(const std::string& url){
        std::string dir = urlDirectory(url);
        dir.pop_back();
        size_t slash = dir.rfind('/');
        return slash == std::string::npos ? dir : dir.substr(slash + 1);
    }

    std::string urlHost(const std::string& url){
        size_t scheme = url.find("://");
        size_t start = scheme == std::string::npos ? 0 : scheme + 3;
        size_t end = url.find('/', start);
        return url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    }

    std::string percentEncodePath(const std::string& path){
        static const char kHex[] = "0123456789ABCDEF";
        std::string out;
        for(unsigned char c : path){
            bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
                || c == '-' || c == '.' || c == '_' || c == '~' || c == '/';
            if(plain){
                out.push_back(char(c));
            }else{
                out.push_back('%');
                out.push_back(kHex[c >> 4]);
                out.push_back(kHex[c & 0xf]);
            }
        }
        return out;
    }

    IndexKind detectIndexKind(const std::string& json){
        JsonDocument doc(json);
        if(!doc.isObject()) return IndexKind::INVALID;
        if(cJSON_IsObject(jsonChild(doc.root(), "projects"))) return IndexKind::ROOT;
        if(cJSON_IsArray(jsonChild(doc.root(), "builds"))) return IndexKind::PROJECT;
        return IndexKind::INVALID;
    }

    bool parseRootIndex(const std::string& json, const std::string& url, std::vector<RootIndexEntry>& out, std::string& error){
        JsonDocument doc(json);
        const cJSON* projects = jsonChild(doc.root(), "projects");
        if(!cJSON_IsObject(projects)){
            error = "not a root index (no 'projects')";
            return false;
        }
        out.clear();
        std::string base = urlDirectory(url);
        const cJSON* project = nullptr;
        cJSON_ArrayForEach(project, projects){
            std::string name = project->string ? project->string : "";
            std::string index = jsonString(project, "index", name + "/index.json");
            if(!isSafeName(name) || !isSafeRelativePath(index)) continue;
            out.push_back({name, base + percentEncodePath(index)});
        }
        return true;
    }

    static void parseJob(const cJSON* jobJson, const std::string& jobName, const std::string& buildId, IndexJob& job){
        job.name = jobName;
        job.status = jsonString(jobJson, "status", "ok");
        std::string launch = jsonString(jobJson, "launch");
        if(isSafeRelativePath(launch)) job.launch = launch;

        const cJSON* files = jsonChild(jobJson, "files");
        const cJSON* fileJson = nullptr;
        cJSON_ArrayForEach(fileJson, files){
            IndexFile file;
            file.name = jsonString(fileJson, "name");
            file.urlPath = jsonString(fileJson, "path", buildId + "/" + file.name);
            file.sha256 = jsonString(fileJson, "sha256");
            double size = jsonNumber(fileJson, "size", -1.0);
            file.executable = jsonBool(fileJson, "executable");
            //One bad file makes the whole job unusable rather than silently incomplete.
            if(!isSafeRelativePath(file.name) || !isSafeRelativePath(file.urlPath) || !isSha256(file.sha256) || size < 0){
                job.status = "invalid";
                job.files.clear();
                return;
            }
            file.size = uint64_t(size);
            job.files.push_back(file);
        }
    }

    bool parseProjectIndex(const std::string& json, const std::string& url, ProjectIndex& out, std::string& error){
        JsonDocument doc(json);
        const cJSON* builds = jsonChild(doc.root(), "builds");
        if(!cJSON_IsArray(builds)){
            error = "not a project index (no 'builds')";
            return false;
        }
        out = ProjectIndex();
        out.name = urlLastDirectoryName(url);
        out.baseUrl = urlDirectory(url);
        if(!isSafeName(out.name)){
            error = "can't tell the project's name from " + url;
            return false;
        }

        const cJSON* buildJson = nullptr;
        cJSON_ArrayForEach(buildJson, builds){
            IndexBuild build;
            build.id = jsonString(buildJson, "id");
            if(!isSafeName(build.id)) continue;
            build.commit = jsonString(buildJson, "commit");
            build.committed = jsonString(buildJson, "committed");

            const cJSON* jobs = jsonChild(buildJson, "jobs");
            const cJSON* jobJson = nullptr;
            cJSON_ArrayForEach(jobJson, jobs){
                std::string jobName = jobJson->string ? jobJson->string : "";
                if(!isSafeName(jobName)) continue;
                IndexJob job;
                parseJob(jobJson, jobName, build.id, job);
                build.jobs.push_back(job);
            }
            out.builds.push_back(build);
        }
        return true;
    }
}
