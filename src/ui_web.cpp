// Web projects: browser chrome around the pages, screen presets, breakpoints, the Site / Pages panel,
// links between pages and the live preview.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "platform.h"
#include "web_preview.h"
#include "web_devices.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>

using namespace theme;
namespace fs = std::filesystem;

namespace
{
    Icon KindIcon(const WebDevice* d)
    {
        if (!d) return Icon::Frame;
        switch (d->kind)
        {
        case DeviceKind::Desktop: return Icon::Desktop;
        case DeviceKind::Tablet: return Icon::Tablet;
        case DeviceKind::Phone: return Icon::Phone;
        default: return Icon::Foldable;
        }
    }

    std::string DeviceName(const Node* f)
    {
        bool rot = false;
        if (const WebDevice* d = WebDeviceOf(f, &rot)) return d->name;
        return "Custom size";
    }

    // "402 x 874": the screen of the device (as rotated), or the frame size.
    std::string ScreenText(const Node* f)
    {
        bool rot = false;
        const WebDevice* d = WebDeviceOf(f, &rot);
        ImVec2 s = d ? WebScreenOf(d, rot) : ImVec2(f->w, f->h);
        return std::to_string((int)std::round(s.x)) + " \xC3\x97 " + std::to_string((int)std::round(s.y));
    }

    // Window widths a breakpoint is shown at.
    std::string RangeText(const WebLayout& l)
    {
        if (l.minW <= 0 && l.maxW <= 0) return "all widths";
        if (l.minW <= 0) return "< " + std::to_string((int)l.maxW + 1);
        if (l.maxW <= 0) return std::to_string((int)l.minW) + "+";
        return std::to_string((int)l.minW) + " - " + std::to_string((int)l.maxW);
    }

    // A list row: device icon, name, and a dim note on the right. Returns true when clicked.
    bool IconRow(const char* id, Icon icon, const char* name, const std::string& note, float w, bool selected, float indent = 0, bool allowOverlap = false)
    {
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (allowOverlap) ImGui::SetNextItemAllowOverlap();
        bool hit = ImGui::Selectable(id, selected, 0, ImVec2(w, h));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        DrawIcon(dl, icon, ImVec2(p.x + indent + U(12), p.y + h * 0.5f), U(14), selected ? Text : TextDim);
        float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f;
        ImVec2 ns = ImGui::CalcTextSize(note.c_str());
        dl->PushClipRect(p, ImVec2(p.x + w - ns.x - U(14), p.y + h), true);
        dl->AddText(ImVec2(p.x + indent + U(28), ty), Text, name);
        dl->PopClipRect();
        if (!note.empty()) dl->AddText(ImVec2(p.x + w - ns.x - U(8), ty), TextFaint, note.c_str());
        return hit;
    }

    bool DeviceRow(const WebDevice& d, float w, bool selected)
    {
        ImGui::PushID(d.key);
        std::string size = std::to_string((int)d.w) + " \xC3\x97 " + std::to_string((int)d.h);
        bool hit = IconRow("##device", KindIcon(&d), d.name, size, w, selected);
        ImGui::PopID();
        return hit;
    }

    void GroupCaption(const char* text)
    {
        ImGui::Dummy(ImVec2(0, U(2)));
        ui::SubLabel(text, U(6));
        ImGui::Dummy(ImVec2(0, U(1)));
    }

    // Screen presets in a popup, grouped like Figma's frame presets. Returns the picked one.
    const WebDevice* DevicePopup(const char* id, const std::string& current)
    {
        const WebDevice* picked = nullptr;
        float w = U(272);
        ImGui::SetNextWindowSizeConstraints(ImVec2(w + U(16), 0), ImVec2(w + U(16), U(480)));
        if (ImGui::BeginPopup(id))
        {
            for (int g = 0; g < kWebDeviceGroupCount; g++)
            {
                GroupCaption(kWebDeviceGroups[g]);
                for (int i = 0; i < kWebDeviceCount; i++)
                {
                    const WebDevice& d = kWebDevices[i];
                    if (strcmp(d.group, kWebDeviceGroups[g]) != 0) continue;
                    if (DeviceRow(d, w, current == d.key)) { picked = &d; ImGui::CloseCurrentPopup(); }
                }
            }
            ImGui::EndPopup();
        }
        return picked;
    }

