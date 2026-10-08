#include "Http.h"

#include "BuildIndex.h"

#include <cstdint>
#include <dlfcn.h>
#include <mutex>

//libcurl is loaded at runtime rather than linked, so the launcher needs no TLS library or CA
//bundle of its own and starts even where libcurl is missing (offline-only, with a reason).
//The option numbers below are libcurl's ABI and haven't changed since 7.x.
namespace OML{
    namespace{
        typedef void CURL;
        typedef int CURLcode;

        const int CURLOPT_WRITEDATA = 10001;
        const int CURLOPT_URL = 10002;
        const int CURLOPT_ERRORBUFFER = 10010;
        const int CURLOPT_WRITEFUNCTION = 20011;
        const int CURLOPT_TIMEOUT = 13;
        const int CURLOPT_USERAGENT = 10018;
        const int CURLOPT_LOW_SPEED_LIMIT = 19;
        const int CURLOPT_LOW_SPEED_TIME = 20;
        const int CURLOPT_NOPROGRESS = 43;
        const int CURLOPT_FAILONERROR = 45;
        const int CURLOPT_FOLLOWLOCATION = 52;
        const int CURLOPT_XFERINFODATA = 10057;
        const int CURLOPT_MAXREDIRS = 68;
        const int CURLOPT_CONNECTTIMEOUT = 78;
        const int CURLOPT_NOSIGNAL = 99;
        const int CURLOPT_XFERINFOFUNCTION = 20219;
        const long CURL_GLOBAL_DEFAULT = 3;
        const int kErrorSize = 256;

        struct CurlApi{
            bool loaded = false;
            std::string reason;
            CURLcode (*globalInit)(long) = nullptr;
            CURL* (*easyInit)() = nullptr;
            CURLcode (*easySetopt)(CURL*, int, ...) = nullptr;
            CURLcode (*easyPerform)(CURL*) = nullptr;
            void (*easyCleanup)(CURL*) = nullptr;
            const char* (*easyStrerror)(CURLcode) = nullptr;
        };

        template<typename T>
        bool loadSymbol(void* lib, const char* name, T& out){
            out = reinterpret_cast<T>(dlsym(lib, name));
            return out != nullptr;
        }

        CurlApi loadCurl(){
            CurlApi api;
#ifdef __APPLE__
            const char* candidates[] = {"libcurl.4.dylib", "/usr/lib/libcurl.4.dylib"};
#else
            //Debian and Ubuntu sometimes only have the GnuTLS build; the API is the same.
            const char* candidates[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl.so"};
#endif
            void* lib = nullptr;
            for(const char* name : candidates){
                lib = dlopen(name, RTLD_NOW | RTLD_LOCAL);
                if(lib) break;
            }
            if(!lib){
                api.reason = "libcurl isn't installed, so only builds already downloaded can be played";
                return api;
            }
            bool ok = loadSymbol(lib, "curl_global_init", api.globalInit)
                && loadSymbol(lib, "curl_easy_init", api.easyInit)
                && loadSymbol(lib, "curl_easy_setopt", api.easySetopt)
                && loadSymbol(lib, "curl_easy_perform", api.easyPerform)
                && loadSymbol(lib, "curl_easy_cleanup", api.easyCleanup)
                && loadSymbol(lib, "curl_easy_strerror", api.easyStrerror);
            if(!ok){
                api.reason = "the installed libcurl is missing functions the launcher needs";
                return api;
            }
            if(api.globalInit(CURL_GLOBAL_DEFAULT) != 0){
                api.reason = "libcurl failed to initialise";
                return api;
            }
            api.loaded = true;
            return api;
        }

        const CurlApi& curlApi(){
            static const CurlApi api = loadCurl();
            return api;
        }

        struct Transfer{
            const HttpSink* sink;
            const std::atomic<bool>* cancel;
            bool sinkFailed;
        };

        size_t writeCallback(char* data, size_t size, size_t count, void* user){
            Transfer* t = static_cast<Transfer*>(user);
            size_t bytes = size * count;
            if(!(*t->sink)(data, bytes)){
                t->sinkFailed = true;
                return 0;
            }
            return bytes;
        }

        int progressCallback(void* user, int64_t, int64_t, int64_t, int64_t){
            Transfer* t = static_cast<Transfer*>(user);
            return t->cancel && t->cancel->load() ? 1 : 0;
        }
    }

    bool platformHttpAvailable(std::string& reason){
        const CurlApi& api = curlApi();
        reason = api.reason;
        return api.loaded;
    }

    bool platformHttpGet(const std::string& url, const HttpSink& sink, const HttpOptions& options, std::string& error){
        const CurlApi& api = curlApi();
        if(!api.loaded){
            error = api.reason;
            return false;
        }
        CURL* curl = api.easyInit();
        if(!curl){
            error = "libcurl couldn't start a request";
            return false;
        }
        Transfer transfer{&sink, options.cancel, false};
        char errorBuffer[kErrorSize] = {};
        api.easySetopt(curl, CURLOPT_URL, url.c_str());
        api.easySetopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        api.easySetopt(curl, CURLOPT_USERAGENT, httpUserAgent());
        api.easySetopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        api.easySetopt(curl, CURLOPT_MAXREDIRS, 5L);
        api.easySetopt(curl, CURLOPT_NOSIGNAL, 1L);
        api.easySetopt(curl, CURLOPT_FAILONERROR, 1L);
        //Fail fast when a LAN source isn't there, and give up on a stalled download.
        api.easySetopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        api.easySetopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
        api.easySetopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
        if(options.totalTimeoutSeconds > 0) api.easySetopt(curl, CURLOPT_TIMEOUT, long(options.totalTimeoutSeconds));
        api.easySetopt(curl, CURLOPT_WRITEFUNCTION, &writeCallback);
        api.easySetopt(curl, CURLOPT_WRITEDATA, &transfer);
        api.easySetopt(curl, CURLOPT_NOPROGRESS, 0L);
        api.easySetopt(curl, CURLOPT_XFERINFOFUNCTION, &progressCallback);
        api.easySetopt(curl, CURLOPT_XFERINFODATA, &transfer);

        CURLcode code = api.easyPerform(curl);
        api.easyCleanup(curl);
        if(code == 0) return true;
        if(options.cancel && options.cancel->load()){
            error = "cancelled";
        }else if(transfer.sinkFailed){
            error = "aborted";
        }else{
            error = urlHost(url) + ": " + (errorBuffer[0] ? errorBuffer : api.easyStrerror(code));
        }
        return false;
    }
}
