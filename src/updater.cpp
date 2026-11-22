#include "updater.h"
#include "http.h"
#include "version.h"
#include "platform.h"
#include "../third_party/json/json.hpp"
#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace updater
{
    static std::mutex g_mutex;
    static std::atomic<State> g_state{ State::Idle };
    static Release g_release;
    static std::string g_error;

    static std::vector<int> Parts(std::string v)
    {
        if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
        std::vector<int> out;
        std::stringstream ss(v);
        std::string part;
        while (std::getline(ss, part, '.')) out.push_back(atoi(part.c_str()));
        while (out.size() < 3) out.push_back(0);
        return out;
    }

    int Compare(const std::string& a, const std::string& b)
    {
        auto pa = Parts(a), pb = Parts(b);
        for (size_t i = 0; i < std::max(pa.size(), pb.size()); i++)
        {
            int x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
            if (x != y) return x < y ? -1 : 1;
        }
        return 0;
    }

    std::string NotesFor(const std::string& changelog, const std::string& version)
    {
        std::stringstream ss(changelog);
        std::string line, out;
        bool inside = false;
        while (std::getline(ss, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("## ", 0) == 0)
            {
                if (inside) break;
                std::string head = line.substr(3);
                size_t p = head.find_first_not_of(" [v");
                head = p == std::string::npos ? "" : head.substr(p);
                std::string ver = head.substr(0, head.find_first_of(" ]"));
                inside = Compare(ver, version) == 0 && !ver.empty() && isdigit((unsigned char)ver[0]);
                continue;
            }
            if (inside) out += line + "\n";
        }
        while (!out.empty() && (out.back() == '\n' || out.back() == ' ')) out.pop_back();
        size_t s = out.find_first_not_of("\n ");
        return s == std::string::npos ? "" : out.substr(s);
    }

    void CheckAsync()
    {
        if (g_state == State::Running) return;
        g_state = State::Running;
        std::thread([] {
            std::string body, repo = platform::GetEnv("FIMGUI_UPDATE_REPO");
            if (repo.empty()) repo = FIMGUI_REPO;
            http::Result r = http::Get("https://api.github.com/repos/" + repo + "/releases/latest",
                                       { { "User-Agent", "Fimgui/" FIMGUI_VERSION }, { "Accept", "application/vnd.github+json" } }, &body);
            std::lock_guard<std::mutex> lock(g_mutex);
            if (!r.error.empty()) { g_error = r.error; g_state = State::Failed; return; }
            if (r.status == 404) { g_error = "No releases published yet"; g_state = State::Failed; return; }
            if (r.status < 200 || r.status >= 300) { g_error = "GitHub returned HTTP " + std::to_string(r.status); g_state = State::Failed; return; }
            nlohmann::json j = nlohmann::json::parse(body, nullptr, false);
            if (j.is_discarded() || !j.is_object()) { g_error = "Unexpected response from GitHub"; g_state = State::Failed; return; }
            g_release.tag = j.value("tag_name", std::string());
            g_release.version = g_release.tag;
            if (!g_release.version.empty() && (g_release.version[0] == 'v' || g_release.version[0] == 'V')) g_release.version.erase(0, 1);
            g_release.name = j.value("name", g_release.tag);
            g_release.notes = j.contains("body") && j["body"].is_string() ? j["body"].get<std::string>() : "";
            g_release.url = j.value("html_url", "https://github.com/" + repo + "/releases/latest");
            g_state = State::Done;
        }).detach();
    }

    State Poll(Release* out, std::string* error)
    {
        State s = g_state;
        if (s == State::Done || s == State::Failed)
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (out) *out = g_release;
            if (error) *error = g_error;
        }
        return s;
    }

    void Reset() { if (g_state != State::Running) g_state = State::Idle; }
}