    // The field that opens the presets: [icon  Name      402 x 874  v]
    bool DeviceField(const char* id, const Node* page, float w)
    {
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton(id, ImVec2(w, h));
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float r = ImGui::GetStyle().FrameRounding;
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), Field, r);
        if (hov) dl->AddRect(p, ImVec2(p.x + w, p.y + h), Border, r);
        DrawIcon(dl, KindIcon(WebDeviceOf(page)), ImVec2(p.x + U(13), p.y + h * 0.5f), U(14), TextDim);
        DrawIcon(dl, Icon::ChevronDown, ImVec2(p.x + w - U(12), p.y + h * 0.5f), U(12), TextDim);
        std::string name = DeviceName(page), size = ScreenText(page);
        ImVec2 ss = ImGui::CalcTextSize(size.c_str());
        float ty = p.y + (h - ImGui::GetFontSize()) * 0.5f, sx = p.x + w - U(24) - ss.x;
        dl->PushClipRect(p, ImVec2(sx - U(8), p.y + h), true);
        dl->AddText(ImVec2(p.x + U(28), ty), Text, name.c_str());
        dl->PopClipRect();
        dl->AddText(ImVec2(sx, ty), TextDim, size.c_str());
        if (hov) ImGui::SetTooltip("Screen the page is designed for");
        return clicked;
    }

    void HelpText(const char* text, float w)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextWrapped("%s", text);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
    }

    float ContentBottom(const Node* f)
    {
        float b = 0;
        for (auto& c : f->children) if (c->visible) b = std::max(b, c->y + c->h);
        return b;
    }

    struct Geo { Node* n; float y, h; };

    // A page made for another width: full-width layers follow the page, centered ones stay centered,
    // the others keep their place relative to the width, and layers too wide for the page are narrowed
    // to it with a margin (text wraps). Auto layout pages re-flow by themselves.
    // Returns where the layers were, for PushDown once text has been re-measured.
    std::vector<Geo> AdaptToWidth(Node* f, float newW)
    {
        std::vector<Geo> old;
        float oldW = f->w;
        if (std::fabs(oldW - newW) < 0.5f || f->layout) return old;
        float k = newW / std::max(1.0f, oldW);
        float margin = std::round(std::max(16.0f, newW * 0.06f));
        for (auto& c : f->children)
        {
            old.push_back({ c.get(), c->y, c->h });
            float right = oldW - (c->x + c->w);
            if (c->x <= 0.5f && right <= 0.5f) { c->w = newW; continue; }
            bool centered = std::fabs(c->x + c->w * 0.5f - oldW * 0.5f) < 1.0f;
            if (c->w > newW - 2 * margin)
            {
                c->w = std::max(8.0f, newW - 2 * margin);
                c->x = margin;
                if (c->type == NodeType::Text && c->textSizing == 1) c->textSizing = 2;
            }
            else if (centered) c->x = std::round((newW - c->w) * 0.5f);
            else c->x = std::clamp(std::round(c->x * k), std::min(margin, c->x), std::max(0.0f, newW - margin - c->w));
        }
        // large text gets smaller on a much narrower screen (a 56 px heading becomes 30 px on a phone)
        if (k < 0.9f)
        {
            std::function<void(Node*)> shrink = [&](Node* n) {
                if (n->type == NodeType::Text && n->fontSize > 20.0f)
                {
                    float lh = n->lineHeight > 0 ? n->lineHeight / n->fontSize : 0.0f;
                    n->fontSize = std::max(20.0f, std::round(n->fontSize * std::sqrt(k)));
                    if (lh > 0) n->lineHeight = std::round(n->fontSize * lh);
                }
                for (auto& c : n->children) shrink(c.get());
            };
            for (auto& c : f->children) shrink(c.get());
        }
        return old;
    }

    // Layers that got taller or shorter (re-measured text) move the layers under them.
    void PushDown(const std::vector<Geo>& old)
    {
        for (const Geo& a : old)
        {
            float grow = a.n->h - a.h;
            if (std::fabs(grow) < 0.5f) continue;
            for (const Geo& b : old)
                if (b.n != a.n && b.y >= a.y + a.h - 0.5f) b.n->y += grow;
        }
    }

    void DashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float dash, float gap)
    {
        float len = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        if (len < 1) return;
        ImVec2 d((b.x - a.x) / len, (b.y - a.y) / len);
        for (float t = 0; t < len; t += dash + gap)
        {
            float e = std::min(len, t + dash);
            dl->AddLine(ImVec2(a.x + d.x * t, a.y + d.y * t), ImVec2(a.x + d.x * e, a.y + d.y * e), col, std::max(1.0f, dpi));
        }
    }

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

