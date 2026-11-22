#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "exporters.h"
#include "platform.h"
#include "gfx.h"
#include "../runtime/fimgui_runtime.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;
using namespace theme;

static Icon TypeIcon(const Node* n)
{
    switch (n->type)
    {
    case NodeType::Frame: return n->layout == 1 ? Icon::AlignCenterV : n->layout == 2 ? Icon::AlignCenterH : Icon::Frame;
    case NodeType::Group: return Icon::Group;
    case NodeType::Rect: return Icon::Rect;
    case NodeType::Ellipse: return Icon::Ellipse;
    case NodeType::Line: return Icon::Line;
    case NodeType::Text: return Icon::Text;
    case NodeType::Image: return Icon::Image;
    case NodeType::Vector: return Icon::Vector;
    }
    return Icon::Rect;
}

// ---------------------------------------------------------------- left panel (layers)

struct LayerDrop { std::string target; int where = 0; };   // where: -1 above, 0 inside, 1 below

static void LayerRow(App* app, Node* n, int depth, LayerDrop& drop)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = U(28);
    float w = ImGui::GetContentRegionAvail().x;
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool sel = app->IsSelected(n->id);
    bool parentSel = false;
    for (Node* q = n->parent; q; q = q->parent) if (app->IsSelected(q->id)) parentSel = true;
    bool hiddenByCond = !NodeConditionVisible(n, app->vars);

    ImGui::PushID(n->id.c_str());
    ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    if (ImGui::IsItemClicked(0))
    {
        if (io.KeyShift || io.KeyCtrl) app->Select(n->id, true);
        else app->Select(n->id);
    }
    if (ImGui::IsItemClicked(1)) { if (!sel) app->Select(n->id); app->openContextMenu = true; }
    if (hov && ImGui::IsMouseDoubleClicked(0))
    {
        app->renamingId = n->id;
        snprintf(app->renameBuf, sizeof(app->renameBuf), "%s", n->name.c_str());
    }
    // Drag & drop
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("FIM_NODE", n->id.c_str(), n->id.size() + 1);
        ImGui::TextUnformatted(n->name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget())
    {
        float my = io.MousePos.y - p.y;
        int where = my < h * 0.28f ? -1 : my > h * 0.72f ? 1 : (n->IsContainer() ? 0 : (my < h * 0.5f ? -1 : 1));
        float ly = where < 0 ? p.y : where > 0 ? p.y + h : 0;
        if (where != 0) dl->AddLine(ImVec2(p.x + U(8) + depth * U(14), ly), ImVec2(p.x + w, ly), Accent, 2.0f);
        else dl->AddRect(p, ImVec2(p.x + w, p.y + h), Accent, U(4), 0, 2.0f);
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("FIM_NODE"))
        {
            drop.target = n->id;
            drop.where = where;
            std::string src((const char*)pl->Data);
            if (!app->IsSelected(src)) app->Select(src);
        }
        ImGui::EndDragDropTarget();
    }

    if (sel && app->scrollLayersToSelection)
    {
        // Bring the selected row into view when the selection changed elsewhere (canvas, AI).
        float wy = ImGui::GetWindowPos().y, wh = ImGui::GetWindowHeight();
        if (p.y < wy || p.y + h > wy + wh) ImGui::SetScrollHereY(0.3f);
        app->scrollLayersToSelection = false;
    }
    if (sel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(13, 153, 255, 70));
    else if (parentSel) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(13, 153, 255, 22));
    else if (hov) dl->AddRect(ImVec2(p.x + 1, p.y), ImVec2(p.x + w - 1, p.y + h), IM_COL32(13, 153, 255, 160), 0, 0, 1.0f);

    float x = p.x + U(8) + depth * U(14);
    float cy = p.y + h * 0.5f;
    // Expand chevron
    if (!n->children.empty())
    {
        ImGui::SetCursorScreenPos(ImVec2(x - U(2), p.y + U(6)));
        if (ImGui::InvisibleButton("##exp", ImVec2(U(14), U(16)))) n->expanded = !n->expanded;
        DrawIcon(dl, n->expanded ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(x + U(5), cy), U(10), TextDim);
    }
    x += U(14);
    ImU32 iconCol = (n->type == NodeType::Frame && !n->parent) ? Text : TextDim;
    if (n->role != Role::None) iconCol = Purple;
    DrawIcon(dl, TypeIcon(n), ImVec2(x + U(6), cy), U(12), iconCol);
    x += U(20);

    float alpha = (n->visible && !hiddenByCond) ? 1.0f : 0.45f;
    if (app->renamingId == n->id)
    {
        ImGui::SetCursorScreenPos(ImVec2(x - U(4), p.y + U(2)));
        ImGui::SetNextItemWidth(w - (x - p.x) - U(8));
        if (ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        bool done = ImGui::InputText("##rename", app->renameBuf, sizeof(app->renameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        if (done || ImGui::IsItemDeactivated())
        {
            if (app->renameBuf[0]) { n->name = app->renameBuf; app->MarkDirty(); }
            app->renamingId.clear();
        }
    }
    else
    {
        ImFont* f = (n->type == NodeType::Frame && !n->parent) ? fonts::UI(fonts::UI_Medium) : fonts::UI(fonts::UI_Regular);
        float fs = ImGui::GetFontSize();
        std::string label = n->name.empty() ? NodeTypeName(n->type) : n->name;
        dl->PushClipRect(ImVec2(x, p.y), ImVec2(p.x + w - U(48), p.y + h), true);
        dl->AddText(f, fs, ImVec2(x, cy - fs * 0.6f), ImGui::ColorConvertFloat4ToU32(ImVec4(1, 1, 1, 0.9f * alpha)), label.c_str());
        dl->PopClipRect();
        if (n->role != Role::None && !n->var.empty())
        {
            std::string tag = n->var;
            ImVec2 ts = fonts::UI(fonts::UI_Regular)->CalcTextSizeA(fs * 0.85f, FLT_MAX, 0, tag.c_str());
            float tx = p.x + w - U(50) - ts.x;
            if (tx > x + U(40)) dl->AddText(fonts::UI(fonts::UI_Regular), fs * 0.85f, ImVec2(tx, cy - fs * 0.5f), IM_COL32(151, 71, 255, 200), tag.c_str());
        }
    }

    // Visibility / lock toggles (shown on hover or when active)
    float bx = p.x + w - U(44);
    bool showEye = hov || !n->visible, showLock = hov || n->locked;
    ImGui::SetCursorScreenPos(ImVec2(bx, p.y + U(4)));
    if (showLock) { if (IconButton("##lock", n->locked ? Icon::Lock : Icon::Unlock, false, U(20))) { n->locked = !n->locked; app->MarkDirty(); } }
    ImGui::SetCursorScreenPos(ImVec2(bx + U(20), p.y + U(4)));
    if (showEye) { if (IconButton("##eye", n->visible ? Icon::Eye : Icon::EyeOff, false, U(20))) { n->visible = !n->visible; app->MarkDirty(); } }

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
    ImGui::PopID();

    if (n->expanded)
        for (int i = (int)n->children.size() - 1; i >= 0; i--) LayerRow(app, n->children[i].get(), depth + 1, drop);
}

void App::DrawLeftPanel()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Bg));
    ImGui::BeginChild("##left", ImVec2(leftW, 0), 0, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(6), U(6)));

    // Header: menu + document name
    ImGui::SetCursorPos(ImVec2(U(8), U(8)));
    {
        // Logo + chevron opens the main menu (like the app menu in design tools).
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##mainmenu", ImVec2(U(40), U(28)))) openMainMenu = true;
        bool hov = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (hov || openMainMenu) dl->AddRectFilled(p, ImVec2(p.x + U(40), p.y + U(28)), Hover, U(6));
        DrawLogo(dl, ImVec2(p.x + U(4), p.y + U(4)), U(20));
        DrawIcon(dl, Icon::ChevronDown, ImVec2(p.x + U(32), p.y + U(14)), U(9), hov ? Text : TextDim);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Main menu");
    }
    ImGui::SameLine(0, U(6));
    ImGui::BeginGroup();
    static bool editingName = false;
    static char nameBuf[256];
    ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
    if (editingName)
    {
        ImGui::SetNextItemWidth(leftW - U(60));
        if (!ImGui::IsAnyItemActive()) ImGui::SetKeyboardFocusHere();
        if (ImGui::InputText("##docname", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll) || ImGui::IsItemDeactivated())
        {
            if (nameBuf[0]) { doc.name = nameBuf; modified = true; }
            editingName = false;
        }
    }
    else
    {
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + U(1));
        ImGui::TextUnformatted(doc.name.c_str());
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { editingName = true; snprintf(nameBuf, sizeof(nameBuf), "%s", doc.name.c_str()); }
    }
    ImGui::PopFont();
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    std::string sub = filePath.empty() ? std::string("Not saved") : !projectDir.empty() ? fs::u8path(projectDir).filename().u8string() + " / " + fs::u8path(filePath).filename().u8string() : fs::u8path(filePath).filename().u8string();
    ImGui::TextUnformatted(sub.c_str());
    if (!projectDir.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", projectDir.c_str());
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    // Tabs
    static int leftTab = 0;
    ImGui::SetCursorPosX(U(8));
    ImGui::Dummy(ImVec2(0, U(2)));
    ImGui::SetCursorPosX(U(10));
    const char* tabs[] = { "Layers", "Assets" };
    for (int i = 0; i < 2; i++)
    {
        if (i) ImGui::SameLine(0, U(14));
        ImGui::PushFont(fonts::UI(leftTab == i ? fonts::UI_SemiBold : fonts::UI_Medium), 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, leftTab == i ? Text : TextDim);
        ImGui::TextUnformatted(tabs[i]);
        if (ImGui::IsItemClicked()) leftTab = i;
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImVec2 lp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lp.x - U(10), lp.y + U(2)), ImVec2(lp.x + leftW, lp.y + U(2)), Divider);
    ImGui::Dummy(ImVec2(0, U(4)));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##layers", ImVec2(0, 0), 0);
    if (leftTab == 0)
    {
        static std::vector<std::string> lastSelection;
        if (lastSelection != selection) { scrollLayersToSelection = !ImGui::IsWindowHovered(); lastSelection = selection; }
        LayerDrop drop;
        for (int i = (int)doc.roots.size() - 1; i >= 0; i--) LayerRow(this, doc.roots[i].get(), 0, drop);
        // Empty area: drop to root / click to deselect
        ImVec2 rest = ImGui::GetContentRegionAvail();
        ImGui::InvisibleButton("##layersrest", ImVec2(std::max(1.0f, rest.x), std::max(U(40), rest.y)));
        if (ImGui::IsItemClicked()) selection.clear();
        if (ImGui::BeginDragDropTarget())
        {
            if (ImGui::AcceptDragDropPayload("FIM_NODE")) { drop.target = "__root__"; drop.where = 0; }
            ImGui::EndDragDropTarget();
        }
        if (!drop.target.empty())
        {
            Node* target = doc.Find(drop.target);
            for (Node* n : SelNodes())
            {
                if (n == target || (target && doc.IsAncestor(n, target))) continue;
                ImVec2 abs = n->AbsPos();
                auto u = doc.Detach(n);
                Node* parent;
                int index;
                if (!target) { parent = nullptr; index = (int)doc.roots.size(); }
                else if (drop.where == 0) { parent = target; index = (int)target->children.size(); }
                else
                {
                    parent = target->parent;
                    index = doc.IndexOf(target) + (drop.where < 0 ? 1 : 0);   // list is drawn top = last
                }
                ImVec2 base = parent ? parent->AbsPos() : ImVec2(0, 0);
                u->x = std::round(abs.x - base.x);
                u->y = std::round(abs.y - base.y);
                doc.Insert(std::move(u), parent, index);
            }
            MarkDirty();
        }
    }
    else
    {
        ImGui::SetCursorPosX(U(10));
        ImGui::BeginGroup();
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(6), U(6)));
        if (ImGui::Button("Import image / SVG...", ImVec2(leftW - U(20), 0)))
        {
            Node* target = Sel() && Sel()->type == NodeType::Frame ? Sel() : nullptr;
            for (auto& f : platform::OpenFilesDialog("Import", "Media|*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.mp4;*.mov;*.wmv;*.m4v|All files|*.*"))
                ImportImageFile(f, target, ImVec2(0, 0));
        }
        if (ImGui::Button("Add font file...", ImVec2(leftW - U(20), 0)))
        {
            std::string f = platform::OpenFileDialog("Add font", "Fonts|*.ttf;*.otf");
            std::string fam;
            if (!f.empty()) { if (fonts::AddFontFile(f, &fam)) Toast("Font added: " + fam); else Toast("Unsupported font", true); }
        }
        ImGui::Dummy(ImVec2(0, U(4)));
        ui::Label("Click an asset to place it in the selected frame.");
        float cell = U(64);
        int cols = std::max(1, (int)((leftW - U(20)) / (cell + U(6))));
        int i = 0;
        for (auto& [id, a] : doc.assets)
        {
            if (i % cols) ImGui::SameLine();
            ImGui::PushID(id.c_str());
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool clicked = ImGui::InvisibleButton("##asset", ImVec2(cell, cell + U(16)));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + cell, p.y + cell), ImGui::IsItemHovered() ? IM_COL32(70, 70, 70, 255) : Field, U(6));
            gfx::TexInfo t = gfx::AssetTexture(a, cell, cell, false);
            if (t.id)
            {
                ImVec2 ia(p.x + U(8), p.y + U(8)), ib(p.x + cell - U(8), p.y + cell - U(8));
                fim::ContainRect(ImVec2((float)std::max(1, a.width), (float)std::max(1, a.height)), &ia, &ib);
                dl->AddImage((ImTextureID)t.id, ia, ib);
            }
            float fs = ImGui::GetFontSize() * 0.85f;
            dl->PushClipRect(ImVec2(p.x, p.y + cell), ImVec2(p.x + cell, p.y + cell + U(16)), true);
            dl->AddText(fonts::UI(fonts::UI_Regular), fs, ImVec2(p.x + U(2), p.y + cell + U(2)), TextDim, a.name.c_str());
            dl->PopClipRect();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s, %dx%d)", a.name.c_str(), a.kind == 1 ? "SVG" : "image", a.width, a.height);
            if (clicked)
            {
                Node* target = Sel() && Sel()->type == NodeType::Frame ? Sel() : (Sel() ? Sel()->parent : nullptr);
                auto n = std::make_unique<Node>();
                n->type = a.kind == 1 ? NodeType::Vector : NodeType::Image;
                n->id = doc.NewId();
                n->name = a.name;
                n->asset = id;
                n->fill.type = 0;
                n->w = (float)std::max(1, a.width);
                n->h = (float)std::max(1, a.height);
                std::string nid = n->id;
                doc.Insert(std::move(n), target);
                Select(nid);
                MarkDirty();
            }
            ImGui::PopID();
            i++;
        }
        if (doc.assets.empty()) { ImGui::Dummy(ImVec2(0, U(8))); ui::Label("No assets yet. Paste or drop PNG/SVG files."); }
        ImGui::PopStyleVar();
        ImGui::EndGroup();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- right panel

