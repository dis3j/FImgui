#include "app.h"
#include "version.h"
#include "theme.h"
#include "fonts.h"
#include "gfx.h"
#include "platform.h"
#include "exporters.h"
#include "icons.h"
#include "imgui_internal.h"
#include <windows.h>
#include <wincrypt.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

App* g_app = nullptr;
float theme::dpi = 1.0f;

static std::function<void()> s_confirmAction;
static std::string s_confirmText;

void theme::Apply(float dpiScale)
{
    dpi = dpiScale;
    ImGuiStyle& st = ImGui::GetStyle();
    st = ImGuiStyle();
    st.WindowPadding = ImVec2(12, 10);
    st.FramePadding = ImVec2(8, 6);
    st.ItemSpacing = ImVec2(8, 7);
    st.ItemInnerSpacing = ImVec2(6, 4);
    st.CellPadding = ImVec2(4, 2);
    st.IndentSpacing = 14;
    st.ScrollbarSize = 10;
    st.GrabMinSize = 8;
    st.WindowRounding = 10;
    st.ChildRounding = 0;
    st.FrameRounding = 5;
    st.PopupRounding = 8;
    st.ScrollbarRounding = 6;
    st.GrabRounding = 4;
    st.TabRounding = 5;
    st.WindowBorderSize = 1;
    st.PopupBorderSize = 1;
    st.FrameBorderSize = 0;
    st.SeparatorTextBorderSize = 1;
    st.WindowTitleAlign = ImVec2(0.0f, 0.5f);
    st.FontSizeBase = 12.0f;
    st.ScaleAllSizes(dpiScale);
    st.FontScaleDpi = dpiScale;

    ImVec4* c = st.Colors;
    auto C = [](ImU32 v) { return ImGui::ColorConvertU32ToFloat4(v); };
    c[ImGuiCol_Text] = C(Text);
    c[ImGuiCol_TextDisabled] = C(TextFaint);
    c[ImGuiCol_WindowBg] = C(Bg);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = C(IM_COL32(30, 30, 30, 250));
    c[ImGuiCol_Border] = C(Border);
    c[ImGuiCol_BorderShadow] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg] = C(Field);
    c[ImGuiCol_FrameBgHovered] = C(IM_COL32(66, 66, 66, 255));
    c[ImGuiCol_FrameBgActive] = C(IM_COL32(66, 66, 66, 255));
    c[ImGuiCol_TitleBg] = C(Bg);
    c[ImGuiCol_TitleBgActive] = C(Bg);
    c[ImGuiCol_TitleBgCollapsed] = C(Bg);
    c[ImGuiCol_MenuBarBg] = C(Bg);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = C(IM_COL32(255, 255, 255, 40));
    c[ImGuiCol_ScrollbarGrabHovered] = C(IM_COL32(255, 255, 255, 70));
    c[ImGuiCol_ScrollbarGrabActive] = C(IM_COL32(255, 255, 255, 90));
    c[ImGuiCol_CheckMark] = C(IM_COL32_WHITE);
    c[ImGuiCol_SliderGrab] = C(Accent);
    c[ImGuiCol_SliderGrabActive] = C(Accent);
    c[ImGuiCol_Button] = C(Field);
    c[ImGuiCol_ButtonHovered] = C(IM_COL32(70, 70, 70, 255));
    c[ImGuiCol_ButtonActive] = C(IM_COL32(80, 80, 80, 255));
    c[ImGuiCol_Header] = C(AccentDim);
    c[ImGuiCol_HeaderHovered] = C(IM_COL32(255, 255, 255, 18));
    c[ImGuiCol_HeaderActive] = C(AccentDim);
    c[ImGuiCol_Separator] = C(Divider);
    c[ImGuiCol_SeparatorHovered] = C(Accent);
    c[ImGuiCol_SeparatorActive] = C(Accent);
    c[ImGuiCol_ResizeGrip] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered] = C(AccentDim);
    c[ImGuiCol_ResizeGripActive] = C(Accent);
    c[ImGuiCol_Tab] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TabHovered] = C(Hover);
    c[ImGuiCol_TabSelected] = C(IM_COL32(255, 255, 255, 20));
    c[ImGuiCol_TabSelectedOverline] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TextSelectedBg] = C(IM_COL32(13, 153, 255, 90));
    c[ImGuiCol_NavCursor] = C(Accent);
    c[ImGuiCol_ModalWindowDimBg] = ImVec4(0, 0, 0, 0.45f);
    c[ImGuiCol_DragDropTarget] = C(Accent);
}

// ---------------------------------------------------------------- init / frame

void App::Init()
{
    g_app = this;
    baseDpi = theme::dpi;
    LoadSettings();
    theme::Apply(baseDpi * uiScale);
    NewDocument();
    modified = false;
    StartMcp();
    showHome = showHomeOnStart;
    homeNeedsScan = true;
}

void App::StartupChecks()
{
    if (checkUpdates) updater::CheckAsync();
    // After an update: show this version's patch notes once.
    if (!lastSeenVersion.empty() && updater::Compare(lastSeenVersion, FIMGUI_VERSION) < 0) OpenWhatsNew(false);
    if (lastSeenVersion != FIMGUI_VERSION) { lastSeenVersion = FIMGUI_VERSION; SaveSettings(); }
}

void App::Shutdown()
{
    aiClient.Cancel();
    mcp.Stop();
    if (persistSettings) SaveSettings();
}

bool App::RequestClose()
{
    if (!modified) return true;
    s_confirmText = "Discard unsaved changes and quit?";
    s_confirmAction = [] { PostQuitMessage(0); };
    return false;
}

void App::Toast(const std::string& msg, bool error)
{
    toast = msg;
    toastError = error;
    toastTime = ImGui::GetTime();
}

