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
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) ImGui::SetWindowFocus();

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

// Cards for the project type: what the design is exported to.
static bool ProjectTypeCards(int* type, float width)
{
    struct Card { const char* title; const char* line1; const char* line2; };
    static const Card cards[3] = {
        { "Dear ImGui", "C++ menu for Dear ImGui", "DirectX 11 example host" },
        { "Minecraft", "Java Edition 1.21.1 GUI", "Fabric / NeoForge Screen" },
        { "Web", "HTML5 + CSS website", "Pages, live browser preview" },
    };
    bool changed = false;
    float gap = U(10), w = (width - gap * 2) / 3, h = U(64);
    for (int i = 0; i < 3; i++)
    {
        if (i) ImGui::SameLine(0, gap);
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##type", ImVec2(w, h)) && *type != i) { *type = i; changed = true; }
        bool hov = ImGui::IsItemHovered(), sel = *type == i;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), sel ? IM_COL32(13, 153, 255, 28) : hov ? IM_COL32(255, 255, 255, 10) : IM_COL32(255, 255, 255, 4), U(8));
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), sel ? Accent : Border, U(8), 0, sel ? 1.5f : 1.0f);
        // radio dot
        ImVec2 c(p.x + w - U(16), p.y + U(16));
        dl->AddCircle(c, U(6), sel ? Accent : TextFaint, 0, 1.2f);
        if (sel) dl->AddCircleFilled(c, U(3), Accent);
        float fs = ImGui::GetFontSize();
        dl->AddText(fonts::UI(fonts::UI_SemiBold), fs * 1.05f, ImVec2(p.x + U(12), p.y + U(9)), Text, cards[i].title);
        dl->AddText(fonts::UI(fonts::UI_Regular), fs * 0.92f, ImVec2(p.x + U(12), p.y + U(29)), TextDim, cards[i].line1);
        dl->AddText(fonts::UI(fonts::UI_Regular), fs * 0.92f, ImVec2(p.x + U(12), p.y + U(44)), TextFaint, cards[i].line2);
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
    ImGui::SetNextWindowSize(ImVec2(U(580), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(18), U(16)));
    if (ImGui::BeginPopupModal("New project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        float W = U(544);
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