void App::DrawRightPanel()
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(Bg));
    ImGui::BeginChild("##right", ImVec2(0, 0), 0, ImGuiWindowFlags_NoScrollbar);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(6), U(6)));

    // Top bar: preview, export, zoom
    ImGui::SetCursorPos(ImVec2(U(10), U(10)));
    if (IconButton("##preview", Icon::Play, preview, U(28), "Preview (Ctrl+Enter)")) { preview = !preview; if (preview) { editingText.clear(); } }
    ImGui::SameLine(0, U(4));
    if (IconButton("##aitab", Icon::Sparkle, rightTab == Tab_AI, U(28), "AI assistant (Ctrl+I)")) rightTab = rightTab == Tab_AI ? Tab_Design : Tab_AI;
    ImGui::SameLine(0, U(4));
    if (IconButton("##settings", Icon::Settings, false, U(28), "Settings")) showSettings = true;
    float zoomW = U(58);
    float exportW = U(72);
    ImGui::SameLine(ImGui::GetWindowWidth() - zoomW - exportW - U(16));
    if (ui::PrimaryButton("Export", ImVec2(exportW, U(28)))) showExport = true;
    ImGui::SameLine(0, U(4));
    char zbuf[16];
    snprintf(zbuf, sizeof(zbuf), "%d%%", (int)std::round(zoom * 100));
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
    if (ImGui::Button(zbuf, ImVec2(zoomW, U(28)))) ImGui::OpenPopup("##zoom");
    ImGui::PopStyleColor();
    if (ImGui::BeginPopup("##zoom"))
    {
        ImVec2 c((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f);
        if (ImGui::MenuItem("Zoom in", "Ctrl+=")) SetZoom(zoom * 2, c);
        if (ImGui::MenuItem("Zoom out", "Ctrl+-")) SetZoom(zoom / 2, c);
        if (ImGui::MenuItem("Zoom to fit", "Shift+1")) ZoomToFit(false);
        if (ImGui::MenuItem("Zoom to selection", "Shift+2")) ZoomToFit(true);
        ImGui::Separator();
        for (float z : { 0.5f, 1.0f, 2.0f })
        {
            char b[16];
            snprintf(b, sizeof(b), "Zoom to %d%%", (int)(z * 100));
            if (ImGui::MenuItem(b, z == 1.0f ? "Ctrl+0" : nullptr)) SetZoom(z, c);
        }
        if (ImGui::MenuItem("Zoom to 400% (pixel grid)")) SetZoom(kPixelGridZoom, c);
        ImGui::Separator();
        ImGui::MenuItem("Pixel grid", "Shift+'", &pixelGrid);
        ImGui::MenuItem("Snap to pixel grid", "Ctrl+Shift+'", &snapToPixel);
        ImGui::MenuItem("Pixel preview", "Ctrl+Alt+Y", &pixelPreview);
        ImGui::Separator();
        ImGui::MenuItem("Show all conditional layers", nullptr, &ignoreConditions);
        ImGui::EndPopup();
    }

    // Tabs
    ImGui::SetCursorPosX(U(12));
    ImGui::Dummy(ImVec2(0, U(2)));
    ImGui::SetCursorPosX(U(12));
    const char* tabs[] = { "Design", "Prototype", "Motion", "Code", "AI" };
    for (int i = 0; i < Tab_Count; i++)
    {
        if (i) ImGui::SameLine(0, U(12));
        ImGui::PushFont(fonts::UI(rightTab == i ? fonts::UI_SemiBold : fonts::UI_Medium), 0.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, rightTab == i ? Text : TextDim);
        ImGui::TextUnformatted(tabs[i]);
        if (ImGui::IsItemClicked()) rightTab = i;
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::PopStyleColor();
        ImGui::PopFont();
    }
    ImVec2 lp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(lp.x - U(12), lp.y + U(2)), ImVec2(lp.x + rightW, lp.y + U(2)), Divider);
    ImGui::Dummy(ImVec2(0, U(2)));

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(12), U(4)));
    ImGui::BeginChild("##rightbody", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (rightTab)
    {
    case Tab_Design: DrawDesignTab(); break;
    case Tab_Prototype: DrawPrototypeTab(); break;
    case Tab_Motion: DrawMotionTab(); break;
    case Tab_Code: DrawCodeTab(); break;
    case Tab_AI: DrawAiTab(); break;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------- design tab

static bool FontCombo(const char* id, std::string* family, float width)
{
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, family->c_str(), ImGuiComboFlags_HeightLarge))
    {
        static char filter[64] = {};
        if (ImGui::IsWindowAppearing()) { filter[0] = 0; ImGui::SetKeyboardFocusHere(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##filter", "Search fonts", filter, sizeof(filter));
        for (auto& f : fonts::Families())
        {
            if (filter[0])
            {
                std::string a = f, b = filter;
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                std::transform(b.begin(), b.end(), b.begin(), ::tolower);
                if (a.find(b) == std::string::npos) continue;
            }
            bool sel = f == *family;
            if (ImGui::Selectable(f.c_str(), sel)) { *family = f; changed = true; }
            if (sel && ImGui::IsWindowAppearing()) ImGui::SetScrollHereY();
        }
        ImGui::EndCombo();
    }
    return changed;
}

static void PaintEditor(App* app, const char* id, Paint* p, float width, bool allowGradient)
{
    ImGui::PushID(id);
    bool ch = false;
    if (p->type == 2)
    {
        ch |= ui::ColorRow("##c0", &p->c0, width);
        ch |= ui::ColorRow("##c1", &p->c1, width);
        ch |= ui::NumField("##ang", "", &p->angle, U(90), 1.0f, -360, 360, "\xC2\xB0", Icon::Radius);
        ImGui::SameLine();
        if (ImGui::SmallButton("Solid")) { p->type = 1; ch = true; }
    }
    else
    {
        ch |= ui::ColorRow("##c0", &p->c0, width);
        if (allowGradient)
        {
            if (ImGui::SmallButton("Gradient")) { p->type = 2; p->c1 = ImVec4(p->c0.x * 0.5f, p->c0.y * 0.5f, p->c0.z * 0.5f, p->c0.w); ch = true; }
        }
    }
    if (ch) app->MarkDirty();
    ImGui::PopID();
}

void App::DrawDesignTab()
{
    float W = ui::Avail();
    float half = (W - U(8)) * 0.5f;
    auto nodes = SelNodes();

    if (nodes.empty())
    {
        ui::Section("Page");
        ImVec4 c = doc.canvasColor;
        if (ui::ColorRow("##canvas", &c, W)) { doc.canvasColor = c; MarkDirty(); }
        ui::Section("State variables");
        auto list = CollectVars(doc);
        if (list.empty()) ui::Label("Add interactions in the Prototype tab - their variables appear here.");
        for (auto& v : list)
        {
            ImGui::PushID(v.name.c_str());
            ui::Label(v.name.c_str(), false);
            ImGui::SameLine(W * 0.45f);
            ImGui::SetNextItemWidth(W * 0.55f);
            if (v.kind == VarKind::Bool) { bool b = vars.Get(v.name) != 0; if (ImGui::Checkbox("##v", &b)) vars.num[v.name] = b ? 1.0f : 0.0f; }
            else if (v.kind == VarKind::String) ui::TextField("##v", &vars.str[v.name], W * 0.55f);
            else { float f = vars.Get(v.name); if (ImGui::DragFloat("##v", &f, (v.kind == VarKind::Int || v.kind == VarKind::Mask) ? 1.0f : 0.1f, 0, 0, (v.kind == VarKind::Int || v.kind == VarKind::Mask) ? "%.0f" : "%.2f")) vars.num[v.name] = (v.kind == VarKind::Int || v.kind == VarKind::Mask) ? std::round(f) : f; }
            ImGui::PopID();
        }
        ui::Section("Export");
        if (ui::PrimaryButton("Export ImGui project...", ImVec2(W, U(28)))) showExport = true;
        ImGui::Dummy(ImVec2(0, U(6)));
        ui::Label("Generates menu .h/.cpp, icons, images,\nfonts and an example host.");
        return;
    }

    Node* n = nodes[0];
    bool multi = nodes.size() > 1;
    ImGui::PushID(n->id.c_str());

    // Alignment row
    ImGui::Dummy(ImVec2(0, U(2)));
    const Icon al[] = { Icon::AlignLeft, Icon::AlignCenterH, Icon::AlignRight, Icon::AlignTop, Icon::AlignCenterV, Icon::AlignBottom };
    const char* alTip[] = { "Align left", "Align horizontal centers", "Align right", "Align top", "Align vertical centers", "Align bottom" };
    for (int i = 0; i < 6; i++)
    {
        if (i) ImGui::SameLine(0, i == 3 ? U(10) : U(2));
        ImGui::PushID(i);
        if (IconButton("##al", al[i], false, U(26), alTip[i])) AlignSelection(i);
        ImGui::PopID();
    }

    // Header: type + name
    ui::Section(multi ? "Mixed" : (n->type == NodeType::Frame ? "Frame" : n->type == NodeType::Group ? "Group" : n->type == NodeType::Rect ? "Rectangle" : n->type == NodeType::Ellipse ? "Ellipse"
                : n->type == NodeType::Line ? "Line" : n->type == NodeType::Text ? "Text" : n->type == NodeType::Image ? "Image" : "Vector"));
    if (!multi)
    {
        if (ui::TextField("##name", &n->name, W)) MarkDirty();
    }

    // Position & size
    ui::Section("Layout");
    bool ch = false;
    ch |= ui::NumField("x", "X", &n->x, half);
    ImGui::SameLine(0, U(8));
    ch |= ui::NumField("y", "Y", &n->y, half);
    float w0 = n->w, h0 = n->h;
    ch |= ui::NumField("w", "W", &n->w, half, 1.0f, 0, 1e6f);
    ImGui::SameLine(0, U(8));
    if (n->type != NodeType::Line) ch |= ui::NumField("h", "H", &n->h, half, 1.0f, 0, 1e6f);
    if (n->w != w0 && n->type == NodeType::Text && n->textSizing == 1) n->textSizing = 2;
    if (n->h != h0 && n->type == NodeType::Text) n->textSizing = 0;
    if (n->w != w0) n->hugW = false;
    if (n->h != h0) n->hugH = false;
    if (n->parent && n->parent->layout)
    {
        ch |= ui::Check("Fill width", &n->fillW);
        ImGui::SameLine(half + U(8));
        ch |= ui::Check("Fill height", &n->fillH);
    }
    if (multi && ch)
        for (size_t i = 1; i < nodes.size(); i++) { nodes[i]->w = n->w; nodes[i]->h = n->h; }

    if (n->type == NodeType::Frame || n->type == NodeType::Rect || n->type == NodeType::Image)
    {
        static bool independent = false;
        bool mixed = !(n->radius[0] == n->radius[1] && n->radius[1] == n->radius[2] && n->radius[2] == n->radius[3]);
        if (mixed) independent = true;
        if (!independent)
        {
            float r = n->radius[0];
            if (ui::NumField("rad", "", &r, half, 1.0f, 0, 1e4f, nullptr, Icon::Radius)) { for (Node* q : nodes) for (float& e : q->radius) e = r; ch = true; }
        }
        else
        {
            float q4 = (W - U(18)) / 4;
            for (int i = 0; i < 4; i++)
            {
                if (i) ImGui::SameLine(0, U(4));
                ImGui::PushID(i);
                ch |= ui::NumField("r", "", &n->radius[i], q4, 1.0f, 0, 1e4f);
                ImGui::PopID();
            }
        }
        ImGui::SameLine(0, U(4));
        if (IconButton("##indep", independent ? Icon::Unlink : Icon::Link, independent, 0, "Independent corners")) independent = !independent;
    }
    if (n->type == NodeType::Frame)
        ch |= ui::Check("Clip content", &n->clip);
    if (ch) MarkDirty();

    // Auto layout
    if (n->type == NodeType::Frame && !multi)
    {
        int b = ui::Section("Auto layout", n->layout ? -1 : 1);
        if (b == 1) { n->layout = 2; n->gap = 8; MarkDirty(); }
        if (b == -1) { n->layout = 0; MarkDirty(); }
        if (n->layout)
        {
            bool lc = false;
            if (IconButton("##dirv", Icon::ArrowDown, n->layout == 2, 0, "Vertical")) { n->layout = 2; lc = true; }
            ImGui::SameLine(0, U(2));
            if (IconButton("##dirh", Icon::ArrowRight, n->layout == 1, 0, "Horizontal")) { n->layout = 1; lc = true; }
            ImGui::SameLine(0, U(10));
            lc |= ui::NumField("gap", "", &n->gap, W - ImGui::GetFrameHeight() * 2 - U(12), 1.0f, -1000, 1000, nullptr, n->layout == 1 ? Icon::AlignCenterV : Icon::AlignCenterH);
            float padH = n->pad[0], padV = n->pad[1];
            if (ui::NumField("padh", "", &padH, half, 1.0f, 0, 1000, nullptr, Icon::AlignCenterH)) { n->pad[0] = n->pad[2] = padH; lc = true; }
            ImGui::SameLine(0, U(8));
            if (ui::NumField("padv", "", &padV, half, 1.0f, 0, 1000, nullptr, Icon::AlignCenterV)) { n->pad[1] = n->pad[3] = padV; lc = true; }
            ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
            ImGui::TextUnformatted("Alignment");
            ImGui::PopStyleColor();
            // 3x3 alignment grid
            ImVec2 gp = ImGui::GetCursorScreenPos();
            float cell = U(18);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(gp, ImVec2(gp.x + cell * 3 + U(8), gp.y + cell * 3 + U(8)), Field, U(5));
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++)
                {
                    ImGui::SetCursorScreenPos(ImVec2(gp.x + U(4) + c * cell, gp.y + U(4) + r * cell));
                    ImGui::PushID(r * 3 + c);
                    int mainIdx = n->layout == 1 ? c : r, crossIdx = n->layout == 1 ? r : c;
                    if (ImGui::InvisibleButton("##g", ImVec2(cell, cell))) { n->alignMain = mainIdx; n->alignCross = crossIdx; lc = true; }
                    bool on = (n->alignMain == mainIdx || (n->alignMain == 3 && mainIdx == 1)) && (n->alignCross == crossIdx || (n->alignCross == 3 && crossIdx == 1));
                    ImVec2 cc(gp.x + U(4) + c * cell + cell * 0.5f, gp.y + U(4) + r * cell + cell * 0.5f);
                    if (on) dl->AddRectFilled(ImVec2(cc.x - U(4), cc.y - U(4)), ImVec2(cc.x + U(4), cc.y + U(4)), Accent, U(1));
                    else dl->AddCircleFilled(cc, U(1.5f), ImGui::IsItemHovered() ? Text : TextFaint);
                    ImGui::PopID();
                }
            ImGui::SetCursorScreenPos(ImVec2(gp.x + cell * 3 + U(18), gp.y));
            ImGui::BeginGroup();
            bool sb = n->alignMain == 3;
            if (ui::Check("Space between", &sb)) { n->alignMain = sb ? 3 : 0; lc = true; }
            bool st = n->alignCross == 3;
            if (ui::Check("Stretch items", &st)) { n->alignCross = st ? 3 : 0; lc = true; }
            lc |= ui::Check("Hug width", &n->hugW);
            lc |= ui::Check("Hug height", &n->hugH);
            ImGui::EndGroup();
            if (lc) MarkDirty();
        }
    }

    // Layer
    ui::Section("Layer");
    float op = std::round(n->opacity * 100);
    if (ui::NumField("op", "", &op, half, 1.0f, 0, 100, "%", Icon::Eye)) { for (Node* q : nodes) q->opacity = op / 100.0f; MarkDirty(); }
    ImGui::SameLine(0, U(8));
    if (ui::Check("Visible", &n->visible)) MarkDirty();

    // Text
    if (n->type == NodeType::Text)
    {
        ui::Section("Text");
        bool tc = false;
        tc |= FontCombo("##font", &n->fontFamily, W);
        auto styles = fonts::Styles(n->fontFamily);
        ImGui::SetNextItemWidth(half);
        if (ImGui::BeginCombo("##style", n->fontStyle.c_str()))
        {
            for (auto& s : styles) if (ImGui::Selectable(s.c_str(), s == n->fontStyle)) { n->fontStyle = s; tc = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, U(8));
        tc |= ui::NumField("fs", "", &n->fontSize, half, 1.0f, 1, 1000, nullptr, Icon::Text);
        float lh = n->lineHeight;
        if (ui::NumField("lh", "LH", &lh, half, 1.0f, 0, 1000)) { n->lineHeight = lh; tc = true; }
        ImGui::SameLine(0, U(8));
        const char* sizing[] = { "Fixed", "Auto W", "Auto H" };
        tc |= ui::Segmented("##sizing", &n->textSizing, sizing, 3, half);
        const Icon ah[] = { Icon::TextLeft, Icon::TextCenter, Icon::TextRight };
        for (int i = 0; i < 3; i++)
        {
            if (i) ImGui::SameLine(0, U(2));
            ImGui::PushID(i);
            if (IconButton("##ah", ah[i], n->alignH == i)) { n->alignH = i; tc = true; }
            ImGui::PopID();
        }
        ImGui::SameLine(0, U(12));
        const Icon av[] = { Icon::AlignTop, Icon::AlignCenterV, Icon::AlignBottom };
        for (int i = 0; i < 3; i++)
        {
            if (i) ImGui::SameLine(0, U(2));
            ImGui::PushID(10 + i);
            if (IconButton("##av", av[i], n->alignV == i)) { n->alignV = i; tc = true; }
            ImGui::PopID();
        }
        tc |= ui::TextField("##content", &n->text, W, nullptr, true, U(56));
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextWrapped("Use {var} or {var:%%.1f} to show state values.");
        ImGui::PopStyleColor();
        if (tc)
        {
            for (Node* q : nodes) if (q != n && q->type == NodeType::Text) { q->fontFamily = n->fontFamily; q->fontStyle = n->fontStyle; q->fontSize = n->fontSize; q->alignH = n->alignH; }
            MarkDirty();
        }
    }

    // Image / vector specifics
    if (n->type == NodeType::Image)
    {
        ui::Section("Image");
        const char* fits[] = { "Fill", "Fit", "Stretch" };
        if (ui::Segmented("##fit", &n->imageFit, fits, 3, W)) MarkDirty();
        if (ImGui::Button("Replace image...", ImVec2(W, 0)))
        {
            std::string f = platform::OpenFileDialog("Replace image", "Images|*.png;*.jpg;*.jpeg;*.bmp;*.gif");
            std::vector<unsigned char> bytes;
            if (!f.empty() && platform::ReadFile(f, bytes)) { n->asset = AddAsset(doc, 0, fs::u8path(f).stem().u8string(), bytes); MarkDirty(); }
        }
    }
    if (n->type == NodeType::Vector)
    {
        ui::Section("Vector");
        if (ui::Check("Recolor (tint with fill)", &n->recolor)) { if (n->recolor && n->fill.type == 0) n->fill = Paint{ 1, ImVec4(1, 1, 1, 1) }; MarkDirty(); }
    }

    // Fill
    if (n->type != NodeType::Line && n->type != NodeType::Group && (n->type != NodeType::Image))
    {
        int b = ui::Section(n->type == NodeType::Text ? "Text color" : "Fill", n->fill.type ? -1 : 1);
        if (b == 1) { for (Node* q : nodes) q->fill = Paint{ 1, n->type == NodeType::Text ? ImVec4(0, 0, 0, 1) : ImVec4(0.85f, 0.85f, 0.85f, 1) }; MarkDirty(); }
        if (b == -1) { for (Node* q : nodes) q->fill.type = 0; MarkDirty(); }
        if (n->fill.type)
        {
            Paint before = n->fill;
            PaintEditor(this, "fill", &n->fill, W, n->type != NodeType::Text && n->type != NodeType::Vector);
            if (multi && memcmp(&before, &n->fill, sizeof(Paint)) != 0) for (Node* q : nodes) q->fill = n->fill;
        }
    }

    // Stroke
    if (n->type != NodeType::Group && n->type != NodeType::Text && n->type != NodeType::Vector)
    {
        int b = ui::Section("Stroke", n->stroke.type ? -1 : 1);
        if (b == 1) { for (Node* q : nodes) { q->stroke = Paint{ 1, ImVec4(0, 0, 0, 1) }; q->strokeWidth = 1; } MarkDirty(); }
        if (b == -1 && n->type != NodeType::Line) { for (Node* q : nodes) q->stroke.type = 0; MarkDirty(); }
        if (n->stroke.type)
        {
            bool sc = ui::ColorRow("##stroke", &n->stroke.c0, W);
            sc |= ui::NumField("sw", "", &n->strokeWidth, half, 0.5f, 0, 1000, nullptr, Icon::Line);
            if (n->type != NodeType::Line && n->type != NodeType::Ellipse)
            {
                ImGui::SameLine(0, U(8));
                const char* aligns[] = { "Inside", "Center", "Outside" };
                sc |= ui::Combo("##salign", &n->strokeAlign, aligns, 3, half);
            }
            if (sc) { for (Node* q : nodes) { q->stroke = n->stroke; q->strokeWidth = n->strokeWidth; q->strokeAlign = n->strokeAlign; } MarkDirty(); }
        }
    }

    DrawEffects(n, nodes);

    // Export / copy
    ui::Section("Export");
    static int scaleIdx = 1, fmt = 0;
    const char* scales[] = { "1x", "2x", "3x", "4x" };
    const char* fmts[] = { "PNG", "SVG" };
    ui::Combo("##escale", &scaleIdx, scales, 4, half);
    ImGui::SameLine(0, U(8));
    ui::Combo("##efmt", &fmt, fmts, 2, half);
    if (ImGui::Button(("Export " + n->name).c_str(), ImVec2(W, 0))) ExportSelectionFile(fmt, (float)(scaleIdx + 1));
    float q = (W - U(18)) / 4;
    if (ImGui::Button("CSS", ImVec2(q, 0))) CopyAs(0);
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("SVG", ImVec2(q, 0))) CopyAs(1);
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("PNG", ImVec2(q, 0))) CopyAs(2);
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("ImGui", ImVec2(q, 0))) CopyAs(3);
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::TextUnformatted("Copy to clipboard as CSS / SVG / PNG / ImGui code");
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, U(12)));
    ImGui::PopID();
}

