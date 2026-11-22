// Window chrome: custom title bar with the menus, window buttons, update / what's new / about dialogs.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "platform.h"
#include "updater.h"
#include "version.h"
#include "imgui_internal.h"
#include <cmath>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;
using namespace theme;

// ---------------------------------------------------------------- menus

void App::MenuFile()
{
    if (ImGui::MenuItem("Projects...", "Ctrl+Shift+O")) OpenHome();
    if (ImGui::MenuItem("New project...", "Ctrl+N")) { newProjectFromCurrent = false; openNewProject = true; }
    if (ImGui::MenuItem("Open file...", "Ctrl+O")) OpenDocument();
    if (ImGui::BeginMenu("Open recent", !recentFiles.empty()))
    {
        for (auto& r : recentFiles)
            if (ImGui::MenuItem(r.c_str())) { OpenDocument(r); break; }
        ImGui::EndMenu();
    }
    ImGui::Separator();
    if (ImGui::MenuItem(projectDir.empty() ? "Save to project..." : "Save", "Ctrl+S")) SaveDocument(false);
    if (ImGui::MenuItem("Save as single file...", "Ctrl+Shift+S")) SaveDocument(true);
    if (!projectDir.empty() && ImGui::MenuItem("Show project folder")) platform::OpenInExplorer(projectDir);
    if (ImGui::MenuItem("New blank design")) NewDocument();
    ImGui::Separator();
    if (ImGui::MenuItem("Place image / SVG / video...", "Ctrl+Shift+K"))
        for (auto& f : platform::OpenFilesDialog("Place image", "Media|*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.mp4;*.mov;*.wmv;*.m4v")) ImportImageFile(f, nullptr, ImVec2(0, 0));
    if (ImGui::MenuItem(!projectDir.empty() && projectType == Project_Minecraft ? "Export Minecraft GUI..." : "Export ImGui project...", "Ctrl+E")) showExport = true;
    ImGui::Separator();
    if (ImGui::MenuItem("Exit", "Alt+F4")) platform::CloseMainWindow();
}

void App::MenuEdit()
{
    if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
    if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z")) Redo();
    ImGui::Separator();
    bool has = !selection.empty();
    if (ImGui::MenuItem("Cut", "Ctrl+X", false, has)) CutSelection();
    if (ImGui::MenuItem("Copy", "Ctrl+C", false, has)) CopySelection();
    if (ImGui::MenuItem("Paste", "Ctrl+V")) Paste();
    if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has)) DuplicateSelection();
    if (ImGui::MenuItem("Delete", "Del", false, has)) DeleteSelection();
    ImGui::Separator();
    if (ImGui::BeginMenu("Copy as", has))
    {
        if (ImGui::MenuItem("CSS")) CopyAs(0);
        if (ImGui::MenuItem("SVG")) CopyAs(1);
        if (ImGui::MenuItem("PNG", "Ctrl+Shift+C")) CopyAs(2);
        if (ImGui::MenuItem("ImGui code")) CopyAs(3);
        if (ImGui::MenuItem("JSON")) CopyAs(4);
        ImGui::EndMenu();
    }
    if (ImGui::MenuItem("Group selection", "Ctrl+G", false, has)) GroupSelection(false);
    if (ImGui::MenuItem("Frame selection", "Ctrl+Alt+G", false, has)) GroupSelection(true);
    if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, has)) Ungroup();
}