void App::OpenWebScreens(Node* page)
{
    OpenWebPreview(page, false);
    if (!webPreview || !webPreview->Running()) return;
    std::string file = page ? WebPageFile(doc, page) : std::string();
    platform::OpenUrl(webPreview->Url("__fimgui/screens.html") + (file.empty() ? std::string() : "?page=" + file));
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
    h ^= std::hash<std::string>{}(webOpt.title + "|" + webOpt.lang + (webOpt.fitWidth ? "|f" : "|") + (webOpt.fonts ? "t" : "") + (webOpt.stretchPhones ? "s" : "")) * 31 + doc.assets.size();
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
            // the breakpoint of the linked page closest to the one being viewed
            if (Node* from = RootOf(n))
                for (auto& l : WebPageLayouts(doc, page))
                    if (std::fabs(l.frame->w - from->w) < std::fabs(page->w - from->w)) page = l.frame;
            float cw = canvasMax.x - canvasMin.x, ch = canvasMax.y - canvasMin.y;
            zoom = std::clamp(std::min(cw / page->w, (ch - U(60)) / page->h) * 0.9f, 0.02f, 4.0f);
            pan = ImVec2(cw * 0.5f - (page->x + page->w * 0.5f) * zoom, ch * 0.5f - (page->y + page->h * 0.5f) * zoom + U(14));
            animClocks.clear();   // entrance animations play again, like on a new page
        }
        else platform::OpenUrl(n->link);
        return;
    }
}

// ---------------------------------------------------------------- canvas

bool App::WebBrowserBar(const Node* n) const
{
    // desktop-wide pages sit in a browser window; phones and other narrow screens get a plain label
    return IsWeb() && n && !n->parent && n->type == NodeType::Frame && n->w >= 600.0f && n->w * zoom >= U(90);
}

std::string App::WebFrameCaption(const Node* n) const
{
    if (!IsWeb() || !n || n->parent || n->type != NodeType::Frame) return "";
    const WebDevice* d = WebDeviceOf(n);
    return (d ? std::string(d->name) + "  " : std::string()) + std::to_string((int)std::round(n->w));
}

// Browser window chrome above desktop-wide pages: dots, the page name (the frame label sits in it), address and screen.
void App::DrawWebChrome(ImDrawList* dl)
{
    if (!IsWeb()) return;
    float barH = U(30);
    ImFont* f = fonts::UI(fonts::UI_Regular);
    float fs = 11.0f * dpi;
    bool live = webPreview && webPreview->Running();
    for (Node* p : PageFrames(doc))
    {
        if (!WebBrowserBar(p)) continue;
        ImVec2 a = WorldToScreen(ImVec2(p->x, p->y)), b = WorldToScreen(ImVec2(p->x + p->w, p->y + p->h));
        if (b.x < canvasMin.x || a.x > canvasMax.x || b.y < canvasMin.y || a.y - barH > canvasMax.y) continue;
        ImVec2 ba(a.x, a.y - barH), bb(b.x, a.y);
        dl->AddRectFilled(ba, bb, IM_COL32(52, 52, 56, 255), U(8), ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(ba.x, bb.y - 0.5f), ImVec2(bb.x, bb.y - 0.5f), IM_COL32(0, 0, 0, 90));
        const ImU32 dots[] = { IM_COL32(255, 95, 87, 200), IM_COL32(254, 188, 46, 200), IM_COL32(40, 200, 64, 200) };
        for (int i = 0; i < 3; i++) dl->AddCircleFilled(ImVec2(ba.x + U(14) + i * U(13), ba.y + barH * 0.5f), U(4.5f), dots[i]);

        // address: the live preview URL, or the file name
        std::string file = WebPageFile(doc, p);
        std::string addr = live ? webPreview->Url(file).substr(7) : file;
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, p->name.c_str());
        std::string cap = WebFrameCaption(p);
        ImVec2 ws = f->CalcTextSizeA(fs, FLT_MAX, 0, cap.c_str());
        float nameEnd = ba.x + U(52) + ts.x + U(12);   // the page name (tab title) ends here
        if (ws.x > (bb.x - ba.x) * 0.4f || bb.x - ws.x - U(10) < nameEnd)
        {
            cap = std::to_string((int)std::round(p->w));
            ws = f->CalcTextSizeA(fs, FLT_MAX, 0, cap.c_str());
            if (bb.x - ws.x - U(10) < nameEnd) cap.clear(), ws = ImVec2(0, 0);
        }
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
        if (bb.x - ba.x > U(160)) dl->AddText(f, fs, ImVec2(bb.x - ws.x - U(10), ba.y + (barH - fs) * 0.5f), TextFaint, cap.c_str());
    }
}