// ---------------------------------------------------------------- prototype tab

static bool VarField(App* app, const char* id, std::string* var, float width);

// Options, default selection and dropdown style of a combo / multi-combo.
static bool ComboEditor(App* app, Node* n, float W)
{
    bool ch = false;
    bool multi = n->role == Role::MultiCombo;
    float half = (W - U(8)) * 0.5f;
    ui::Section(multi ? "Options (multi-select)" : "Options");
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::TextWrapped(multi ? "Checked = selected by default. Stored as a bit mask (unsigned int)." : "Dot = selected by default. Stored as the item index (int).");
    ImGui::PopStyleColor();
    unsigned mask = (unsigned)n->value;
    int remove = -1, up = -1;
    for (int i = 0; i < (int)n->options.size(); i++)
    {
        ImGui::PushID(i);
        bool def = multi ? (mask & (1u << i)) != 0 : (int)n->value == i;
        if (multi) { if (ImGui::Checkbox("##def", &def)) { n->value = (float)(def ? (mask | (1u << i)) : (mask & ~(1u << i))); ch = true; } }
        else if (ImGui::RadioButton("##def", def)) { n->value = (float)i; ch = true; }
        ImGui::SameLine(0, U(6));
        float bw = ImGui::GetFrameHeight();
        ch |= ui::TextField("##opt", &n->options[i], W - bw * 3 - U(16) - ImGui::GetFrameHeight());
        ImGui::SameLine(0, U(2));
        if (IconButton("##up", Icon::ChevronDown, false, 0, "Move down") && i + 1 < (int)n->options.size()) up = i;
        ImGui::SameLine(0, U(2));
        if (IconButton("##rm", Icon::Minus, false, 0, "Remove")) remove = i;
        ImGui::PopID();
    }
    if (up >= 0)
    {
        std::swap(n->options[up], n->options[up + 1]);
        if (multi)
        {
            unsigned a = (mask >> up) & 1u, b = (mask >> (up + 1)) & 1u;
            mask = (mask & ~((1u << up) | (1u << (up + 1)))) | (b << up) | (a << (up + 1));
            n->value = (float)mask;
        }
        else if ((int)n->value == up) n->value = (float)(up + 1);
        else if ((int)n->value == up + 1) n->value = (float)up;
        ch = true;
    }
    if (remove >= 0)
    {
        n->options.erase(n->options.begin() + remove);
        if (multi)
        {
            unsigned low = mask & ((1u << remove) - 1u), high = remove < 31 ? (mask >> (remove + 1)) << remove : 0u;
            n->value = (float)(low | high);
        }
        else n->value = std::clamp(n->value - (n->value > remove ? 1.0f : 0.0f), 0.0f, std::max(0.0f, (float)n->options.size() - 1));
        ch = true;
    }
    if (n->options.size() < 24 && ImGui::Button("+ Add option", ImVec2(half, 0)))
    {
        n->options.push_back("Option " + std::to_string(n->options.size() + 1));
        ch = true;
    }
    ImGui::SameLine(0, U(8));
    bool hasLabel = false;
    for (auto& c : n->children) if (c->type == NodeType::Text && c->text.find("{" + n->var + ":label}") != std::string::npos) hasLabel = true;
    ImGui::BeginDisabled(hasLabel || n->type != NodeType::Frame);
    if (ImGui::Button("Add value + arrow", ImVec2(half, 0)))
    {
        // Text showing the current selection and a chevron, laid out inside the box.
        Node* t = app->CreateNode(NodeType::Text, ImVec2(n->AbsPos().x + 12, n->AbsPos().y), ImVec2(10, 10), n);
        t->name = "Value";
        t->text = "{" + n->var + ":label}";
        t->fontSize = 13;
        bool dark = !(n->fill.type && (n->fill.c0.x + n->fill.c0.y + n->fill.c0.z) / 3 > 0.6f && n->fill.c0.w > 0.5f);
        t->fill = Paint{ 1, dark ? ImVec4(0.9f, 0.9f, 0.93f, 1) : ImVec4(0.1f, 0.1f, 0.12f, 1) };
        app->doc.FitText(t);
        t->y = std::round((n->h - t->h) * 0.5f);
        const char* chevron = "<svg xmlns='http://www.w3.org/2000/svg' width='16' height='16' viewBox='0 0 16 16'><g fill='none' stroke='#FFFFFF' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'><path d='M4 6l4 4 4-4'/></g></svg>";
        if (Node* c = ImportSVG(app->doc, chevron, "chevron", n, ImVec2(n->w - 28, std::round((n->h - 16) * 0.5f))))
        {
            c->name = "Arrow";
            c->recolor = true;
            c->fill = Paint{ 1, ImVec4(t->fill.c0.x, t->fill.c0.y, t->fill.c0.z, 0.6f) };
        }
        ch = true;
    }
    ImGui::EndDisabled();
    if (hasLabel == false && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Adds a text with {%s:label} and a chevron icon", n->var.c_str());

    ui::Section("Dropdown list");
    ui::Label("Background");
    ch |= ui::ColorRow("##lfill", &n->listFill, W);
    ui::Label("Selected / check");
    ch |= ui::ColorRow("##lacc", &n->listAccent, W);
    float ih = n->itemHeight > 0 ? n->itemHeight : n->h;
    if (ui::NumField("ih", "", &ih, half, 1.0f, 12, 200, " px", Icon::AlignCenterV)) { n->itemHeight = ih; ch = true; }
    ImGui::SameLine(0, U(8));
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("item height");
    ImGui::PopStyleColor();
    if (multi)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::TextWrapped("Text when none / all selected (the \"all\" text also adds a select-all row; leave it empty to hide)");
        ImGui::PopStyleColor();
        ch |= ui::TextField("##none", &n->noneLabel, half, "None");
        ImGui::SameLine(0, U(8));
        ch |= ui::TextField("##all", &n->allLabel, half, "All");
    }
    ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
    ImGui::TextWrapped("Show the selection in a child text with {%s:label}. Font and text color of the list come from that text.", n->var.c_str());
    ImGui::PopStyleColor();
    return ch;
}