void App::Frame()
{
    ImGuiIO& io = ImGui::GetIO();
    AiPump();
    g_textVars = &vars;
    // While dragging, children of auto-layout frames float freely; layout re-applies on drop.
    if (m_drag != Drag::Move) doc.ApplyLayout();
    EnsureVars(doc, vars);

    if (pendingUiScale > 0)
    {
        uiScale = pendingUiScale;
        pendingUiScale = 0;
        float l = leftW / theme::dpi, r = rightW / theme::dpi;
        theme::Apply(baseDpi * uiScale);
        leftW = theme::U(l);
        rightW = theme::U(r);
        if (persistSettings) SaveSettings();
    }
    PollUpdates();
    TickTimeline();
    CollectPageColors();
    DrawTitleBar();
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::Begin("##host", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(4);

    if (leftW <= 0) leftW = theme::U(248);
    if (rightW <= 0) rightW = theme::U(296);
    float totalW = vp->WorkSize.x;
    leftW = std::clamp(leftW, theme::U(180), totalW * 0.35f);
    rightW = std::clamp(rightW, theme::U(240), totalW * 0.45f);
    float splitter = theme::U(4);

    DrawLeftPanel();
    ImGui::SameLine(0, 0);
    // left splitter
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImVec2 sp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##splitL", ImVec2(1, vp->WorkSize.y));
    ImGui::GetWindowDrawList()->AddLine(sp, ImVec2(sp.x, sp.y + vp->WorkSize.y), theme::Divider);
    ImGui::SetCursorScreenPos(ImVec2(sp.x - splitter * 0.5f, sp.y));
    ImGui::InvisibleButton("##splitLgrab", ImVec2(splitter, vp->WorkSize.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) leftW += io.MouseDelta.x;
    ImGui::SetCursorScreenPos(ImVec2(sp.x + 1, sp.y));
    float canvasW = totalW - leftW - rightW - 2;
    float tlH = 0;
    if (timelineOpen && !preview)
    {
        if (timelineH <= 0) timelineH = theme::U(250);
        tlH = std::clamp(timelineH, theme::U(150), vp->WorkSize.y * 0.65f);
    }
    ImGui::BeginGroup();
    ImGui::BeginChild("##canvas", ImVec2(canvasW, vp->WorkSize.y - tlH), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
    DrawCanvas();
    ImGui::EndChild();
    if (tlH > 0)
    {
        // splitter + timeline
        ImVec2 tp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##splitT", ImVec2(canvasW, theme::U(4)));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (ImGui::IsItemActive()) timelineH -= io.MouseDelta.y;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(tp.x, tp.y + theme::U(2)), ImVec2(tp.x + canvasW, tp.y + theme::U(2)), theme::Divider);
        ImGui::BeginChild("##timeline", ImVec2(canvasW, tlH - theme::U(4)), 0, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove);
        DrawTimeline();
        ImGui::EndChild();
    }
    ImGui::EndGroup();
    ImGui::SameLine(0, 0);
    sp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(sp, ImVec2(sp.x, sp.y + vp->WorkSize.y), theme::Divider);
    ImGui::SetCursorScreenPos(ImVec2(sp.x - splitter * 0.5f, sp.y));
    ImGui::InvisibleButton("##splitRgrab", ImVec2(splitter, vp->WorkSize.y));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive()) rightW -= io.MouseDelta.x;
    ImGui::SetCursorScreenPos(ImVec2(sp.x + 1, sp.y));
    DrawRightPanel();
    ImGui::PopStyleVar();
    ImGui::End();

    DrawToolbar();
    DrawHome();
    DrawNewProjectDialog();
    DrawExportDialog();
    DrawSettingsDialog();
    DrawMainMenu();
    DrawUpdateDialogs();
    HandleShortcuts();

    // Confirm modal
    if (s_confirmAction && !ImGui::IsPopupOpen("Confirm")) ImGui::OpenPopup("Confirm");
    ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar))
    {
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
        ImGui::TextUnformatted(s_confirmText.c_str());
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, theme::U(8)));
        if (ImGui::Button("Cancel", ImVec2(theme::U(100), 0))) { s_confirmAction = nullptr; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(theme::Danger));
        if (ImGui::Button("Discard", ImVec2(theme::U(100), 0))) { auto a = s_confirmAction; s_confirmAction = nullptr; ImGui::CloseCurrentPopup(); a(); }
        ImGui::PopStyleColor();
        ImGui::EndPopup();
    }

    // Toast
    if (!toast.empty() && ImGui::GetTime() - toastTime < 3.0)
    {
        ImDrawList* fg = ImGui::GetForegroundDrawList();
        ImFont* f = fonts::UI(fonts::UI_Medium);
        float fs = ImGui::GetFontSize();
        ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0, toast.c_str());
        ImVec2 c((canvasMin.x + canvasMax.x) * 0.5f, canvasMax.y - theme::U(90));
        ImVec2 a(c.x - ts.x * 0.5f - theme::U(14), c.y - ts.y * 0.5f - theme::U(9)), b(c.x + ts.x * 0.5f + theme::U(14), c.y + ts.y * 0.5f + theme::U(9));
        float alpha = (float)std::clamp((3.0 - (ImGui::GetTime() - toastTime)) * 3.0, 0.0, 1.0);
        fg->AddRectFilled(a, b, toastError ? IM_COL32(180, 50, 30, (int)(240 * alpha)) : IM_COL32(20, 20, 20, (int)(240 * alpha)), theme::U(8));
        fg->AddText(f, fs, ImVec2(a.x + theme::U(14), a.y + theme::U(9)), IM_COL32(255, 255, 255, (int)(255 * alpha)), toast.c_str());
    }

    Checkpoint();

    std::string shownName = !projectDir.empty() ? fs::u8path(projectDir).filename().u8string() : (doc.name.empty() ? "Untitled" : doc.name);
    std::string title = shownName + (modified ? " *" : "") + " - Fimgui";
    if (title != m_lastTitle) { platform::SetWindowTitle(title); m_lastTitle = title; }
}

void App::RunDeferred()
{
    auto jobs = std::move(deferred);
    deferred.clear();
    for (auto& j : jobs) j();
    // MCP tool calls run here: after rendering, so they can also render PNGs on the GPU.
    PumpMcp();
}

// ---------------------------------------------------------------- selection

std::vector<Node*> App::SelNodes()
{
    std::vector<Node*> out;
    for (auto& id : selection)
        if (Node* n = doc.Find(id)) out.push_back(n);
    return out;
}

bool App::IsSelected(const std::string& id) const
{
    return std::find(selection.begin(), selection.end(), id) != selection.end();
}

void App::Select(const std::string& id, bool additive)
{
    if (!additive) selection.clear();
    auto it = std::find(selection.begin(), selection.end(), id);
    if (it != selection.end()) { if (additive) selection.erase(it); }
    else if (!id.empty()) selection.push_back(id);
    if (Node* n = doc.Find(id)) RevealNode(n);
}

