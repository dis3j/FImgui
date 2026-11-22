#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct AiSettings
{
    std::string apiKey;                          // falls back to ANTHROPIC_API_KEY
    std::string model = "claude-opus-5-5";
    std::string effort = "medium";
    std::string baseUrl = "https://api.anthropic.com";
    bool sendSnapshot = true;                    // attach a PNG render of the canvas so the model sees the result
};

struct AiTurn
{
    bool user = true;
    std::string text;                            // what goes back into the conversation history
    std::vector<std::vector<unsigned char>> images;   // PNG bytes (user turns only)
};

struct AiEvent
{
    enum Type { Text, Thinking, Done, Error, Status } type = Text;
    std::string text;
};

class AiClient
{
public:
    ~AiClient();
    // history: previous turns; the last user turn gets the document context + images.
    void Start(const AiSettings& s, const std::vector<AiTurn>& history, const std::string& docContext, const std::vector<std::vector<unsigned char>>& snapshot);
    void Cancel();
    bool Busy() const { return m_busy; }
    std::vector<AiEvent> Poll();

    static std::string ResolveKey(const AiSettings& s);

private:
    void Push(AiEvent::Type t, const std::string& text);
    std::thread m_thread;
    std::atomic<bool> m_busy{ false };
    std::atomic<bool> m_cancel{ false };
    std::mutex m_mutex;
    std::vector<AiEvent> m_events;
};

const char* AiSystemPrompt();
