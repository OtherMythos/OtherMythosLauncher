#include "Test.h"

#include "BuildIndex.h"
#include "Http.h"

#include <cstdio>

using namespace OML;

//Exercises the real HTTPS stack (WinHTTP, or libcurl loaded at runtime) against the public host.
NETWORK_TEST(fetchesThePublicIndexOverHttps){
    std::string reason;
    CHECK(httpAvailable(reason));
    std::string body;
    std::string error;
    //The host is a real one, reached from shared CI runners: a single timeout now and then
    //(v0.1.1's first Windows build) isn't the HTTPS stack failing.
    bool ok = false;
    for(int attempt = 1; attempt <= 3 && !ok; attempt++){
        ok = httpGetString("https://builds.othermythos.com/avEngine/index.json", body, error);
        if(!ok) printf("    attempt %d: %s\n", attempt, error.c_str());
    }
    CHECK(ok);
    ProjectIndex project;
    CHECK(parseProjectIndex(body, "https://builds.othermythos.com/avEngine/index.json", project, error));
    CHECK(!project.builds.empty());

    CHECK(!httpGetString("https://builds.othermythos.com/doesNotExist/index.json", body, error));
    CHECK(!error.empty());
}
