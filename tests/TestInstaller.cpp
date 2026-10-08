#include "Test.h"

#include "Archive.h"
#include "Catalog.h"
#include "Installer.h"
#include "Paths.h"
#include "Sha256.h"

#include <map>
#include <miniz.h>

using namespace OML;

namespace{
    struct HostFile{
        std::string name;
        std::string content;
        bool executable;
    };

    //Lays out a project the way the private archive does (<id>/<job>/<name>, with 'path'), or the
    //way builds.othermythos.com does (<id>/<name>), and returns its index url.
    std::string makeHost(const std::filesystem::path& host, const std::string& project, const std::string& buildId, const std::string& job,
            const std::vector<HostFile>& files, bool archiveLayout, bool corruptHash = false){
        std::string filesJson;
        for(const HostFile& f : files){
            std::string relative = archiveLayout ? buildId + "/" + job + "/" + f.name : buildId + "/" + f.name;
            OMLTest::writeText(host / project / relative, f.content);
            std::string sha = Sha256::hashString(corruptHash ? f.content + "x" : f.content);
            if(!filesJson.empty()) filesJson += ",";
            filesJson += "{\"name\":\"" + f.name + "\",\"size\":" + std::to_string(f.content.size()) + ",\"sha256\":\"" + sha + "\""
                + (archiveLayout ? ",\"path\":\"" + relative + "\"" : std::string())
                + (f.executable ? ",\"executable\":true" : "") + "}";
        }
        std::string index = "{\"builds\":[{\"id\":\"" + buildId + "\",\"commit\":\"abc1234\",\"committed\":\"2026-10-07T19:48:30Z\",\"jobs\":{\""
            + job + "\":{\"status\":\"ok\",\"files\":[" + filesJson + "]}}}]}";
        OMLTest::writeText(host / project / "index.json", index);
        return OMLTest::fileUrl(host / project / "index.json");
    }

    std::string makeZip(const std::vector<std::pair<std::string, std::string>>& entries){
        mz_zip_archive zip;
        mz_zip_zero_struct(&zip);
        mz_zip_writer_init_heap(&zip, 0, 0);
        for(const auto& e : entries){
            mz_zip_writer_add_mem(&zip, e.first.c_str(), e.second.data(), e.second.size(), MZ_DEFAULT_COMPRESSION);
        }
        void* data = nullptr;
        size_t size = 0;
        mz_zip_writer_finalize_heap_archive(&zip, &data, &size);
        std::string out(static_cast<const char*>(data), size);
        mz_zip_writer_end(&zip);
        return out;
    }

    //The first job of the newest build, as the UI would hand it to the installer.
    bool targetFrom(const std::vector<SourceConfig>& sources, const std::filesystem::path& cache, const std::string& platform, InstallTarget& target){
        std::vector<SourceSnapshot> snapshots;
        for(const SourceConfig& s : sources) snapshots.push_back(fetchSource(s, cache, true));
        std::vector<CatalogProject> projects = buildCatalog(snapshots, {}, platform, {});
        if(projects.empty() || projects[0].builds.empty() || projects[0].builds[0].jobs.empty()) return false;
        const CatalogBuild& build = projects[0].builds[0];
        target.project = projects[0].name;
        target.buildId = build.id;
        target.commit = build.commit;
        target.committed = build.committed;
        target.job = build.jobs[0];
        return true;
    }

    bool hasIncoming(const std::filesystem::path& projectDirectory){
        std::error_code ec;
        for(const auto& e : std::filesystem::directory_iterator(projectDirectory, ec)){
            if(e.path().filename().string().rfind(".incoming-", 0) == 0) return true;
        }
        return false;
    }
}

