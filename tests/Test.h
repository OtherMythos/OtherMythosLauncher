#pragma once

#include <filesystem>
#include <sstream>
#include <string>

//A deliberately tiny test runner: TEST registers a function, CHECK and CHECK_EQUAL record
//failures and carry on, so one run reports everything that's wrong.
namespace OMLTest{
    typedef void (*TestFunction)();

    struct Registrar{
        Registrar(const char* name, TestFunction function, bool network);
    };

    void recordCheck(bool ok, const std::string& what, const char* file, int line);

    template<typename A, typename B>
    void checkEqual(const A& a, const B& b, const char* as, const char* bs, const char* file, int line){
        bool ok = a == b;
        std::ostringstream ss;
        ss << as << " == " << bs;
        if(!ok) ss << "\n        got: " << a << "\n   expected: " << b;
        recordCheck(ok, ss.str(), file, line);
    }

    //A fresh empty directory, removed when the test run ends.
    std::filesystem::path tempDirectory(const std::string& name);
    std::filesystem::path fixturesDirectory();
    std::filesystem::path testExecutable();
    std::string fileUrl(const std::filesystem::path& path);
    void writeText(const std::filesystem::path& path, const std::string& text);
}

#define OML_CONCAT2(a, b) a##b
#define OML_CONCAT(a, b) OML_CONCAT2(a, b)
#define TEST(name) \
    static void name(); \
    static OMLTest::Registrar OML_CONCAT(registrar_, name)(#name, &name, false); \
    static void name()
#define NETWORK_TEST(name) \
    static void name(); \
    static OMLTest::Registrar OML_CONCAT(registrar_, name)(#name, &name, true); \
    static void name()
#define CHECK(cond) OMLTest::recordCheck(bool(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQUAL(a, b) OMLTest::checkEqual((a), (b), #a, #b, __FILE__, __LINE__)