void App::MenuView()
{
    ImVec2 c((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f);
    if (ImGui::MenuItem("Zoom in", "Ctrl+=")) SetZoom(zoom * 1.5f, c);
    if (ImGui::MenuItem("Zoom out", "Ctrl+-")) SetZoom(zoom / 1.5f, c);
    if (ImGui::MenuItem("Zoom to fit", "Shift+1")) ZoomToFit(false);
    if (ImGui::MenuItem("Zoom to selection", "Shift+2")) ZoomToFit(true);
    if (ImGui::MenuItem("Zoom to 100%", "Ctrl+0")) SetZoom(1.0f, c);
    ImGui::Separator();
    ImGui::MenuItem("Pixel grid (from 400%)", "Shift+'", &pixelGrid);
    ImGui::MenuItem("Snap to pixel grid", "Ctrl+Shift+'", &snapToPixel);
    ImGui::MenuItem("Pixel preview", "Ctrl+Alt+Y", &pixelPreview);
    ImGui::Separator();
    ImGui::MenuItem("Show all conditional layers", nullptr, &ignoreConditions);
    ImGui::MenuItem("Timeline", "Alt+T", &timelineOpen);
    if (ImGui::MenuItem("Preview", "Ctrl+Enter", preview)) preview = !preview;
    ImGui::Separator();
    if (ImGui::BeginMenu("Interface scale"))
    {
        for (float s : { 1.0f, 1.15f, 1.25f, 1.5f, 1.75f })
        {
            char b[16];
            snprintf(b, sizeof(b), "%d%%", (int)std::round(s * 100));
            if (ImGui::MenuItem(b, nullptr, std::fabs(uiScale - s) < 0.01f)) pendingUiScale = s;
        }
        ImGui::EndMenu();
    }
}

void App::MenuHelp()
{
    if (ImGui::MenuItem("What's new")) OpenWhatsNew(true);
    if (ImGui::MenuItem("Check for updates")) CheckForUpdates();
    ImGui::Separator();
    if (ImGui::MenuItem("Keyboard shortcuts")) showShortcuts = true;
    if (ImGui::MenuItem("Settings...")) showSettings = true;
    ImGui::Separator();
    if (ImGui::MenuItem("GitHub repository")) platform::OpenUrl("https://github.com/" FIMGUI_REPO);
    if (ImGui::MenuItem("Report a problem")) platform::OpenUrl("https://github.com/" FIMGUI_REPO "/issues");
    if (ImGui::MenuItem("About Fimgui")) showAbout = true;
}

// ---------------------------------------------------------------- title bar

float App::TitleBarHeight() const { return U(42); }

static void WindowGlyph(ImDrawList* dl, int kind, ImVec2 c, float s, ImU32 col)
{
    float h = s * 0.5f;
    if (kind == 0) dl->AddLine(ImVec2(c.x - h, c.y), ImVec2(c.x + h, c.y), col, 1.0f);
    else if (kind == 1) dl->AddRect(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, 1.5f, 0, 1.0f);
    else if (kind == 2)
    {
        float o = s * 0.22f;
        dl->AddRect(ImVec2(c.x - h, c.y - h + o), ImVec2(c.x + h - o, c.y + h), col, 1.5f, 0, 1.0f);
        dl->AddLine(ImVec2(c.x - h + o, c.y - h + o), ImVec2(c.x - h + o, c.y - h), col, 1.0f);
        dl->AddLine(ImVec2(c.x - h + o, c.y - h), ImVec2(c.x + h, c.y - h), col, 1.0f);
        dl->AddLine(ImVec2(c.x + h, c.y - h), ImVec2(c.x + h, c.y + h - o), col, 1.0f);
        dl->AddLine(ImVec2(c.x + h, c.y + h - o), ImVec2(c.x + h - o, c.y + h - o), col, 1.0f);
    }
    else
    {
        dl->AddLine(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), col, 1.0f);
        dl->AddLine(ImVec2(c.x + h, c.y - h), ImVec2(c.x - h, c.y + h), col, 1.0f);
    }
}