// Over pages made for a device: where the first screen ends, the fold of a foldable, and fixed layers.
void App::DrawWebGuides(ImDrawList* dl)
{
    if (!IsWeb()) return;
    ImFont* f = fonts::UI(fonts::UI_Regular);
    float fs = 10.0f * dpi;
    for (Node* p : PageFrames(doc))
    {
        ImVec2 a = WorldToScreen(ImVec2(p->x, p->y)), b = WorldToScreen(ImVec2(p->x + p->w, p->y + p->h));
        if (b.x - a.x < U(60) || b.x < canvasMin.x || a.x > canvasMax.x || b.y < canvasMin.y || a.y > canvasMax.y) continue;
        float dash = U(6), gap = U(4);

        // fixed layers: a small pin tag at their corner
        for (auto& c : p->children)
            if (c->scroll == 1 && c->visible)
            {
                ImVec2 q = WorldToScreen(ImVec2(p->x + c->x + c->w, p->y + c->y));
                const char* tag = "Fixed";
                ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, tag);
                ImVec2 ta(q.x - ts.x - U(10), q.y + U(3)), tb(q.x - U(3), q.y + U(3) + fs + U(4));
                if (ta.x < a.x) continue;
                dl->AddRectFilled(ta, tb, IM_COL32(13, 153, 255, 200), U(3));
                dl->AddText(f, fs, ImVec2(ta.x + U(3.5f), ta.y + U(2)), IM_COL32_WHITE, tag);
            }

        bool rot = false;
        const WebDevice* d = WebDeviceOf(p, &rot);
        if (!d) continue;
        ImVec2 scr = WebScreenOf(d, rot);
        if (d->fold)
        {
            // book-style: the hinge runs down the middle (across it when rotated); flip phones the other way
            ImU32 col = IM_COL32(151, 71, 255, 170);
            bool down = (d->fold == 1) != rot;
            if (down)
            {
                float x = std::round((a.x + b.x) * 0.5f) + 0.5f;
                DashedLine(dl, ImVec2(x, a.y), ImVec2(x, b.y), col, dash, gap);
                dl->AddText(f, fs, ImVec2(x + U(4), a.y + U(4)), col, "Fold");
            }
            else if (scr.y * 0.5f < p->h)
            {
                float y = std::round(WorldToScreen(ImVec2(0, p->y + scr.y * 0.5f)).y) + 0.5f;
                DashedLine(dl, ImVec2(a.x, y), ImVec2(b.x, y), col, dash, gap);
                dl->AddText(f, fs, ImVec2(a.x + U(4), y - fs - U(2)), col, "Fold");
            }
        }
        if (p->h > scr.y + 0.5f)
        {
            ImU32 col = IM_COL32(13, 153, 255, 150);
            float y = std::round(WorldToScreen(ImVec2(0, p->y + scr.y)).y) + 0.5f;
            DashedLine(dl, ImVec2(a.x, y), ImVec2(b.x, y), col, dash, gap);
            const char* label = "Screen end";
            ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, label);
            if (b.x - a.x > ts.x + U(12)) dl->AddText(f, fs, ImVec2(b.x - ts.x - U(4), y - fs - U(2)), col, label);
        }
    }
}

// ---------------------------------------------------------------- pages and breakpoints

Node* App::AddWebPage(const char* deviceKey)
{
    const WebDevice* d = FindWebDevice(deviceKey);
    if (!d) return nullptr;
    int pages = 0;
    float x = 0;
    for (auto& r : doc.roots)
    {
        x = std::max(x, r->x + r->w + 160);
        if (WebLayoutBase(doc, r.get()) == r.get()) pages++;
    }
    Node* f = CreateNode(NodeType::Frame, ImVec2(x, 0), ImVec2(d->w, d->h), nullptr);
    f->name = pages == 0 ? "Home" : "Page " + std::to_string(pages + 1);
    f->fill = Paint{ 1, ImVec4(1, 1, 1, 1) };
    f->device = d->key;
    MarkDirty();
    SetSelection({ f->id });
    ZoomToFit(true);
    return f;
}

