#include "Http.h"

#include "Paths.h"
#include "Version.h"

#include <fstream>

namespace OML{
    const char* httpUserAgent(){
        static const std::string agent = std::string("OtherMythosLauncher/") + kVersion;
        return agent.c_str();
    }

    static int hexValue(char c){
        if(c >= '0' && c <= '9') return c - '0';
        if(c >= 'a' && c <= 'f') return c - 'a' + 10;
        if(c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string fileUrlToPath(const std::string& url){
        std::string rest = url.substr(7);
        //file://host/path isn't supported; an empty host leaves the path's own leading '/'.
        if(rest.size() >= 3 && rest[0] == '/' && rest[2] == ':') rest = rest.substr(1);
        std::string out;
        for(size_t i = 0; i < rest.size(); i++){
            if(rest[i] == '%' && i + 2 < rest.size() && hexValue(rest[i + 1]) >= 0 && hexValue(rest[i + 2]) >= 0){
                out.push_back(char(hexValue(rest[i + 1]) * 16 + hexValue(rest[i + 2])));
                i += 2;
            }else{
                out.push_back(rest[i]);
            }
        }
        return out;
    }

    static bool fileGet(const std::string& url, const HttpSink& sink, const HttpOptions& options, std::string& error){
        std::string path = fileUrlToPath(url);
        std::ifstream file(utf8ToPath(path), std::ios::binary);
        if(!file){
            error = "can't open " + path;
            return false;
        }
        char buffer[64 * 1024];
        while(file){
            if(options.cancel && options.cancel->load()){
                error = "cancelled";
                return false;
            }
            file.read(buffer, sizeof(buffer));
            std::streamsize got = file.gcount();
            if(got > 0 && !sink(buffer, size_t(got))){
                error = "aborted";
                return false;
            }
        }
        return true;
    }

    bool httpGet(const std::string& url, const HttpSink& sink, const HttpOptions& options, std::string& error){
        if(url.compare(0, 7, "file://") == 0) return fileGet(url, sink, options, error);
        if(url.compare(0, 7, "http://") != 0 && url.compare(0, 8, "https://") != 0){
            error = "unsupported url: " + url;
            return false;
        }
        return platformHttpGet(url, sink, options, error);
    }

    bool httpGetString(const std::string& url, std::string& body, std::string& error, int timeoutSeconds, const std::atomic<bool>* cancel){
        body.clear();
        HttpOptions options;
        options.totalTimeoutSeconds = timeoutSeconds;
        options.cancel = cancel;
        //Indexes are a few KB; anything wildly bigger isn't one.
        return httpGet(url, [&](const char* data, size_t size){
            if(body.size() + size > 64 * 1024 * 1024) return false;
            body.append(data, size);
            return true;
        }, options, error);
    }

    bool httpAvailable(std::string& reason){
        return platformHttpAvailable(reason);
    }
}