void App::DrawTitleBar()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float h = TitleBarHeight();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(30, 30, 30, 255));
    ImGui::BeginViewportSideBar("##titlebar", vp, ImGuiDir_Up, h, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                                ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    float W = ImGui::GetWindowWidth();
    dl->AddLine(ImVec2(o.x, o.y + h - 1), ImVec2(o.x + W, o.y + h - 1), Divider);

    // Logo: Projects home
    ImGui::SetCursorScreenPos(ImVec2(o.x + U(6), o.y + U(5)));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool click = ImGui::InvisibleButton("##home", ImVec2(U(40), h - U(10)));
        if (ImGui::IsItemHovered() || showHome) dl->AddRectFilled(p, ImVec2(p.x + U(40), p.y + h - U(10)), Hover, U(6));
        DrawLogo(dl, ImVec2(p.x + U(8), p.y + (h - U(10) - U(24)) * 0.5f), U(24));
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Projects (Ctrl+Shift+O)");
        if (click) { if (showHome) showHome = false; else OpenHome(); }
    }

    // Menus: large, clearly separated buttons that behave like a menu bar.
    const char* names[] = { "File", "Edit", "View", "Help" };
    float x = o.x + U(56);
    ImFont* mf = fonts::UI(fonts::UI_Medium);
    float fs = U(14.5f);
    int hoveredMenu = -1;
    ImVec2 menuPos[4];
    for (int i = 0; i < 4; i++)
    {
        ImVec2 ts = mf->CalcTextSizeA(fs, FLT_MAX, 0, names[i]);
        ImVec2 p(x, o.y + U(6));
        ImVec2 size(ts.x + U(24), h - U(12));
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        bool click = ImGui::InvisibleButton("##menu", size);
        ImGui::PopID();
        // Raw rect test: an open menu popup blocks normal hovering of the title bar.
        bool hov = ImGui::IsItemHovered() || (openMenu >= 0 && ImGui::IsMouseHoveringRect(p, ImVec2(p.x + size.x, p.y + size.y)));
        if (hov) hoveredMenu = i;
        bool open = openMenu == i;
        if (open) dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(13, 153, 255, 70), U(6));
        else if (hov) dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), IM_COL32(255, 255, 255, 22), U(6));
        dl->AddText(mf, fs, ImVec2(p.x + U(12), p.y + (size.y - ts.y) * 0.5f), open || hov ? Text : IM_COL32(255, 255, 255, 200), names[i]);
        menuPos[i] = ImVec2(p.x, o.y + h);
        if (click) openMenu = open ? -1 : i;
        x += size.x + U(4);
    }
    // Hovering another menu while one is open switches to it.
    if (openMenu >= 0 && hoveredMenu >= 0 && hoveredMenu != openMenu) openMenu = hoveredMenu;
    float menusEnd = x;

    // Window buttons
    float bw = U(46);
    float bx = o.x + W - bw * 3;
    for (int i = 0; i < 3; i++)
    {
        ImVec2 p(bx + bw * i, o.y);
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(100 + i);
        bool click = ImGui::InvisibleButton("##win", ImVec2(bw, h - 1));
        ImGui::PopID();
        bool hov = ImGui::IsItemHovered();
        if (hov) dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + h - 1), i == 2 ? IM_COL32(196, 43, 28, 255) : IM_COL32(255, 255, 255, 22));
        int glyph = i == 0 ? 0 : i == 1 ? (platform::IsWindowMaximized() ? 2 : 1) : 3;
        WindowGlyph(dl, glyph, ImVec2(p.x + bw * 0.5f, p.y + h * 0.5f), U(10), hov && i == 2 ? IM_COL32_WHITE : Text);
        if (click)
        {
            if (i == 0) platform::MinimizeWindow();
            else if (i == 1) platform::ToggleMaximizeWindow();
            else platform::CloseMainWindow();
        }
    }

    // Title in the middle
    {
        std::string title = !projectDir.empty() ? fs::u8path(projectDir).filename().u8string() : (doc.name.empty() ? "Untitled" : doc.name);
        ImFont* tf = fonts::UI(fonts::UI_SemiBold);
        ImVec2 ts = tf->CalcTextSizeA(fs, FLT_MAX, 0, title.c_str());
        float cx = std::max(menusEnd + U(16), o.x + (W - ts.x) * 0.5f);
        if (cx + ts.x < bx - U(16))
        {
            dl->AddText(tf, fs, ImVec2(cx, o.y + (h - ts.y) * 0.5f), Text, title.c_str());
            if (modified) dl->AddCircleFilled(ImVec2(cx + ts.x + U(9), o.y + h * 0.5f), U(3), TextDim);
            if (!projectDir.empty())
            {
                const char* hint = projectType == Project_Minecraft ? "Minecraft project" : "ImGui project";
                ImVec2 hs = mf->CalcTextSizeA(U(11), FLT_MAX, 0, hint);
                if (cx - hs.x - U(14) > menusEnd) dl->AddText(mf, U(11), ImVec2(cx - hs.x - U(12), o.y + (h - hs.y) * 0.5f), TextFaint, hint);
            }
        }
    }

    // Drag the window from empty space, double click maximizes.
    ImVec2 m = ImGui::GetIO().MousePos;
    bool overBar = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && m.y < o.y + h;
    if (overBar && ImGui::IsMouseDoubleClicked(0)) platform::ToggleMaximizeWindow();
    else if (overBar && ImGui::IsMouseClicked(0)) { platform::StartWindowDrag(); ImGui::GetIO().AddMouseButtonEvent(0, false); }
    ImGui::End();

    // Menu popups
    // Open a popup only when the open menu changes; a popup closed by ImGui (outside click, item chosen) resets openMenu.
    bool menuChanged = openMenu != lastOpenMenu;
    lastOpenMenu = openMenu;
    for (int i = 0; i < 4; i++)
    {
        char id[16];
        snprintf(id, sizeof(id), "##menupop%d", i);
        if (openMenu == i && menuChanged) ImGui::OpenPopup(id);
        ImGui::SetNextWindowPos(menuPos[i]);
        ImGui::SetNextWindowSizeConstraints(ImVec2(U(240), 0), ImVec2(FLT_MAX, FLT_MAX));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(8), U(8)));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(12), U(9)));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, U(8));
        ImGui::PushFont(nullptr, 14.0f);
        if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoScrollbar))
        {
            if (i == 0) MenuFile(); else if (i == 1) MenuEdit(); else if (i == 2) MenuView(); else MenuHelp();
            if (openMenu != i) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        else if (openMenu == i) { openMenu = -1; lastOpenMenu = -1; }
        ImGui::PopFont();
        ImGui::PopStyleVar(3);
    }
}

