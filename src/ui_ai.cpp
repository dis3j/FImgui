#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "exporters.h"
#include "platform.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

using namespace theme;

// ---------------------------------------------------------------- operations

static Node* FindRef(Document& doc, const json& v)
{
    if (!v.is_string()) return nullptr;
    std::string s = v.get<std::string>();
    if (Node* n = doc.Find(s)) return n;
    return doc.FindByName(s);
}

// "pattern:noise" style texture references become generated assets.
static void ResolvePatterns(Document& doc, Node* n)
{
    if (!n) return;
    const std::string& a = n->texture.asset;
    if (a.rfind("pattern:", 0) == 0)
    {
        std::string kind = a.substr(8);
        int idx = kind == "scanlines" ? 1 : kind == "grid" ? 2 : kind == "stripes" ? 3 : 0;
        n->texture.asset = MakeTextureAsset(doc, idx);
    }
    for (auto& c : n->children) ResolvePatterns(doc, c.get());
}

static bool ApplyAiOpImpl(App* app, const json& op, std::string* err);

bool App::ApplyAiOp(const json& op, std::string* err)
{
    bool ok = ApplyAiOpImpl(this, op, err);
    if (ok)
    {
        std::string id = op.contains("node") && op["node"].is_object() ? op["node"].value("id", std::string()) : op.value("id", std::string());
        ResolvePatterns(doc, doc.Find(id));
    }
    return ok;
}

static bool ApplyAiOpImpl(App* app, const json& op, std::string* err)
{
    Document& doc = app->doc;
    std::vector<std::string>& selection = app->selection;
    std::string o = op.value("op", "");
    if (o == "create")
    {
        if (!op.contains("node") || !op["node"].is_object()) { *err = "create: missing node"; return false; }
        json nj = op["node"];
        Node* parent = op.contains("parent") ? FindRef(doc, op["parent"]) : nullptr;
        while (parent && !parent->IsContainer()) parent = parent->parent;
        int index = op.value("index", -1);
        std::string id = nj.value("id", "");
        std::unique_ptr<Node> replaced;
        if (Node* old = doc.Find(id))
        {
            // Replace in place
            if (!op.contains("parent") || op["parent"].is_null()) parent = old->parent;
            if (index < 0 && old->parent == parent) index = doc.IndexOf(old);
            if (parent && (parent == old || doc.IsAncestor(old, parent))) parent = old->parent;
            replaced = doc.Detach(old);
        }
        auto n = NodeFromJson(nj, &doc);
        // Re-creating a container without "children" keeps its existing content.
        if (replaced && !nj.contains("children") && n->IsContainer())
            for (auto& c : replaced->children) { c->parent = n.get(); n->children.push_back(std::move(c)); }
        doc.Insert(std::move(n), parent, index);
        return true;
    }
    if (o == "update")
    {
        Node* n = FindRef(doc, op.value("id", json()));
        if (!n) { *err = "update: unknown id " + op.value("id", std::string()); return false; }
        if (op.contains("props")) ApplyProps(n, op["props"], &doc);
        else
        {
            json p = op;
            p.erase("op");
            p.erase("id");
            ApplyProps(n, p, &doc);
        }
        return true;
    }
    if (o == "delete")
    {
        Node* n = FindRef(doc, op.value("id", json()));
        if (!n) { *err = "delete: unknown id"; return false; }
        selection.erase(std::remove(selection.begin(), selection.end(), n->id), selection.end());
        doc.Detach(n);
        return true;
    }
    if (o == "move")
    {
        Node* n = FindRef(doc, op.value("id", json()));
        if (!n) { *err = "move: unknown id"; return false; }
        Node* parent = op.contains("parent") ? FindRef(doc, op["parent"]) : n->parent;
        if (parent && (parent == n || doc.IsAncestor(n, parent))) { *err = "move: cycle"; return false; }
        auto u = doc.Detach(n);
        doc.Insert(std::move(u), parent, op.value("index", -1));
        return true;
    }
    if (o == "svg")
    {
        std::string svg = op.value("svg", "");
        json nj = op.value("node", json::object());
        Node* parent = op.contains("parent") ? FindRef(doc, op["parent"]) : nullptr;
        while (parent && !parent->IsContainer()) parent = parent->parent;
        std::string id = nj.value("id", "");
        int index = -1;
        if (Node* old = doc.Find(id)) { if (old->parent == parent) index = doc.IndexOf(old); doc.Detach(old); }
        Node* n = ImportSVG(doc, svg, nj.value("name", std::string("icon")), parent, ImVec2(nj.value("x", 0.0f), nj.value("y", 0.0f)));
        if (!n) { *err = "svg: invalid markup"; return false; }
        if (!id.empty()) n->id = id;
        nj.erase("id");
        if (!nj.contains("fill") && nj.value("recolor", false)) nj["fill"] = "#FFFFFF";
        ApplyProps(n, nj, &doc);
        if (index >= 0)
        {
            auto u = doc.Detach(n);
            doc.Insert(std::move(u), parent, index);
        }
        return true;
    }
    if (o == "clear")
    {
        doc.roots.clear();
        selection.clear();
        return true;
    }
    if (o == "select")
    {
        std::vector<std::string> ids;
        if (op.contains("ids") && op["ids"].is_array()) for (auto& v : op["ids"]) if (v.is_string()) ids.push_back(v.get<std::string>());
        if (op.contains("id") && op["id"].is_string()) ids.push_back(op["id"].get<std::string>());
        app->SetSelection(ids);
        return true;
    }
    *err = "unknown op '" + o + "'";
    return false;
}