static bool VarField(App* app, const char* id, std::string* var, float width)
{
    bool changed = ui::TextField(id, var, width - ImGui::GetFrameHeight() - U(2), "variable");
    ImGui::SameLine(0, U(2));
    ImGui::PushID(id);
    if (IconButton("##varlist", Icon::ChevronDown)) ImGui::OpenPopup("##vars");
    if (ImGui::BeginPopup("##vars"))
    {
        auto list = CollectVars(app->doc);
        if (list.empty()) ImGui::TextDisabled("No variables yet");
        for (auto& v : list)
            if (ImGui::Selectable(v.name.c_str())) { *var = v.name; changed = true; }
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

void App::DrawPrototypeTab()
{
    float W = ui::Avail();
    float half = (W - U(8)) * 0.5f;
    Node* n = Sel();
    if (!n)
    {
        ui::Section("Prototype");
        ImGui::PushTextWrapPos(0);
        ui::Label("Select a layer to make it interactive. Roles map to real ImGui behaviour in the exported code:");
        ImGui::BulletText("Button - click sets a variable");
        ImGui::BulletText("Checkbox / toggle - bool");
        ImGui::BulletText("Slider - float with fill & knob parts");
        ImGui::BulletText("Tab - selects a page (int)");
        ImGui::BulletText("Input - text field");
        ImGui::BulletText("Combo - dropdown, pick one (int)");
        ImGui::BulletText("Multi-combo - dropdown, pick several or all (bit mask)");
        ui::Label("Use \"Show when\" on pages and on/off visuals. Press Play to try it.");
        ImGui::PopTextWrapPos();
        return;
    }
    ImGui::PushID(n->id.c_str());
    bool ch = false;
    ui::Section("Interaction");
    const char* roles[] = { "None", "Button", "Checkbox / Toggle", "Slider", "Tab", "Text input", "Combo (dropdown)", "Multi-combo" };
    int r = (int)n->role;
    if (ui::Combo("##role", &r, roles, 8, W))
    {
        n->role = (Role)r;
        if ((n->role == Role::Combo || n->role == Role::MultiCombo) && n->options.empty())
        {
            n->options = { "Option 1", "Option 2", "Option 3" };
            n->value = n->role == Role::MultiCombo ? 1.0f : 0.0f;
        }
        if (n->var.empty() && n->role != Role::None)
            n->var = n->role == Role::Tab ? "tab" : SanitizeIdent(n->name, "value");
        if (n->role == Role::Tab && !n->hasActive) { n->hasActive = true; n->activeColor = ImVec4(1, 1, 1, 0.08f); }
        if (n->role != Role::None && !n->hasHover) { n->hasHover = true; n->hoverColor = n->fill.type ? ImVec4(std::min(1.0f, n->fill.c0.x + 0.06f), std::min(1.0f, n->fill.c0.y + 0.06f), std::min(1.0f, n->fill.c0.z + 0.06f), std::max(0.06f, n->fill.c0.w)) : ImVec4(1, 1, 1, 0.06f); }
        ch = true;
    }
    if (n->role != Role::None)
    {
        ui::Label("Variable");
        ch |= VarField(this, "##var", &n->var, W);
        if (n->role == Role::Button || n->role == Role::Tab)
        {
            ui::Label(n->role == Role::Tab ? "Tab index (value set on click)" : "Value set on click");
            ch |= ui::NumField("val", "=", &n->value, half, 1.0f);
        }
        if (n->role == Role::Checkbox)
        {
            bool on = n->value != 0;
            if (ui::Check("Checked by default", &on)) { n->value = on ? 1.0f : 0.0f; vars.num[n->var] = n->value; ch = true; }
        }
        if (n->role == Role::Slider)
        {
            ch |= ui::NumField("min", "Min", &n->vmin, half, 1.0f);
            ImGui::SameLine(0, U(8));
            ch |= ui::NumField("max", "Max", &n->vmax, half, 1.0f);
            if (ui::NumField("def", "Def", &n->value, half, 1.0f)) { vars.num[n->var] = n->value; ch = true; }
            ImGui::SameLine(0, U(8));
            if (ImGui::Button("Add fill + knob", ImVec2(half, 0)))
            {
                Node* fill = CreateNode(NodeType::Rect, n->AbsPos(), ImVec2(n->w * 0.5f, n->h), n);
                fill->name = "Fill"; fill->sliderPart = 1; fill->fill = Paint{ 1, ImVec4(0.05f, 0.6f, 1, 1) };
                for (int i = 0; i < 4; i++) fill->radius[i] = n->radius[i];
                float k = std::max(12.0f, n->h + 8);
                Node* knob = CreateNode(NodeType::Ellipse, ImVec2(n->AbsPos().x, n->AbsPos().y + (n->h - k) * 0.5f), ImVec2(k, k), n);
                knob->name = "Knob"; knob->sliderPart = 2; knob->fill = Paint{ 1, ImVec4(1, 1, 1, 1) };
                n->clip = false;
                ch = true;
            }
        }
        if (n->role == Role::Combo || n->role == Role::MultiCombo) ch |= ComboEditor(this, n, W);
        ui::Section("States");
        ch |= ui::Check("Hover color", &n->hasHover);
        if (n->hasHover) ch |= ui::ColorRow("##hov", &n->hoverColor, W);
        ch |= ui::Check(n->role == Role::Tab ? "Selected color" : n->role == Role::Checkbox ? "Checked color" : "Active color", &n->hasActive);
        if (n->hasActive) ch |= ui::ColorRow("##act", &n->activeColor, W);
    }

    ui::Section("Show when");
    bool cond = !n->showVar.empty();
    if (ui::Check("Only visible when variable matches", &cond))
    {
        if (cond) { n->showVar = "tab"; n->showValue = 0; }
        else n->showVar.clear();
        ch = true;
    }
    if (!n->showVar.empty())
    {
        ch |= VarField(this, "##showvar", &n->showVar, W);
        int op = n->showNot ? 1 : 0;
        const char* ops[] = { "equals", "not equal" };
        if (ui::Combo("##op", &op, ops, 2, half)) { n->showNot = op == 1; ch = true; }
        ImGui::SameLine(0, U(8));
        ch |= ui::NumField("sv", "=", &n->showValue, half, 1.0f);
    }

    if (SliderOwner(n))
    {
        ui::Section("Slider part");
        const char* parts[] = { "None", "Fill (width)", "Knob (position)" };
        ch |= ui::Combo("##sp", &n->sliderPart, parts, 3, W);
    }

    ui::Section("Preview state");
    auto list = CollectVars(doc);
    for (auto& v : list)
    {
        ImGui::PushID(v.name.c_str());
        ui::Label(v.name.c_str(), false);
        ImGui::SameLine(W * 0.45f);
        ImGui::SetNextItemWidth(W * 0.55f);
        if (v.kind == VarKind::Bool) { bool b = vars.Get(v.name) != 0; if (ImGui::Checkbox("##v", &b)) vars.num[v.name] = b ? 1.0f : 0.0f; }
        else if (v.kind == VarKind::String) ui::TextField("##v", &vars.str[v.name], W * 0.55f);
        else { float f = vars.Get(v.name); if (ImGui::DragFloat("##v", &f, (v.kind == VarKind::Int || v.kind == VarKind::Mask) ? 0.1f : 0.5f, 0, 0, (v.kind == VarKind::Int || v.kind == VarKind::Mask) ? "%.0f" : "%.2f")) vars.num[v.name] = (v.kind == VarKind::Int || v.kind == VarKind::Mask) ? std::round(f) : f; }
        ImGui::PopID();
    }
    if (ch) MarkDirty();
    ImGui::PopID();
}

// ---------------------------------------------------------------- code tab

void App::DrawCodeTab()
{
    float W = ui::Avail();
    const char* langs[] = { "CSS", "SVG", "ImGui", "JSON" };
    ImGui::Dummy(ImVec2(0, U(4)));
    ui::Segmented("##lang", &codeLang, langs, 4, W);
    Node* n = Sel();
    static std::string code;
    static std::string key;
    static int frameCounter = 0;
    std::string k = (n ? n->id : std::string("-")) + "|" + std::to_string(codeLang);
    if (k != key || ++frameCounter % 20 == 0)
    {
        key = k;
        if (!n) code = "// Select a layer to inspect its code.\n// Export > ImGui project generates the full menu.";
        else switch (codeLang)
        {
        case 0: code = NodeToCSS(doc, n, true); break;
        case 1: code = NodeToSVG(doc, n); if (code.size() > 200000) code = code.substr(0, 200000) + "\n<!-- truncated -->"; break;
        case 2: code = GenerateSnippet(doc, n); break;
        case 3: code = NodeToJson(n).dump(2); break;
        }
    }
    ImGui::Dummy(ImVec2(0, U(2)));
    if (ui::PrimaryButton("Copy", ImVec2(W * 0.5f - U(3), 0)) && n)
    {
        if (codeLang == 3) CopyAs(4);
        else { platform::SetClipboardText(code); Toast("Copied " + std::string(langs[codeLang])); }
    }
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("Copy PNG @2x", ImVec2(W * 0.5f - U(3), 0))) CopyAs(2);
    ImGui::PushFont(nullptr, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(30, 30, 30, 255));
    ImGui::InputTextMultiline("##code", code.data(), code.size() + 1, ImVec2(W, ImGui::GetContentRegionAvail().y - U(8)), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// ---------------------------------------------------------------- toolbar

void App::DrawToolbar()
{
    float h = U(44);
    struct T { Tool tool; Icon icon; const char* tip; };
    const T tools[] = {
        { Tool::Move, Icon::Move, "Move (V)" }, { Tool::Hand, Icon::Hand, "Hand (H)" },
        { Tool::Frame, Icon::Frame, "Frame (F)" }, { Tool::Rect, Icon::Rect, "Rectangle (R)" }, { Tool::Ellipse, Icon::Ellipse, "Ellipse (O)" },
        { Tool::Line, Icon::Line, "Line (L)" }, { Tool::Text, Icon::Text, "Text (T)" },
    };
    float bs = U(32);
    float width = bs * 10 + U(4) * 9 + U(16) + U(20);
    ImVec2 pos((canvasMin.x + canvasMax.x - width) * 0.5f, canvasMax.y - h - U(16));
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(width, h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(8), U(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, U(12));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(4), 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(44, 44, 44, 255));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 255, 255, 20));
    // Soft shadow
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    (void)bg;
    ImGui::Begin("##toolbar", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
    for (int i = 0; i < 7; i++)
    {
        if (i) ImGui::SameLine(0, i == 2 ? U(10) : U(4));
        ImGui::PushID(i);
        if (IconButton("##t", tools[i].icon, tool == tools[i].tool && !preview, bs, tools[i].tip)) { tool = tools[i].tool; preview = false; }
        ImGui::PopID();
    }
    ImGui::SameLine(0, U(4));
    if (IconButton("##img", Icon::Image, false, bs, "Place image / SVG (Ctrl+Shift+K)"))
    {
        Node* target = Sel() && Sel()->type == NodeType::Frame ? Sel() : nullptr;
        for (auto& f : platform::OpenFilesDialog("Place image", "Media|*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.mp4;*.mov;*.wmv;*.m4v"))
            ImportImageFile(f, target, target ? ImVec2(0, 0) : ScreenToWorld(ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f)));
    }
    ImGui::SameLine(0, U(10));
    if (IconButton("##ai", Icon::Sparkle, rightTab == Tab_AI, bs, "AI assistant (Ctrl+I)")) rightTab = rightTab == Tab_AI ? Tab_Design : Tab_AI;
    ImGui::SameLine(0, U(4));
    if (IconButton("##play", Icon::Play, preview, bs, "Preview (Ctrl+Enter)")) preview = !preview;
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// ---------------------------------------------------------------- dialogs

void App::DrawExportDialog()
{
    if (showExport) { ImGui::OpenPopup("Export ImGui project"); showExport = false; lastExport = ExportResult(); }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(U(520), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(18), U(16)));
    if (ImGui::BeginPopupModal("Export ImGui project", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        float W = U(484);
        ui::Label("Output folder");
        ui::TextField("##dir", &exportOpt.outDir, W - U(90), "C:\\path\\to\\export");
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(U(84), 0)))
        {
            std::string d = platform::PickFolderDialog("Export folder");
            if (!d.empty()) exportOpt.outDir = d;
        }
        ui::Label("C++ namespace");
        ImGui::SameLine(U(150));
        ui::TextField("##ns", &exportOpt.ns, W - U(150));
        ui::Label("File name");
        ImGui::SameLine(U(150));
        ui::TextField("##base", &exportOpt.fileBase, W - U(150));
        ui::Label("Icon PNG scale");
        ImGui::SameLine(U(150));
        ImGui::SetNextItemWidth(W - U(150));
        ImGui::SliderFloat("##iconscale", &exportOpt.iconScale, 1.0f, 4.0f, "%.0fx");
        exportOpt.iconScale = std::round(exportOpt.iconScale);
        ui::Check("Embed fonts into the binary", &exportOpt.embedFonts);
        ui::Check("Write example host (Win32 + DirectX 11)", &exportOpt.writeExample);
        ImGui::Dummy(ImVec2(0, U(6)));

        int frames = 0;
        for (auto& r : doc.roots) if (r->type == NodeType::Frame && r->visible) frames++;
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::Text("%d top-level frame(s) -> ImGui windows, %d asset(s), %d state variable(s)", frames, (int)doc.assets.size(), (int)CollectVars(doc).size());
        ImGui::PopStyleColor();

        if (!lastExport.message.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, lastExport.ok ? Success : Danger);
            ImGui::TextWrapped("%s", lastExport.message.c_str());
            ImGui::PopStyleColor();
            if (lastExport.ok)
            {
                ImGui::BeginChild("##files", ImVec2(W, U(120)), ImGuiChildFlags_Borders);
                for (auto& f : lastExport.files) ImGui::TextUnformatted(f.c_str());
                ImGui::EndChild();
            }
        }
        ImGui::Dummy(ImVec2(0, U(6)));
        if (ImGui::Button("Close", ImVec2(U(100), 0))) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        if (lastExport.ok && ImGui::Button("Open folder", ImVec2(U(110), 0))) platform::OpenInExplorer(exportOpt.outDir);
        ImGui::SameLine(W - U(120) + ImGui::GetStyle().WindowPadding.x);
        if (ui::PrimaryButton("Export", ImVec2(U(120), 0)))
        {
            if (exportOpt.outDir.empty())
            {
                std::string d = platform::PickFolderDialog("Export folder");
                if (!d.empty()) exportOpt.outDir = d;
            }
            if (!exportOpt.outDir.empty())
            {
                lastExport = ExportProject(doc, exportOpt, platform::ExeDir() + "/runtime");
                MarkDirty();
                SaveSettings();
            }
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void App::DrawSettingsDialog()
{
    if (showSettings) { ImGui::OpenPopup("Settings"); showSettings = false; }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(U(500), 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(18), U(16)));
    if (ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings))
    {
        float W = U(464);
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
        ImGui::TextUnformatted("Claude API");
        ImGui::PopFont();
        ui::Label("API key");
        ImGui::SetNextItemWidth(W);
        static char key[256];
        if (ImGui::IsWindowAppearing()) snprintf(key, sizeof(key), "%s", ai.apiKey.c_str());
        if (ImGui::InputTextWithHint("##key", platform::GetEnv("ANTHROPIC_API_KEY").empty() ? "sk-ant-..." : "Using ANTHROPIC_API_KEY from the environment", key, sizeof(key), ImGuiInputTextFlags_Password))
            ai.apiKey = key;
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextWrapped("Stored encrypted for your Windows user (DPAPI) in %%APPDATA%%\\Fimgui. Leave empty to use ANTHROPIC_API_KEY.");
        ImGui::PopStyleColor();
        if (ImGui::SmallButton("Get an API key")) platform::OpenUrl("https://platform.claude.com/settings/keys");

        const char* models[] = { "claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5", "claude-fable-5-1" };
        const char* modelNames[] = { "Claude Opus 5.5 (default)", "Claude Sonnet 5.5 (faster)", "Claude Haiku 5.5 (fastest)", "Claude Fable 5.1 (most capable)" };
        int mi = 0;
        for (int i = 0; i < 4; i++) if (ai.model == models[i]) mi = i;
        ui::Label("Model");
        if (ui::Combo("##model", &mi, modelNames, 4, W)) ai.model = models[mi];
        const char* efforts[] = { "low", "medium", "high", "xhigh", "max" };
        int ei = 1;
        for (int i = 0; i < 5; i++) if (ai.effort == efforts[i]) ei = i;
        ui::Label("Effort (lower = faster drawing, higher = more careful designs)");
        if (ui::Segmented("##effort", &ei, efforts, 5, W)) ai.effort = efforts[ei];
        ui::Check("Send a canvas snapshot so the model sees the current design", &ai.sendSnapshot);
        ui::Label("API base URL");
        ui::TextField("##base", &ai.baseUrl, W);

        ImGui::Dummy(ImVec2(0, U(10)));
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
        ImGui::TextUnformatted("MCP server");
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextWrapped("Lets external AI clients (Claude Desktop, Claude Code and other MCP clients) read and edit the design live. Listens on 127.0.0.1 only.");
        ImGui::PopStyleColor();
        bool mcpChanged = ui::Check("Enable MCP server", &mcpEnabled);
        ImGui::SameLine(U(200));
        ui::Label("Port");
        ImGui::SameLine();
        static float portF = 0;
        if (ImGui::IsWindowAppearing()) portF = (float)mcpPort;
        if (ui::NumField("port", "", &portF, U(90), 1.0f, 1024, 65535)) {}
        if (ImGui::IsItemDeactivated() || (!ImGui::IsAnyItemActive() && (int)portF != mcpPort)) { if ((int)portF != mcpPort) { mcpPort = (int)portF; mcpChanged = true; } }
        if (mcpChanged) { StartMcp(); SaveSettings(); }
        ImGui::PushStyleColor(ImGuiCol_Text, mcp.Running() ? Success : (mcpEnabled ? Danger : TextFaint));
        if (mcp.Running()) ImGui::Text("Listening on http://127.0.0.1:%d/mcp  (%d calls)", mcp.Port(), mcp.Calls());
        else ImGui::TextUnformatted(mcpEnabled ? ("Not running: " + mcpError).c_str() : "Disabled");
        ImGui::PopStyleColor();
        std::string url = "http://127.0.0.1:" + std::to_string(mcpPort) + "/mcp";
        std::string bridge = platform::ExeDir() + "\\fimgui-mcp.exe";
        float bw = (W - U(12)) / 3;
        if (ImGui::Button("Claude Code", ImVec2(bw, 0)))
        {
            platform::SetClipboardText("claude mcp add --transport http fimgui " + url);
            Toast("Copied: claude mcp add --transport http fimgui ...");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies: claude mcp add --transport http fimgui %s", url.c_str());
        ImGui::SameLine(0, U(6));
        if (ImGui::Button("Claude Desktop", ImVec2(bw, 0)))
        {
            json cfg = { { "mcpServers", { { "fimgui", { { "command", bridge }, { "args", { "--port", std::to_string(mcpPort) } } } } } } };
            platform::SetClipboardText(cfg.dump(2));
            Toast("Copied claude_desktop_config.json snippet");
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copies a claude_desktop_config.json entry that runs fimgui-mcp.exe (stdio bridge)");
        ImGui::SameLine(0, U(6));
        if (ImGui::Button("URL", ImVec2(bw, 0))) { platform::SetClipboardText(url); Toast("Copied " + url); }
        ImGui::Dummy(ImVec2(0, U(8)));
        if (ui::PrimaryButton("Done", ImVec2(U(100), 0))) { SaveSettings(); ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

void App::DrawMainMenu()
{
    if (openMainMenu) { ImGui::OpenPopup("##mainmenupopup"); openMainMenu = false; }
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetMainViewport()->WorkPos.x + U(8), ImGui::GetMainViewport()->WorkPos.y + U(40)), ImGuiCond_Appearing);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(6), U(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(8), U(5)));
    if (ImGui::BeginPopup("##mainmenupopup"))
    {
        if (ImGui::BeginMenu("File"))
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
            if (ImGui::MenuItem("Place image / SVG...", "Ctrl+Shift+K"))
                for (auto& f : platform::OpenFilesDialog("Place image", "Media|*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.mp4;*.mov;*.wmv;*.m4v")) ImportImageFile(f, nullptr, ImVec2(0, 0));
            if (ImGui::MenuItem("Export ImGui project...", "Ctrl+E")) showExport = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit"))
        {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z")) Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Copy", "Ctrl+C")) CopySelection();
            if (ImGui::MenuItem("Paste", "Ctrl+V")) Paste();
            if (ImGui::MenuItem("Duplicate", "Ctrl+D")) DuplicateSelection();
            if (ImGui::MenuItem("Delete", "Del")) DeleteSelection();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            if (ImGui::MenuItem("Zoom to fit", "Shift+1")) ZoomToFit(false);
            if (ImGui::MenuItem("Zoom to selection", "Shift+2")) ZoomToFit(true);
            ImGui::MenuItem("Show all conditional layers", nullptr, &ignoreConditions);
            ImGui::Separator();
            ImGui::MenuItem("Pixel grid (from 400%)", "Shift+'", &pixelGrid);
            ImGui::MenuItem("Snap to pixel grid", "Ctrl+Shift+'", &snapToPixel);
            ImGui::MenuItem("Pixel preview", "Ctrl+Alt+Y", &pixelPreview);
            ImGui::Separator();
            if (ImGui::MenuItem("Preview", "Ctrl+Enter", preview)) preview = !preview;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help"))
        {
            if (ImGui::MenuItem("Keyboard shortcuts")) showShortcuts = true;
            if (ImGui::MenuItem("Settings...")) showSettings = true;
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);

    if (showShortcuts)
    {
        ImGui::SetNextWindowSize(ImVec2(U(420), U(460)), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::Begin("Keyboard shortcuts", &showShortcuts, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings))
        {
            const char* rows[][2] = {
                { "V / H", "Move / Hand" }, { "F, R, O, L, T", "Frame, Rect, Ellipse, Line, Text" }, { "Space + drag, middle mouse", "Pan" },
                { "Ctrl + wheel", "Zoom" }, { "Shift+1 / Shift+2 / Ctrl+0", "Fit all / selection / 100%" }, { "Ctrl+click", "Select deepest layer" },
                { "Double-click", "Enter group / edit text" }, { "Alt + drag", "Duplicate" }, { "Shift + drag", "Constrain / keep ratio" },
                { "Ctrl+G / Ctrl+Shift+G", "Group / Ungroup" }, { "Ctrl+Alt+G", "Frame selection" }, { "Ctrl+[ / Ctrl+]", "Send backward / forward" },
                { "Ctrl+D", "Duplicate" }, { "Ctrl+Shift+C", "Copy as PNG" }, { "Ctrl+V", "Paste layers / SVG / PNG / CSS / text" },
                { "Ctrl+Enter", "Preview" }, { "Shift+'", "Pixel grid (from 400%)" }, { "Ctrl+Shift+'", "Snap to pixel grid" }, { "Ctrl+Alt+Y", "Pixel preview" }, { "Ctrl+I", "AI assistant" }, { "Ctrl+E", "Export project" }, { "Esc", "Select parent / exit" },
            };
            if (ImGui::BeginTable("##sc", 2, ImGuiTableFlags_RowBg))
            {
                for (auto& r : rows)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0); ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(TextDim), "%s", r[0]);
                    ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(r[1]);
                }
                ImGui::EndTable();
            }
        }
        ImGui::End();
    }
}

