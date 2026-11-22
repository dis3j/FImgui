// Minecraft projects: the menu frame inside a game window, and the Minecraft panel of the Design tab.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "widgets.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <set>

using namespace theme;
namespace fs = std::filesystem;

namespace
{
    struct McWindow { const char* label; int w, h; };
    const McWindow kWindows[] = {
        { "854 x 480 (default)", 854, 480 }, { "1280 x 720", 1280, 720 }, { "1366 x 768", 1366, 768 },
        { "1920 x 1080", 1920, 1080 }, { "2560 x 1440", 2560, 1440 }, { "3840 x 2160", 3840, 2160 },
    };
    constexpr int kWindowCount = (int)(sizeof(kWindows) / sizeof(kWindows[0]));

    // Window.calculateScale(0, false): the largest scale that keeps the GUI at least 320 x 240.
    int AutoGuiScale(int w, int h)
    {
        int i = 1;
        while (i < w && i < h && w / (i + 1) >= 320 && h / (i + 1) >= 240) i++;
        return i;
    }
}

Node* App::McMenuFrame()
{
    for (auto& r : doc.roots) if (r->type == NodeType::Frame && r->visible) return r.get();   // the one ExportJava turns into the Screen
    return nullptr;
}

void App::McScreen(int* guiW, int* guiH, int* scale)
{
    const McWindow& win = kWindows[std::clamp(mcWindow, 0, kWindowCount - 1)];
    int autoScale = AutoGuiScale(win.w, win.h);
    int s = mcGuiScale <= 0 || mcGuiScale > autoScale ? autoScale : mcGuiScale;
    if (scale) *scale = s;
    if (guiW) *guiW = (win.w + s - 1) / s;   // Window.getGuiScaledWidth rounds up
    if (guiH) *guiH = (win.h + s - 1) / s;
}

// The generated Screen centers the menu and shrinks it to fit (never above 1 design px per GUI px), see codegen_java.cpp.
float App::McFit(Node* menu)
{
    int sw, sh;
    McScreen(&sw, &sh, nullptr);
    return std::min(1.0f, std::min((sw - 16) / std::max(1.0f, menu->w), (sh - 16) / std::max(1.0f, menu->h)));
}

// The game window in world coordinates: the menu stays 1:1, so a shrunk menu means a larger screen around it.
bool App::McScreenRect(ImVec2* a, ImVec2* b)
{
    Node* menu = McMenuFrame();
    if (!menu) return false;
    int sw, sh;
    McScreen(&sw, &sh, nullptr);
    float k = 1.0f / McFit(menu);
    ImVec2 p = menu->AbsPos();
    ImVec2 c(p.x + menu->w * 0.5f, p.y + menu->h * 0.5f);
    *a = ImVec2(c.x - sw * k * 0.5f, c.y - sh * k * 0.5f);
    *b = ImVec2(c.x + sw * k * 0.5f, c.y + sh * k * 0.5f);
    return true;
}

void App::McFitScreen()
{
    ImVec2 a, b;
    if (!mcScreenShown || !McScreenRect(&a, &b)) { ZoomToFit(false); return; }
    float cw = canvasMax.x - canvasMin.x, ch = canvasMax.y - canvasMin.y;
    if (cw <= 0 || ch <= 0) return;
    float bw = b.x - a.x, bh = b.y - a.y;
    zoom = std::clamp(std::min(cw / bw, (ch - U(40)) / bh) * 0.92f, 0.02f, 8.0f);
    pan = ImVec2(cw * 0.5f - (a.x + bw * 0.5f) * zoom, ch * 0.5f - (a.y + bh * 0.5f) * zoom);
}

