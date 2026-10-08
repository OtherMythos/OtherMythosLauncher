#include "Runner.h"

#include "Json.h"
#include "Paths.h"
#include "TimeFormat.h"

#include <algorithm>
#include <cJSON.h>
#include <ctime>
#include <memory>
#include <thread>
#include <vector>

namespace OML{
    static const size_t kKeepRuns = 20;
    static const char* const kRunFile = "run.json";

    static std::string runJson(const RunRecord& r){
        cJSON* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "project", r.project.c_str());
        cJSON_AddStringToObject(root, "build", r.buildId.c_str());
        cJSON_AddStringToObject(root, "commit", r.commit.c_str());
        cJSON_AddStringToObject(root, "job", r.job.c_str());
        cJSON_AddStringToObject(root, "started", isoTimeUtc(r.started).c_str());
        cJSON_AddStringToObject(root, "ended", isoTimeUtc(r.ended).c_str());
        cJSON_AddNumberToObject(root, "exitCode", r.exitCode);
        cJSON_AddBoolToObject(root, "crashed", r.crashed);
        cJSON_AddBoolToObject(root, "stoppedByLauncher", r.stoppedByLauncher);
        cJSON_AddStringToObject(root, "result", r.description.c_str());
        return jsonPrintAndDelete(root);
    }

    static bool readRun(const std::filesystem::path& directory, RunRecord& r){
        std::string text;
        if(!readFile(directory / kRunFile, text)) return false;
        JsonDocument doc(text);
        if(!doc.isObject()) return false;
        r.project = jsonString(doc.root(), "project");
        r.buildId = jsonString(doc.root(), "build");
        r.commit = jsonString(doc.root(), "commit");
        r.job = jsonString(doc.root(), "job");
        parseIsoUtc(jsonString(doc.root(), "started"), r.started);
        parseIsoUtc(jsonString(doc.root(), "ended"), r.ended);
        r.exitCode = int(jsonNumber(doc.root(), "exitCode"));
        r.crashed = jsonBool(doc.root(), "crashed");
        r.stoppedByLauncher = jsonBool(doc.root(), "stoppedByLauncher");
        r.description = jsonString(doc.root(), "result");
        r.directory = directory;
        return !r.project.empty();
    }

    //Run directories sort by time, so the oldest are the first names.
    static void pruneRuns(const std::filesystem::path& runsDirectory){
        std::error_code ec;
        std::vector<std::filesystem::path> runs;
        for(const auto& entry : std::filesystem::directory_iterator(runsDirectory, ec)){
            if(entry.is_directory(ec)) runs.push_back(entry.path());
        }
        if(runs.size() <= kKeepRuns) return;
        std::sort(runs.begin(), runs.end());
        for(size_t i = 0; i + kKeepRuns < runs.size(); i++) std::filesystem::remove_all(runs[i], ec);
    }

    bool launchBuild(const std::filesystem::path& runsDirectory, const InstalledBuild& build, std::function<void(const RunRecord&)> onExit,
            std::shared_ptr<ChildProcess>& process, std::string& error){
        auto record = std::make_shared<RunRecord>();
        record->project = build.project;
        record->buildId = build.buildId;
        record->commit = build.commit;
        record->job = build.job;
        record->started = int64_t(time(nullptr));

        std::error_code ec;
        std::filesystem::path directory = runsDirectory / utf8ToPath(compactTimeUtc(record->started) + "-" + build.project);
        for(int i = 2; std::filesystem::exists(directory, ec); i++){
            directory = runsDirectory / utf8ToPath(compactTimeUtc(record->started) + "-" + build.project + "-" + std::to_string(i));
        }
        std::filesystem::create_directories(directory, ec);
        if(ec){
            error = "can't create " + pathToUtf8(directory) + ": " + ec.message();
            return false;
        }
        record->directory = directory;
        pruneRuns(runsDirectory);

        std::filesystem::path executable = build.directory / utf8ToPath(build.entry);
#ifndef _WIN32
        //A build copied in by hand, or unpacked by a tool that dropped the mode bits.
        std::filesystem::permissions(executable, std::filesystem::perms::owner_exec, std::filesystem::perm_options::add, ec);
#endif
        std::vector<std::pair<std::string, std::string>> environment = {
            {"OTHERMYTHOS_LAUNCHER", "1"},
            {"OTHERMYTHOS_RUN_DIR", pathToUtf8(directory)},
            {"OTHERMYTHOS_PROJECT", build.project},
            {"OTHERMYTHOS_BUILD", build.buildId},
        };
        process = std::make_shared<ChildProcess>();
        if(!process->start(executable, executable.parent_path(), directory / "output.log", environment, error)){
            record->ended = record->started;
            record->crashed = true;
            record->description = error;
            writeFileAtomically(directory / kRunFile, runJson(*record));
            return false;
        }

        std::thread([process, record, onExit]{
            ProcessExit exit = process->wait();
            record->ended = int64_t(time(nullptr));
            record->exitCode = exit.code;
            record->crashed = exit.crashed;
            record->description = exit.description;
            record->stoppedByLauncher = exit.stopRequested;
            //A crash while closing is still a crash; otherwise say what actually ended it.
            if(exit.forced) record->description = "force quit from the launcher";
            else if(exit.stopRequested && !exit.crashed) record->description = "quit from the launcher";
            writeFileAtomically(record->directory / kRunFile, runJson(*record));
            if(onExit) onExit(*record);
        }).detach();
        return true;
    }

    std::map<std::string, RunRecord> latestRuns(const std::filesystem::path& runsDirectory){
        std::map<std::string, RunRecord> out;
        std::error_code ec;
        std::vector<std::filesystem::path> runs;
        for(const auto& entry : std::filesystem::directory_iterator(runsDirectory, ec)){
            if(entry.is_directory(ec)) runs.push_back(entry.path());
        }
        std::sort(runs.begin(), runs.end());
        for(const std::filesystem::path& dir : runs){
            RunRecord r;
            if(readRun(dir, r)) out[r.project] = r;
        }
        return out;
    }
}