// ---------------------------------------------------------------- shortcuts

void App::HandleShortcuts()
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId)) return;
    if (showHome) return;
    bool ctrl = io.KeyCtrl, shift = io.KeyShift, alt = io.KeyAlt;
    auto P = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };
    auto PR = [](ImGuiKey k) { return ImGui::IsKeyPressed(k, true); };

    if (preview)
    {
        if (P(ImGuiKey_Escape) || (ctrl && P(ImGuiKey_Enter))) preview = false;
        return;
    }
    if (ctrl)
    {
        if (P(ImGuiKey_Z)) { if (shift) Redo(); else Undo(); }
        else if (P(ImGuiKey_Y)) Redo();
        else if (P(ImGuiKey_C)) { if (shift) CopyAs(2); else CopySelection(); }
        else if (P(ImGuiKey_X)) CutSelection();
        else if (P(ImGuiKey_V)) Paste();
        else if (P(ImGuiKey_D)) DuplicateSelection();
        else if (P(ImGuiKey_G)) { if (shift) Ungroup(); else GroupSelection(alt); }
        else if (P(ImGuiKey_S)) SaveDocument(shift);
        else if (P(ImGuiKey_O)) { if (shift) OpenHome(); else OpenDocument(); }
        else if (P(ImGuiKey_N)) { newProjectFromCurrent = false; openNewProject = true; }
        else if (P(ImGuiKey_E)) showExport = true;
        else if (P(ImGuiKey_I)) rightTab = rightTab == Tab_AI ? Tab_Design : Tab_AI;
        else if (P(ImGuiKey_Enter)) preview = !preview;
        else if (shift && P(ImGuiKey_Apostrophe)) { snapToPixel = !snapToPixel; Toast(snapToPixel ? "Snap to pixel grid: on" : "Snap to pixel grid: off"); }
        else if (alt && P(ImGuiKey_Y)) { pixelPreview = !pixelPreview; Toast(pixelPreview ? "Pixel preview: on" : "Pixel preview: off"); }
        else if (P(ImGuiKey_A))
        {
            Node* s = Sel();
            auto& list = s ? doc.SiblingsOf(s) : doc.roots;
            std::vector<std::string> ids;
            for (auto& n : list) ids.push_back(n->id);
            SetSelection(ids);
        }
        else if (P(ImGuiKey_RightBracket)) ReorderSelection(shift ? 100 : 1);
        else if (P(ImGuiKey_LeftBracket)) ReorderSelection(shift ? -100 : -1);
        else if (P(ImGuiKey_0)) SetZoom(1.0f, ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
        else if (P(ImGuiKey_Equal) || P(ImGuiKey_KeypadAdd)) SetZoom(zoom * 1.5f, ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
        else if (P(ImGuiKey_Minus) || P(ImGuiKey_KeypadSubtract)) SetZoom(zoom / 1.5f, ImVec2((canvasMin.x + canvasMax.x) * 0.5f, (canvasMin.y + canvasMax.y) * 0.5f));
        else if (shift && P(ImGuiKey_H)) { for (Node* n : SelNodes()) n->visible = !n->visible; MarkDirty(); }
        else if (shift && P(ImGuiKey_L)) { for (Node* n : SelNodes()) n->locked = !n->locked; MarkDirty(); }
        else if (shift && P(ImGuiKey_K))
            for (auto& f : platform::OpenFilesDialog("Place image", "Media|*.png;*.jpg;*.jpeg;*.svg;*.bmp;*.gif;*.mp4;*.mov;*.wmv;*.m4v")) ImportImageFile(f, Sel() && Sel()->type == NodeType::Frame ? Sel() : nullptr, ImVec2(0, 0));
        return;
    }
    if (shift && P(ImGuiKey_1)) { ZoomToFit(false); return; }
    if (shift && P(ImGuiKey_Apostrophe)) { pixelGrid = !pixelGrid; Toast(pixelGrid ? "Pixel grid: on (from 400%)" : "Pixel grid: off"); return; }
    if (shift && P(ImGuiKey_2)) { ZoomToFit(true); return; }
    if (P(ImGuiKey_V)) tool = Tool::Move;
    if (P(ImGuiKey_H)) tool = Tool::Hand;
    if (P(ImGuiKey_F) || P(ImGuiKey_A)) tool = Tool::Frame;
    if (P(ImGuiKey_R)) tool = Tool::Rect;
    if (P(ImGuiKey_O)) tool = Tool::Ellipse;
    if (P(ImGuiKey_L)) tool = Tool::Line;
    if (P(ImGuiKey_T)) tool = Tool::Text;
    if (P(ImGuiKey_Delete) || P(ImGuiKey_Backspace)) DeleteSelection();
    if (P(ImGuiKey_Escape))
    {
        if (tool != Tool::Move) tool = Tool::Move;
        else if (Node* s = Sel()) { if (s->parent) Select(s->parent->id); else selection.clear(); }
    }
    if (P(ImGuiKey_Enter))
    {
        Node* s = Sel();
        if (s && s->type == NodeType::Text) StartTextEdit(s);
        else if (s && !s->children.empty())
        {
            std::vector<std::string> ids;
            for (auto& c : s->children) ids.push_back(c->id);
            SetSelection(ids);
        }
    }
    float step = shift ? 10.0f : 1.0f;
    if (PR(ImGuiKey_LeftArrow)) NudgeSelection(-step, 0);
    if (PR(ImGuiKey_RightArrow)) NudgeSelection(step, 0);
    if (PR(ImGuiKey_UpArrow)) NudgeSelection(0, -step);
    if (PR(ImGuiKey_DownArrow)) NudgeSelection(0, step);
}
