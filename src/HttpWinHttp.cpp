#include "Http.h"

#include "BuildIndex.h"

#include <chrono>
#include <windows.h>
#include <winhttp.h>

namespace OML{
    namespace{
        struct Handle{
            HINTERNET h = nullptr;
            ~Handle(){ if(h) WinHttpCloseHandle(h); }
        };

        std::wstring widen(const std::string& s){
            if(s.empty()) return std::wstring();
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
            std::wstring out(size_t(n), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &out[0], n);
            return out;
        }

        std::string narrow(const std::wstring& s){
            if(s.empty()) return std::string();
            int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
            std::string out(size_t(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), &out[0], n, nullptr, nullptr);
            return out;
        }

        std::string describeError(DWORD code){
            wchar_t* buffer = nullptr;
            DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_FROM_SYSTEM;
            HMODULE module = nullptr;
            //WinHTTP's own errors (12000-12999) only have text in winhttp.dll.
            if(code >= 12000 && code <= 12999){
                module = GetModuleHandleW(L"winhttp.dll");
                flags |= FORMAT_MESSAGE_FROM_HMODULE;
            }
            DWORD len = FormatMessageW(flags, module, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
            std::wstring text = len ? std::wstring(buffer, len) : L"error " + std::to_wstring(code);
            if(buffer) LocalFree(buffer);
            while(!text.empty() && (text.back() == L'\n' || text.back() == L'\r' || text.back() == L'.')) text.pop_back();
            return narrow(text);
        }
    }

    bool platformHttpAvailable(std::string& reason){
        reason.clear();
        return true;
    }

    bool platformHttpGet(const std::string& url, const HttpSink& sink, const HttpOptions& options, std::string& error){
        std::wstring wideUrl = widen(url);
        URL_COMPONENTS parts = {};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = DWORD(-1);
        parts.dwUrlPathLength = DWORD(-1);
        parts.dwExtraInfoLength = DWORD(-1);
        if(!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)){
            error = "bad url: " + url;
            return false;
        }
        std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
        if(parts.lpszExtraInfo) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        if(path.empty()) path = L"/";
        std::string where = urlHost(url) + ": ";

        Handle session;
        session.h = WinHttpOpen(widen(httpUserAgent()).c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if(!session.h){
            error = where + describeError(GetLastError());
            return false;
        }
        //Fail fast when a LAN source isn't there; a receive that stalls for 20 s gives up.
        WinHttpSetTimeouts(session.h, 5000, 5000, 15000, 20000);
        Handle connection;
        connection.h = WinHttpConnect(session.h, host.c_str(), parts.nPort, 0);
        Handle request;
        if(connection.h){
            DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            request.h = WinHttpOpenRequest(connection.h, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        }
        if(!request.h
            || !WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
            || !WinHttpReceiveResponse(request.h, nullptr)){
            error = where + describeError(GetLastError());
            return false;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
        if(status < 200 || status >= 300){
            error = where + "HTTP " + std::to_string(status);
            return false;
        }

        auto start = std::chrono::steady_clock::now();
        char buffer[64 * 1024];
        while(true){
            if(options.cancel && options.cancel->load()){
                error = "cancelled";
                return false;
            }
            if(options.totalTimeoutSeconds > 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(options.totalTimeoutSeconds)){
                error = where + "timed out";
                return false;
            }
            DWORD got = 0;
            if(!WinHttpReadData(request.h, buffer, sizeof(buffer), &got)){
                error = where + describeError(GetLastError());
                return false;
            }
            if(got == 0) return true;
            if(!sink(buffer, got)){
                error = "aborted";
                return false;
            }
        }
    }
}