std::string App::AiDocContext()
{
    json d = doc.ToJson(false);
    d.erase("format");
    d.erase("version");
    d.erase("nextId");
    json ctx;
    ctx["document"] = d;
    ctx["selection"] = selection;
    ImVec2 a = ScreenToWorld(canvasMin), b = ScreenToWorld(canvasMax);
    ctx["visibleCanvas"] = { { "x", std::round(a.x) }, { "y", std::round(a.y) }, { "w", std::round(b.x - a.x) }, { "h", std::round(b.y - a.y) } };
    json fams = json::array();
    const char* preferred[] = { "Inter", "Segoe UI", "Arial", "Consolas", "Roboto", "Montserrat", "Poppins", "JetBrains Mono", "Bahnschrift", "Tahoma", "Verdana", "Trebuchet MS" };
    for (const char* p : preferred)
        for (auto& f : fonts::Families()) if (f == p) { fams.push_back(f); break; }
    ctx["fonts"] = fams;
    json st = json::object();
    for (auto& v : CollectVars(doc))
        st[v.name] = v.kind == VarKind::String ? json(vars.str[v.name]) : json(vars.Get(v.name));
    ctx["state"] = st;
    return ctx.dump();
}

// ---------------------------------------------------------------- send / pump

void App::AiSend()
{
    if (aiClient.Busy()) return;
    std::string text = aiInput;
    while (!text.empty() && (text.back() == '\n' || text.back() == ' ')) text.pop_back();
    if (text.empty() && aiAttachments.empty()) return;
    if (AiClient::ResolveKey(ai).empty()) { Toast("Add your Claude API key in Settings", true); showSettings = true; return; }

    ChatMessage um;
    um.user = true;
    um.text = text;
    um.images = aiAttachments;
    chat.push_back(um);
    aiInput[0] = 0;
    aiAttachments.clear();

    ChatMessage am;
    am.user = false;
    am.streaming = true;
    chat.push_back(am);
    m_aiLine.clear();

    // Everything the AI does becomes one undo step.
    m_dirty = true;
    Checkpoint();
    m_aiApplying = true;

    std::vector<AiTurn> history;
    size_t first = chat.size() > 14 ? chat.size() - 14 : 0;
    for (size_t i = first; i + 1 < chat.size(); i++)
    {
        const ChatMessage& m = chat[i];
        if ((m.error && !m.user) || m.source == "mcp") continue;
        AiTurn t;
        t.user = m.user;
        t.text = m.user ? m.text : m.raw;
        if (m.user) t.images = m.images;
        if (!history.empty() && history.back().user == t.user) history.back().text += "\n\n" + t.text;
        else history.push_back(t);
    }
    while (!history.empty() && !history.front().user) history.erase(history.begin());
    std::string ctx = AiDocContext();

    bool snapshot = ai.sendSnapshot && !doc.roots.empty();
    if (!snapshot) { aiClient.Start(ai, history, ctx, {}); return; }
    // Render the canvas after this frame (GPU), then start the request.
    m_aiPendingSend = true;
    deferred.push_back([this, history, ctx] {
        m_aiPendingSend = false;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (auto& n : doc.roots)
        {
            if (!n->visible) continue;
            x0 = std::min(x0, n->x); y0 = std::min(y0, n->y);
            x1 = std::max(x1, n->x + n->w); y1 = std::max(y1, n->y + n->h);
        }
        std::vector<std::vector<unsigned char>> shots;
        if (x1 > x0 && y1 > y0)
        {
            float pad = 16;
            ImVec2 a(x0 - pad, y0 - pad), b(x1 + pad, y1 + pad);
            float scale = std::min(1.0f, 1400.0f / std::max(b.x - a.x, b.y - a.y));
            std::vector<unsigned char> png;
            if (RenderRegionPNG(a, b, scale, png)) shots.push_back(png);
        }
        aiClient.Start(ai, history, ctx, shots);
    });
}

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