void App::SetWebDevice(Node* f, const char* deviceKey, bool rotated)
{
    const WebDevice* d = FindWebDevice(deviceKey);
    if (!d || !f) return;
    doc.ApplyLayout();   // text measured before deciding what fits
    ImVec2 scr = WebScreenOf(d, rotated);
    // a page longer than its screen stays as long; a one-screen page takes the new screen height
    bool oldRot = false;
    const WebDevice* old = WebDeviceOf(f, &oldRot);
    float oldScreenH = old ? WebScreenOf(old, oldRot).y : f->h;
    bool longPage = f->h > oldScreenH + 0.5f;
    auto geo = AdaptToWidth(f, scr.x);
    f->w = scr.x;
    f->device = d->key;
    doc.ApplyLayout();
    PushDown(geo);
    f->h = std::max({ scr.y, ContentBottom(f), longPage ? f->h : 0.0f });
    MarkDirty();
}

Node* App::AddWebLayout(Node* page, const char* deviceKey)
{
    const WebDevice* d = FindWebDevice(deviceKey);
    Node* base = WebLayoutBase(doc, page);
    if (!d || !base) return nullptr;
    doc.ApplyLayout();   // text measured before deciding what fits
    auto layouts = WebPageLayouts(doc, base);
    // start from the narrowest breakpoint at least as wide (narrowing keeps more of the design than widening), else the widest
    Node* src = layouts.back().frame;
    for (auto& l : layouts) if (l.frame->w >= d->w - 0.5f) { src = l.frame; break; }
    float right = -1e9f;
    int index = 0;
    for (auto& l : layouts)
    {
        right = std::max(right, l.frame->x + l.frame->w);
        index = std::max(index, doc.IndexOf(l.frame) + 1);
    }

    std::unique_ptr<Node> copy = doc.CloneNode(src, true);
    Node* f = copy.get();
    f->layoutOf = base->id;
    f->breakpoint = 0;
    f->pageTitle.clear();
    f->pageDesc.clear();
    f->name = base->name + " \xC2\xB7 " + d->name;
    float srcScreenH = src->h;
    bool srcRot = false;
    if (const WebDevice* sd = WebDeviceOf(src, &srcRot)) srcScreenH = WebScreenOf(sd, srcRot).y;
    bool longPage = src->h > srcScreenH + 0.5f;
    auto geo = AdaptToWidth(f, d->w);
    f->w = d->w;
    f->device = d->key;
    f->x = right + 160;
    f->y = base->y;
    doc.Insert(std::move(copy), nullptr, index);
    doc.ApplyLayout();
    PushDown(geo);
    f->h = std::max({ d->h, ContentBottom(f), longPage ? f->h : 0.0f });

    // next to the other breakpoints of the page, or under everything when that place is taken
    auto overlaps = [&]() {
        for (auto& r : doc.roots)
            if (r.get() != f && f->x < r->x + r->w + 80 && r->x < f->x + f->w + 80 && f->y < r->y + r->h + 80 && r->y < f->y + f->h + 80) return true;
        return false;
    };
    if (overlaps())
    {
        float bottom = 0;
        for (auto& r : doc.roots) if (r.get() != f) bottom = std::max(bottom, r->y + r->h);
        f->x = base->x;
        f->y = bottom + 200;
    }
    MarkDirty();
    SetSelection({ f->id });
    ZoomToFit(true);
    return f;
}

// ---------------------------------------------------------------- panels

