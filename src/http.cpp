#include "http.h"
#include "platform.h"
#include <windows.h>
#include <winhttp.h>

namespace http
{
    struct Handle
    {
        HINTERNET h = nullptr;
        ~Handle() { if (h) WinHttpCloseHandle(h); }
    };

    static std::string LastError(const char* what)
    {
        DWORD e = GetLastError();
        char buf[128];
        snprintf(buf, sizeof(buf), "%s failed (WinHTTP error %lu)", what, e);
        return buf;
    }

    Result PostStream(const std::string& url, const Headers& headers, const std::string& body,
                      const std::function<bool(const char*, size_t)>& onData, std::atomic<bool>* cancel)
    {
        Result r;
        std::wstring wurl = platform::Widen(url);
        URL_COMPONENTS uc = {};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {}, path[2048] = {};
        uc.lpszHostName = host; uc.dwHostNameLength = 256;
        uc.lpszUrlPath = path; uc.dwUrlPathLength = 2048;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) { r.error = "Invalid URL: " + url; return r; }

        Handle session, connect, request;
        session.h = WinHttpOpen(L"Fimgui/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session.h) { r.error = LastError("WinHttpOpen"); return r; }
        WinHttpSetTimeouts(session.h, 15000, 15000, 30000, 600000);
        connect.h = WinHttpConnect(session.h, host, uc.nPort, 0);
        if (!connect.h) { r.error = LastError("WinHttpConnect"); return r; }
        DWORD flags = uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
        request.h = WinHttpOpenRequest(connect.h, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!request.h) { r.error = LastError("WinHttpOpenRequest"); return r; }

        std::wstring hdr;
        for (auto& [k, v] : headers) hdr += platform::Widen(k + ": " + v + "\r\n");
        if (!WinHttpSendRequest(request.h, hdr.c_str(), (DWORD)-1, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0))
        {
            r.error = LastError("Send");
            return r;
        }
        if (!WinHttpReceiveResponse(request.h, nullptr)) { r.error = LastError("Receive"); return r; }

        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &sz, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)status;
        bool ok = status >= 200 && status < 300;

        std::vector<char> buf(16384);
        for (;;)
        {
            if (cancel && cancel->load()) break;
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request.h, &avail)) { r.error = LastError("Read"); break; }
            if (avail == 0) break;
            while (avail > 0)
            {
                DWORD toRead = avail < buf.size() ? avail : (DWORD)buf.size(), read = 0;
                if (!WinHttpReadData(request.h, buf.data(), toRead, &read) || read == 0) { avail = 0; break; }
                avail -= read;
                if (ok) { if (!onData(buf.data(), read)) return r; }
                else r.errorBody.append(buf.data(), read);
            }
        }
        return r;
    }
}
