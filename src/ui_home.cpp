// Projects screen (file browser of the workspace) and the New project dialog.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "platform.h"
#include "imgui_internal.h"
#include <algorithm>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;
using namespace theme;

static std::string EditedText(long long unixTime)
{
    if (unixTime <= 0) return "";
    long long now = (long long)time(nullptr);
    long long d = now - unixTime;
    if (d < 60) return "Edited just now";
    if (d < 3600) return "Edited " + std::to_string(d / 60) + " min ago";
    if (d < 86400) return "Edited " + std::to_string(d / 3600) + " h ago";
    if (d < 86400 * 30) return "Edited " + std::to_string(d / 86400) + " days ago";
    time_t t = (time_t)unixTime;
    tm lt;
    localtime_s(&lt, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "Edited %d.%m.%Y", &lt);
    return buf;
}

static std::string SuggestName(App* app)
{
    for (int i = 0; i < 26 * 4; i++)
    {
        std::string name = i < 26 ? std::string("Project_") + (char)('A' + i) : "Project_" + std::to_string(i - 25);
        std::error_code ec;
        if (!fs::exists(fs::u8path(app->WorkspaceDir()) / fs::u8path(name), ec)) return name;
    }
    return "Project";
}

void App::DrawHome()
{
    if (!showHome) return;
    if (homeNeedsScan) { ScanProjects(); homeNeedsScan = false; }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, BgDeep);
    ImGui::Begin("##home", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoDocking);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    // Keep the home above the editor, but never in the middle of a click elsewhere (title bar buttons and menus):
    // taking focus clears the active item, and the click would never complete.
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) && !ImGui::IsAnyItemActive())
        ImGui::SetWindowFocus();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float sideW = U(232);
    ImVec2 o = ImGui::GetWindowPos();
    float H = ImGui::GetWindowHeight();

    // Sidebar
    dl->AddRectFilled(o, ImVec2(o.x + sideW, o.y + H), Bg);
    dl->AddLine(ImVec2(o.x + sideW, o.y), ImVec2(o.x + sideW, o.y + H), Divider);
    DrawLogo(dl, ImVec2(o.x + U(18), o.y + U(18)), U(28));
    ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 15.0f);
    dl->AddText(ImVec2(o.x + U(56), o.y + U(23)), Text, "Fimgui");
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(o.x + U(12), o.y + U(70)));
    ImGui::BeginGroup();
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + sideW - U(24), p.y + U(30)), IM_COL32(255, 255, 255, 16), U(6));
        DrawIcon(dl, Icon::Frame, ImVec2(p.x + U(16), p.y + U(15)), U(13), Text);
        dl->AddText(ImVec2(p.x + U(32), p.y + U(8)), Text, "Projects");
        ImGui::Dummy(ImVec2(sideW - U(24), U(36)));
    }
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::TextUnformatted("Workspace");
    ImGui::PopStyleColor();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + sideW - U(24));
    ImGui::TextUnformatted(WorkspaceDir().c_str());
    ImGui::PopTextWrapPos();
    if (ImGui::Button("Change...", ImVec2((sideW - U(30)) * 0.5f, 0)))
    {
        std::string d = platform::PickFolderDialog("Workspace folder");
        if (!d.empty()) { workspace = d; SaveSettings(); homeNeedsScan = true; }
    }
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("Open", ImVec2((sideW - U(30)) * 0.5f, 0)))
    {
        std::error_code ec;
        fs::create_directories(fs::u8path(WorkspaceDir()), ec);
        platform::OpenInExplorer(WorkspaceDir());
    }
    ImGui::Dummy(ImVec2(0, U(10)));
    if (ImGui::Button("Open file...", ImVec2(sideW - U(24), 0))) OpenDocument();
    if (!recentFiles.empty())
    {
        ImGui::Dummy(ImVec2(0, U(10)));
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextUnformatted("Recent files");
        ImGui::PopStyleColor();
        for (size_t i = 0; i < recentFiles.size() && i < 6; i++)
        {
            ImGui::PushID((int)i);
            fs::path rp = fs::u8path(recentFiles[i]);
            std::string label = rp.parent_path().filename().u8string() + " / " + rp.filename().u8string();
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(sideW - U(24), 0))) OpenDocument(recentFiles[i]);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", recentFiles[i].c_str());
            ImGui::PopID();
        }
    }
    ImGui::Dummy(ImVec2(0, U(10)));
    ImGui::Checkbox("Show at startup", &showHomeOnStart);
    ImGui::EndGroup();

    // Main area
    float mx = o.x + sideW + U(36);
    float mw = ImGui::GetWindowWidth() - sideW - U(72);
    ImGui::SetCursorScreenPos(ImVec2(mx, o.y + U(26)));
    ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 20.0f);
    ImGui::TextUnformatted("Projects");
    ImGui::PopFont();
    ImGui::SameLine(0, U(10));
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + U(6));
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::Text("%d", (int)projects.size());
    ImGui::PopStyleColor();

    static char filter[128] = {};
    float bw = U(130);
    ImGui::SetCursorScreenPos(ImVec2(mx + mw - bw - U(220) - U(40), o.y + U(26)));
    ImGui::SetNextItemWidth(U(220));
    ImGui::InputTextWithHint("##search", "Search projects", filter, sizeof(filter));
    ImGui::SameLine(0, U(8));
    if (ui::PrimaryButton("+ New project", ImVec2(bw, ImGui::GetFrameHeight()))) { newProjectFromCurrent = false; openNewProject = true; }
    ImGui::SameLine(0, U(8));
    if (IconButton("##homeclose", Icon::Close, false, ImGui::GetFrameHeight(), "Back to the editor (Esc)")) showHome = false;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) showHome = false;

    // Grid of project cards
    ImGui::SetCursorScreenPos(ImVec2(mx, o.y + U(80)));
    ImGui::BeginChild("##grid", ImVec2(mw, H - U(96)), 0);
    float cardW = U(250), thumbH = U(156), cardH = thumbH + U(54), gap = U(20);
    int cols = std::max(1, (int)((mw + gap) / (cardW + gap)));
    std::string f = filter;
    std::transform(f.begin(), f.end(), f.begin(), ::tolower);
    int shown = 0;
    std::string openPath;
    for (size_t i = 0; i < projects.size(); i++)
    {
        ProjectEntry& p = projects[i];
        std::string ln = p.name;
        std::transform(ln.begin(), ln.end(), ln.begin(), ::tolower);
        if (!f.empty() && ln.find(f) == std::string::npos) continue;
        if (shown % cols) ImGui::SameLine(0, gap);
        shown++;
        ImGui::PushID((int)i);
        ImVec2 cp = ImGui::GetCursorScreenPos();
        bool clicked = ImGui::InvisibleButton("##card", ImVec2(cardW, cardH));
        bool hov = ImGui::IsItemHovered();
        ImDrawList* gdl = ImGui::GetWindowDrawList();
        ImVec2 ta = cp, tb(cp.x + cardW, cp.y + thumbH);
        gdl->AddRectFilled(ta, tb, IM_COL32(44, 44, 48, 255), U(10), ImDrawFlags_RoundCornersTop);
        if (p.thumb)
        {
            ImVec2 ia(ta.x + U(10), ta.y + U(10)), ib(tb.x - U(10), tb.y - U(10));
            fim::ContainRect(ImVec2((float)p.thumbW, (float)p.thumbH), &ia, &ib);
            gdl->AddImage(p.thumb, ia, ib);
        }
        else
            DrawLogo(gdl, ImVec2((ta.x + tb.x) * 0.5f - U(20), (ta.y + tb.y) * 0.5f - U(20)), U(40), 0.35f);
        {
            const char* badge = p.type == Project_Minecraft ? "Minecraft" : p.type == Project_Web ? "Web" : "ImGui";
            float bfs = ImGui::GetFontSize() * 0.85f;
            ImVec2 bs = fonts::UI(fonts::UI_Medium)->CalcTextSizeA(bfs, FLT_MAX, 0, badge);
            ImVec2 ba(ta.x + U(8), ta.y + U(8)), bb(ba.x + bs.x + U(12), ba.y + bs.y + U(6));
            gdl->AddRectFilled(ba, bb, p.type == Project_Minecraft ? IM_COL32(60, 140, 60, 230) : p.type == Project_Web ? IM_COL32(214, 104, 36, 230) : IM_COL32(13, 110, 200, 230), U(5));
            gdl->AddText(fonts::UI(fonts::UI_Medium), bfs, ImVec2(ba.x + U(6), ba.y + U(3)), IM_COL32(255, 255, 255, 255), badge);
        }
        gdl->AddRectFilled(ImVec2(cp.x, tb.y), ImVec2(cp.x + cardW, cp.y + cardH), Bg, U(10), ImDrawFlags_RoundCornersBottom);
        gdl->AddRect(cp, ImVec2(cp.x + cardW, cp.y + cardH), hov ? Accent : Border, U(10), 0, hov ? 1.5f : 1.0f);
        gdl->AddText(fonts::UI(fonts::UI_Medium), ImGui::GetFontSize() * 1.05f, ImVec2(cp.x + U(14), tb.y + U(10)), Text, p.name.c_str());
        std::string ed = EditedText(p.modified);
        gdl->AddText(fonts::UI(fonts::UI_Regular), ImGui::GetFontSize() * 0.92f, ImVec2(cp.x + U(14), tb.y + U(30)), TextFaint, ed.c_str());
        if (clicked) openPath = p.file;
        if (ImGui::BeginPopupContextItem("##ctx"))
        {
            if (ImGui::MenuItem("Open")) openPath = p.file;
            if (ImGui::MenuItem("Show in Explorer")) platform::OpenInExplorer(p.dir);
            ImGui::EndPopup();
        }
        if (hov) ImGui::SetTooltip("%s", p.file.c_str());
        ImGui::PopID();
    }
    if (projects.empty())
    {
        ImGui::Dummy(ImVec2(0, U(60)));
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::Text("No projects yet in %s", WorkspaceDir().c_str());
        ImGui::TextUnformatted("Each project is a folder with its design, fonts, images, icons, videos and export (Dear ImGui C++, a Minecraft mod GUI or a website).");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, U(8)));
        if (ui::PrimaryButton("+ Create the first project", ImVec2(U(220), 0))) { newProjectFromCurrent = false; openNewProject = true; }
    }
    ImGui::EndChild();
    ImGui::End();
    if (!openPath.empty()) OpenDocument(openPath);
}

