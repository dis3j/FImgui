// fimgui-mcp: bridges an MCP client that speaks stdio (newline-delimited JSON-RPC) to the
// MCP server inside the running Fimgui editor (Streamable HTTP on 127.0.0.1). Starts Fimgui when needed.
//
// Usage: fimgui-mcp.exe [--port 7317]
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp")

static int g_port = 7317;
static std::wstring g_session;
static bool g_launched = false;

static std::wstring Widen(const std::string& s)
{
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

static std::string Narrow(const std::wstring& w)
{
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

static void Log(const std::string& s)
{
    std::string line = "[fimgui-mcp] " + s + "\n";
    DWORD wr;
    WriteFile(GetStdHandle(STD_ERROR_HANDLE), line.data(), (DWORD)line.size(), &wr, nullptr);
}

static void WriteOut(const std::string& s)
{
    std::string line = s + "\n";
    DWORD wr;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), line.data(), (DWORD)line.size(), &wr, nullptr);
    FlushFileBuffers(GetStdHandle(STD_OUTPUT_HANDLE));
}

// Returns false on connection failure; status 0 means no HTTP response.
static bool Post(const std::string& body, int* status, std::string* reply)
{
    *status = 0;
    reply->clear();
    HINTERNET session = WinHttpOpen(L"fimgui-mcp/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return false;
    WinHttpSetTimeouts(session, 5000, 5000, 30000, 180000);
    HINTERNET connect = WinHttpConnect(session, L"127.0.0.1", (INTERNET_PORT)g_port, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"POST", L"/mcp", nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0) : nullptr;
    bool ok = false;
    if (request)
    {
        std::wstring headers = L"Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\n";
        if (!g_session.empty()) headers += L"Mcp-Session-Id: " + g_session + L"\r\n";
        if (WinHttpSendRequest(request, headers.c_str(), (DWORD)-1, (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0) &&
            WinHttpReceiveResponse(request, nullptr))
        {
            ok = true;
            DWORD code = 0, sz = sizeof(code);
            WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
            *status = (int)code;
            wchar_t sid[256];
            DWORD sidSize = sizeof(sid);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, L"Mcp-Session-Id", sid, &sidSize, WINHTTP_NO_HEADER_INDEX)) g_session = sid;
            for (;;)
            {
                DWORD avail = 0;
                if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
                std::string chunk(avail, 0);
                DWORD rd = 0;
                if (!WinHttpReadData(request, chunk.data(), avail, &rd) || rd == 0) break;
                reply->append(chunk.data(), rd);
            }
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

static void LaunchFimgui()
{
    if (g_launched) return;
    g_launched = true;
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring exe = path;
    exe = exe.substr(0, exe.find_last_of(L"\\/") + 1) + L"Fimgui.exe";
    Log("Fimgui is not running, starting " + Narrow(exe));
    ShellExecuteW(nullptr, L"open", exe.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::string IdOf(const std::string& line)
{
    // Extract the raw "id" value without a JSON library (used only for error replies).
    size_t p = line.find("\"id\"");
    if (p == std::string::npos) return "";
    p = line.find(':', p);
    if (p == std::string::npos) return "";
    p++;
    while (p < line.size() && line[p] == ' ') p++;
    size_t e = p;
    if (e < line.size() && line[e] == '"') { e = line.find('"', e + 1); if (e != std::string::npos) e++; }
    else while (e < line.size() && line[e] != ',' && line[e] != '}') e++;
    return e == std::string::npos ? "" : line.substr(p, e - p);
}

static void Forward(const std::string& line)
{
    int status = 0;
    std::string reply;
    bool ok = Post(line, &status, &reply);
    for (int i = 0; !ok && i < 60; i++)
    {
        LaunchFimgui();
        Sleep(250);
        ok = Post(line, &status, &reply);
    }
    std::string id = IdOf(line);
    if (!ok)
    {
        if (!id.empty())
            WriteOut("{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":-32000,\"message\":\"Cannot reach Fimgui on 127.0.0.1:" + std::to_string(g_port) +
                     ". Start Fimgui and enable the MCP server in Settings.\"}}");
        return;
    }
    if (reply.empty()) return;   // 202 for notifications
    if (reply.rfind("event:", 0) == 0 || reply.rfind("data:", 0) == 0)
    {
        size_t p = 0;
        while ((p = reply.find("data:", p)) != std::string::npos)
        {
            size_t e = reply.find('\n', p);
            std::string data = reply.substr(p + 5, e == std::string::npos ? std::string::npos : e - p - 5);
            while (!data.empty() && (data[0] == ' ')) data.erase(0, 1);
            while (!data.empty() && (data.back() == '\r')) data.pop_back();
            if (!data.empty()) WriteOut(data);
            if (e == std::string::npos) break;
            p = e;
        }
        return;
    }
    WriteOut(reply);
}

int wmain(int argc, wchar_t** argv)
{
    wchar_t env[32];
    if (GetEnvironmentVariableW(L"FIMGUI_MCP_PORT", env, 32)) g_port = _wtoi(env);
    for (int i = 1; i < argc; i++)
        if (!wcscmp(argv[i], L"--port") && i + 1 < argc) g_port = _wtoi(argv[++i]);
    Log("bridging stdio to http://127.0.0.1:" + std::to_string(g_port) + "/mcp");

    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    std::string buf;
    char tmp[65536];
    for (;;)
    {
        DWORD rd = 0;
        if (!ReadFile(in, tmp, sizeof(tmp), &rd, nullptr) || rd == 0) break;
        buf.append(tmp, rd);
        size_t nl;
        while ((nl = buf.find('\n')) != std::string::npos)
        {
            std::string line = buf.substr(0, nl);
            buf.erase(0, nl + 1);
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty()) Forward(line);
        }
    }
    return 0;
}
