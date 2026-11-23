// Web projects: browser chrome around the pages, the Site / Pages panel, links between pages and the live preview.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "platform.h"
#include "web_preview.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

using namespace theme;
namespace fs = std::filesystem;

namespace
{
    struct Viewport { const char* label; float w, h; };
    const Viewport kViewports[] = { { "Desktop", 1440, 900 }, { "Laptop", 1280, 800 }, { "Tablet", 768, 1024 }, { "Mobile", 390, 844 } };
    constexpr int kViewportCount = 4;

    std::vector<Node*> PageFrames(Document& doc)
    {
        std::vector<Node*> out;
        for (auto& r : doc.roots) if (r->type == NodeType::Frame && r->visible) out.push_back(r.get());
        return out;
    }

    Node* RootOf(Node* n)
    {
        while (n && n->parent) n = n->parent;
        return n;
    }
}

ExportResult App::RunWebExport()
{
    if (webOpt.outDir.empty() && !projectDir.empty()) webOpt.outDir = (fs::u8path(projectDir) / "export").u8string();
    return ExportWeb(doc, webOpt, platform::ExeDir() + "/runtime");
}

Node* App::WebCurrentPage()
{
    for (Node* s : SelNodes())
        if (Node* r = RootOf(s); r && r->type == NodeType::Frame && r->visible) return r;
    ImVec2 c = ScreenToWorld(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
    Node* best = nullptr;
    float bestD = 1e30f;
    for (Node* p : PageFrames(doc))
    {
        float dx = std::max({ p->x - c.x, 0.0f, c.x - (p->x + p->w) }), dy = std::max({ p->y - c.y, 0.0f, c.y - (p->y + p->h) });
        float d = dx * dx + dy * dy;
        if (d < bestD) { bestD = d; best = p; }
    }
    return best;
}

void App::OpenWebPreview(Node* page, bool openBrowser)
{
    if (!webPreview) webPreview = std::make_shared<WebPreview>();
    std::string err;
    if (!webPreview->Start(&err)) { Toast("Preview: " + err, true); return; }
    webBuiltHash = 0;   // publish now
    WebPreviewTick();
    std::string file = page ? WebPageFile(doc, page) : std::string("index.html");
    webPreview->SetPage(file);
    if (openBrowser) platform::OpenUrl(webPreview->Url(file));
}

void App::StopWebPreview()
{
    if (webPreview) webPreview->Stop();
}

void App::WebPreviewTick()
{
    if (!webPreview || !webPreview->Running()) return;
    double now = ImGui::GetTime();
    if (webBuiltHash && (ImGui::IsMouseDown(0) || now - webBuiltTime < 0.25)) return;
    // The undo snapshot covers the layers; assets and site settings are added on top.
    size_t h = std::hash<std::string>{}(m_snapshot);
    h ^= std::hash<std::string>{}(webOpt.title + "|" + webOpt.lang + (webOpt.fitWidth ? "|f" : "|") + (webOpt.fonts ? "t" : "")) * 31 + doc.assets.size();
    if (Node* page = WebCurrentPage()) webPreview->SetPage(WebPageFile(doc, page));
    if (h == webBuiltHash) return;
    webBuiltHash = h;
    webBuiltTime = now;
    webPreview->Publish(BuildWebSite(doc, webOpt, platform::ExeDir() + "/runtime", true));
}

void App::FollowLink(Node* n)
{
    for (; n; n = n->parent)
    {
        if (n->link.empty()) continue;
        if (n->link.rfind("page:", 0) == 0)
        {
            Node* page = doc.Find(n->link.substr(5));
            if (!page) return;
            float cw = canvasMax.x - canvasMin.x, ch = canvasMax.y - canvasMin.y;
            zoom = std::clamp(std::min(cw / page->w, (ch - U(60)) / page->h) * 0.9f, 0.02f, 4.0f);
            pan = ImVec2(cw * 0.5f - (page->x + page->w * 0.5f) * zoom, ch * 0.5f - (page->y + page->h * 0.5f) * zoom + U(14));
            animClocks.clear();   // entrance animations play again, like on a new page
        }
        else platform::OpenUrl(n->link);
        return;
    }
}

// Browser window chrome above every page: dots, the page name (the frame label sits in it), address and width.
void App::DrawWebChrome(ImDrawList* dl)
{
    if (!IsWeb()) return;
    float barH = U(30);
    ImFont* f = fonts::UI(fonts::UI_Regular);
    float fs = 11.0f * dpi;
    bool live = webPreview && webPreview->Running();
    for (Node* p : PageFrames(doc))
    {
        ImVec2 a = WorldToScreen(ImVec2(p->x, p->y)), b = WorldToScreen(ImVec2(p->x + p->w, p->y + p->h));
        if (b.x - a.x < U(90) || b.x < canvasMin.x || a.x > canvasMax.x || b.y < canvasMin.y || a.y - barH > canvasMax.y) continue;
        ImVec2 ba(a.x, a.y - barH), bb(b.x, a.y);
        dl->AddRectFilled(ba, bb, IM_COL32(52, 52, 56, 255), U(8), ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(ba.x, bb.y - 0.5f), ImVec2(bb.x, bb.y - 0.5f), IM_COL32(0, 0, 0, 90));
        const ImU32 dots[] = { IM_COL32(255, 95, 87, 200), IM_COL32(254, 188, 46, 200), IM_COL32(40, 200, 64, 200) };
        for (int i = 0; i < 3; i++) dl->AddCircleFilled(ImVec2(ba.x + U(14) + i * U(13), ba.y + barH * 0.5f), U(4.5f), dots[i]);

        // address: the live preview URL, or the file name
        std::string file = WebPageFile(doc, p);
        std::string addr = live ? webPreview->Url(file).substr(7) : file;
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, p->name.c_str());
        char width[32];
        snprintf(width, sizeof(width), "%d px", (int)std::round(p->w));
        ImVec2 ws = f->CalcTextSizeA(fs, FLT_MAX, 0, width);
        float x0 = std::max(ba.x + U(52) + ts.x + U(16), ba.x + (bb.x - ba.x) * 0.3f), x1 = bb.x - ws.x - U(20);
        if (x1 - x0 > U(90))
        {
            ImVec2 pa(x0, ba.y + U(6)), pb(x1, bb.y - U(6));
            dl->AddRectFilled(pa, pb, IM_COL32(32, 32, 35, 255), U(6));
            ImVec2 as = f->CalcTextSizeA(fs, FLT_MAX, 0, addr.c_str());
            float tx = std::max(pa.x + U(10), (pa.x + pb.x - as.x) * 0.5f);
            if (live) dl->AddCircleFilled(ImVec2(tx - U(8), (pa.y + pb.y) * 0.5f), U(3), Success);
            dl->PushClipRect(pa, pb, true);
            dl->AddText(f, fs, ImVec2(tx, (pa.y + pb.y - fs) * 0.5f), TextDim, addr.c_str());
            dl->PopClipRect();
        }
        if (bb.x - ba.x > U(160)) dl->AddText(f, fs, ImVec2(bb.x - ws.x - U(10), ba.y + (barH - fs) * 0.5f), TextFaint, width);
    }
}