// ---------------------------------------------------------------- project type previews
// Miniature mock-ups drawn with the draw list (crisp at any UI scale). They show the kind of
// interface each project type makes; none of them copies a real product.

static unsigned PreviewHash(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static ImU32 Shade(int r, int g, int b, int d, int a = 255)
{
    auto c = [](int v) { return v < 0 ? 0 : v > 255 ? 255 : v; };
    return IM_COL32(c(r + d), c(g + d), c(b + d), a);
}

static ImU32 WithAlpha(ImU32 c, int a) { return (c & ~IM_COL32_A_MASK) | ((ImU32)a << IM_COL32_A_SHIFT); }

// Top part of a card: rounded top corners, square bottom, and a vertical gradient.
static void PreviewBackground(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 top, ImU32 bottom, float r)
{
    dl->AddRectFilled(a, ImVec2(b.x, a.y + r * 2), top, r, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilledMultiColor(ImVec2(a.x, a.y + r), b, top, top, bottom, bottom);
}

// A dark game menu: tab sidebar, two group boxes with toggles, sliders, checkboxes and a combo.
static void MockGameMenu(ImDrawList* dl, ImVec2 o, ImVec2 sz, ImU32 accent, int activeTab)
{
    float k = sz.y / 100.0f;
    ImVec2 e(o.x + sz.x, o.y + sz.y);
    ImFont* fm = fonts::UI(fonts::UI_Medium);
    ImFont* fb = fonts::UI(fonts::UI_SemiBold);
    for (int i = 1; i <= 5; i++)
        dl->AddRectFilled(ImVec2(o.x - i * 1.4f * k, o.y - i * 0.8f * k + 3 * k), ImVec2(e.x + i * 1.4f * k, e.y + i * 1.8f * k + 3 * k), IM_COL32(0, 0, 0, 16), 6 * k + i * k);
    dl->AddRectFilled(o, e, IM_COL32(15, 16, 22, 255), 5 * k);
    float sw = sz.x * 0.27f;
    dl->AddRectFilled(o, ImVec2(o.x + sw, e.y), IM_COL32(11, 12, 17, 255), 5 * k, ImDrawFlags_RoundCornersLeft);
    dl->AddLine(ImVec2(o.x + sw, o.y), ImVec2(o.x + sw, e.y), IM_COL32(255, 255, 255, 12));
    dl->AddRect(o, e, IM_COL32(255, 255, 255, 18), 5 * k);

    dl->AddRectFilled(ImVec2(o.x + 6 * k, o.y + 7 * k), ImVec2(o.x + 14 * k, o.y + 15 * k), accent, 2.2f * k);
    dl->AddRectFilled(ImVec2(o.x + 17 * k, o.y + 9.5f * k), ImVec2(o.x + sw - 8 * k, o.y + 12.5f * k), IM_COL32(220, 222, 230, 255), 1.5f * k);

    static const char* tabs[] = { "Aimbot", "Visuals", "Misc", "Config" };
    for (int i = 0; i < 4; i++)
    {
        float y = o.y + 26 * k + i * 12 * k;
        bool on = i == activeTab;
        if (on) dl->AddRectFilled(ImVec2(o.x + 4 * k, y - 3 * k), ImVec2(o.x + sw - 4 * k, y + 7 * k), WithAlpha(accent, 40), 2.5f * k);
        dl->AddCircleFilled(ImVec2(o.x + 9 * k, y + 2 * k), 1.8f * k, on ? accent : IM_COL32(90, 94, 110, 255));
        dl->AddText(fm, 6.4f * k, ImVec2(o.x + 13.5f * k, y - 1.4f * k), on ? IM_COL32(236, 238, 244, 255) : IM_COL32(118, 122, 138, 255), tabs[i]);
    }

    float cx = o.x + sw + 6 * k;
    dl->AddText(fb, 8 * k, ImVec2(cx, o.y + 5.5f * k), IM_COL32(236, 238, 244, 255), tabs[activeTab]);
    dl->AddRectFilled(ImVec2(e.x - 30 * k, o.y + 6 * k), ImVec2(e.x - 6 * k, o.y + 13 * k), IM_COL32(24, 25, 34, 255), 3.5f * k);

    float gy = o.y + 20 * k, gw = (e.x - 6 * k - cx - 5 * k) * 0.5f;
    for (int g = 0; g < 2; g++)
    {
        ImVec2 a(cx + g * (gw + 5 * k), gy), b(a.x + gw, e.y - 6 * k);
        dl->AddRectFilled(a, b, IM_COL32(20, 21, 29, 255), 3 * k);
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 10), 3 * k);
        dl->AddText(fm, 5.6f * k, ImVec2(a.x + 5 * k, a.y + 3.5f * k), IM_COL32(150, 154, 170, 255), g == 0 ? "General" : "Other");
        float lx = a.x + 5 * k, rx = b.x - 5 * k;
        for (int row = 0; row < 4; row++)
        {
            float y = a.y + 15 * k + row * 11.5f * k;
            if (y + 6 * k > b.y) break;
            int kind = g == 0 ? row : (row + 2) % 4;   // 0 toggle on, 1 toggle off, 2 slider, 3 combo / checkbox
            float lw = (12 + (PreviewHash(g, row) % 12)) * k;
            dl->AddRectFilled(ImVec2(lx, y + 1 * k), ImVec2(lx + lw, y + 3 * k), IM_COL32(140, 144, 160, 255), 1 * k);
            if (kind == 0 || kind == 1)
            {
                bool on = kind == 0;
                ImVec2 ta(rx - 11 * k, y - 0.6f * k), tb(rx, y + 5 * k);
                dl->AddRectFilled(ta, tb, on ? accent : IM_COL32(44, 46, 58, 255), 3 * k);
                dl->AddCircleFilled(ImVec2(on ? tb.x - 2.8f * k : ta.x + 2.8f * k, (ta.y + tb.y) * 0.5f), 2 * k, on ? IM_COL32_WHITE : IM_COL32(140, 144, 160, 255));
            }
            else if (kind == 2)
            {
                float t = g == 0 ? 0.62f : 0.35f, ty = y + 7 * k;
                dl->AddRectFilled(ImVec2(lx, ty - 0.8f * k), ImVec2(rx, ty + 0.8f * k), IM_COL32(44, 46, 58, 255), 1 * k);
                dl->AddRectFilled(ImVec2(lx, ty - 0.8f * k), ImVec2(lx + (rx - lx) * t, ty + 0.8f * k), accent, 1 * k);
                dl->AddCircleFilled(ImVec2(lx + (rx - lx) * t, ty), 2.2f * k, IM_COL32_WHITE);
            }
            else if (g == 0)
            {
                ImVec2 ca(rx - 20 * k, y - 1 * k), cb(rx, y + 5.5f * k);
                dl->AddRectFilled(ca, cb, IM_COL32(28, 29, 38, 255), 1.5f * k);
                dl->AddRect(ca, cb, IM_COL32(255, 255, 255, 14), 1.5f * k);
                float mx = cb.x - 3.5f * k, my = (ca.y + cb.y) * 0.5f;
                dl->AddTriangleFilled(ImVec2(mx - 1.5f * k, my - 0.8f * k), ImVec2(mx + 1.5f * k, my - 0.8f * k), ImVec2(mx, my + 1 * k), IM_COL32(140, 144, 160, 255));
            }
            else
            {
                ImVec2 ca(rx - 5.5f * k, y - 0.6f * k), cb(rx, y + 4.9f * k);
                dl->AddRectFilled(ca, cb, accent, 1.2f * k);
                ImVec2 pts[3] = { ImVec2(ca.x + 1.3f * k, ca.y + 2.8f * k), ImVec2(ca.x + 2.4f * k, ca.y + 3.9f * k), ImVec2(cb.x - 1.2f * k, ca.y + 1.5f * k) };
                dl->AddPolyline(pts, 3, IM_COL32_WHITE, 0, 0.8f * k);
                dl->AddRectFilled(ImVec2(rx - 14 * k, y - 0.6f * k), ImVec2(rx - 8.5f * k, y + 4.9f * k), IM_COL32(255, 196, 64, 255), 1.2f * k);
            }
        }
    }
}

