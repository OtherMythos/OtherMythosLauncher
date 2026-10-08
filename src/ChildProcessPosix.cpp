#include "ChildProcess.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace OML{
    ChildProcess::ChildProcess()
        : mExited(false),
          mStopRequested(false),
          mForced(false),
          mPid(-1) {
    }

    ChildProcess::~ChildProcess(){
    }

    bool ChildProcess::start(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory, const std::filesystem::path& logFile,
            const std::vector<std::pair<std::string, std::string>>& environment, std::string& error){
        //Built here rather than with setenv, which isn't safe while other threads may call getenv.
        std::vector<std::string> envStrings;
        for(char** e = environ; *e; e++){
            std::string entry = *e;
            bool replaced = false;
            for(const auto& kv : environment) if(entry.compare(0, kv.first.size() + 1, kv.first + "=") == 0) replaced = true;
            if(!replaced) envStrings.push_back(entry);
        }
        for(const auto& kv : environment) envStrings.push_back(kv.first + "=" + kv.second);
        std::vector<char*> envp;
        for(std::string& s : envStrings) envp.push_back(&s[0]);
        envp.push_back(nullptr);

        std::string exe = executable.string();
        std::vector<char*> argv = {&exe[0], nullptr};

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
        posix_spawn_file_actions_addopen(&actions, 1, logFile.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, 1, 2);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
        posix_spawn_file_actions_addchdir_np(&actions, workingDirectory.c_str());
#pragma GCC diagnostic pop

        //A spawned child inherits ignored signals and the signal mask, so put both back to the
        //defaults; the game shouldn't depend on what the launcher happened to block.
        posix_spawnattr_t attr;
        posix_spawnattr_init(&attr);
        sigset_t all;
        sigset_t none;
        sigfillset(&all);
        sigemptyset(&none);
        posix_spawnattr_setsigdefault(&attr, &all);
        posix_spawnattr_setsigmask(&attr, &none);
        posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);

        pid_t pid = -1;
        int result = posix_spawn(&pid, exe.c_str(), &actions, &attr, argv.data(), envp.data());
        posix_spawn_file_actions_destroy(&actions);
        posix_spawnattr_destroy(&attr);
        if(result != 0){
            error = "couldn't start " + executable.filename().string() + ": " + strerror(result);
            return false;
        }
        mPid = pid;
        return true;
    }

    ProcessExit ChildProcess::wait(){
        ProcessExit exit;
        if(mPid < 0){
            exit.crashed = true;
            exit.description = "not started";
            return exit;
        }
        //Wait without reaping first: until it's reaped the pid can't be reused, so a stop request
        //racing the exit can't signal some other process.
        siginfo_t info;
        while(waitid(P_PID, id_t(mPid), &info, WEXITED | WNOWAIT) < 0 && errno == EINTR){
        }
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mExited = true;
            exit.stopRequested = mStopRequested;
            exit.forced = mForced;
        }
        int status = 0;
        while(waitpid(mPid, &status, 0) < 0 && errno == EINTR){
        }
        if(WIFSIGNALED(status)){
            int sig = WTERMSIG(status);
            exit.crashed = true;
            exit.code = 128 + sig;
            const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS" : sig == SIGILL ? "SIGILL"
                : sig == SIGFPE ? "SIGFPE" : sig == SIGKILL ? "SIGKILL" : sig == SIGTERM ? "SIGTERM" : nullptr;
            exit.description = std::string("crashed (") + (name ? name : "signal " + std::to_string(sig)) + ")";
            if(sig == SIGTERM || sig == SIGKILL){
                exit.crashed = false;
                exit.description = std::string("killed (") + name + ")";
            }
        }else{
            exit.code = WEXITSTATUS(status);
            exit.description = exit.code == 0 ? "exited 0" : "exit code " + std::to_string(exit.code);
        }
        return exit;
    }

    bool ChildProcess::requestStop(){
        std::lock_guard<std::mutex> lock(mMutex);
        if(mExited || mPid < 0) return false;
        mStopRequested = true;
        //The direct child only. For an AppImage that's the game itself (AppRun execs it); the
        //AppImage's FUSE runtime is a separate process, and ending it first pulls the mounted files
        //out from under the game, which then dies of SIGBUS.
        return kill(mPid, SIGTERM) == 0;
    }

    bool ChildProcess::forceStop(){
        std::lock_guard<std::mutex> lock(mMutex);
        if(mExited || mPid < 0) return false;
        mStopRequested = true;
        mForced = true;
        return kill(mPid, SIGKILL) == 0;
    }
}
