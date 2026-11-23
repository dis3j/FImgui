#include "web_preview.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <algorithm>
#include <chrono>

#pragma comment(lib, "ws2_32")

WebPreview::~WebPreview() { Stop(); }

bool WebPreview::Start(std::string* error)
{
    if (m_running) return true;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) { if (error) *error = "WSAStartup failed"; return false; }
    for (int port = 7420; port < 7440; port++)
    {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (s == INVALID_SOCKET) break;
        BOOL excl = TRUE;
        setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&excl, sizeof(excl));
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons((u_short)port);
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        if (bind(s, (sockaddr*)&addr, sizeof(addr)) != 0 || listen(s, 32) != 0) { closesocket(s); continue; }
        m_listen = (uintptr_t)s;
        m_port = port;
        m_running = true;
        m_thread = std::thread(&WebPreview::AcceptLoop, this);
        return true;
    }
    if (error) *error = "Ports 7420-7439 are busy";
    return false;
}

void WebPreview::Stop()
{
    if (!m_running) return;
    m_running = false;
    closesocket((SOCKET)m_listen);
    m_listen = ~(uintptr_t)0;
    if (m_thread.joinable()) m_thread.join();
    for (int i = 0; i < 100 && m_active > 0; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

std::string WebPreview::Url(const std::string& file) const
{
    return "http://127.0.0.1:" + std::to_string(m_port) + "/" + (file == "index.html" ? std::string() : file);
}

void WebPreview::Publish(WebSite site)
{
    auto s = std::make_shared<const WebSite>(std::move(site));
    std::lock_guard<std::mutex> lock(m_mutex);
    m_site = s;
    m_version++;
}

void WebPreview::SetPage(const std::string& file)
{
    if (file.empty()) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_page = file;
}

void WebPreview::AcceptLoop()
{
    while (m_running)
    {
        SOCKET c = accept((SOCKET)m_listen, nullptr, nullptr);
        if (c == INVALID_SOCKET) break;
        m_active++;
        std::thread([this, c]() {
            Handle((uintptr_t)c);
            m_active--;
        }).detach();
    }
}

static const char* ContentType(const std::string& path)
{
    std::string ext = path.substr(path.find_last_of('.') == std::string::npos ? path.size() : path.find_last_of('.'));
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)tolower(c); });
    if (ext == ".html") return "text/html; charset=utf-8";
    if (ext == ".css") return "text/css; charset=utf-8";
    if (ext == ".js") return "text/javascript; charset=utf-8";
    if (ext == ".json") return "application/json";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".mp4" || ext == ".m4v") return "video/mp4";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mov") return "video/quicktime";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".otf") return "font/otf";
    if (ext == ".woff") return "font/woff";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".md" || ext == ".txt") return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static std::string UrlDecode(const std::string& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); i++)
    {
        if (s[i] == '%' && i + 2 < s.size() && isxdigit((unsigned char)s[i + 1]) && isxdigit((unsigned char)s[i + 2]))
        {
            o += (char)std::stoi(s.substr(i + 1, 2), nullptr, 16);
            i += 2;
        }
        else o += s[i];
    }
    return o;
}

static void SendAll(SOCKET s, const char* data, size_t size)
{
    size_t off = 0;
    while (off < size)
    {
        int n = send(s, data + off, (int)std::min<size_t>(size - off, 1 << 20), 0);
        if (n <= 0) return;
        off += n;
    }
}

void WebPreview::Handle(uintptr_t sockv)
{
    SOCKET s = (SOCKET)sockv;
    DWORD timeout = 10000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));
    std::string req;
    char tmp[8192];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536)
    {
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) { closesocket(s); return; }
        req.append(tmp, n);
    }
    std::string line = req.substr(0, req.find("\r\n"));
    size_t p1 = line.find(' '), p2 = line.find(' ', p1 + 1);
    std::string method = line.substr(0, p1);
    std::string path = p1 != std::string::npos && p2 != std::string::npos ? line.substr(p1 + 1, p2 - p1 - 1) : "/";
    path = UrlDecode(path.substr(0, path.find_first_of("?#")));
    while (!path.empty() && path[0] == '/') path.erase(0, 1);
    if (path.empty()) path = "index.html";

    std::shared_ptr<const WebSite> site;
    std::string page;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        site = m_site;
        page = m_page;
    }
    int status = 200;
    std::string type, owned;
    const std::string* body = nullptr;
    if (path == "__fimgui/live")
    {
        owned = "{\"v\":" + std::to_string(m_version.load()) + ",\"page\":\"" + page + "\"}";
        type = "application/json";
    }
    else if (site)
    {
        auto it = site->files.find(path);
        if (it == site->files.end() && path.find('.') == std::string::npos) it = site->files.find(path + ".html");
        if (it != site->files.end() && it->second) { body = it->second.get(); type = ContentType(it->first); }
    }
    if (!body && owned.empty())
    {
        status = 404;
        type = "text/html; charset=utf-8";
        owned = "<!doctype html><meta charset=utf-8><title>Not found</title><body style=\"font:14px system-ui;padding:40px;color:#888;background:#1e1e1e\">"
                "<p>" + path + " is not part of the site. <a href=\"/\" style=\"color:#0d99ff\">Open the first page</a></p>";
    }
    if (!body) body = &owned;
    std::string head = "HTTP/1.1 " + std::string(status == 200 ? "200 OK" : "404 Not Found") + "\r\nContent-Type: " + type +
                       "\r\nContent-Length: " + std::to_string(body->size()) + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
    SendAll(s, head.data(), head.size());
    if (method != "HEAD") SendAll(s, body->data(), body->size());
    shutdown(s, SD_SEND);
    closesocket(s);
}