// Frame tool (F) in a web project: screen presets, like Figma's frame presets.
void App::DrawWebFramePresets(float W)
{
    static std::set<std::string> collapsed;
    ui::Section("Frame");
    HelpText("Pick a screen for a new page, or drag on the canvas for any size.", W);
    ImGui::Dummy(ImVec2(0, U(4)));
    for (int g = 0; g < kWebDeviceGroupCount; g++)
    {
        const char* group = kWebDeviceGroups[g];
        bool open = !collapsed.count(group);
        ImGui::PushID(group);
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##group", ImVec2(W, h))) { if (open) collapsed.insert(group); else collapsed.erase(group); }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), Hover, ImGui::GetStyle().FrameRounding);
        DrawIcon(dl, open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(p.x + U(10), p.y + h * 0.5f), U(12), TextDim);
        ImGui::PushFont(fonts::UI(fonts::UI_Medium), ImGui::GetStyle().FontSizeBase);
        dl->AddText(ImVec2(p.x + U(22), p.y + (h - ImGui::GetFontSize()) * 0.5f), Text, group);
        ImGui::PopFont();
        if (open)
            for (int i = 0; i < kWebDeviceCount; i++)
            {
                const WebDevice& d = kWebDevices[i];
                if (strcmp(d.group, group) != 0) continue;
                if (DeviceRow(d, W, false))
                {
                    AddWebPage(d.key);
                    tool = Tool::Move;
                }
            }
        ImGui::PopID();
    }
}

