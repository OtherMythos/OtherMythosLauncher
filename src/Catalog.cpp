#include "Catalog.h"

#include "Http.h"
#include "Json.h"
#include "Paths.h"

#include <algorithm>
#include <cJSON.h>
#include <ctime>
#include <map>

namespace OML{
    //Cache layout per source: source.json is whatever the source url returned, projects/ holds
    //a root index's project indexes, and meta.json says which url and when.
    static const char* const kSourceFile = "source.json";
    static const char* const kMetaFile = "meta.json";

    std::string jobPlatform(const std::string& job){
        size_t dash = job.find('-');
        return dash == std::string::npos ? std::string() : job.substr(0, dash);
    }

    std::string jobBuildType(const std::string& job){
        size_t dash = job.find('-');
        return dash == std::string::npos ? job : job.substr(dash + 1);
    }

    static void writeMeta(const std::filesystem::path& dir, const std::string& url, int64_t fetchedAt){
        cJSON* meta = cJSON_CreateObject();
        cJSON_AddStringToObject(meta, "url", url.c_str());
        cJSON_AddNumberToObject(meta, "fetched", double(fetchedAt));
        writeFileAtomically(dir / kMetaFile, jsonPrintAndDelete(meta));
    }

    //Builds the snapshot from a source's index text, fetching (or reading cached) project indexes.
    static bool readSourceText(const SourceConfig& source, const std::string& text, const std::filesystem::path& dir,
            bool useNetwork, const std::atomic<bool>* cancel, SourceSnapshot& snapshot, std::string& error){
        IndexKind kind = detectIndexKind(text);
        if(kind == IndexKind::PROJECT){
            ProjectIndex project;
            if(!parseProjectIndex(text, source.url, project, error)) return false;
            snapshot.projects.push_back(project);
            return true;
        }
        if(kind != IndexKind::ROOT){
            error = "not a build index";
            return false;
        }
        std::vector<RootIndexEntry> entries;
        if(!parseRootIndex(text, source.url, entries, error)) return false;
        for(const RootIndexEntry& entry : entries){
            std::filesystem::path cachePath = dir / "projects" / (entry.name + ".json");
            std::string projectText;
            std::string projectError;
            bool fetched = useNetwork && httpGetString(entry.indexUrl, projectText, projectError, 20, cancel);
            if(fetched){
                writeFileAtomically(cachePath, projectText);
            }else if(!readFile(cachePath, projectText)){
                if(error.empty()) error = entry.name + ": " + projectError;
                continue;
            }
            ProjectIndex project;
            if(!parseProjectIndex(projectText, entry.indexUrl, project, projectError)){
                if(error.empty()) error = entry.name + ": " + projectError;
                continue;
            }
            project.name = entry.name;
            snapshot.projects.push_back(project);
        }
        return true;
    }

    SourceSnapshot fetchSource(const SourceConfig& source, const std::filesystem::path& cacheDirectory, bool useNetwork, const std::atomic<bool>* cancel){
        SourceSnapshot snapshot;
        snapshot.status.name = source.name;
        snapshot.status.url = source.url;
        std::filesystem::path dir = cacheDirectory / utf8ToPath(source.name);

        std::string text;
        std::string fetchError;
        if(useNetwork && httpGetString(source.url, text, fetchError, 20, cancel)){
            std::string parseError;
            if(readSourceText(source, text, dir, true, cancel, snapshot, parseError)){
                writeFileAtomically(dir / kSourceFile, text);
                int64_t now = int64_t(time(nullptr));
                writeMeta(dir, source.url, now);
                snapshot.status.state = SourceState::OK;
                snapshot.status.fetchedAt = now;
                //A root index can be fine while one of its projects isn't.
                snapshot.status.error = parseError;
                return snapshot;
            }
            fetchError = parseError;
            snapshot.projects.clear();
        }
        if(!useNetwork && fetchError.empty()) fetchError = "not fetched yet";

        //Fall back to the last good copy, as long as it came from the same url.
        std::string metaText;
        std::string cachedText;
        JsonDocument meta(readFile(dir / kMetaFile, metaText) ? metaText : std::string());
        if(jsonString(meta.root(), "url") == source.url && readFile(dir / kSourceFile, cachedText)){
            std::string cacheError;
            if(readSourceText(source, cachedText, dir, false, nullptr, snapshot, cacheError)){
                snapshot.status.state = SourceState::CACHED;
                snapshot.status.fetchedAt = int64_t(jsonNumber(meta.root(), "fetched"));
                snapshot.status.error = fetchError;
                return snapshot;
            }
        }
        snapshot.projects.clear();
        snapshot.status.state = SourceState::FAILED;
        snapshot.status.error = fetchError;
        return snapshot;
    }