void App::SetSelection(const std::vector<std::string>& ids)
{
    selection.clear();
    for (auto& id : ids) if (doc.Find(id)) selection.push_back(id);
    for (auto* n : SelNodes()) RevealNode(n);
}

void App::RevealNode(Node* n)
{
    // Selecting something inside a conditional page switches the preview state to show it.
    for (Node* q = n; q; q = q->parent)
    {
        if (q->showVar.empty() || NodeConditionVisible(q, vars)) continue;
        if (!q->showNot) vars.num[q->showVar] = q->showValue;
        else if (std::fabs(vars.Get(q->showVar) - q->showValue) < 1e-4f) vars.num[q->showVar] = q->showValue == 0 ? 1.0f : 0.0f;
    }
    for (Node* q = n->parent; q; q = q->parent) q->expanded = true;
}

// ---------------------------------------------------------------- history

static void RestoreSnapshot(Document& doc, const std::string& snap)
{
    auto assets = std::move(doc.assets);
    std::string name = doc.name;
    json j = json::parse(snap, nullptr, false);
    if (j.is_discarded()) { doc.assets = std::move(assets); return; }
    doc.FromJson(j);
    for (auto& [id, a] : assets) doc.assets[id] = std::move(a);
}

void App::Checkpoint()
{
    if (!m_dirty || aiClient.Busy() || m_aiApplying) return;
    if (ImGui::IsMouseDown(0) || ImGui::IsAnyItemActive() || !editingText.empty()) return;
    std::string s = doc.ToJson(false).dump();
    m_dirty = false;
    if (s == m_snapshot) return;
    m_undo.push_back(m_snapshot);
    if (m_undo.size() > 300) m_undo.erase(m_undo.begin());
    m_snapshot = s;
    m_redo.clear();
}

void App::Undo()
{
    Checkpoint();
    if (m_undo.empty()) return;
    m_redo.push_back(m_snapshot);
    m_snapshot = m_undo.back();
    m_undo.pop_back();
    RestoreSnapshot(doc, m_snapshot);
    SetSelection(selection);
    modified = true;
}

void App::Redo()
{
    if (m_redo.empty()) return;
    m_undo.push_back(m_snapshot);
    m_snapshot = m_redo.back();
    m_redo.pop_back();
    RestoreSnapshot(doc, m_snapshot);
    SetSelection(selection);
    modified = true;
}

void App::ResetHistory()
{
    m_undo.clear();
    m_redo.clear();
    doc.ApplyLayout();
    m_snapshot = doc.ToJson(false).dump();
    m_dirty = false;
}

// ---------------------------------------------------------------- files

void App::NewDocumentConfirm(std::function<void()> action)
{
    if (!modified) { action(); return; }
    s_confirmText = "Discard unsaved changes?";
    s_confirmAction = action;
}

void App::NewDocument()
{
    auto act = [this] {
        doc.Clear();
        doc.name = "Untitled";
        filePath.clear();
        projectDir.clear();
        selection.clear();
        vars = VarStore();
        chat.clear();
        zoom = 1.0f;
        pan = ImVec2(theme::U(80), theme::U(80));
        ResetHistory();
        modified = false;
    };
    if (modified) { s_confirmText = "Discard unsaved changes?"; s_confirmAction = act; }
    else act();
}

void App::LoadFromJsonText(const std::string& text, const std::string& path)
{
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded() || !j.is_object() || !j.contains("nodes")) { Toast("Cannot open: not a Fimgui design", true); return; }
    std::string baseDir = fs::u8path(path).parent_path().u8string();
    bool isProject = j.contains("project");
    if (!isProject && j.contains("assets") && j["assets"].is_object())
        for (auto& a : j["assets"]) if (a.contains("file")) isProject = true;
    for (auto& [id, a] : doc.assets) gfx::InvalidateAsset(id);
    std::string err;
    doc.FromJson(j, &err, baseDir);
    if (!err.empty()) Toast(err, true);
    filePath = path;
    projectDir = isProject ? baseDir : std::string();
    if (isProject)
    {
        fonts::AddFontDir((fs::u8path(baseDir) / "fonts").u8string());
        ApplyProjectExportSettings(j["project"].is_object() ? j["project"].value("export", json()) : json());
    }
    AddRecent(path);
    showHome = false;
    if (doc.name.empty() || doc.name == "Untitled") doc.name = fs::u8path(path).stem().u8string();
    selection.clear();
    vars = VarStore();
    ResetHistory();
    modified = false;
    deferred.push_back([this] { ZoomToFit(false); });
    Toast("Opened " + (isProject ? fs::u8path(baseDir).filename().u8string() : fs::u8path(path).filename().u8string()));
}

void App::OpenDocument(const std::string& pathIn)
{
    auto act = [this, pathIn] {
        std::string path = pathIn.empty() ? platform::OpenFileDialog("Open design", "Fimgui design|*.fimgui;*.json|All files|*.*") : pathIn;
        if (path.empty()) return;
        std::string text;
        if (!platform::ReadText(path, text)) { Toast("Cannot read " + path, true); return; }
        LoadFromJsonText(text, path);
    };
    if (modified) { s_confirmText = "Discard unsaved changes?"; s_confirmAction = act; }
    else act();
}

void App::SaveDocument(bool saveAs)
{
    if (!saveAs && !projectDir.empty()) { SaveProject(); return; }
    if (!saveAs && filePath.empty())
    {
        // Untitled designs become projects in the workspace.
        newProjectFromCurrent = true;
        openNewProject = true;
        return;
    }
    std::string path = filePath;
    if (saveAs || path.empty())
    {
        path = platform::SaveFileDialog("Save design", "Fimgui design|*.fimgui", (doc.name + ".fimgui").c_str(), "fimgui");
        if (path.empty()) return;
    }
    if (!platform::WriteText(path, doc.ToJson(true).dump(1))) { Toast("Save failed", true); return; }
    filePath = path;
    projectDir.clear();   // a single-file copy embeds its assets
    AddRecent(path);
    if (doc.name == "Untitled") doc.name = fs::u8path(path).stem().u8string();
    modified = false;
    Toast("Saved");
}

// ---------------------------------------------------------------- settings (API key protected with DPAPI)