void App::AiPump()
{
    auto events = aiClient.Poll();
    if (events.empty() || chat.empty()) return;
    // MCP activity may have been appended meanwhile: write into the streaming assistant message.
    size_t target = chat.size() - 1;
    for (size_t i = chat.size(); i-- > 0;)
        if (!chat[i].user && chat[i].streaming) { target = i; break; }
    ChatMessage& m = chat[target];
    auto handleLine = [&](const std::string& rawLine) {
        std::string line = Trim(rawLine);
        if (line.rfind("```", 0) == 0) return;
        if (line.rfind("{\"op\"", 0) == 0 || line.rfind("{ \"op\"", 0) == 0)
        {
            json op = json::parse(line, nullptr, false);
            std::string err;
            if (op.is_discarded()) m.notes.push_back("Skipped a malformed operation");
            else if (ApplyAiOp(op, &err)) { m.ops++; doc.ApplyLayout(); }
            else m.notes.push_back(err);
            return;
        }
        if (line.empty() && (m.text.empty() || m.text.back() == '\n')) return;
        m.text += line + "\n";
    };
    for (auto& e : events)
    {
        switch (e.type)
        {
        case AiEvent::Text:
        {
            m.raw += e.text;
            m_aiLine += e.text;
            size_t nl;
            while ((nl = m_aiLine.find('\n')) != std::string::npos)
            {
                handleLine(m_aiLine.substr(0, nl));
                m_aiLine.erase(0, nl + 1);
            }
            break;
        }
        case AiEvent::Thinking: m.thinking += e.text; break;
        case AiEvent::Status: m.notes.push_back(e.text); break;
        case AiEvent::Error: m.error = true; m.text += (m.text.empty() ? "" : "\n") + e.text; break;
        case AiEvent::Done:
            if (!m_aiLine.empty()) { handleLine(m_aiLine); m_aiLine.clear(); }
            m.streaming = false;
            m_aiApplying = false;
            if (m.ops > 0) { MarkDirty(); EnsureVars(doc, vars); }
            while (!m.text.empty() && m.text.back() == '\n') m.text.pop_back();
            break;
        }
    }
}

// ---------------------------------------------------------------- UI

