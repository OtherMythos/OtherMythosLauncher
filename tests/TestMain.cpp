#include "Test.h"

#include "Paths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
#else
    #include <signal.h>
    #include <unistd.h>
#endif

namespace OMLTest{
    struct TestEntry{
        const char* name;
        TestFunction function;
        bool network;
    };

    static std::vector<TestEntry>& tests(){
        static std::vector<TestEntry> list;
        return list;
    }

    static int gChecks = 0;
    static int gFailures = 0;
    static std::filesystem::path gTempRoot;
    static std::filesystem::path gExecutable;

    Registrar::Registrar(const char* name, TestFunction function, bool network){
        tests().push_back({name, function, network});
    }

    void recordCheck(bool ok, const std::string& what, const char* file, int line){
        gChecks++;
        if(ok) return;
        gFailures++;
        printf("    FAIL %s:%d: %s\n", file, line, what.c_str());
    }

    std::filesystem::path tempDirectory(const std::string& name){
        std::filesystem::path dir = gTempRoot / name;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
        return dir;
    }

    std::filesystem::path fixturesDirectory(){
        return OML_FIXTURES_DIR;
    }

    std::filesystem::path testExecutable(){
        return gExecutable;
    }

    std::string fileUrl(const std::filesystem::path& path){
        std::string p = std::filesystem::absolute(path).generic_u8string();
        return p[0] == '/' ? "file://" + p : "file:///" + p;
    }

    void writeText(const std::filesystem::path& path, const std::string& text){
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << text;
    }
}

//TestRunner launches this binary as a fake game. "wait" runs until asked to stop the way the
//launcher asks (SIGTERM, or WM_CLOSE to its window) and exits 7; "stubborn" ignores that.
//Either gives up after a minute so a broken test can't leave it running.
namespace{
    volatile int gStopRequested = 0;
    bool gStubborn = false;

#ifdef _WIN32
    LRESULT CALLBACK testWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam){
        if(message == WM_CLOSE){
            if(!gStubborn){
                gStopRequested = 1;
                PostQuitMessage(7);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    int runUntilStopped(bool stubborn){
        gStubborn = stubborn;
        WNDCLASSW windowClass = {};
        windowClass.lpfnWndProc = testWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = L"OmlTestChild";
        RegisterClassW(&windowClass);
        CreateWindowExW(0, L"OmlTestChild", L"OML test child", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, nullptr, nullptr, windowClass.hInstance, nullptr);
        SetTimer(nullptr, 0, 60000, nullptr);
        printf("ready\n");
        fflush(stdout);
        MSG message;
        while(GetMessageW(&message, nullptr, 0, 0) > 0){
            if(message.message == WM_TIMER) return 9;
            DispatchMessageW(&message);
        }
        return gStopRequested ? 7 : 9;
    }
#else
    void onTerminate(int){
        gStopRequested = 1;
    }

    int runUntilStopped(bool stubborn){
        signal(SIGTERM, stubborn ? SIG_IGN : onTerminate);
        printf("ready\n");
        fflush(stdout);
        for(int i = 0; i < 600 && !gStopRequested; i++) usleep(100000);
        return gStopRequested ? 7 : 9;
    }
#endif
}

int main(int argc, char** argv){
    if(const char* mode = getenv("OML_TEST_CHILD")){
        if(strcmp(mode, "wait") == 0) return runUntilStopped(false);
        if(strcmp(mode, "stubborn") == 0) return runUntilStopped(true);
        const char* runDir = getenv("OTHERMYTHOS_RUN_DIR");
        printf("child ran in %s\n", runDir ? runDir : "(no run dir)");
        return 3;
    }

    bool network = argc > 1 && strcmp(argv[1], "--network") == 0;
    OMLTest::gExecutable = std::filesystem::absolute(argv[0]);
#ifdef _WIN32
    unsigned long pid = GetCurrentProcessId();
#else
    unsigned long pid = (unsigned long)getpid();
#endif
    OMLTest::gTempRoot = std::filesystem::temp_directory_path() / ("omlTests-" + std::to_string(pid));

    int ran = 0;
    for(const OMLTest::TestEntry& t : OMLTest::tests()){
        if(t.network != network) continue;
        int before = OMLTest::gFailures;
        printf("%s\n", t.name);
        t.function();
        if(OMLTest::gFailures != before) printf("  ^ failed\n");
        ran++;
    }
    std::error_code ec;
    std::filesystem::remove_all(OMLTest::gTempRoot, ec);
    printf("%d tests, %d checks, %d failures\n", ran, OMLTest::gChecks, OMLTest::gFailures);
    return OMLTest::gFailures == 0 ? 0 : 1;
}