static std::string Protect(const std::string& s)
{
    if (s.empty()) return {};
    DATA_BLOB in{ (DWORD)s.size(), (BYTE*)s.data() }, out{};
    if (!CryptProtectData(&in, L"Fimgui", nullptr, nullptr, nullptr, 0, &out)) return {};
    std::string r = Base64Encode(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
}

static std::string Unprotect(const std::string& b64)
{
    if (b64.empty()) return {};
    std::vector<unsigned char> d = Base64Decode(b64);
    DATA_BLOB in{ (DWORD)d.size(), d.data() }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return {};
    std::string r((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
}

void App::SaveSettings()
{
    if (!persistSettings) return;
    json j;
    j["apiKey"] = Protect(ai.apiKey);
    j["model"] = ai.model;
    j["effort"] = ai.effort;
    j["baseUrl"] = ai.baseUrl;
    j["sendSnapshot"] = ai.sendSnapshot;
    j["mcpEnabled"] = mcpEnabled;
    j["pixelGrid"] = pixelGrid;
    j["workspace"] = workspace;
    j["showHomeOnStart"] = showHomeOnStart;
    j["checkUpdates"] = checkUpdates;
    j["uiScale"] = uiScale;
    j["skippedVersion"] = skippedVersion;
    j["lastSeenVersion"] = lastSeenVersion;
    j["recentFiles"] = recentFiles;
    j["snapToPixel"] = snapToPixel;
    j["mcpPort"] = mcpPort;
    j["exportDir"] = exportOpt.outDir;
    j["exportNs"] = exportOpt.ns;
    j["exportBase"] = exportOpt.fileBase;
    j["embedFonts"] = exportOpt.embedFonts;
    j["writeExample"] = exportOpt.writeExample;
    j["iconScale"] = exportOpt.iconScale;
    j["exportTarget"] = exportTarget;
    j["javaDir"] = javaOpt.outDir;
    j["javaLoader"] = javaOpt.loader;
    j["javaModId"] = javaOpt.modId;
    j["javaPackage"] = javaOpt.package;
    j["javaTextureScale"] = javaOpt.textureScale;
    j["javaTtf"] = javaOpt.ttfFonts;
    j["leftW"] = leftW / theme::dpi;
    j["rightW"] = rightW / theme::dpi;
    platform::WriteText(platform::AppDataDir() + "/settings.json", j.dump(1));
}

void App::LoadSettings()
{
    std::string text;
    if (!platform::ReadText(platform::AppDataDir() + "/settings.json", text)) return;
    json j = json::parse(text, nullptr, false);
    if (j.is_discarded()) return;
    ai.apiKey = Unprotect(j.value("apiKey", std::string()));
    ai.model = j.value("model", ai.model);
    ai.effort = j.value("effort", ai.effort);
    ai.baseUrl = j.value("baseUrl", ai.baseUrl);
    ai.sendSnapshot = j.value("sendSnapshot", ai.sendSnapshot);
    mcpEnabled = j.value("mcpEnabled", mcpEnabled);
    pixelGrid = j.value("pixelGrid", pixelGrid);
    workspace = j.value("workspace", workspace);
    showHomeOnStart = j.value("showHomeOnStart", showHomeOnStart);
    checkUpdates = j.value("checkUpdates", checkUpdates);
    uiScale = std::clamp(j.value("uiScale", uiScale), 0.8f, 2.0f);
    skippedVersion = j.value("skippedVersion", skippedVersion);
    lastSeenVersion = j.value("lastSeenVersion", lastSeenVersion);
    if (j.contains("recentFiles") && j["recentFiles"].is_array())
        for (auto& r : j["recentFiles"]) if (r.is_string()) recentFiles.push_back(r.get<std::string>());
    snapToPixel = j.value("snapToPixel", snapToPixel);
    mcpPort = std::clamp(j.value("mcpPort", mcpPort), 1024, 65535);
    exportOpt.outDir = j.value("exportDir", exportOpt.outDir);
    exportOpt.ns = j.value("exportNs", exportOpt.ns);
    exportOpt.fileBase = j.value("exportBase", exportOpt.fileBase);
    exportOpt.embedFonts = j.value("embedFonts", exportOpt.embedFonts);
    exportOpt.writeExample = j.value("writeExample", exportOpt.writeExample);
    exportOpt.iconScale = j.value("iconScale", exportOpt.iconScale);
    exportTarget = j.value("exportTarget", exportTarget);
    javaOpt.outDir = j.value("javaDir", javaOpt.outDir);
    javaOpt.loader = j.value("javaLoader", javaOpt.loader);
    javaOpt.modId = j.value("javaModId", javaOpt.modId);
    javaOpt.package = j.value("javaPackage", javaOpt.package);
    javaOpt.textureScale = j.value("javaTextureScale", javaOpt.textureScale);
    javaOpt.ttfFonts = j.value("javaTtf", javaOpt.ttfFonts);
    leftW = j.value("leftW", 0.0f) * theme::dpi;
    rightW = j.value("rightW", 0.0f) * theme::dpi;
}

// ---------------------------------------------------------------- node creation helpers

std::string App::UniqueName(const std::string& base)
{
    int maxN = 0;
    doc.ForEach([&](Node* n) {
        if (n->name.rfind(base + " ", 0) == 0)
        {
            try { maxN = std::max(maxN, std::stoi(n->name.substr(base.size() + 1))); } catch (...) {}
        }
    });
    return base + " " + std::to_string(maxN + 1);
}

static float Luma(ImVec4 c) { return 0.299f * c.x + 0.587f * c.y + 0.114f * c.z; }

Node* App::CreateNode(NodeType t, ImVec2 world, ImVec2 size, Node* parent)
{
    auto n = std::make_unique<Node>();
    n->type = t;
    n->id = doc.NewId();
    ImVec2 base = parent ? parent->AbsPos() : ImVec2(0, 0);
    n->x = std::round(world.x - base.x);
    n->y = std::round(world.y - base.y);
    n->w = std::round(size.x);
    n->h = std::round(size.y);
    // Pick a default color that contrasts with the parent background.
    bool darkBg = true;
    for (Node* q = parent; q; q = q->parent)
        if (q->fill.type) { darkBg = Luma(q->fill.c0) < 0.5f; break; }
    switch (t)
    {
    case NodeType::Frame:
        n->name = UniqueName("Frame");
        n->fill = Paint{ 1, parent ? ImVec4(1, 1, 1, 0) : ImVec4(1, 1, 1, 1) };
        if (parent) n->fill.type = 0;
        break;
    case NodeType::Rect: n->name = UniqueName("Rectangle"); n->fill = Paint{ 1, ImVec4(0.85f, 0.85f, 0.85f, 1) }; break;
    case NodeType::Ellipse: n->name = UniqueName("Ellipse"); n->fill = Paint{ 1, ImVec4(0.85f, 0.85f, 0.85f, 1) }; break;
    case NodeType::Line:
        n->name = UniqueName("Line"); n->fill.type = 0; n->h = 0;
        n->stroke = Paint{ 1, darkBg && parent ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1) };
        break;
    case NodeType::Text:
        n->name = "Text";
        n->text = "";
        n->fill = Paint{ 1, darkBg && parent ? ImVec4(1, 1, 1, 1) : ImVec4(0, 0, 0, 1) };
        if (!parent) n->fill.c0 = ImVec4(1, 1, 1, 1);
        n->textSizing = 1;
        break;
    case NodeType::Group: n->name = UniqueName("Group"); n->fill.type = 0; n->clip = false; break;
    default: break;
    }
    Node* raw = n.get();
    doc.Insert(std::move(n), parent);
    MarkDirty();
    return raw;
}

Node* App::ContainerAt(ImVec2 w, Node* exclude)
{
    Node* best = nullptr;
    std::function<void(std::vector<std::unique_ptr<Node>>&)> rec = [&](std::vector<std::unique_ptr<Node>>& list) {
        for (int i = (int)list.size() - 1; i >= 0; i--)
        {
            Node* n = list[i].get();
            if (n == exclude || !n->visible || n->locked) continue;
            if (!ignoreConditions && !NodeConditionVisible(n, vars)) continue;
            if (n->type != NodeType::Frame) continue;
            ImVec2 a, b;
            NodeWorldRect(n, &vars, &a, &b);
            if (w.x >= a.x && w.x <= b.x && w.y >= a.y && w.y <= b.y)
            {
                best = n;
                rec(n->children);
                return;
            }
        }
    };
    rec(doc.roots);
    return best;
}

// ---------------------------------------------------------------- edit commands

void App::DeleteSelection()
{
    for (Node* n : SelNodes())
    {
        if (n->id == editingText) editingText.clear();
        doc.Detach(n);
    }
    selection.clear();
    MarkDirty();
}

void App::DuplicateSelection()
{
    std::vector<std::string> ids;
    for (Node* n : SelNodes())
    {
        auto c = doc.CloneNode(n, true);
        bool inLayout = n->parent && n->parent->layout;
        if (!inLayout) { c->x += 10; c->y += 10; }
        std::string id = c->id;
        doc.Insert(std::move(c), n->parent, doc.IndexOf(n) + 1);
        ids.push_back(id);
    }
    SetSelection(ids);
    MarkDirty();
}

static json ClipboardJson(Document& doc, const std::vector<Node*>& nodes)
{
    json j;
    j["fimgui-clipboard"] = 1;
    json arr = json::array();
    std::set<std::string> assets;
    for (Node* n : nodes)
    {
        json nj = NodeToJson(n);
        ImVec2 p = n->AbsPos();
        nj["x"] = p.x;
        nj["y"] = p.y;
        arr.push_back(nj);
        std::function<void(Node*)> collect = [&](Node* q) { if (!q->asset.empty()) assets.insert(q->asset); for (auto& c : q->children) collect(c.get()); };
        collect(n);
    }
    j["nodes"] = arr;
    json a = json::object();
    for (auto& id : assets)
    {
        auto it = doc.assets.find(id);
        if (it == doc.assets.end()) continue;
        const Asset& as = it->second;
        a[id] = { { "name", as.name }, { "kind", AssetKindName(as.kind) }, { "width", as.width }, { "height", as.height },
                  { "data", as.kind == 1 ? std::string(as.data.begin(), as.data.end()) : Base64Encode(as.data.data(), as.data.size()) } };
    }
    j["assets"] = a;
    return j;
}

void App::CopySelection()
{
    auto nodes = SelNodes();
    if (nodes.empty()) return;
    platform::SetClipboardText(ClipboardJson(doc, nodes).dump());
}

void App::CutSelection()
{
    CopySelection();
    DeleteSelection();
}

void App::PasteNodes(const json& j)
{
    if (j.contains("assets") && j["assets"].is_object())
        for (auto it = j["assets"].begin(); it != j["assets"].end(); ++it)
        {
            if (doc.assets.count(it.key())) continue;
            Asset as;
            as.id = it.key();
            as.name = it.value().value("name", as.id);
            as.kind = AssetKindFromName(it.value().value("kind", std::string()));
            as.width = it.value().value("width", 0);
            as.height = it.value().value("height", 0);
            std::string d = it.value().value("data", std::string());
            if (as.kind == 1) as.data.assign(d.begin(), d.end()); else as.data = Base64Decode(d);
            doc.assets[as.id] = std::move(as);
        }
    Node* target = nullptr;
    if (Node* s = Sel())
    {
        if (s->type == NodeType::Frame && selection.size() == 1) target = s;
        else target = s->parent;
    }
    std::vector<std::string> ids;
    ImVec2 base = target ? target->AbsPos() : ImVec2(0, 0);
    for (auto& nj : j["nodes"])
    {
        json c = nj;
        std::function<void(json&)> strip = [&](json& e) { e["id"] = ""; if (e.contains("children")) for (auto& ch : e["children"]) strip(ch); };
        strip(c);
        auto n = NodeFromJson(c, &doc);
        n->x -= base.x;
        n->y -= base.y;
        if (target && (n->x > target->w || n->y > target->h || n->x + n->w < 0 || n->y + n->h < 0))
        {
            n->x = std::round((target->w - n->w) * 0.5f);
            n->y = std::round((target->h - n->h) * 0.5f);
        }
        // Offset when pasting directly on top of an identical existing node.
        for (auto& s : (target ? target->children : doc.roots))
            if (s->x == n->x && s->y == n->y && s->w == n->w && s->h == n->h && !(target && target->layout)) { n->x += 10; n->y += 10; break; }
        ids.push_back(n->id);
        doc.Insert(std::move(n), target);
    }
    SetSelection(ids);
    MarkDirty();
}

void App::PasteText(const std::string& textIn)
{
    std::string text = textIn;
    size_t a = text.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return;
    text = text.substr(a);
    Node* target = nullptr;
    if (Node* s = Sel()) target = s->type == NodeType::Frame ? s : s->parent;
    ImVec2 center = ScreenToWorld(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
    ImVec2 local = target ? ImVec2(std::max(0.0f, std::round(target->w * 0.5f - 12)), std::max(0.0f, std::round(target->h * 0.5f - 12))) : center;

    if (text[0] == '{')
    {
        json j = json::parse(text, nullptr, false);
        if (!j.is_discarded() && j.contains("fimgui-clipboard")) { PasteNodes(j); return; }
        if (!j.is_discarded() && j.contains("nodes") && j.contains("format")) { j["fimgui-clipboard"] = 1; PasteNodes(j); return; }
        // One or more AI operations
        if (text.rfind("{\"op\"", 0) == 0)
        {
            int count = 0;
            size_t p = 0;
            while (p < text.size())
            {
                size_t nl = text.find('\n', p);
                std::string line = text.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
                json op = json::parse(line, nullptr, false);
                std::string err;
                if (!op.is_discarded() && ApplyAiOp(op, &err)) count++;
                if (nl == std::string::npos) break;
                p = nl + 1;
            }
            Toast("Applied " + std::to_string(count) + " operations");
            MarkDirty();
            return;
        }
    }
    if (text.rfind("<svg", 0) == 0 || (text.rfind("<?xml", 0) == 0 && text.find("<svg") != std::string::npos))
    {
        if (Node* n = ImportSVG(doc, text, "icon", target, local)) { Select(n->id); MarkDirty(); Toast("Pasted SVG"); }
        else Toast("Invalid SVG", true);
        return;
    }
    if (LooksLikeCSS(text))
    {
        auto sel = SelNodes();
        if (!sel.empty())
        {
            int total = 0;
            for (Node* n : sel) total += ApplyCSS(n, text);
            MarkDirty();
            Toast("Applied " + std::to_string(total) + " CSS properties");
            return;
        }
        bool isText = text.find("font-") != std::string::npos;
        Node* n = CreateNode(isText ? NodeType::Text : NodeType::Rect, target ? ImVec2(target->AbsPos().x + local.x, target->AbsPos().y + local.y) : center, ImVec2(100, 100), target);
        if (isText) n->text = "Text";
        ImVec2 keep(n->x, n->y);
        ApplyCSS(n, text);
        n->x = keep.x; n->y = keep.y;   // CSS left/top are relative to the source layout
        Select(n->id);
        Toast("Created layer from CSS");
        return;
    }
    Node* n = CreateNode(NodeType::Text, target ? ImVec2(target->AbsPos().x + local.x, target->AbsPos().y + local.y) : center, ImVec2(10, 10), target);
    n->text = textIn;
    Select(n->id);
}

void App::Paste()
{
    auto files = platform::GetClipboardFiles();
    if (!files.empty()) { OnDropFiles(files); return; }
    std::string text = platform::GetClipboardText();
    bool textIsOurs = !text.empty() && (text[0] == '{' || text.find("<svg") != std::string::npos || LooksLikeCSS(text));
    if (!textIsOurs && platform::ClipboardHasImage())
    {
        std::vector<unsigned char> png;
        if (platform::GetClipboardImage(png))
        {
            Node* target = nullptr;
            if (Node* s = Sel()) target = s->type == NodeType::Frame ? s : s->parent;
            ImVec2 pos = target ? ImVec2(0, 0) : ScreenToWorld(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
            if (Node* n = ImportImage(doc, png, "image", target, pos)) { Select(n->id); MarkDirty(); Toast("Pasted image"); }
            return;
        }
    }
    if (!text.empty()) PasteText(text);
}

void App::GroupSelection(bool asFrame)
{
    auto nodes = SelNodes();
    if (nodes.empty()) return;
    Node* parent = nodes[0]->parent;
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [&](Node* n) { return n->parent != parent; }), nodes.end());
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    int maxIndex = 0;
    for (Node* n : nodes)
    {
        x0 = std::min(x0, n->x); y0 = std::min(y0, n->y);
        x1 = std::max(x1, n->x + n->w); y1 = std::max(y1, n->y + n->h);
        maxIndex = std::max(maxIndex, doc.IndexOf(n));
    }
    auto g = std::make_unique<Node>();
    g->type = asFrame ? NodeType::Frame : NodeType::Group;
    g->id = doc.NewId();
    g->name = UniqueName(asFrame ? "Frame" : "Group");
    g->x = x0; g->y = y0; g->w = x1 - x0; g->h = y1 - y0;
    g->fill.type = 0;
    g->clip = false;
    // Keep original order
    std::sort(nodes.begin(), nodes.end(), [&](Node* a, Node* b) { return doc.IndexOf(a) < doc.IndexOf(b); });
    int insertAt = maxIndex - (int)nodes.size() + 1;
    for (Node* n : nodes)
    {
        auto u = doc.Detach(n);
        u->x -= x0; u->y -= y0;
        u->parent = g.get();
        g->children.push_back(std::move(u));
    }
    std::string id = g->id;
    doc.Insert(std::move(g), parent, std::max(0, insertAt));
    Select(id);
    MarkDirty();
}

void App::Ungroup()
{
    std::vector<std::string> ids;
    for (Node* g : SelNodes())
    {
        if (!g->IsContainer() || g->children.empty()) continue;
        Node* parent = g->parent;
        int idx = doc.IndexOf(g);
        while (!g->children.empty())
        {
            std::unique_ptr<Node> c = std::move(g->children.front());
            g->children.erase(g->children.begin());
            c->x += g->x; c->y += g->y;
            ids.push_back(c->id);
            doc.Insert(std::move(c), parent, idx++);
        }
        doc.Detach(g);
    }
    SetSelection(ids);
    MarkDirty();
}

void App::ReorderSelection(int delta)
{
    auto nodes = SelNodes();
    for (Node* n : nodes)
    {
        auto& sib = doc.SiblingsOf(n);
        int i = doc.IndexOf(n);
        int j = delta == 100 ? (int)sib.size() - 1 : delta == -100 ? 0 : std::clamp(i + delta, 0, (int)sib.size() - 1);
        if (i == j) continue;
        Node* parent = n->parent;
        auto u = doc.Detach(n);
        doc.Insert(std::move(u), parent, j);
    }
    MarkDirty();
}

void App::AlignSelection(int mode)
{
    auto nodes = SelNodes();
    if (nodes.empty()) return;
    float x0, y0, x1, y1;
    if (nodes.size() == 1)
    {
        Node* n = nodes[0];
        if (!n->parent) return;
        x0 = 0; y0 = 0; x1 = n->parent->w; y1 = n->parent->h;
    }
    else
    {
        x0 = y0 = 1e9f; x1 = y1 = -1e9f;
        for (Node* n : nodes)
        {
            ImVec2 p = n->AbsPos();
            x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x + n->w); y1 = std::max(y1, p.y + n->h);
        }
    }
    for (Node* n : nodes)
    {
        ImVec2 base = nodes.size() == 1 ? ImVec2(0, 0) : (n->parent ? n->parent->AbsPos() : ImVec2(0, 0));
        switch (mode)
        {
        case 0: n->x = x0 - base.x; break;
        case 1: n->x = std::round((x0 + x1 - n->w) * 0.5f - base.x); break;
        case 2: n->x = x1 - n->w - base.x; break;
        case 3: n->y = y0 - base.y; break;
        case 4: n->y = std::round((y0 + y1 - n->h) * 0.5f - base.y); break;
        case 5: n->y = y1 - n->h - base.y; break;
        }
    }
    MarkDirty();
}

void App::NudgeSelection(float dx, float dy)
{
    for (Node* n : SelNodes())
        if (!n->locked) { n->x += dx; n->y += dy; }
    MarkDirty();
}

void App::ImportImageFile(const std::string& path, Node* parent, ImVec2 localPos)
{
    std::string ext = fs::u8path(path).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string name = fs::u8path(path).stem().u8string();
    Node* n = nullptr;
    if (ext == ".svg")
    {
        std::string svg;
        if (platform::ReadText(path, svg)) n = ImportSVG(doc, svg, name, parent, localPos);
    }
    else if (ext == ".mp4" || ext == ".mov" || ext == ".wmv" || ext == ".m4v" || ext == ".avi")
    {
        std::vector<unsigned char> bytes;
        if (platform::ReadFile(path, bytes)) n = ImportVideo(doc, bytes, name, parent, localPos);
    }
    else
    {
        std::vector<unsigned char> bytes;
        if (platform::ReadFile(path, bytes)) n = ImportImage(doc, bytes, name, parent, localPos);
    }
    if (n) { Select(n->id); MarkDirty(); }
    else Toast("Cannot import " + fs::u8path(path).filename().u8string(), true);
}

void App::OnDropFiles(const std::vector<std::string>& files)
{
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool overCanvas = mouse.x >= canvasMin.x && mouse.x <= canvasMax.x && mouse.y >= canvasMin.y && mouse.y <= canvasMax.y;
    ImVec2 world = overCanvas ? ScreenToWorld(mouse) : ScreenToWorld(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
    float offset = 0;
    for (auto& f : files)
    {
        std::string ext = fs::u8path(f).extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".fimgui" || ext == ".json") { OpenDocument(f); return; }
        if (ext == ".ttf" || ext == ".otf")
        {
            std::string fam;
            if (fonts::AddFontFile(f, &fam)) Toast("Font added: " + fam); else Toast("Unsupported font", true);
            continue;
        }
        if (rightTab == Tab_AI && ImGui::GetIO().MousePos.x > canvasMax.x && (ext == ".png" || ext == ".jpg" || ext == ".jpeg"))
        {
            std::vector<unsigned char> bytes;
            if (platform::ReadFile(f, bytes)) aiAttachments.push_back(bytes);
            continue;
        }
        Node* parent = ContainerAt(world);
        ImVec2 base = parent ? parent->AbsPos() : ImVec2(0, 0);
        ImportImageFile(f, parent, ImVec2(world.x - base.x + offset, world.y - base.y + offset));
        offset += 20;
    }
}

// ---------------------------------------------------------------- copy as / export

static void ExpandForEffects(Node* n, ImVec2& a, ImVec2& b)
{
    if (n->effect.type == 1)
    {
        float e = n->effect.blur + std::max(0.0f, n->effect.spread);
        a.x = std::min(a.x, a.x + n->effect.offset.x - e); a.y = std::min(a.y, a.y + n->effect.offset.y - e);
        b.x = std::max(b.x, b.x + n->effect.offset.x + e); b.y = std::max(b.y, b.y + n->effect.offset.y + e);
    }
}

bool App::RenderRegionPNG(ImVec2 wa, ImVec2 wb, float scale, std::vector<unsigned char>& png)
{
    int w = (int)std::ceil((wb.x - wa.x) * scale), h = (int)std::ceil((wb.y - wa.y) * scale);
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> rgba;
    bool ok = gfx::RenderToRGBA(w, h, [&](ImDrawList* dl) {
        RenderCtx ctx;
        ctx.dl = dl;
        ctx.zoom = scale;
        ctx.origin = ImVec2(-wa.x * scale, -wa.y * scale);
        ctx.vars = &vars;
        ctx.doc = &doc;
        ctx.ignoreConditions = false;
        RenderDocument(ctx);
    }, rgba);
    if (!ok) return false;
    png = gfx::EncodePNG(rgba.data(), w, h);
    return !png.empty();
}

bool App::RenderNodePNG(Node* n, float scale, std::vector<unsigned char>& png, int* outW, int* outH, ImVec2* worldMin)
{
    ImVec2 a, b;
    NodeWorldRect(n, &vars, &a, &b);
    if (n->type == NodeType::Line) { a.y -= n->strokeWidth * 0.5f; b.y = a.y + std::max(1.0f, n->strokeWidth); }
    ExpandForEffects(n, a, b);
    a.x = std::floor(a.x); a.y = std::floor(a.y);
    b.x = std::ceil(b.x); b.y = std::ceil(b.y);
    int w = (int)std::ceil((b.x - a.x) * scale), h = (int)std::ceil((b.y - a.y) * scale);
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> rgba;
    ImVec2 parentWorld = n->parent ? n->parent->AbsPos() : ImVec2(0, 0);
    bool ok = gfx::RenderToRGBA(w, h, [&](ImDrawList* dl) {
        RenderCtx ctx;
        ctx.dl = dl;
        ctx.zoom = scale;
        ctx.origin = ImVec2(-a.x * scale, -a.y * scale);
        ctx.vars = &vars;
        ctx.doc = &doc;
        ctx.ignoreConditions = true;
        RenderNode(ctx, n, parentWorld, 1.0f);
    }, rgba);
    if (!ok) return false;
    png = gfx::EncodePNG(rgba.data(), w, h);
    if (outW) *outW = w;
    if (outH) *outH = h;
    if (worldMin) *worldMin = a;
    return true;
}

bool App::BakeLayerPNG(Node* n, int variant, float scale, std::vector<unsigned char>& png, int* w, int* h, ImVec2* offset)
{
    // Temporarily strip the layer down to its own appearance.
    std::vector<bool> childVis;
    for (auto& c : n->children) { childVis.push_back(c->visible); c->visible = false; }
    Paint fill = n->fill;
    float width = n->w, opacity = n->opacity;
    AnimFx anim = n->anim;
    n->opacity = 1.0f;
    n->anim.preset = 0;
    if (variant == 1) n->fill = Paint{ 1, n->hoverColor };
    if (variant == 2) n->fill = Paint{ 1, n->activeColor };
    if (variant == 3) if (Node* owner = SliderOwner(n)) n->w = owner->w;
    ImVec2 a, b, wmin;
    NodeWorldRect(n, &vars, &a, &b);
    bool ok = RenderNodePNG(n, scale, png, w, h, &wmin);
    if (variant == 3) a.x = n->parent ? n->parent->AbsPos().x + n->x : a.x;
    for (size_t i = 0; i < childVis.size(); i++) n->children[i]->visible = childVis[i];
    n->fill = fill;
    n->w = width;
    n->opacity = opacity;
    n->anim = anim;
    if (ok && offset) *offset = ImVec2(wmin.x - a.x, wmin.y - a.y);
    return ok;
}

ExportResult App::RunJavaExport()
{
    if (javaOpt.outDir.empty()) javaOpt.outDir = exportOpt.outDir.empty() ? std::string() : (fs::u8path(exportOpt.outDir).parent_path() / "minecraft").u8string();
    return ExportJava(doc, javaOpt, platform::ExeDir() + "/runtime",
                      [this](Node* n, int variant, float scale, std::vector<unsigned char>& png, int* w, int* h, ImVec2* off) { return BakeLayerPNG(n, variant, scale, png, w, h, off); });
}

void App::CopyAs(int format)
{
    auto nodes = SelNodes();
    if (nodes.empty()) { Toast("Select a layer first", true); return; }
    Node* n = nodes[0];
    switch (format)
    {
    case 0:
    {
        std::string css;
        for (Node* q : nodes) css += NodeToCSS(doc, q, false) + (nodes.size() > 1 ? "\n" : "");
        platform::SetClipboardText(css);
        Toast("Copied CSS");
        break;
    }
    case 1: platform::SetClipboardText(NodeToSVG(doc, n)); Toast("Copied SVG"); break;
    case 2:
    {
        std::string id = n->id;
        deferred.push_back([this, id] {
            Node* q = doc.Find(id);
            std::vector<unsigned char> png;
            if (!q || !RenderNodePNG(q, 2.0f, png)) { Toast("PNG render failed", true); return; }
            std::vector<unsigned char> rgba;
            int w, h;
            gfx::DecodeImage(png.data(), png.size(), rgba, &w, &h);
            platform::SetClipboardImage(rgba.data(), w, h);
            Toast("Copied PNG (2x)");
        });
        break;
    }
    case 3: platform::SetClipboardText(GenerateSnippet(doc, n)); Toast("Copied ImGui code"); break;
    case 4: platform::SetClipboardText(ClipboardJson(doc, nodes).dump(1)); Toast("Copied JSON"); break;
    }
}

void App::ExportSelectionFile(int format, float scale)
{
    Node* n = Sel();
    if (!n) return;
    std::string name = SanitizeIdent(n->name, "layer");
    if (format == 1)
    {
        std::string path = platform::SaveFileDialog("Export SVG", "SVG|*.svg", (name + ".svg").c_str(), "svg");
        if (!path.empty() && platform::WriteText(path, NodeToSVG(doc, n))) Toast("Exported " + fs::u8path(path).filename().u8string());
        return;
    }
    std::string suffix = scale == 1.0f ? "" : "@" + std::to_string((int)scale) + "x";
    std::string path = platform::SaveFileDialog("Export PNG", "PNG|*.png", (name + suffix + ".png").c_str(), "png");
    if (path.empty()) return;
    std::string id = n->id;
    deferred.push_back([this, id, path, scale] {
        Node* q = doc.Find(id);
        std::vector<unsigned char> png;
        if (q && RenderNodePNG(q, scale, png) && platform::WriteFile(path, png.data(), png.size())) Toast("Exported " + fs::u8path(path).filename().u8string());
        else Toast("Export failed", true);
    });
}

// ---------------------------------------------------------------- view

void App::SetZoom(float z, ImVec2 anchor)
{
    z = std::clamp(z, 0.02f, 256.0f);
    ImVec2 w = ScreenToWorld(anchor);
    zoom = z;
    pan = ImVec2(anchor.x - canvasMin.x - w.x * zoom, anchor.y - canvasMin.y - w.y * zoom);
}

void App::ZoomToFit(bool selectionOnly)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    auto add = [&](Node* n) {
        ImVec2 a, b;
        NodeWorldRect(n, &vars, &a, &b);
        x0 = std::min(x0, a.x); y0 = std::min(y0, a.y); x1 = std::max(x1, b.x); y1 = std::max(y1, b.y);
    };
    if (selectionOnly) for (Node* n : SelNodes()) add(n);
    else for (auto& n : doc.roots) add(n.get());
    if (x0 > x1) { zoom = 1; pan = ImVec2(theme::U(80), theme::U(80)); return; }
    float cw = canvasMax.x - canvasMin.x, ch = canvasMax.y - canvasMin.y;
    if (cw <= 0 || ch <= 0) return;
    float bw = std::max(1.0f, x1 - x0), bh = std::max(1.0f, y1 - y0);
    zoom = std::clamp(std::min(cw / bw, ch / bh) * 0.85f, 0.02f, selectionOnly ? 64.0f : 4.0f);
    pan = ImVec2(cw * 0.5f - (x0 + bw * 0.5f) * zoom, ch * 0.5f - (y0 + bh * 0.5f) * zoom);
}
