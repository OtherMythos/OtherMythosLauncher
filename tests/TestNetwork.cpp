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
    bool ok = httpGetString("https://builds.othermythos.com/avEngine/index.json", body, error);
    CHECK(ok);
    if(!ok) printf("    %s\n", error.c_str());
    ProjectIndex project;
    CHECK(parseProjectIndex(body, "https://builds.othermythos.com/avEngine/index.json", project, error));
    CHECK(!project.builds.empty());

    CHECK(!httpGetString("https://builds.othermythos.com/doesNotExist/index.json", body, error));
    CHECK(!error.empty());
}