TEST(installsAppImageFromArchiveLayout){
    std::filesystem::path root = OMLTest::tempDirectory("installAppImage");
    std::string url = makeHost(root / "host", "game", "20261007-194830-abc1234", "linux-Release",
        {{"Game-Release-abc1234.AppImage", std::string(200000, 'g'), true}}, true);
    InstallTarget target;
    CHECK(targetFrom({{"archive", url}}, root / "cache", "linux", target));

    InstallProgress progress;
    int calls = 0;
    progress.onProgress = [&]{ calls++; };
    std::string error;
    CHECK(installBuild(root / "projects", target, progress, error));
    CHECK_EQUAL(error, std::string());
    CHECK_EQUAL(progress.done.load(), uint64_t(200000));
    CHECK(calls > 0);

    std::filesystem::path jobDirectory = root / "projects" / "game" / "20261007-194830-abc1234" / "linux-Release";
    CHECK(std::filesystem::is_regular_file(jobDirectory / "Game-Release-abc1234.AppImage"));
    CHECK(std::filesystem::is_regular_file(jobDirectory / "build.json"));
#ifndef _WIN32
    auto perms = std::filesystem::status(jobDirectory / "Game-Release-abc1234.AppImage").permissions();
    CHECK((perms & std::filesystem::perms::owner_exec) != std::filesystem::perms::none);
#endif

    std::vector<InstalledBuild> installed = scanInstalled(root / "projects");
    CHECK_EQUAL(installed.size(), size_t(1));
    if(installed.size() == 1){
        CHECK_EQUAL(installed[0].entry, std::string("Game-Release-abc1234.AppImage"));
        CHECK_EQUAL(installed[0].source, std::string("archive"));
        CHECK_EQUAL(installed[0].commit, std::string("abc1234"));
        CHECK_EQUAL(installed[0].size, uint64_t(200000));
        CHECK(deleteInstalledBuild(installed[0], error));
        CHECK(!std::filesystem::exists(root / "projects" / "game" / "20261007-194830-abc1234"));
    }
}

TEST(installsAndExtractsZipFromPublicLayout){
    std::filesystem::path root = OMLTest::tempDirectory("installZip");
    std::string zip = makeZip({{"Game.exe", "MZ fake"}, {"Game.dll", "dll"}, {"essential/common/a.material", "material"}});
    std::string url = makeHost(root / "host", "game", "20261007-194830-abc1234", "windows-Release", {{"GameWindows-Release.zip", zip, false}}, false);
    InstallTarget target;
    CHECK(targetFrom({{"public", url}}, root / "cache", "windows", target));
    InstallProgress progress;
    std::string error;
    CHECK(installBuild(root / "projects", target, progress, error));
    CHECK_EQUAL(error, std::string());
    std::filesystem::path jobDirectory = root / "projects" / "game" / "20261007-194830-abc1234" / "windows-Release";
    CHECK(std::filesystem::is_regular_file(jobDirectory / "essential" / "common" / "a.material"));
    CHECK(!std::filesystem::exists(jobDirectory / "GameWindows-Release.zip"));
    std::vector<InstalledBuild> installed = scanInstalled(root / "projects");
    CHECK(installed.size() == 1 && installed[0].entry == "Game.exe");
}

TEST(rejectsHashMismatch){
    std::filesystem::path root = OMLTest::tempDirectory("badHash");
    std::string url = makeHost(root / "host", "game", "1", "linux-Release", {{"Game.AppImage", "content", true}}, true, true);
    InstallTarget target;
    CHECK(targetFrom({{"archive", url}}, root / "cache", "linux", target));
    InstallProgress progress;
    std::string error;
    CHECK(!installBuild(root / "projects", target, progress, error));
    CHECK(error.find("sha256") != std::string::npos);
    CHECK(!std::filesystem::exists(root / "projects" / "game" / "1"));
    CHECK(!hasIncoming(root / "projects" / "game"));
}