// ---------------------------------------------------------------- Markdown-ish view for patch notes

static void MarkdownView(const std::string& text)
{
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line))
    {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string clean;
        for (size_t i = 0; i < line.size(); i++)
        {
            if (line.compare(i, 2, "**") == 0) { i++; continue; }
            if (line[i] == '`') continue;
            if (line[i] == '[')
            {
                // [text](url) -> text
                size_t close = line.find("](", i), end = close == std::string::npos ? close : line.find(')', close);
                if (end != std::string::npos) { clean += line.substr(i + 1, close - i - 1); i = end; continue; }
            }
            clean += line[i];
        }
        if (clean.rfind("### ", 0) == 0 || clean.rfind("## ", 0) == 0)
        {
            ImGui::Dummy(ImVec2(0, U(4)));
            ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), clean[2] == '#' ? 13.0f : 15.0f);
            ImGui::TextUnformatted(clean.substr(clean.find(' ') + 1).c_str());
            ImGui::PopFont();
        }
        else if (clean.rfind("- ", 0) == 0 || clean.rfind("* ", 0) == 0)
        {
            ImGui::Bullet();
            ImGui::SameLine();
            ImGui::TextWrapped("%s", clean.substr(2).c_str());
        }
        else if (clean.empty()) ImGui::Dummy(ImVec2(0, U(2)));
        else ImGui::TextWrapped("%s", clean.c_str());
    }
}

// ---------------------------------------------------------------- update / what's new / about

void App::OpenWhatsNew(bool force)
{
    std::string changelog;
    platform::ReadText(platform::ExeDir() + "/CHANGELOG.md", changelog);
    whatsNewText = updater::NotesFor(changelog, FIMGUI_VERSION);
    if (whatsNewText.empty())
    {
        if (force) Toast("No patch notes for " FIMGUI_VERSION, true);
        return;
    }
    showWhatsNew = true;
}

void App::CheckForUpdates()
{
    manualUpdateCheck = true;
    updater::Reset();
    updater::CheckAsync();
    Toast("Checking for updates...");
}

