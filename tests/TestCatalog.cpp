#include "Test.h"

#include "BuildIndex.h"
#include "Catalog.h"
#include "Paths.h"

using namespace OML;

static std::string fixture(const std::string& relative){
    std::string text;
    readFile(OMLTest::fixturesDirectory() / relative, text);
    return text;
}

static ProjectIndex parseFixture(const std::string& relative, const std::string& url){
    ProjectIndex project;
    std::string error;
    bool ok = parseProjectIndex(fixture(relative), url, project, error);
    OMLTest::recordCheck(ok, "parses " + relative + " " + error, __FILE__, __LINE__);
    return project;
}

static const IndexJob* findJob(const IndexBuild& build, const std::string& name){
    for(const IndexJob& j : build.jobs) if(j.name == name) return &j;
    return nullptr;
}

TEST(urlHelpers){
    CHECK_EQUAL(urlDirectory("http://archive:8090/exampleGame/index.json"), std::string("http://archive:8090/exampleGame/"));
    CHECK_EQUAL(urlDirectory("http://host/a/index.json?nocache=1"), std::string("http://host/a/"));
    CHECK_EQUAL(urlDirectory("http://host"), std::string("http://host/"));
    CHECK_EQUAL(urlLastDirectoryName("https://builds.othermythos.com/avEngine/index.json"), std::string("avEngine"));
    CHECK_EQUAL(urlHost("http://archive:8090/index.json"), std::string("archive:8090"));
    CHECK_EQUAL(percentEncodePath("a b/c#d%.zip"), std::string("a%20b/c%23d%25.zip"));
}

TEST(detectsIndexKinds){
    CHECK(detectIndexKind(fixture("archive/index.json")) == IndexKind::ROOT);
    CHECK(detectIndexKind(fixture("public/index.json")) == IndexKind::ROOT);
    CHECK(detectIndexKind(fixture("archive/exampleGame/index.json")) == IndexKind::PROJECT);
    CHECK(detectIndexKind(fixture("public/avEngine/index.json")) == IndexKind::PROJECT);
    CHECK(detectIndexKind("<html>404</html>") == IndexKind::INVALID);
    CHECK(detectIndexKind("{\"something\": 1}") == IndexKind::INVALID);
}

TEST(parsesRootIndex){
    std::vector<RootIndexEntry> entries;
    std::string error;
    CHECK(parseRootIndex(fixture("archive/index.json"), "http://archive:8090/index.json", entries, error));
    CHECK_EQUAL(entries.size(), size_t(2));
    if(entries.size() == 2){
        CHECK_EQUAL(entries[0].name, std::string("avEngine"));
        CHECK_EQUAL(entries[0].indexUrl, std::string("http://archive:8090/avEngine/index.json"));
        CHECK_EQUAL(entries[1].indexUrl, std::string("http://archive:8090/exampleGame/index.json"));
    }
}

TEST(parsesArchiveProjectIndex){
    ProjectIndex p = parseFixture("archive/exampleGame/index.json", "http://archive:8090/exampleGame/index.json");
    CHECK_EQUAL(p.name, std::string("exampleGame"));
    CHECK_EQUAL(p.baseUrl, std::string("http://archive:8090/exampleGame/"));
    CHECK_EQUAL(p.builds.size(), size_t(3));
    if(p.builds.empty()) return;
    const IndexJob* job = findJob(p.builds[0], "linux-Release");
    CHECK(job != nullptr);
    if(!job) return;
    CHECK_EQUAL(job->status, std::string("ok"));
    CHECK_EQUAL(job->files.size(), size_t(1));
    CHECK_EQUAL(job->files[0].urlPath, std::string("20261007-194830-143694b/linux-Release/ExampleGame-Release-143694b.AppImage"));
    CHECK_EQUAL(job->files[0].size, uint64_t(224926200));
    CHECK(job->files[0].executable);
}

TEST(parsesPublicProjectIndex){
    ProjectIndex p = parseFixture("public/avEngine/index.json", "https://builds.example.com/avEngine/index.json");
    CHECK_EQUAL(p.builds.size(), size_t(2));
    if(p.builds.size() != 2) return;
    const IndexJob* failed = findJob(p.builds[0], "linux-Release");
    CHECK(failed && failed->status == "failed" && failed->files.empty());
    const IndexJob* pending = findJob(p.builds[0], "linux-Debug");
    CHECK(pending && pending->status == "pending");
    const IndexJob* ok = findJob(p.builds[1], "windows-Release");
    CHECK(ok && ok->status == "ok");
    //No 'path' on this host: files sit directly in the build's directory.
    if(ok && !ok->files.empty()) CHECK_EQUAL(ok->files[0].urlPath, std::string("20261007-143508-cbabfc2/avEngineWindows-Release.zip"));
}

TEST(unsafeFileMakesJobInvalid){
    std::string json = R"({"builds":[{"id":"1","jobs":{
        "linux-Release":{"status":"ok","files":[{"name":"../../.bashrc","size":1,"sha256":"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"}]},
        "linux-Debug":{"status":"ok","files":[{"name":"a","size":1,"sha256":"not-a-hash"}]}}},
        {"id":"../escape","jobs":{}}]})";
    ProjectIndex p;
    std::string error;
    CHECK(parseProjectIndex(json, "http://h/p/index.json", p, error));
    CHECK_EQUAL(p.builds.size(), size_t(1));
    if(p.builds.empty()) return;
    for(const IndexJob& j : p.builds[0].jobs){
        CHECK_EQUAL(j.status, std::string("invalid"));
        CHECK(j.files.empty());
    }
}

