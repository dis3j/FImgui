#include "mcp_server.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>
#include <random>

#pragma comment(lib, "ws2_32")

static const char* kServerVersion = "1.0.0";

McpServer::~McpServer() { Stop(); }

bool McpServer::Start(int port, std::string* error)
{
    Stop();
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { if (error) *error = "WSAStartup failed"; return false; }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) { if (error) *error = "socket() failed"; return false; }
    // Exclusive use: a second instance must not silently share the port.
    BOOL excl = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
    sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 16) != 0)
    {
        if (error) *error = "Port " + std::to_string(port) + " is busy";
        closesocket(s);
        return false;
    }
    m_listen = (uintptr_t)s;
    m_port = port;
    m_running = true;
    m_thread = std::thread(&McpServer::AcceptLoop, this);
    return true;
}

void McpServer::Stop()
{
    if (!m_running) return;
    m_running = false;
    closesocket((SOCKET)m_listen);
    m_listen = ~(uintptr_t)0;
    if (m_thread.joinable()) m_thread.join();
    // Fail pending jobs so connection threads can finish.
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& j : m_jobs) j->done.set_value(json{ { "content", json::array({ { { "type", "text" }, { "text", "Fimgui is shutting down" } } }) }, { "isError", true } });
        m_jobs.clear();
    }
    for (int i = 0; i < 100 && m_active > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

std::string McpServer::ClientName()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_client;
}

void McpServer::AcceptLoop()
{
    while (m_running)
    {
        SOCKET c = accept((SOCKET)m_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        m_active++;
        std::thread([this, c]() {
            HandleConnection((uintptr_t)c);
            m_active--;
        }).detach();
    }
}

static std::string Lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return (char)tolower(ch); });
    return s;
}

static void SendAll(SOCKET s, const std::string& data)
{
    size_t off = 0;
    while (off < data.size())
    {
        int n = send(s, data.data() + off, (int)std::min<size_t>(data.size() - off, 1 << 20), 0);
        if (n <= 0) return;
        off += n;
    }
}

static void Respond(SOCKET s, int status, const char* reason, const std::string& body, const std::string& extraHeaders = "")
{
    std::string head = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    if (!body.empty()) head += "Content-Type: application/json\r\n";
    head += "Content-Length: " + std::to_string(body.size()) + "\r\nConnection: close\r\n" + extraHeaders + "\r\n";
    SendAll(s, head + body);
}

void McpServer::HandleConnection(uintptr_t sockv)
{
    SOCKET s = (SOCKET)sockv;
    DWORD timeout = 30000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    std::string buf;
    char tmp[16384];
    size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos)
    {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) { closesocket(s); return; }
        buf.append(tmp, n);
        headerEnd = buf.find("\r\n\r\n");
        if (buf.size() > (1 << 20) && headerEnd == std::string::npos) { closesocket(s); return; }
    }
    std::string head = buf.substr(0, headerEnd);
    std::string body = buf.substr(headerEnd + 4);
    std::string method = head.substr(0, head.find(' '));
    size_t p1 = head.find(' '), p2 = head.find(' ', p1 + 1);
    std::string path = p1 != std::string::npos && p2 != std::string::npos ? head.substr(p1 + 1, p2 - p1 - 1) : "/";
    size_t contentLength = 0;
    std::string origin;
    size_t pos = head.find("\r\n");
    while (pos != std::string::npos && pos < head.size())
    {
        size_t next = head.find("\r\n", pos + 2);
        std::string line = head.substr(pos + 2, next == std::string::npos ? std::string::npos : next - pos - 2);
        size_t colon = line.find(':');
        if (colon != std::string::npos)
        {
            std::string k = Lower(line.substr(0, colon)), v = line.substr(colon + 1);
            v.erase(0, v.find_first_not_of(' '));
            if (k == "content-length") contentLength = (size_t)std::strtoull(v.c_str(), nullptr, 10);
            else if (k == "origin") origin = Lower(v);
        }
        pos = next;
    }

    // DNS-rebinding protection: browsers send Origin; only local pages may talk to us.
    if (!origin.empty() && origin != "null" && origin.find("://localhost") == std::string::npos && origin.find("://127.0.0.1") == std::string::npos)
    {
        Respond(s, 403, "Forbidden", "");
        closesocket(s);
        return;
    }
    std::string route = path.substr(0, path.find('?'));
    if (route != "/mcp" && route != "/")
    {
        Respond(s, 404, "Not Found", "");
        closesocket(s);
        return;
    }
    if (method == "GET" || method == "DELETE")
    {
        // No server-initiated stream; sessions are not tracked server side.
        Respond(s, 405, "Method Not Allowed", "", "Allow: POST\r\n");
        closesocket(s);
        return;
    }
    if (method != "POST")
    {
        Respond(s, method == "OPTIONS" ? 204 : 405, method == "OPTIONS" ? "No Content" : "Method Not Allowed", "", "Allow: POST\r\n");
        closesocket(s);
        return;
    }
    if (contentLength > (64u << 20)) { Respond(s, 413, "Payload Too Large", ""); closesocket(s); return; }
    while (body.size() < contentLength)
    {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        body.append(tmp, n);
    }

    json msg = json::parse(body, nullptr, false);
    if (msg.is_discarded())
    {
        Respond(s, 400, "Bad Request", json{ { "jsonrpc", "2.0" }, { "id", nullptr }, { "error", { { "code", -32700 }, { "message", "Parse error" } } } }.dump());
        closesocket(s);
        return;
    }
    json reply;
    if (msg.is_array())
    {
        reply = json::array();
        for (auto& m : msg)
        {
            bool note = false;
            json r = HandleMessage(m, &note);
            if (!note) reply.push_back(r);
        }
        if (reply.empty()) reply = nullptr;
    }
    else
    {
        bool note = false;
        json r = HandleMessage(msg, &note);
        if (!note) reply = r;
    }
    std::string headers;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_session.empty()) headers = "Mcp-Session-Id: " + m_session + "\r\n";
    }
    if (reply.is_null()) Respond(s, 202, "Accepted", "", headers);
    else Respond(s, 200, "OK", reply.dump(), headers);
    shutdown(s, SD_SEND);
    closesocket(s);
}

