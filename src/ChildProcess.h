#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace OML{
    struct ProcessExit{
        bool crashed = false;
        int code = 0;
        //"exited 0", "exit code 3", "crashed (SIGSEGV)", "crashed (0xC0000005 access violation)"
        std::string description;
        //requestStop() or forceStop() was called before it exited.
        bool stopRequested = false;
        bool forced = false;
    };

    //A child process with its stdout and stderr in a log file. ChildProcessPosix.cpp, ChildProcessWin.cpp.
    //Not called Process.h: on Windows that name shadows the CRT's <process.h>, which <thread> needs.
    class ChildProcess{
    public:
        ChildProcess();
        ~ChildProcess();
        ChildProcess(const ChildProcess&) = delete;
        ChildProcess& operator=(const ChildProcess&) = delete;

        bool start(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory, const std::filesystem::path& logFile,
            const std::vector<std::pair<std::string, std::string>>& environment, std::string& error);
        //Blocks until the process exits. Called on one thread; the stop calls may come from another.
        ProcessExit wait();

        //Asks it to close the way a user would: SIGTERM, or WM_CLOSE to its windows on Windows.
        //False once it has exited.
        bool requestStop();
        //Ends it at once: SIGKILL, TerminateProcess.
        bool forceStop();

    private:
        //Guards the stop calls against the process exiting under them. On POSIX, wait() marks the
        //exit before reaping, so a pid the system has handed to another process is never signalled.
        std::mutex mMutex;
        bool mExited;
        bool mStopRequested;
        bool mForced;
#ifdef _WIN32
        void* mProcess;
#else
        int mPid;
#endif
    };
}