static void PreviewImGui(ImDrawList* dl, ImVec2 a, ImVec2 b, float r)
{
    PreviewBackground(dl, a, b, IM_COL32(30, 32, 44, 255), IM_COL32(12, 13, 18, 255), r);
    float h = b.y - a.y, w = b.x - a.x;
    ImVec2 sz(std::min(w * 0.7f, h * 1.42f), h * 0.74f);
    MockGameMenu(dl, ImVec2(b.x - sz.x * 0.86f, a.y + h * 0.06f), sz, IM_COL32(236, 72, 120, 255), 1);
    dl->AddRectFilled(a, b, IM_COL32(14, 15, 21, 120), r, ImDrawFlags_RoundCornersTop);
    MockGameMenu(dl, ImVec2(a.x + w * 0.07f, a.y + h * 0.2f), sz, IM_COL32(64, 156, 255, 255), 0);
}

// A blocky world, dimmed like an open game screen, with a click GUI of module panels on top.
static void PreviewMinecraft(ImDrawList* dl, ImVec2 a, ImVec2 b, float r)
{
    float h = b.y - a.y, w = b.x - a.x, k = h / 100.0f;
    PreviewBackground(dl, a, b, IM_COL32(108, 156, 236, 255), IM_COL32(176, 208, 255, 255), r);
    dl->AddRectFilled(ImVec2(b.x - 22 * k, a.y + 8 * k), ImVec2(b.x - 12 * k, a.y + 18 * k), IM_COL32(255, 244, 196, 255));
    dl->AddRectFilled(ImVec2(a.x + 10 * k, a.y + 14 * k), ImVec2(a.x + 34 * k, a.y + 19 * k), IM_COL32(255, 255, 255, 220));
    dl->AddRectFilled(ImVec2(a.x + 16 * k, a.y + 10 * k), ImVec2(a.x + 28 * k, a.y + 15 * k), IM_COL32(255, 255, 255, 220));
    float bs = 7.5f * k;
    int cols = (int)(w / bs) + 1;
    auto groundOf = [](int c) { return 4 + (int)(PreviewHash(c / 3, 1) % 3) + (c % 7 == 3 ? 1 : 0); };
    for (int c = 0; c < cols; c++)
    {
        int ground = groundOf(c);
        float x = a.x + c * bs;
        for (int row = 0; row < ground; row++)
        {
            float y = b.y - (row + 1) * bs;
            int d = (int)(PreviewHash(c, row) % 21) - 10;
            int depth = ground - 1 - row;
            ImU32 col = depth == 0 ? Shade(134, 96, 67, d) : depth < 3 ? Shade(134, 96, 67, d) : Shade(122, 122, 122, d);
            dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bs, y + bs), col);
            if (depth == 0) dl->AddRectFilled(ImVec2(x, y), ImVec2(x + bs, y + bs * 0.32f), Shade(94, 158, 52, d));
        }
    }
    if (cols > 12)   // a tree
    {
        float tx = a.x + 3 * bs, ty = b.y - groundOf(3) * bs;
        dl->AddRectFilled(ImVec2(tx, ty - 3 * bs), ImVec2(tx + bs, ty), IM_COL32(104, 78, 46, 255));
        dl->AddRectFilled(ImVec2(tx - bs, ty - 5 * bs), ImVec2(tx + 2 * bs, ty - 3 * bs), IM_COL32(64, 128, 40, 255));
    }
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 96), r, ImDrawFlags_RoundCornersTop);

    struct Panel { const char* name; const char* mods[4]; int on; };
    static const Panel panels[3] = {
        { "Combat", { "KillAura", "Velocity", "Criticals", "AutoTotem" }, 0b0101 },
        { "Movement", { "Sprint", "Speed", "NoFall", "Step" }, 0b1001 },
        { "Render", { "ESP", "Tracers", "Fullbright", "Nametags" }, 0b0111 },
    };
    ImU32 accent = IM_COL32(150, 110, 255, 255);
    ImFont* fm = fonts::UI(fonts::UI_Medium);
    ImFont* fb = fonts::UI(fonts::UI_SemiBold);
    float gap = 5 * k, pw = (w - gap * 4) / 3, rh = 9 * k;
    for (int i = 0; i < 3; i++)
    {
        float px = a.x + gap + i * (pw + gap), py = a.y + 12 * k + (i == 1 ? 6 : i == 2 ? 2 : 0) * k;
        dl->AddRectFilled(ImVec2(px, py), ImVec2(px + pw, py + 11 * k), IM_COL32(18, 18, 22, 240));
        dl->AddRectFilled(ImVec2(px, py + 11 * k - 1.2f * k), ImVec2(px + pw, py + 11 * k), accent);
        dl->AddText(fb, 6.6f * k, ImVec2(px + 4 * k, py + 1.8f * k), IM_COL32(240, 240, 245, 255), panels[i].name);
        for (int m = 0; m < 4; m++)
        {
            float y = py + 11 * k + m * rh;
            bool on = (panels[i].on >> m) & 1;
            dl->AddRectFilled(ImVec2(px, y), ImVec2(px + pw, y + rh), on ? IM_COL32(30, 24, 48, 230) : IM_COL32(12, 12, 15, 215));
            if (on) dl->AddRectFilled(ImVec2(px, y), ImVec2(px + 1.2f * k, y + rh), accent);
            dl->AddText(fm, 6 * k, ImVec2(px + 4 * k, y + 1.4f * k), on ? IM_COL32(200, 184, 255, 255) : IM_COL32(170, 170, 178, 255), panels[i].mods[m]);
        }
    }
}

