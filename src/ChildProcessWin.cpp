#include "ChildProcess.h"

#include <cstdio>
#include <cwchar>
#include <windows.h>

namespace OML{
    ChildProcess::ChildProcess()
        : mExited(false),
          mStopRequested(false),
          mForced(false),
          mProcess(nullptr) {
    }

    ChildProcess::~ChildProcess(){
        if(mProcess) CloseHandle(mProcess);
    }

    static std::wstring widen(const std::string& s){
        if(s.empty()) return std::wstring();
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
        std::wstring out(size_t(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &out[0], n);
        return out;
    }

    bool ChildProcess::start(const std::filesystem::path& executable, const std::filesystem::path& workingDirectory, const std::filesystem::path& logFile,
            const std::vector<std::pair<std::string, std::string>>& environment, std::string& error){
        //The child inherits the launcher's environment, and SetEnvironmentVariable is safe to call
        //with other threads running, unlike setenv.
        for(const auto& kv : environment) SetEnvironmentVariableW(widen(kv.first).c_str(), widen(kv.second).c_str());

        SECURITY_ATTRIBUTES inherit = {sizeof(inherit), nullptr, TRUE};
        HANDLE log = CreateFileW(logFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inherit, OPEN_EXISTING, 0, nullptr);

        STARTUPINFOW startup = {};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = input;
        startup.hStdOutput = log;
        startup.hStdError = log;

        std::wstring commandLine = L"\"" + executable.wstring() + L"\"";
        PROCESS_INFORMATION info = {};
        //CREATE_NO_WINDOW stops a console-subsystem game opening a console window of its own.
        BOOL ok = CreateProcessW(executable.c_str(), &commandLine[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, workingDirectory.c_str(), &startup, &info);
        DWORD lastError = GetLastError();
        if(log != INVALID_HANDLE_VALUE) CloseHandle(log);
        if(input != INVALID_HANDLE_VALUE) CloseHandle(input);
        if(!ok){
            error = "couldn't start " + executable.filename().u8string() + " (error " + std::to_string(lastError) + ")";
            return false;
        }
        CloseHandle(info.hThread);
        mProcess = info.hProcess;
        return true;
    }

    ProcessExit ChildProcess::wait(){
        ProcessExit exit;
        if(!mProcess){
            exit.crashed = true;
            exit.description = "not started";
            return exit;
        }
        WaitForSingleObject(mProcess, INFINITE);
        DWORD code = 0;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            GetExitCodeProcess(mProcess, &code);
            CloseHandle(mProcess);
            mProcess = nullptr;
            mExited = true;
            exit.stopRequested = mStopRequested;
            exit.forced = mForced;
        }
        exit.code = int(code);
        //A crash shows up as an NTSTATUS error code (0xC0000000-0xCFFFFFFF). Anything else,
        //including -1 from a TerminateProcess, is an ordinary exit code.
        if(code >= 0xC0000000u && code < 0xD0000000u){
            char hex[16];
            snprintf(hex, sizeof(hex), "0x%08lX", (unsigned long)code);
            const char* name = code == 0xC0000005u ? " access violation" : code == 0xC00000FDu ? " stack overflow"
                : code == 0xC0000409u ? " stack buffer overrun" : code == 0xC000001Du ? " illegal instruction" : "";
            exit.crashed = true;
            exit.description = std::string("crashed (") + hex + name + ")";
        }else{
            exit.description = code == 0 ? "exited 0" : "exit code " + std::to_string(int(code));
        }
        return exit;
    }

    namespace{
        struct CloseSearch{
            DWORD pid;
            int posted;
        };

        BOOL CALLBACK postClose(HWND window, LPARAM param){
            CloseSearch* search = reinterpret_cast<CloseSearch*>(param);
            DWORD pid = 0;
            GetWindowThreadProcessId(window, &pid);
            if(pid != search->pid || GetWindow(window, GW_OWNER)) return TRUE;
            //Every GUI thread also owns an input-method window; it isn't the game's to close.
            wchar_t className[32] = {};
            GetClassNameW(window, className, 32);
            if(wcscmp(className, L"IME") == 0 || wcscmp(className, L"MSCTFIME UI") == 0) return TRUE;
            PostMessageW(window, WM_CLOSE, 0, 0);
            search->posted++;
            return TRUE;
        }
    }

    bool ChildProcess::requestStop(){
        std::lock_guard<std::mutex> lock(mMutex);
        if(mExited || !mProcess) return false;
        mStopRequested = true;
        //What clicking the window's close button sends; SDL turns it into SDL_QUIT.
        CloseSearch search = {GetProcessId(mProcess), 0};
        EnumWindows(&postClose, reinterpret_cast<LPARAM>(&search));
        return search.posted > 0;
    }

    bool ChildProcess::forceStop(){
        std::lock_guard<std::mutex> lock(mMutex);
        if(mExited || !mProcess) return false;
        mStopRequested = true;
        mForced = true;
        return TerminateProcess(mProcess, 1) != 0;
    }
}
