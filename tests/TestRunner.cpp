#include "Test.h"

#include "Paths.h"
#include "Runner.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

using namespace OML;

namespace{
    void setTestChild(const char* mode){
#ifdef _WIN32
        _putenv_s("OML_TEST_CHILD", mode ? mode : "");
#else
        if(mode) setenv("OML_TEST_CHILD", mode, 1);
        else unsetenv("OML_TEST_CHILD");
#endif
    }

    //This test binary, installed as if it were a game, and run in one of TestMain's child modes.
    struct FakeGame{
        std::mutex mutex;
        std::condition_variable done;
        bool finished = false;
        RunRecord result;
        std::shared_ptr<ChildProcess> process;
        std::filesystem::path runs;

        bool start(const std::filesystem::path& root, const char* mode){
            InstalledBuild build;
            build.project = "game";
            build.buildId = "20261007-194830-abc1234";
            build.commit = "abc1234";
            build.job = "linux-Release";
            build.directory = root / "projects" / "game" / build.buildId / build.job;
            std::filesystem::create_directories(build.directory);
            std::filesystem::path exe = OMLTest::testExecutable();
            std::filesystem::copy_file(exe, build.directory / exe.filename());
            build.entry = exe.filename().u8string();
            runs = root / "runs";

            setTestChild(mode);
            std::string error;
            bool started = launchBuild(runs, build, [this](const RunRecord& r){
                std::lock_guard<std::mutex> lock(mutex);
                result = r;
                finished = true;
                done.notify_one();
            }, process, error);
            setTestChild(nullptr);
            OMLTest::recordCheck(started && error.empty(), "launchBuild: " + error, __FILE__, __LINE__);
            return started;
        }

        bool waitForExit(int seconds){
            std::unique_lock<std::mutex> lock(mutex);
            return done.wait_for(lock, std::chrono::seconds(seconds), [this]{ return finished; });
        }

        //Until the child has put its signal handler (or window) in place, a stop request would
        //just kill it, which isn't what's being tested.
        bool waitUntilReady(){
            for(int i = 0; i < 200; i++){
                std::string log;
                for(const auto& entry : std::filesystem::directory_iterator(runs)){
                    if(readFile(entry.path() / "output.log", log) && log.find("ready") != std::string::npos) return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return false;
        }
    };
}

//With OML_TEST_CHILD set the binary prints its run directory and exits 3, which checks the
//spawn, the working directory, the log redirect and the environment.
TEST(launchesAndRecordsARun){
    std::filesystem::path root = OMLTest::tempDirectory("run");
    FakeGame game;
    if(!game.start(root, "1")) return;
    CHECK(game.waitForExit(30));
    CHECK_EQUAL(game.result.exitCode, 3);
    CHECK(!game.result.crashed);
    CHECK(!game.result.stoppedByLauncher);
    CHECK_EQUAL(game.result.description, std::string("exit code 3"));

    std::string log;
    CHECK(readFile(game.result.directory / "output.log", log));
    CHECK(log.find("child ran in " + game.result.directory.u8string()) != std::string::npos);

    std::map<std::string, RunRecord> runs = latestRuns(root / "runs");
    CHECK(runs.count("game") == 1 && runs["game"].exitCode == 3 && runs["game"].commit == "abc1234");
    //Asking a game that has already gone to stop does nothing.
    CHECK(!game.process->requestStop());
    CHECK(!game.process->forceStop());
}

TEST(quitsAGameWhenAsked){
    std::filesystem::path root = OMLTest::tempDirectory("quit");
    FakeGame game;
    if(!game.start(root, "wait")) return;
    CHECK(game.waitUntilReady());
    CHECK(game.process->requestStop());
    CHECK(game.waitForExit(30));
    CHECK_EQUAL(game.result.exitCode, 7);
    CHECK(game.result.stoppedByLauncher);
    CHECK_EQUAL(game.result.description, std::string("quit from the launcher"));
    std::map<std::string, RunRecord> runs = latestRuns(root / "runs");
    CHECK(runs.count("game") == 1 && runs["game"].stoppedByLauncher);
}

TEST(forceQuitsAGameThatWontClose){
    std::filesystem::path root = OMLTest::tempDirectory("forceQuit");
    FakeGame game;
    if(!game.start(root, "stubborn")) return;
    CHECK(game.waitUntilReady());
    game.process->requestStop();
    CHECK(!game.waitForExit(1));
    CHECK(game.process->forceStop());
    CHECK(game.waitForExit(30));
    CHECK(game.result.stoppedByLauncher);
    CHECK_EQUAL(game.result.description, std::string("force quit from the launcher"));
}