static std::vector<SourceSnapshot> fixtureSnapshots(){
    SourceSnapshot archive;
    archive.status.name = "archive";
    archive.projects.push_back(parseFixture("archive/avEngine/index.json", "http://archive:8090/avEngine/index.json"));
    archive.projects.push_back(parseFixture("archive/exampleGame/index.json", "http://archive:8090/exampleGame/index.json"));
    SourceSnapshot publicHost;
    publicHost.status.name = "public";
    publicHost.projects.push_back(parseFixture("public/avEngine/index.json", "https://builds.example.com/avEngine/index.json"));
    return {archive, publicHost};
}

static const CatalogProject* findProject(const std::vector<CatalogProject>& projects, const std::string& name){
    for(const CatalogProject& p : projects) if(p.name == name) return &p;
    return nullptr;
}

TEST(catalogMergesSourcesForThePlatform){
    std::vector<CatalogProject> projects = buildCatalog(fixtureSnapshots(), {}, "linux", {});
    CHECK_EQUAL(projects.size(), size_t(2));
    const CatalogProject* game = findProject(projects, "exampleGame");
    CHECK(game != nullptr);
    if(game){
        CHECK_EQUAL(game->builds.size(), size_t(3));
        CHECK_EQUAL(game->builds[0].commit, std::string("143694b"));
        CHECK_EQUAL(game->builds[2].commit, std::string("a1b2c3d"));
        //Release first; the windows job isn't this platform's.
        CHECK_EQUAL(game->builds[0].jobs.size(), size_t(2));
        CHECK_EQUAL(game->builds[0].jobs[0].name, std::string("linux-Release"));
    }
    const CatalogProject* engine = findProject(projects, "avEngine");
    CHECK(engine != nullptr);
    if(engine){
        //The public host's newer build only has failed and pending jobs, so it isn't offered.
        CHECK_EQUAL(engine->builds.size(), size_t(1));
        const CatalogJob& job = engine->builds[0].jobs[0];
        CHECK_EQUAL(job.sources.size(), size_t(2));
        if(job.sources.size() == 2){
            CHECK_EQUAL(job.sources[0].sourceName, std::string("archive"));
            CHECK_EQUAL(job.sources[0].baseUrl + job.sources[0].urlPaths[0],
                std::string("http://archive:8090/avEngine/20261007-143508-cbabfc2/linux-Release/av-x86_64-Release.AppImage"));
            CHECK_EQUAL(job.sources[1].baseUrl + job.sources[1].urlPaths[0],
                std::string("https://builds.example.com/avEngine/20261007-143508-cbabfc2/av-x86_64-Release.AppImage"));
        }
    }
}

TEST(catalogHidesProjectsAndAddsInstalledOnes){
    InstalledBuild installed;
    installed.project = "oldGame";
    installed.buildId = "20250101-000000-0000000";
    installed.commit = "0000000";
    installed.job = "linux-Release";
    installed.entry = "Old.AppImage";
    std::vector<CatalogProject> projects = buildCatalog(fixtureSnapshots(), {installed}, "linux", {"avEngine"});
    CHECK(findProject(projects, "avEngine") == nullptr);
    const CatalogProject* old = findProject(projects, "oldGame");
    CHECK(old && old->builds.size() == 1 && old->builds[0].jobs[0].installed && old->builds[0].jobs[0].sources.empty());

    std::vector<CatalogProject> windows = buildCatalog(fixtureSnapshots(), {}, "windows", {});
    const CatalogProject* game = findProject(windows, "exampleGame");
    CHECK(game && game->builds.size() == 1 && game->builds[0].jobs[0].name == "windows-Release");
}

TEST(fetchSourceCachesForOffline){
    std::filesystem::path cache = OMLTest::tempDirectory("cache");
    SourceConfig source{"archive", OMLTest::fileUrl(OMLTest::fixturesDirectory() / "archive" / "index.json")};

    SourceSnapshot online = fetchSource(source, cache, true);
    CHECK(online.status.state == SourceState::OK);
    CHECK_EQUAL(online.projects.size(), size_t(2));

    SourceSnapshot offline = fetchSource(source, cache, false);
    CHECK(offline.status.state == SourceState::CACHED);
    CHECK_EQUAL(offline.projects.size(), size_t(2));
    CHECK(offline.status.fetchedAt > 0);

    //A cache from a different url isn't trusted.
    SourceConfig moved{"archive", OMLTest::fileUrl(OMLTest::fixturesDirectory() / "missing" / "index.json")};
    SourceSnapshot gone = fetchSource(moved, cache, true);
    CHECK(gone.status.state == SourceState::FAILED);
    CHECK(gone.projects.empty());
    CHECK(!gone.status.error.empty());

    SourceConfig single{"public", OMLTest::fileUrl(OMLTest::fixturesDirectory() / "public" / "avEngine" / "index.json")};
    SourceSnapshot project = fetchSource(single, cache, true);
    CHECK(project.status.state == SourceState::OK);
    CHECK(project.projects.size() == 1 && project.projects[0].name == "avEngine");
}