void App::DrawAiTab()
{
    float W = ui::Avail();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::Dummy(ImVec2(0, U(4)));

    // Header
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s  \xC2\xB7  %s", ai.model.c_str(), ai.effort.c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine(W - U(80) - ImGui::GetFrameHeight());
    if (ImGui::Button("New chat", ImVec2(U(76), 0)) && !aiClient.Busy()) { chat.clear(); }
    ImGui::SameLine(0, U(4));
    if (IconButton("##aisettings", Icon::Settings, false, 0, "AI settings")) showSettings = true;

    {
        // MCP status
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool on = mcp.Running();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x + U(4), p.y + ImGui::GetTextLineHeight() * 0.5f + U(1)), U(3.5f), on ? Success : (mcpEnabled ? Danger : TextFaint));
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + U(12));
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        std::string client = mcp.ClientName();
        if (on) ImGui::Text("MCP on :%d%s%s", mcp.Port(), client.empty() ? "" : "  \xC2\xB7  ", client.c_str());
        else ImGui::TextUnformatted(mcpEnabled ? ("MCP: " + mcpError).c_str() : "MCP server off");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("External AI clients (Claude Desktop, Claude Code, ...) can draw here over MCP.\nSettings shows how to connect.");
        if (ImGui::IsItemClicked()) showSettings = true;
    }

    if (AiClient::ResolveKey(ai).empty())
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(0, U(4)));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
        ImGui::TextUnformatted("Connect Claude to build menus from a description. Add your API key in Settings, or set ANTHROPIC_API_KEY. You can also drive Fimgui from Claude Desktop or Claude Code over MCP (see Settings).");
        ImGui::PopTextWrapPos();
        if (ui::PrimaryButton("Add API key", ImVec2(W, 0))) showSettings = true;
        (void)p;
    }

    // Chat history
    float inputH = U(96) + (aiAttachments.empty() ? 0 : U(28));
    ImGui::BeginChild("##chat", ImVec2(W, ImGui::GetContentRegionAvail().y - inputH), 0);
    float cw = ImGui::GetContentRegionAvail().x;
    if (chat.empty())
    {
        ImGui::Dummy(ImVec2(0, U(16)));
        DrawIcon(ImGui::GetWindowDrawList(), Icon::Sparkle, ImVec2(ImGui::GetCursorScreenPos().x + cw * 0.5f, ImGui::GetCursorScreenPos().y + U(14)), U(26), Accent);
        ImGui::Dummy(ImVec2(0, U(36)));
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
        const char* title = "What should we build?";
        ImGui::SetCursorPosX((cw - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        ImGui::PushTextWrapPos(cw);
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::TextWrapped("Describe a menu and Claude draws it live on the canvas. Attach a screenshot to recreate a reference. Every change is one Ctrl+Z away.");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0, U(6)));
        const char* ideas[] = {
            "Dark game menu 720x480: sidebar tabs Aimbot / Visuals / Misc / Settings with icons, toggles, sliders and a color accent",
            "Settings window with sections, checkboxes, sliders and a text input",
            "Minimal login window: logo, username and password inputs, Sign in button",
            "Make the current design more modern: softer colors, consistent spacing, better hierarchy",
        };
        for (const char* idea : ideas)
        {
            ImGui::PushID(idea);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImVec2 ts = ImGui::CalcTextSize(idea, nullptr, false, cw - U(20));
            ImVec2 size(cw, ts.y + U(14));
            if (ImGui::InvisibleButton("##idea", size)) snprintf(aiInput, sizeof(aiInput), "%s", idea);
            ImU32 bg = ImGui::IsItemHovered() ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 10);
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), bg, U(8));
            ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, ImVec2(p.x + U(10), p.y + U(7)), TextDim, idea, nullptr, cw - U(20));
            ImGui::PopID();
            ImGui::Dummy(ImVec2(0, U(2)));
        }
    }
    for (size_t i = 0; i < chat.size(); i++)
    {
        ChatMessage& m = chat[i];
        ImGui::PushID((int)i);
        ImGui::Dummy(ImVec2(0, U(6)));
        if (m.source == "mcp")
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            DrawIcon(ImGui::GetWindowDrawList(), Icon::Link, ImVec2(p.x + U(7), p.y + U(8)), U(13), Purple);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + U(20));
            ImGui::BeginGroup();
            ImGui::PushStyleColor(ImGuiCol_Text, Purple);
            ImGui::Text("%s via MCP", m.thinking.c_str());
            ImGui::PopStyleColor();
            ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
            size_t first = m.notes.size() > 6 ? m.notes.size() - 6 : 0;
            if (first) ImGui::Text("... %d earlier calls", (int)first);
            for (size_t k = first; k < m.notes.size(); k++) ImGui::TextUnformatted(m.notes[k].c_str());
            ImGui::PopStyleColor();
            ImGui::EndGroup();
        }
        else if (m.user)
        {
            std::string t = m.text;
            if (!m.images.empty()) t += (t.empty() ? "" : "\n") + std::string("[") + std::to_string(m.images.size()) + " image(s) attached]";
            ImVec2 ts = ImGui::CalcTextSize(t.c_str(), nullptr, false, cw - U(40));
            float bw = std::min(cw, ts.x + U(20));
            ImVec2 p = ImGui::GetCursorScreenPos();
            p.x += cw - bw;
            ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + bw, p.y + ts.y + U(14)), IM_COL32(13, 153, 255, 50), U(10));
            ImGui::GetWindowDrawList()->AddText(nullptr, 0.0f, ImVec2(p.x + U(10), p.y + U(7)), Text, t.c_str(), nullptr, cw - U(40));
            ImGui::Dummy(ImVec2(cw, ts.y + U(14)));
        }
        else
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            DrawIcon(ImGui::GetWindowDrawList(), Icon::Sparkle, ImVec2(p.x + U(7), p.y + U(8)), U(13), m.error ? Danger : Accent);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + U(20));
            ImGui::BeginGroup();
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + cw - U(22));
            if (!m.thinking.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
                if (ImGui::TreeNodeEx("##think", ImGuiTreeNodeFlags_NoTreePushOnOpen, m.streaming && m.text.empty() && m.ops == 0 ? "Thinking..." : "Thought process"))
                    ImGui::TextWrapped("%s", m.thinking.c_str());
                ImGui::PopStyleColor();
            }
            std::string shown = m.text;
            if (m.streaming && !m_aiLine.empty())
            {
                std::string partial = Trim(m_aiLine);
                if (!partial.empty() && partial[0] != '{' && partial.rfind("```", 0) != 0) shown += partial;
            }
            if (!shown.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, m.error ? Danger : Text);
                ImGui::TextWrapped("%s", shown.c_str());
                ImGui::PopStyleColor();
            }
            if (m.ops > 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, Success);
                ImGui::Text("%d change%s applied to the canvas", m.ops, m.ops == 1 ? "" : "s");
                ImGui::PopStyleColor();
            }
            if (m.streaming)
            {
                float t = (float)ImGui::GetTime();
                ImVec2 q = ImGui::GetCursorScreenPos();
                for (int k = 0; k < 3; k++)
                {
                    float a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 6.0f - k * 0.8f));
                    ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(q.x + U(5) + k * U(10), q.y + U(8)), U(3), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, a)));
                }
                ImGui::Dummy(ImVec2(U(40), U(16)));
            }
            if (!m.notes.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
                size_t shownNotes = std::min<size_t>(m.notes.size(), 4);
                for (size_t k = 0; k < shownNotes; k++) ImGui::TextWrapped("%s", m.notes[k].c_str());
                if (m.notes.size() > shownNotes) ImGui::Text("+%d more", (int)(m.notes.size() - shownNotes));
                ImGui::PopStyleColor();
            }
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
        }
        ImGui::PopID();
    }
    if (aiClient.Busy() && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - U(40)) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Attachments
    if (!aiAttachments.empty())
    {
        for (size_t i = 0; i < aiAttachments.size(); i++)
        {
            if (i) ImGui::SameLine();
            ImGui::PushID((int)i);
            char lbl[64];
            snprintf(lbl, sizeof(lbl), "image %d (%d KB)  x", (int)i + 1, (int)(aiAttachments[i].size() / 1024));
            if (ImGui::SmallButton(lbl)) { aiAttachments.erase(aiAttachments.begin() + i); ImGui::PopID(); break; }
            ImGui::PopID();
        }
    }

    // Input box
    ImVec2 boxMin = ImGui::GetCursorScreenPos();
    float boxH = U(88);
    dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(boxMin, ImVec2(boxMin.x + W, boxMin.y + boxH), IM_COL32(56, 56, 56, 255), U(10));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(U(10), U(8)));
    bool busy = aiClient.Busy() || m_aiPendingSend;
    bool submit = ImGui::InputTextMultiline("##aiinput", aiInput, sizeof(aiInput), ImVec2(W, boxH - U(32)),
                                            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine);
    bool inputActive = ImGui::IsItemActive();
    if (inputActive && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && platform::ClipboardHasImage())
    {
        std::vector<unsigned char> png;
        if (platform::GetClipboardImage(png)) aiAttachments.push_back(png);
    }
    if (aiInput[0] == 0 && !inputActive)
        dl->AddText(ImVec2(boxMin.x + U(10), boxMin.y + U(8)), TextFaint, "Describe a menu or a change...");
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(boxMin.x + U(6), boxMin.y + boxH - U(30)));
    if (IconButton("##attach", Icon::Attach, false, U(26), "Attach reference image (or paste with Ctrl+V)"))
        for (auto& f : platform::OpenFilesDialog("Attach image", "Images|*.png;*.jpg;*.jpeg"))
        {
            std::vector<unsigned char> d;
            if (platform::ReadFile(f, d)) aiAttachments.push_back(d);
        }
    ImGui::SameLine(0, U(4));
    bool snap = ai.sendSnapshot;
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + U(2));
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    if (ImGui::Checkbox("canvas snapshot", &snap)) { ai.sendSnapshot = snap; SaveSettings(); }
    ImGui::PopStyleColor();
    ImGui::SetCursorScreenPos(ImVec2(boxMin.x + W - U(32), boxMin.y + boxH - U(30)));
    if (busy)
    {
        if (IconButton("##stop", Icon::Stop, true, U(26), "Stop")) aiClient.Cancel();
    }
    else if (IconButton("##send", Icon::Send, aiInput[0] != 0, U(26), "Send (Enter)") || submit)
        AiSend();
    ImGui::SetCursorScreenPos(ImVec2(boxMin.x, boxMin.y + boxH + U(4)));
    ImGui::Dummy(ImVec2(W, 0));
}