void App::DrawMinecraftScreen(ImDrawList* dl)
{
    ImVec2 wa, wb;
    if (!IsMinecraft() || !mcScreenShown || !McScreenRect(&wa, &wb)) return;
    ImVec2 a = WorldToScreen(wa), b = WorldToScreen(wb);
    a = ImVec2(std::round(a.x), std::round(a.y));
    b = ImVec2(std::round(b.x), std::round(b.y));

    // In a world, Screen.renderBackground blurs the game and darkens it: a blurred sky over terrain, then the menu tint.
    float hz = a.y + (b.y - a.y) * 0.58f;
    dl->AddRectFilledMultiColor(a, ImVec2(b.x, hz), IM_COL32(104, 146, 214, 255), IM_COL32(120, 160, 222, 255), IM_COL32(176, 204, 238, 255), IM_COL32(168, 198, 236, 255));
    dl->AddRectFilledMultiColor(ImVec2(a.x, hz), b, IM_COL32(96, 128, 72, 255), IM_COL32(88, 120, 66, 255), IM_COL32(70, 82, 52, 255), IM_COL32(76, 88, 56, 255));
    dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 120));
    dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1), IM_COL32(255, 255, 255, 40));

    int sw, sh, scale;
    McScreen(&sw, &sh, &scale);
    const McWindow& win = kWindows[std::clamp(mcWindow, 0, kWindowCount - 1)];
    char label[128];
    snprintf(label, sizeof(label), "Minecraft  %d x %d  -  GUI scale %d  -  %d x %d GUI px", win.w, win.h, scale, sw, sh);
    ImFont* f = fonts::UI(fonts::UI_Medium);
    float fs = 11 * dpi;
    dl->AddText(f, fs, ImVec2(a.x, a.y - fs - U(6)), TextDim, label);
    Node* menu = McMenuFrame();
    float fit = McFit(menu);
    if (fit < 0.999f)
    {
        snprintf(label, sizeof(label), "The menu is larger than the screen: in game it is shrunk to %d%%", (int)std::floor(fit * 100));
        dl->AddText(f, fs, ImVec2(a.x, b.y + U(6)), IM_COL32(255, 196, 64, 255), label);
    }
}

