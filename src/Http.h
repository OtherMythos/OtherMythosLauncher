#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <string>

namespace OML{
    //Receives the body as it arrives. Returning false aborts the transfer.
    typedef std::function<bool(const char* data, size_t size)> HttpSink;

    struct HttpOptions{
        //A limit on the whole request, for small fetches like an index. Downloads leave it at
        //0 and rely on the stall timeout instead, since a 200 MB build can take a while.
        int totalTimeoutSeconds = 0;
        const std::atomic<bool>* cancel = nullptr;
    };

    //http(s):// goes through the platform's HTTP stack, file:// reads from disk.
    bool httpGet(const std::string& url, const HttpSink& sink, const HttpOptions& options, std::string& error);
    bool httpGetString(const std::string& url, std::string& body, std::string& error, int timeoutSeconds = 20, const std::atomic<bool>* cancel = nullptr);

    //False when http(s) can't work at all here, e.g. libcurl couldn't be loaded.
    bool httpAvailable(std::string& reason);

    //file:///home/x -> /home/x, file:///C:/x -> C:/x, with percent-escapes decoded.
    std::string fileUrlToPath(const std::string& url);

    //Per platform: HttpWinHttp.cpp, HttpCurl.cpp.
    bool platformHttpGet(const std::string& url, const HttpSink& sink, const HttpOptions& options, std::string& error);
    bool platformHttpAvailable(std::string& reason);
    const char* httpUserAgent();
}