// A light website in a browser window: navigation, headline, call to action, cards.
static void PreviewWeb(ImDrawList* dl, ImVec2 a, ImVec2 b, float r)
{
    float h = b.y - a.y, w = b.x - a.x, k = h / 100.0f;
    PreviewBackground(dl, a, b, IM_COL32(40, 42, 50, 255), IM_COL32(24, 25, 30, 255), r);
    ImVec2 wa(a.x + 8 * k, a.y + 9 * k), wb(b.x - 8 * k, b.y + 4 * k);
    for (int i = 1; i <= 4; i++)
        dl->AddRectFilled(ImVec2(wa.x - i * k, wa.y - i * 0.5f * k + 2 * k), ImVec2(wb.x + i * k, wb.y), IM_COL32(0, 0, 0, 18), 4 * k + i * k);
    dl->AddRectFilled(wa, wb, IM_COL32(250, 250, 252, 255), 4 * k, ImDrawFlags_RoundCornersTop);
    dl->AddRectFilled(wa, ImVec2(wb.x, wa.y + 10 * k), IM_COL32(234, 235, 239, 255), 4 * k, ImDrawFlags_RoundCornersTop);
    for (int i = 0; i < 3; i++) dl->AddCircleFilled(ImVec2(wa.x + (5 + i * 4.5f) * k, wa.y + 5 * k), 1.5f * k, IM_COL32(196, 198, 205, 255));
    ImVec2 aa(wa.x + 20 * k, wa.y + 2.2f * k), ab(wb.x - 20 * k, wa.y + 7.8f * k);
    dl->AddRectFilled(aa, ab, IM_COL32(255, 255, 255, 255), 2.8f * k);
    ImFont* fr = fonts::UI(fonts::UI_Regular);
    ImFont* fb = fonts::UI(fonts::UI_SemiBold);
    const char* url = "yoursite.com";
    ImVec2 us = fr->CalcTextSizeA(4.6f * k, FLT_MAX, 0, url);
    dl->AddText(fr, 4.6f * k, ImVec2((aa.x + ab.x - us.x) * 0.5f, aa.y + 0.4f * k), IM_COL32(110, 112, 120, 255), url);

    float px = wa.x + 8 * k, pr = wb.x - 8 * k, y = wa.y + 16 * k;
    dl->AddCircleFilled(ImVec2(px + 2.5f * k, y + 2 * k), 2.6f * k, IM_COL32(24, 24, 28, 255));
    dl->AddRectFilled(ImVec2(px + 7 * k, y + 1.1f * k), ImVec2(px + 19 * k, y + 3 * k), IM_COL32(24, 24, 28, 255), 1 * k);
    for (int i = 0; i < 3; i++)
    {
        float lx = (px + pr) * 0.5f - 12 * k + i * 11 * k;
        dl->AddRectFilled(ImVec2(lx, y + 1.3f * k), ImVec2(lx + 8 * k, y + 2.8f * k), IM_COL32(150, 152, 160, 255), 1 * k);
    }
    dl->AddRectFilled(ImVec2(pr - 15 * k, y - 1 * k), ImVec2(pr, y + 5 * k), IM_COL32(24, 24, 28, 255), 3 * k);

    ImU32 accent = IM_COL32(0, 110, 230, 255);
    float hy = y + 12 * k, mid = (px + pr) * 0.5f;
    dl->AddText(fb, 10.5f * k, ImVec2(px, hy), IM_COL32(20, 20, 24, 255), "Design it.");
    dl->AddText(fb, 10.5f * k, ImVec2(px, hy + 11 * k), accent, "Ship it.");
    dl->AddRectFilled(ImVec2(px, hy + 26 * k), ImVec2(mid - 6 * k, hy + 27.6f * k), IM_COL32(160, 162, 170, 255), 1 * k);
    dl->AddRectFilled(ImVec2(px, hy + 30 * k), ImVec2(mid - 16 * k, hy + 31.6f * k), IM_COL32(160, 162, 170, 255), 1 * k);
    ImVec2 ba(px, hy + 36 * k), bb(px + 24 * k, hy + 43 * k);
    dl->AddRectFilled(ba, bb, accent, 3.5f * k);
    const char* cta = "Get started";
    ImVec2 cs = fb->CalcTextSizeA(4.4f * k, FLT_MAX, 0, cta);
    dl->AddText(fb, 4.4f * k, ImVec2((ba.x + bb.x - cs.x) * 0.5f, (ba.y + bb.y - cs.y) * 0.5f), IM_COL32_WHITE, cta);

    ImVec2 ia(mid + 2 * k, hy - 1 * k), ib(pr, hy + 43 * k);
    dl->AddRectFilledMultiColor(ia, ib, IM_COL32(232, 240, 255, 255), IM_COL32(214, 228, 255, 255), IM_COL32(200, 218, 252, 255), IM_COL32(226, 236, 255, 255));
    dl->AddRect(ia, ib, IM_COL32(210, 220, 240, 255), 3 * k);
    float bw = (ib.x - ia.x - 10 * k) / 6;
    for (int i = 0; i < 6; i++)
    {
        float bh = (8 + (PreviewHash(i, 3) % 22)) * k;
        float bx = ia.x + 5 * k + i * bw;
        dl->AddRectFilled(ImVec2(bx + bw * 0.2f, ib.y - 5 * k - bh), ImVec2(bx + bw * 0.8f, ib.y - 5 * k), WithAlpha(accent, i == 4 ? 255 : 110), 1 * k);
    }

    float cy = hy + 50 * k, cw = (pr - px - 8 * k) / 3;
    for (int i = 0; i < 3; i++)
    {
        ImVec2 ca(px + i * (cw + 4 * k), cy), cb(ca.x + cw, cy + 24 * k);
        dl->AddRectFilled(ca, cb, IM_COL32_WHITE, 3 * k);
        dl->AddRect(ca, cb, IM_COL32(226, 227, 232, 255), 3 * k);
        dl->AddRectFilled(ImVec2(ca.x + 4 * k, ca.y + 4 * k), ImVec2(ca.x + 10 * k, ca.y + 10 * k), WithAlpha(accent, 40), 1.5f * k);
        dl->AddRectFilled(ImVec2(ca.x + 4 * k, ca.y + 14 * k), ImVec2(cb.x - 8 * k, ca.y + 15.6f * k), IM_COL32(60, 62, 70, 255), 1 * k);
    }
}

