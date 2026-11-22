#pragma once
#include "../third_party/json/json.hpp"
#include <atomic>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using json = nlohmann::json;

// Minimal MCP server (Streamable HTTP transport, JSON responses) bound to 127.0.0.1.
// Network I/O runs on background threads; tool calls are executed on the UI thread via Pump().
class McpServer
{
public:
    using ToolCall = std::function<json(const std::string& name, const json& args)>;

    ~McpServer();
    bool Start(int port, std::string* error);
    void Stop();
    bool Running() const { return m_running; }
    int Port() const { return m_port; }
    int Calls() const { return m_calls; }
    std::string ClientName();

    // UI thread: run queued tool calls.
    void Pump(const ToolCall& call);

private:
    struct Job
    {
        std::string name;
        json args;
        std::promise<json> done;
    };

    void AcceptLoop();
    void HandleConnection(uintptr_t sock);
    json HandleMessage(const json& msg, bool* notification);
    json CallOnUiThread(const std::string& name, const json& args);

    std::thread m_thread;
    std::atomic<bool> m_running{ false };
    std::atomic<int> m_calls{ 0 };
    std::atomic<int> m_active{ 0 };
    uintptr_t m_listen = ~(uintptr_t)0;
    int m_port = 0;
    std::mutex m_mutex;
    std::deque<std::shared_ptr<Job>> m_jobs;
    std::string m_client;
    std::string m_session;
};

// Implemented in mcp_tools.cpp
json McpToolList();
const char* McpInstructions();
