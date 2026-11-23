#pragma once
#include "codegen_web.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Live preview of a web project: serves the site built in memory on http://127.0.0.1:<port>/.
// Pages built with liveReload poll /__fimgui/live and reload when a new version is published,
// or switch to the page the editor reports with SetPage.
class WebPreview
{
public:
    ~WebPreview();
    bool Start(std::string* error);           // the first free port from 7420
    void Stop();
    bool Running() const { return m_running; }
    int Port() const { return m_port; }
    std::string Url(const std::string& file = "") const;

    void Publish(WebSite site);               // UI thread
    void SetPage(const std::string& file);    // the page being edited ("" = keep)
    int Version() const { return m_version; }

private:
    void AcceptLoop();
    void Handle(uintptr_t sock);

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<int> m_active{ 0 };
    std::atomic<int> m_version{ 0 };
    uintptr_t m_listen = ~(uintptr_t)0;
    int m_port = 0;
    std::mutex m_mutex;
    std::shared_ptr<const WebSite> m_site;
    std::string m_page;
};