// Cards for the project type: what the design is exported to.
static bool ProjectTypeCards(int* type, float width)
{
    struct Card { const char* title; const char* line1; const char* line2; bool beta; void (*preview)(ImDrawList*, ImVec2, ImVec2, float); };
    static const Card cards[3] = {
        { "Dear ImGui", "C++ menu for Dear ImGui", "DirectX 11 example host", false, PreviewImGui },
        { "Minecraft", "Java Edition 1.21.1 GUI", "Fabric / NeoForge Screen", true, PreviewMinecraft },
        { "Web", "HTML5 + CSS website", "Pages, live browser preview", false, PreviewWeb },
    };
    bool changed = false;
    float gap = U(12), w = (width - gap * 2) / 3, ph = U(150), h = ph + U(62), r = U(8);
    float fs = ImGui::GetFontSize();
    for (int i = 0; i < 3; i++)
    {
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##type", ImVec2(w, h)) && *type != i) { *type = i; changed = true; }
        bool hov = ImGui::IsItemHovered(), sel = *type == i;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 e(p.x + w, p.y + h), pe(e.x, p.y + ph);
        dl->AddRectFilled(p, e, sel ? IM_COL32(13, 153, 255, 22) : hov ? IM_COL32(255, 255, 255, 10) : IM_COL32(255, 255, 255, 4), r);
        dl->PushClipRect(p, pe, true);
        cards[i].preview(dl, p, pe, r);
        dl->PopClipRect();
        dl->AddLine(ImVec2(p.x, pe.y), ImVec2(e.x, pe.y), Border);
        dl->AddRect(p, e, sel ? Accent : hov ? TextFaint : Border, r, 0, sel ? 2.0f : 1.0f);

        ImVec2 c(e.x - U(16), pe.y + U(17));
        dl->AddCircle(c, U(6), sel ? Accent : TextFaint, 0, 1.2f);
        if (sel) dl->AddCircleFilled(c, U(3), Accent);
        ImFont* fb = fonts::UI(fonts::UI_SemiBold);
        ImVec2 tp(p.x + U(12), pe.y + U(9));
        dl->AddText(fb, fs * 1.05f, tp, Text, cards[i].title);
        if (cards[i].beta)
        {
            // The Minecraft target is still in beta: say so on the card
            float tw = fb->CalcTextSizeA(fs * 1.05f, FLT_MAX, 0, cards[i].title).x;
            float bfs = fs * 0.72f;
            ImVec2 bsz = fb->CalcTextSizeA(bfs, FLT_MAX, 0, "BETA");
            ImVec2 ba(tp.x + tw + U(7), tp.y + U(1.5f)), bb(ba.x + bsz.x + U(10), ba.y + bsz.y + U(4));
            dl->AddRectFilled(ba, bb, IM_COL32(255, 176, 32, 30), U(4));
            dl->AddRect(ba, bb, IM_COL32(255, 176, 32, 170), U(4));
            dl->AddText(fb, bfs, ImVec2(ba.x + U(5), ba.y + U(2)), IM_COL32(255, 196, 90, 255), "BETA");
        }
        dl->AddText(fonts::UI(fonts::UI_Regular), fs * 0.92f, ImVec2(p.x + U(12), pe.y + U(29)), TextDim, cards[i].line1);
        dl->AddText(fonts::UI(fonts::UI_Regular), fs * 0.92f, ImVec2(p.x + U(12), pe.y + U(44)), TextFaint, cards[i].line2);
        ImGui::PopID();
    }
    return changed;
}