TEST(fallsBackToTheNextSource){
    std::filesystem::path root = OMLTest::tempDirectory("fallback");
    std::string good = makeHost(root / "good", "game", "1", "linux-Release", {{"Game.AppImage", "content", true}}, true);
    makeHost(root / "broken", "game", "1", "linux-Release", {{"Game.AppImage", "content", true}}, true);
    std::filesystem::remove(root / "broken" / "game" / "1" / "linux-Release" / "Game.AppImage");
    InstallTarget target;
    CHECK(targetFrom({{"broken", OMLTest::fileUrl(root / "broken" / "game" / "index.json")}, {"good", good}}, root / "cache", "linux", target));
    CHECK_EQUAL(target.job.sources.size(), size_t(2));
    InstallProgress progress;
    std::string error;
    CHECK(installBuild(root / "projects", target, progress, error));
    std::vector<InstalledBuild> installed = scanInstalled(root / "projects");
    CHECK(installed.size() == 1 && installed[0].source == "good");
}

TEST(cancelLeavesNothingBehind){
    std::filesystem::path root = OMLTest::tempDirectory("cancel");
    std::string url = makeHost(root / "host", "game", "1", "linux-Release", {{"Game.AppImage", std::string(500000, 'x'), true}}, true);
    InstallTarget target;
    CHECK(targetFrom({{"archive", url}}, root / "cache", "linux", target));
    InstallProgress progress;
    progress.cancel = true;
    std::string error;
    CHECK(!installBuild(root / "projects", target, progress, error));
    CHECK_EQUAL(error, std::string("cancelled"));
    CHECK(!hasIncoming(root / "projects" / "game"));
}

TEST(removesInterruptedDownloads){
    std::filesystem::path root = OMLTest::tempDirectory("incoming");
    OMLTest::writeText(root / "projects" / "game" / ".incoming-1-linux-Release" / "half.AppImage", "half");
    OMLTest::writeText(root / "projects" / "game" / "1" / "linux-Release" / "keep", "keep");
    removeIncomingDirectories(root / "projects");
    CHECK(!std::filesystem::exists(root / "projects" / "game" / ".incoming-1-linux-Release"));
    CHECK(std::filesystem::exists(root / "projects" / "game" / "1" / "linux-Release" / "keep"));
}

TEST(zipCantEscapeItsDirectory){
    std::filesystem::path root = OMLTest::tempDirectory("zipSlip");
    OMLTest::writeText(root / "evil.zip", makeZip({{"fine.txt", "ok"}, {"../evil.txt", "gotcha"}}));
    std::filesystem::create_directories(root / "out");
    std::string error;
    CHECK(!extractZip(root / "evil.zip", root / "out", nullptr, error));
    CHECK(error.find("unsafe") != std::string::npos);
    CHECK(!std::filesystem::exists(root / "evil.txt"));
}

TEST(findsTheEntryPoint){
    std::filesystem::path root = OMLTest::tempDirectory("entry");
    std::string error;

    OMLTest::writeText(root / "nested" / "Game" / "Game.exe", "MZ");
    OMLTest::writeText(root / "nested" / "Game" / "Game.dll", "dll");
    CHECK_EQUAL(findEntry(root / "nested", "", "windows", error), std::string("Game/Game.exe"));

    OMLTest::writeText(root / "two" / "a.exe", "MZ");
    OMLTest::writeText(root / "two" / "b.exe", "MZ");
    CHECK_EQUAL(findEntry(root / "two", "", "windows", error), std::string());
    CHECK(error.find("more than one") != std::string::npos);
    CHECK_EQUAL(findEntry(root / "two", "b.exe", "windows", error), std::string("b.exe"));
    CHECK_EQUAL(findEntry(root / "two", "c.exe", "windows", error), std::string());

    OMLTest::writeText(root / "image" / "Game.AppImage", "elf");
    OMLTest::writeText(root / "image" / "readme.txt", "txt");
    CHECK_EQUAL(findEntry(root / "image", "", "linux", error), std::string("Game.AppImage"));

#ifndef _WIN32
    //Windows reports every file as executable, so a Linux job's exec bits only mean anything
    //on a POSIX host; the launcher only ever looks at its own platform's jobs.
    OMLTest::writeText(root / "empty" / "readme.txt", "txt");
    CHECK_EQUAL(findEntry(root / "empty", "", "linux", error), std::string());
#endif
}