void App::PollUpdates()
{
    updater::Release rel;
    std::string err;
    updater::State st = updater::Poll(&rel, &err);
    if (st == updater::State::Done)
    {
        updater::Reset();
        bool newer = updater::Compare(rel.version, FIMGUI_VERSION) > 0;
        if (newer && (manualUpdateCheck || rel.version != skippedVersion)) { latestRelease = rel; showUpdate = true; }
        else if (manualUpdateCheck) Toast("Fimgui " FIMGUI_VERSION " is up to date");
        manualUpdateCheck = false;
    }
    else if (st == updater::State::Failed)
    {
        updater::Reset();
        if (manualUpdateCheck) Toast("Update check failed: " + err, true);
        manualUpdateCheck = false;
    }
}

void App::DrawUpdateDialogs()
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    auto notesBox = [&](const std::string& text) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(24, 24, 24, 255));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(14), U(10)));
        ImGui::BeginChild("##notes", ImVec2(U(520), U(300)), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);
        ImGui::PushTextWrapPos(0);
        MarkdownView(text.empty() ? std::string("No patch notes.") : text);
        ImGui::PopTextWrapPos();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    };

    if (showUpdate) { ImGui::OpenPopup("Update available"); showUpdate = false; }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(20), U(18)));
    if (ImGui::BeginPopupModal("Update available", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        DrawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), U(40));
        ImGui::Dummy(ImVec2(U(40), U(40)));
        ImGui::SameLine(0, U(14));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 16.0f);
        ImGui::Text("Fimgui %s is available", latestRelease.version.c_str());
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::Text("You have %s. The release page opens in your browser - download the zip there.", FIMGUI_VERSION);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, U(6)));
        ImGui::TextUnformatted("Patch notes");
        notesBox(latestRelease.notes);
        ImGui::Dummy(ImVec2(0, U(8)));
        if (ui::PrimaryButton("Download on GitHub", ImVec2(U(190), U(32)))) { platform::OpenUrl(latestRelease.url); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if (ImGui::Button("Later", ImVec2(U(100), U(32)))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("Skip this version", ImVec2(U(150), U(32)))) { skippedVersion = latestRelease.version; SaveSettings(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    if (showWhatsNew) { ImGui::OpenPopup("What's new"); showWhatsNew = false; }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(20), U(18)));
    if (ImGui::BeginPopupModal("What's new", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        DrawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), U(40));
        ImGui::Dummy(ImVec2(U(40), U(40)));
        ImGui::SameLine(0, U(14));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 16.0f);
        ImGui::Text("What's new in Fimgui %s", FIMGUI_VERSION);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::TextUnformatted("Patch notes of this version");
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        ImGui::Dummy(ImVec2(0, U(6)));
        notesBox(whatsNewText);
        ImGui::Dummy(ImVec2(0, U(8)));
        if (ui::PrimaryButton("Got it", ImVec2(U(120), U(32))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (ImGui::Button("All releases", ImVec2(U(130), U(32)))) platform::OpenUrl("https://github.com/" FIMGUI_REPO "/releases");
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();

    if (showAbout) { ImGui::OpenPopup("About Fimgui"); showAbout = false; }
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(24), U(20)));
    if (ImGui::BeginPopupModal("About Fimgui", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        DrawLogo(ImGui::GetWindowDrawList(), ImGui::GetCursorScreenPos(), U(64));
        ImGui::Dummy(ImVec2(U(64), U(64)));
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 18.0f);
        ImGui::Text("Fimgui %s", FIMGUI_VERSION);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::TextUnformatted("Design editor for Dear ImGui menus. Interface design inspired by Figma.");
        ImGui::TextUnformatted("Not affiliated with Figma, Inc. Free tool - your projects are yours to sell; Fimgui itself may not be sold.");
        ImGui::TextUnformatted("Fonts rendered with FreeType. Portions of this software are copyright (c) 2024 The FreeType Project.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, U(8)));
        if (ImGui::Button("GitHub", ImVec2(U(110), U(30)))) platform::OpenUrl("https://github.com/" FIMGUI_REPO);
        ImGui::SameLine();
        if (ImGui::Button("License", ImVec2(U(110), U(30)))) platform::OpenUrl("https://github.com/" FIMGUI_REPO "/blob/main/LICENSE");
        ImGui::SameLine();
        if (ui::PrimaryButton("Close", ImVec2(U(110), U(30))) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