void App::DrawNewProjectDialog()
{
    static char name[128];
    static std::string error;
    static int type = Project_ImGui;
    if (openNewProject)
    {
        openNewProject = false;
        std::string suggestion = newProjectFromCurrent && doc.name != "Untitled" && !doc.name.empty() ? doc.name : SuggestName(this);
        snprintf(name, sizeof(name), "%s", suggestion.c_str());
        error.clear();
        type = newProjectFromCurrent ? projectType : Project_ImGui;
        ImGui::OpenPopup("New project");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(U(760), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(18), U(16)));
    if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        float W = U(724);
        ui::Label(newProjectFromCurrent ? "Save the current design as a project" : "Create an empty project");
        ui::Label("Type", false);
        ProjectTypeCards(&type, W);
        ui::Label("Name", false);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(W);
        bool enter = ImGui::InputText("##pname", name, sizeof(name), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        std::string n = name, verr;
        bool valid = ValidProjectName(n, &verr);
        fs::path dir = fs::u8path(WorkspaceDir()) / fs::u8path(valid ? n : std::string("..."));
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextUnformatted((dir / fs::u8path(ProjectFileName(n))).u8string().c_str());
        ImGui::TextUnformatted("+ fonts\\  images\\  icons\\  videos\\  export\\");
        ImGui::PopStyleColor();
        if (!valid && !n.empty()) error = verr;
        if (!error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Danger);
            ImGui::TextWrapped("%s", error.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Dummy(ImVec2(0, U(6)));
        if (ImGui::Button("Cancel", ImVec2(U(100), 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine(W - U(120) + ImGui::GetStyle().WindowPadding.x);
        ImGui::BeginDisabled(!valid);
        if (ui::PrimaryButton("Create", ImVec2(U(120), 0)) || (enter && valid))
        {
            std::string err;
            int t = type;
            auto create = [this, n, t]() {
                std::string e;
                if (CreateProject(n, newProjectFromCurrent, &e, t)) { showHome = false; homeNeedsScan = true; Toast("Project created: " + n); }
                else Toast(e, true);
            };
            if (!newProjectFromCurrent && modified)
            {
                // Reuse the editor's discard confirmation.
                ImGui::CloseCurrentPopup();
                NewDocumentConfirm(create);
            }
            else if (CreateProject(n, newProjectFromCurrent, &err, t)) { showHome = false; homeNeedsScan = true; Toast("Project created: " + n); ImGui::CloseCurrentPopup(); }
            else error = err;
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