// Nothing selected: the site, its pages with their breakpoints, and the preview.
void App::DrawWebPanel(float W)
{
    float lx = W * 0.34f, x0 = ImGui::GetCursorPosX();
    auto Row = [&](const char* label) { ui::Label(label); ImGui::SameLine(x0 + lx); };

    ui::Section("Site");
    Row("Title");
    if (ui::TextField("##wtitle", &webOpt.title, W - lx, doc.name.c_str())) modified = true;
    Row("Language");
    if (ui::TextField("##wlang", &webOpt.lang, W - lx, "en")) modified = true;
    if (ui::Check("Fit pages into narrow windows", &webOpt.fitWidth)) modified = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("A page wider than the browser window is scaled down to fit it.");
    if (ui::Check("Scale phone layouts to the screen", &webOpt.stretchPhones)) modified = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Breakpoints narrower than 600 px fill the width of the phone\n(up to 125%%), so a 402 px design also fits a 440 px screen.");
    if (ui::Check("Ship design fonts", &webOpt.fonts)) modified = true;

    if (ui::Section("Pages", 1) == 1) ImGui::OpenPopup("##addpage");
    if (const WebDevice* d = DevicePopup("##addpage", "")) AddWebPage(d->key);
    std::vector<Node*> pages;
    for (Node* p : PageFrames(doc)) if (WebLayoutBase(doc, p) == p) pages.push_back(p);
    float fh = ImGui::GetFrameHeight();
    for (Node* p : pages)
    {
        ImGui::PushID(p->id.c_str());
        std::string file = WebPageFile(doc, p);
        if (IconRow("##page", file == "index.html" ? Icon::Globe : Icon::Frame, p->name.c_str(), file, W - fh - U(4), false, 0, true))
        {
            SetSelection({ p->id });
            ZoomToFit(true);
        }
        ImGui::SameLine(0, U(4));
        if (IconButton("##open", Icon::Play, false, fh, "Preview in the browser")) OpenWebPreview(p);
        auto layouts = WebPageLayouts(doc, p);
        if (layouts.size() > 1)
            for (auto& l : layouts)
            {
                ImGui::PushID(l.frame->id.c_str());
                std::string name = DeviceName(l.frame);
                if (IconRow("##bp", KindIcon(WebDeviceOf(l.frame)), name.c_str(), RangeText(l), W, false, U(16)))
                {
                    SetSelection({ l.frame->id });
                    ZoomToFit(true);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Shown in browser windows %s px wide", RangeText(l).c_str());
                ImGui::PopID();
            }
        ImGui::PopID();
    }
    if (pages.empty())
    {
        HelpText("No pages yet. Add one for a screen, or draw a frame (F): every top-level frame is a page.", W);
        if (ui::PrimaryButton("Add page", ImVec2(W, U(28)))) ImGui::OpenPopup("##addpage2");
        if (const WebDevice* d = DevicePopup("##addpage2", "")) AddWebPage(d->key);
    }

    ui::Section("Preview");
    float half = (W - U(8)) * 0.5f;
    if (ui::PrimaryButton("Open in browser", ImVec2(half, U(28)))) OpenWebPreview(WebCurrentPage());
    ImGui::SameLine(0, U(8));
    if (ImGui::Button("All screens", ImVec2(half, U(28)))) OpenWebScreens(WebCurrentPage());
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Every breakpoint of the page side by side in the browser, live");
    bool live = webPreview && webPreview->Running();
    if (live)
    {
        std::string url = webPreview->Url();
        HelpText(("Live at " + url + " - the browser reloads as you edit and follows the page you work on.").c_str(), W);
        if (ImGui::Button("Stop preview", ImVec2(W, U(28)))) StopWebPreview();
    }
    else HelpText("The browser reloads as you edit and follows the page you work on.", W);
}

// A page (or one of its breakpoints) is selected: its screen, breakpoints, and search settings.
void App::DrawWebPageSection(Node* page, float W)
{
    ImGui::PushID("##webpage");
    Node* base = WebLayoutBase(doc, page);
    if (!base) base = page;
    bool isLayout = base != page;
    float fh = ImGui::GetFrameHeight();
    std::string file = WebPageFile(doc, page);

    if (ui::Section(isLayout ? "Breakpoint" : "Page", 0, Icon::Play, "Preview in the browser") == 2) OpenWebPreview(page);
    std::string caption = isLayout ? file + " \xC2\xB7 " + base->name : file + (file == "index.html" ? " \xC2\xB7 home page" : "");
    ui::SubLabel(caption.c_str());
    ImGui::Dummy(ImVec2(0, U(2)));

    // screen: [device field] [rotate] [fold]
    bool rot = false;
    const WebDevice* dev = WebDeviceOf(page, &rot);
    const WebDevice* other = dev && dev->other ? FindWebDevice(dev->other) : nullptr;
    int buttons = dev ? (other ? 2 : 1) : 0;
    if (DeviceField("##device", page, W - buttons * (fh + U(4)))) ImGui::OpenPopup("##devices");
    if (dev)
    {
        ImGui::SameLine(0, U(4));
        if (IconButton("##rotate", Icon::Orientation, false, fh, rot ? "Back to the usual orientation" : "Rotate the screen")) SetWebDevice(page, dev->key, !rot);
        if (other)
        {
            ImGui::SameLine(0, U(4));
            std::string tip = std::string(dev->fold ? "Fold: " : "Unfold: ") + other->name;
            if (IconButton("##fold", Icon::Foldable, dev->fold != 0, fh, tip.c_str())) SetWebDevice(page, other->key);
        }
    }
    if (const WebDevice* d = DevicePopup("##devices", page->device)) SetWebDevice(page, d->key);

    // breakpoints: the same page for other screens; the browser shows the one for its width
    if (ui::Section("Breakpoints", 1) == 1) ImGui::OpenPopup("##addbp");
    if (const WebDevice* d = DevicePopup("##addbp", "")) AddWebLayout(page, d->key);
    auto layouts = WebPageLayouts(doc, page);
    std::string removeId;
    const WebLayout* current = nullptr;
    for (auto& l : layouts)
    {
        ImGui::PushID(l.frame->id.c_str());
        bool sel = l.frame == page;
        if (sel) current = &l;
        bool canRemove = l.frame != base;
        ImVec2 rp = ImGui::GetCursorScreenPos();
        std::string name = DeviceName(l.frame), range = layouts.size() > 1 ? RangeText(l) : std::string("all widths");
        if (IconRow("##bp", KindIcon(WebDeviceOf(l.frame)), name.c_str(), range, W, sel, 0, canRemove) && !sel)
        {
            SetSelection({ l.frame->id });
            ZoomToFit(true);
        }
        bool hov = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenOverlappedByItem | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s px wide, shown in windows %s px wide", std::to_string((int)l.frame->w).c_str(), range.c_str());
        if (canRemove && hov)
        {
            // remove: over the range, while the row is hovered
            ImVec2 bp(rp.x + W - fh, rp.y);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(bp.x - U(70), rp.y), ImVec2(rp.x + W, rp.y + fh), sel ? IM_COL32(40, 70, 95, 255) : IM_COL32(52, 52, 52, 255), ImGui::GetStyle().FrameRounding);
            ImGui::SetCursorScreenPos(bp);
            if (IconButton("##remove", Icon::Minus, false, fh, "Remove this breakpoint")) removeId = l.frame->id;
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + fh + ImGui::GetStyle().ItemSpacing.y));
        }
        ImGui::PopID();
    }
    if (layouts.size() == 1)
        HelpText("Add breakpoints to lay the page out for phones, tablets and foldables. The site switches between them by window width.", W);
    else if (current && current->minW > 0)
    {
        // where this breakpoint starts: automatic (halfway between the widths) or set by hand
        float x0 = ImGui::GetCursorPosX();
        ui::Label("Shown from");
        ImGui::SameLine(x0 + W * 0.42f);
        float v = current->minW;
        float fw = W * 0.58f - (page->breakpoint > 0 ? fh + U(4) : 0);
        if (ui::NumField("##minw", "", &v, fw, 1.0f, 1.0f, 10000.0f, "px")) { page->breakpoint = std::round(v); MarkDirty(); }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browser width from which this breakpoint replaces the narrower one");
        if (page->breakpoint > 0)
        {
            ImGui::SameLine(0, U(4));
            if (IconButton("##autobp", Icon::Undo, false, fh, "Automatic: halfway between the two widths")) { page->breakpoint = 0; MarkDirty(); }
        }
    }

    // search engines and link previews (the page, shared by its breakpoints)
    ui::Section("Search & sharing");
    ui::SubLabel("Title");
    std::string site = webOpt.title.empty() ? doc.name : webOpt.title;
    std::string autoTitle = file == "index.html" ? site : base->name + " \xC2\xB7 " + site;
    if (ui::TextField("##ptitle", &base->pageTitle, W, autoTitle.c_str())) MarkDirty();
    ui::SubLabel("Description");
    bool emptyDesc = base->pageDesc.empty();
    ImVec2 dp = ImGui::GetCursorScreenPos();
    if (ui::TextField("##pdesc", &base->pageDesc, W, nullptr, true, U(54))) MarkDirty();
    if (emptyDesc && !ImGui::IsItemActive())
    {
        ImVec2 pad = ImGui::GetStyle().FramePadding;
        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(dp.x + pad.x, dp.y + pad.y), TextFaint,
                                            "Shown in search results and link previews", nullptr, W - pad.x * 2);
    }
    ImGui::PopID();

    // the layer sections follow: a divider like the one between sections
    ImGui::Dummy(ImVec2(0, U(6)));
    ImVec2 lp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lp.x - U(16), lp.y), ImVec2(lp.x + W + U(16), lp.y), Divider);

    if (!removeId.empty())
    {
        // after the frame: the inspector still draws this page
        std::string baseId = base->id;
        deferred.push_back([this, removeId, baseId] {
            if (!doc.Find(removeId)) return;
            SetSelection({ removeId });
            DeleteSelection();
            if (doc.Find(baseId)) SetSelection({ baseId });
        });
    }
}