json McpServer::HandleMessage(const json& msg, bool* notification)
{
    *notification = !msg.contains("id");
    json id = msg.contains("id") ? msg["id"] : json(nullptr);
    std::string method = msg.value("method", "");
    const json params = msg.contains("params") ? msg["params"] : json::object();
    auto result = [&](json r) { return json{ { "jsonrpc", "2.0" }, { "id", id }, { "result", r } }; };
    auto error = [&](int code, const std::string& text) { return json{ { "jsonrpc", "2.0" }, { "id", id }, { "error", { { "code", code }, { "message", text } } } }; };

    if (method.empty()) return error(-32600, "Invalid request");   // also covers client responses
    if (method == "initialize")
    {
        std::string version = params.value("protocolVersion", std::string("2025-06-18"));
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (params.contains("clientInfo") && params["clientInfo"].is_object())
                m_client = params["clientInfo"].value("title", params["clientInfo"].value("name", std::string("MCP client")));
            std::mt19937_64 rng((unsigned long long)std::chrono::steady_clock::now().time_since_epoch().count());
            char sid[40];
            snprintf(sid, sizeof(sid), "%016llx%016llx", (unsigned long long)rng(), (unsigned long long)rng());
            m_session = sid;
        }
        return result({
            { "protocolVersion", version },
            { "capabilities", { { "tools", { { "listChanged", false } } } } },
            { "serverInfo", { { "name", "fimgui" }, { "title", "Fimgui" }, { "version", kServerVersion } } },
            { "instructions", McpInstructions() },
        });
    }
    if (method == "ping") return result(json::object());
    if (method == "tools/list") return result({ { "tools", McpToolList() } });
    if (method == "tools/call")
    {
        std::string name = params.value("name", "");
        json args = params.contains("arguments") && params["arguments"].is_object() ? params["arguments"] : json::object();
        m_calls++;
        return result(CallOnUiThread(name, args));
    }
    if (method == "resources/list") return result({ { "resources", json::array() } });
    if (method == "prompts/list") return result({ { "prompts", json::array() } });
    if (*notification) return nullptr;   // notifications/initialized, cancelled, ...
    return error(-32601, "Method not found: " + method);
}

json McpServer::CallOnUiThread(const std::string& name, const json& args)
{
    auto job = std::make_shared<Job>();
    job->name = name;
    job->args = args;
    std::future<json> f = job->done.get_future();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_jobs.push_back(job);
    }
    if (f.wait_for(std::chrono::seconds(120)) != std::future_status::ready)
        return json{ { "content", json::array({ { { "type", "text" }, { "text", "Fimgui did not respond in time (is the window frozen or a dialog open?)" } } }) }, { "isError", true } };
    return f.get();
}

void McpServer::Pump(const ToolCall& call)
{
    for (;;)
    {
        std::shared_ptr<Job> job;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_jobs.empty()) return;
            job = m_jobs.front();
            m_jobs.pop_front();
        }
        json r;
        try { r = call(job->name, job->args); }
        catch (const std::exception& e)
        {
            r = json{ { "content", json::array({ { { "type", "text" }, { "text", std::string("Tool failed: ") + e.what() } } }) }, { "isError", true } };
        }
        job->done.set_value(r);
    }
}