void App::DrawMinecraftPanel(float W)
{
    float lx = W * 0.36f;
    auto Row = [&](const char* label) { ui::Label(label); ImGui::SameLine(lx); };

    // ---- game screen
    ui::Section("Minecraft screen");
    ui::Check("Show the game screen", &mcScreenShown);
    const char* windows[kWindowCount];
    for (int i = 0; i < kWindowCount; i++) windows[i] = kWindows[i].label;
    Row("Window");
    if (ui::Combo("##mcwin", &mcWindow, windows, kWindowCount, W - lx)) modified = true;
    const McWindow& win = kWindows[std::clamp(mcWindow, 0, kWindowCount - 1)];
    int autoScale = AutoGuiScale(win.w, win.h);
    char autoLabel[32];
    snprintf(autoLabel, sizeof(autoLabel), "Auto (%d)", autoScale);
    const char* scales[] = { autoLabel, "1", "2", "3", "4", "5", "6" };
    int scaleIdx = mcGuiScale > autoScale ? 0 : mcGuiScale;
    Row("GUI scale");
    if (ui::Combo("##mcscale", &scaleIdx, scales, autoScale + 1, W - lx)) { mcGuiScale = scaleIdx; modified = true; }

    int sw, sh, scale;
    McScreen(&sw, &sh, &scale);
    Node* menu = McMenuFrame();
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
    if (!menu) ImGui::TextWrapped("Draw a frame (F): the first top-level frame becomes the Minecraft screen. 1 design px = 1 GUI px.");
    else
    {
        float fit = McFit(menu);
        if (fit < 0.999f) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 196, 64, 255));
        ImGui::Text("Menu %d x %d in a %d x %d GUI screen%s", (int)menu->w, (int)menu->h, sw, sh, fit < 0.999f ? "" : ".");
        if (fit < 0.999f)
        {
            ImGui::Text("It is shrunk to %d%% in game: keep it under %d x %d.", (int)std::floor(fit * 100), sw - 16, sh - 16);
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::PopStyleColor();
    float half = (W - U(6)) * 0.5f;
    if (ImGui::Button("Fit screen", ImVec2(half, 0))) McFitScreen();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show the whole game window (Shift+1 fits the layers)");
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("Actual size", ImVec2(half, 0)) && menu)
    {
        // As big as on a monitor of the chosen resolution: one GUI px is `scale` screen px.
        float cw = canvasMax.x - canvasMin.x, ch = canvasMax.y - canvasMin.y;
        ImVec2 p = menu->AbsPos();
        zoom = (float)scale;
        pan = ImVec2(cw * 0.5f - (p.x + menu->w * 0.5f) * zoom, ch * 0.5f - (p.y + menu->h * 0.5f) * zoom);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zoom %d00%%: the menu as large as in game at GUI scale %d", scale, scale);

    // ---- mod
    ui::Section("Mod");
    const char* loaders[] = { "Fabric", "NeoForge" };
    Row("Loader");
    if (ui::Segmented("##mcloader", &javaOpt.loader, loaders, 2, W - lx)) modified = true;
    Row("Mod id");
    if (ui::TextField("##mcmodid", &javaOpt.modId, W - lx, "examplemod")) modified = true;
    Row("Package");
    if (ui::TextField("##mcpkg", &javaOpt.package, W - lx, "com.example.gui")) modified = true;
    Row("Class");
    if (ui::TextField("##mccls", &javaOpt.className, W - lx, menu ? menu->name.c_str() : "from the frame name")) modified = true;
    Row("Textures");
    const char* texScales[] = { "1x", "2x", "3x", "4x" };
    int ts = std::clamp((int)std::lround(javaOpt.textureScale), 1, 4) - 1;
    if (ui::Segmented("##mctex", &ts, texScales, 4, W - lx)) { javaOpt.textureScale = (float)(ts + 1); modified = true; }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Resolution of the baked layer textures: sharp up to this GUI scale");
    if (ui::Check("Design fonts as TTF providers", &javaOpt.ttfFonts)) modified = true;

    // ---- what changes in the game
    struct Note { std::string id, text; };
    std::vector<Note> notes;
    if (menu)
    {
        int frames = 0;
        for (auto& r : doc.roots) if (r->type == NodeType::Frame && r->visible) frames++;
        if (frames > 1) notes.push_back({ menu->id, "Only \"" + menu->name + "\" is exported: it is the first top-level frame" });
        std::set<std::string> fontsSeen;
        std::function<void(Node*)> scan = [&](Node* n) {
            if (!n->visible) return;
            if (n->blur.on || n->glass.on) notes.push_back({ n->id, n->name + ": blur / glass is baked without the world behind it" });
            for (const std::string* asset : { &n->asset, n->texture.on ? &n->texture.asset : nullptr })
            {
                if (!asset || asset->empty()) continue;
                auto it = doc.assets.find(*asset);
                if (it != doc.assets.end() && (it->second.kind == 2 || it->second.kind == 3))
                    notes.push_back({ n->id, n->name + ": " + (it->second.kind == 2 ? "GIF" : "video") + " shows its first frame" });
            }
            if (n->type == NodeType::Text && javaOpt.ttfFonts && fontsSeen.insert(n->fontFamily + " " + n->fontStyle).second)
            {
                const FontFace* face = fonts::Find(n->fontFamily, n->fontStyle);
                std::string ext = face ? fs::u8path(face->path).extension().u8string() : std::string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext != ".ttf") notes.push_back({ n->id, n->fontFamily + " " + n->fontStyle + ": not a .ttf font, the vanilla font is used" });
            }
            for (auto& c : n->children) scan(c.get());
        };
        scan(menu);
    }
    if (!notes.empty())
    {
        ui::Section("In game");
        for (size_t i = 0; i < notes.size(); i++)
        {
            ImGui::PushID((int)i);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 196, 64, 230));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
            ImGui::TextWrapped("%s", notes[i].text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); ImGui::SetTooltip("Select the layer"); }
            if (ImGui::IsItemClicked()) Select(notes[i].id);
            ImGui::PopID();
        }
    }
}