// Prototype tab: where a click on the layer leads, and whether it scrolls with the page.
bool App::DrawLinkSection(Node* n, float W)
{
    bool ch = false;
    if (IsWeb() && n->parent && !n->parent->parent && n->parent->type == NodeType::Frame && !n->parent->layout)
    {
        ui::Section("Scrolling");
        const char* modes[] = { "With the page", "Fixed" };
        int s = n->scroll;
        if (ui::Segmented("##scroll", &s, modes, 2, W)) { n->scroll = s; ch = true; }
        HelpText(n->scroll ? "Stays on screen while the page scrolls, like a header or a chat button." : "Scrolls away with the rest of the page.", W);
    }

    ui::Section("Link");
    std::vector<Node*> pages;
    for (Node* p : PageFrames(doc)) if (WebLayoutBase(doc, p) == p) pages.push_back(p);
    std::vector<std::string> labels = { "None" };
    for (Node* p : pages) labels.push_back("Page: " + p->name);
    labels.push_back("Web address (URL)");
    std::vector<const char*> items;
    for (auto& l : labels) items.push_back(l.c_str());
    int cur = 0;
    if (n->link.rfind("page:", 0) == 0)
    {
        Node* target = WebLayoutBase(doc, doc.Find(n->link.substr(5)));
        for (size_t i = 0; i < pages.size(); i++) if (pages[i] == target) cur = (int)i + 1;
    }
    else if (!n->link.empty()) cur = (int)pages.size() + 1;
    if (ui::Combo("##link", &cur, items.data(), (int)items.size(), W))
    {
        if (cur == 0) n->link.clear();
        else if (cur <= (int)pages.size()) n->link = "page:" + pages[cur - 1]->id;
        else n->link = "https://";
        ch = true;
    }
    if (cur == (int)pages.size() + 1) ch |= ui::TextField("##url", &n->link, W, "https://example.com");
    HelpText("Exported as a link. In Play mode a click opens the page.", W);
    return ch;
}