void App::DrawWebPanel(float W)
{
    float lx = W * 0.34f;
    auto Row = [&](const char* label) { ui::Label(label); ImGui::SameLine(lx); };

    ui::Section("Site");
    Row("Title");
    if (ui::TextField("##wtitle", &webOpt.title, W - lx, doc.name.c_str())) modified = true;
    Row("Language");
    if (ui::TextField("##wlang", &webOpt.lang, W - lx, "en")) modified = true;
    if (ui::Check("Fit pages into narrow windows", &webOpt.fitWidth)) modified = true;
    if (ui::Check("Ship design fonts", &webOpt.fonts)) modified = true;

    ui::Section("Pages");
    auto pages = PageFrames(doc);
    for (Node* p : pages)
    {
        ImGui::PushID(p->id.c_str());
        std::string file = WebPageFile(doc, p);
        bool sel = selection.size() == 1 && selection[0] == p->id;
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float rowH = ImGui::GetFrameHeight();
        if (ImGui::Selectable("##page", sel, 0, ImVec2(W - rowH - U(4), rowH)))
        {
            SetSelection({ p->id });
            ZoomToFit(true);
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float ty = pos.y + (rowH - ImGui::GetFontSize()) * 0.5f;
        dl->AddText(ImVec2(pos.x + U(6), ty), Text, p->name.c_str());
        ImVec2 fsz = ImGui::CalcTextSize(file.c_str());
        dl->AddText(ImVec2(pos.x + W - rowH - U(12) - fsz.x, ty), TextDim, file.c_str());
        ImGui::SameLine(0, U(4));
        if (IconButton("##open", Icon::Play, false, rowH, "Open in the browser")) OpenWebPreview(p);
        ImGui::PopID();
    }
    if (pages.empty()) ui::Label("Draw a frame (F): every top-level frame is a page.");
    ui::Label("Add a page");
    float bw = (W - U(4) * (kViewportCount - 1)) / kViewportCount;
    for (int i = 0; i < kViewportCount; i++)
    {
        if (i) ImGui::SameLine(0, U(4));
        ImGui::PushID(i);
        if (ImGui::Button(kViewports[i].label, ImVec2(bw, 0)))
        {
            float x = 0;
            for (auto& r : doc.roots) x = std::max(x, r->x + r->w + 160);
            Node* f = CreateNode(NodeType::Frame, ImVec2(x, 0), ImVec2(kViewports[i].w, kViewports[i].h), nullptr);
            f->name = "Page " + std::to_string(pages.size() + 1);
            f->fill = Paint{ 1, ImVec4(1, 1, 1, 1) };
            SetSelection({ f->id });
            ZoomToFit(true);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%d x %d", (int)kViewports[i].w, (int)kViewports[i].h);
        ImGui::PopID();
    }

    ui::Section("Preview");
    if (ui::PrimaryButton("Open in browser", ImVec2(W, U(28)))) OpenWebPreview(WebCurrentPage());
    bool live = webPreview && webPreview->Running();
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    if (live) ImGui::TextWrapped("Live at %s - the browser reloads as you edit and follows the page you work on.", webPreview->Url().c_str());
    else ImGui::TextWrapped("Opens the site in your browser from a local server. It reloads as you edit and follows the page you work on.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    if (live && ImGui::Button("Stop preview", ImVec2(W, 0))) StopWebPreview();
}

void App::DrawWebPageSection(Node* page, float W)
{
    ImGui::PushID("##webpage");
    ui::Section("Page");
    std::string file = WebPageFile(doc, page);
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::Text("%s%s", file.c_str(), file == "index.html" ? "  (home page)" : "");
    ImGui::PopStyleColor();
    float bw = (W - U(4) * (kViewportCount - 1)) / kViewportCount;
    for (int i = 0; i < kViewportCount; i++)
    {
        if (i) ImGui::SameLine(0, U(4));
        ImGui::PushID(i);
        bool on = std::fabs(page->w - kViewports[i].w) < 0.5f;
        if (on) ImGui::PushStyleColor(ImGuiCol_Button, AccentDim);
        if (ImGui::Button(kViewports[i].label, ImVec2(bw, 0))) { page->w = kViewports[i].w; MarkDirty(); }
        if (on) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Width %d px", (int)kViewports[i].w);
        ImGui::PopID();
    }
    if (ImGui::Button("Open in browser", ImVec2(W, 0))) OpenWebPreview(page);
    ImGui::PopID();
}

// Prototype tab: where a click on the layer leads.
bool App::DrawLinkSection(Node* n, float W)
{
    ui::Section("Link");
    auto pages = PageFrames(doc);
    std::vector<std::string> labels = { "None" };
    for (Node* p : pages) labels.push_back("Page: " + p->name);
    labels.push_back("Web address (URL)");
    std::vector<const char*> items;
    for (auto& l : labels) items.push_back(l.c_str());
    int cur = 0;
    if (n->link.rfind("page:", 0) == 0)
    {
        for (size_t i = 0; i < pages.size(); i++) if (pages[i]->id == n->link.substr(5)) cur = (int)i + 1;
    }
    else if (!n->link.empty()) cur = (int)pages.size() + 1;
    bool ch = false;
    if (ui::Combo("##link", &cur, items.data(), (int)items.size(), W))
    {
        if (cur == 0) n->link.clear();
        else if (cur <= (int)pages.size()) n->link = "page:" + pages[cur - 1]->id;
        else n->link = "https://";
        ch = true;
    }
    if (cur == (int)pages.size() + 1) ch |= ui::TextField("##url", &n->link, W, "https://example.com");
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    ImGui::TextWrapped("Exported as a link. In Play mode a click opens the page.");
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    return ch;
}