    static bool sameFiles(const std::vector<IndexFile>& a, const std::vector<IndexFile>& b){
        if(a.size() != b.size()) return false;
        for(size_t i = 0; i < a.size(); i++){
            if(a[i].name != b[i].name || a[i].sha256 != b[i].sha256) return false;
        }
        return true;
    }

    static CatalogProject& findProject(std::vector<CatalogProject>& projects, const std::string& name){
        for(CatalogProject& p : projects) if(p.name == name) return p;
        projects.push_back(CatalogProject());
        projects.back().name = name;
        return projects.back();
    }

    static CatalogBuild& findBuild(CatalogProject& project, const std::string& id, const std::string& commit, const std::string& committed){
        for(CatalogBuild& b : project.builds) if(b.id == id) return b;
        project.builds.push_back(CatalogBuild());
        CatalogBuild& b = project.builds.back();
        b.id = id;
        b.commit = commit;
        b.committed = committed;
        return b;
    }

    static CatalogJob* findJob(CatalogBuild& build, const std::string& name){
        for(CatalogJob& j : build.jobs) if(j.name == name) return &j;
        return nullptr;
    }

    std::vector<CatalogProject> buildCatalog(const std::vector<SourceSnapshot>& sources, const std::vector<InstalledBuild>& installed,
            const std::string& platform, const std::vector<std::string>& hiddenProjects){
        auto hidden = [&](const std::string& name){
            return std::find(hiddenProjects.begin(), hiddenProjects.end(), name) != hiddenProjects.end();
        };
        std::vector<CatalogProject> projects;
        for(const SourceSnapshot& source : sources){
            for(const ProjectIndex& index : source.projects){
                if(hidden(index.name)) continue;
                for(const IndexBuild& indexBuild : index.builds){
                    for(const IndexJob& indexJob : indexBuild.jobs){
                        if(indexJob.status != "ok" || indexJob.files.empty() || jobPlatform(indexJob.name) != platform) continue;
                        CatalogProject& project = findProject(projects, index.name);
                        CatalogBuild& build = findBuild(project, indexBuild.id, indexBuild.commit, indexBuild.committed);
                        CatalogJob* job = findJob(build, indexJob.name);
                        if(!job){
                            build.jobs.push_back(CatalogJob());
                            job = &build.jobs.back();
                            job->name = indexJob.name;
                            job->launch = indexJob.launch;
                            job->files = indexJob.files;
                        }else if(!sameFiles(job->files, indexJob.files)){
                            continue;
                        }
                        JobSource where;
                        where.sourceName = source.status.name;
                        where.baseUrl = index.baseUrl;
                        for(const IndexFile& f : indexJob.files) where.urlPaths.push_back(f.urlPath);
                        job->sources.push_back(where);
                    }
                }
            }
        }

        for(const InstalledBuild& install : installed){
            if(hidden(install.project) || jobPlatform(install.job) != platform) continue;
            CatalogProject& project = findProject(projects, install.project);
            CatalogBuild& build = findBuild(project, install.buildId, install.commit, install.committed);
            CatalogJob* job = findJob(build, install.job);
            if(!job){
                build.jobs.push_back(CatalogJob());
                job = &build.jobs.back();
                job->name = install.job;
            }
            job->installed = true;
            job->install = install;
        }

        for(CatalogProject& project : projects){
            std::sort(project.builds.begin(), project.builds.end(), [](const CatalogBuild& a, const CatalogBuild& b){
                const std::string& ka = a.committed.empty() ? a.id : a.committed;
                const std::string& kb = b.committed.empty() ? b.id : b.committed;
                return ka != kb ? ka > kb : a.id > b.id;
            });
            for(CatalogBuild& build : project.builds){
                std::sort(build.jobs.begin(), build.jobs.end(), [](const CatalogJob& a, const CatalogJob& b){
                    bool ra = jobBuildType(a.name) == "Release";
                    bool rb = jobBuildType(b.name) == "Release";
                    return ra != rb ? ra : a.name < b.name;
                });
            }
        }
        std::sort(projects.begin(), projects.end(), [](const CatalogProject& a, const CatalogProject& b){
            return a.name < b.name;
        });
        return projects;
    }
}
