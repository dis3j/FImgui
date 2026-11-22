// Fill picker tabs (Solid, Gradient, Pattern, Image, Video, Shader, Blend mode, Contrast) on top of the color picker.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "exporters.h"
#include "platform.h"
#include "gfx.h"
#include "../runtime/fimgui_fx.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;
using namespace theme;

enum PickTab { Pick_Solid, Pick_Gradient, Pick_Pattern, Pick_Image, Pick_Video, Pick_Shader, Pick_Blend, Pick_Contrast, Pick_Count };

static void TabGlyph(ImDrawList* dl, int tab, ImVec2 c, float s, ImU32 col)
{
    float h = s * 0.5f;
    ImVec2 a(c.x - h, c.y - h), b(c.x + h, c.y + h);
    switch (tab)
    {
    case Pick_Solid: dl->AddRect(a, b, col, U(2), 0, 1.2f); dl->AddRectFilled(ImVec2(a.x + U(3), a.y + U(3)), ImVec2(b.x - U(3), b.y - U(3)), col, U(1)); break;
    case Pick_Gradient:
        dl->AddRect(a, b, col, U(2), 0, 1.2f);
        for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) if ((i + j) % 2 == 0) dl->AddRectFilled(ImVec2(a.x + U(2.5f) + i * s * 0.28f, a.y + U(2.5f) + j * s * 0.28f), ImVec2(a.x + U(2.5f) + i * s * 0.28f + s * 0.2f, a.y + U(2.5f) + j * s * 0.28f + s * 0.2f), (col & 0x00FFFFFF) | ((ImU32)(80 + 60 * j) << 24));
        break;
    case Pick_Pattern:
        dl->AddRect(a, b, col, U(2), 0, 1.2f);
        for (int i = 0; i < 3; i++) dl->AddLine(ImVec2(a.x + s * (0.3f + 0.2f * i), a.y + U(2)), ImVec2(a.x + s * (0.3f + 0.2f * i), b.y - U(2)), col, 1.0f);
        dl->AddLine(ImVec2(a.x + U(2), c.y), ImVec2(b.x - U(2), c.y), col, 1.0f);
        break;
    case Pick_Image:
        dl->AddRect(a, b, col, U(2), 0, 1.2f);
        dl->AddTriangleFilled(ImVec2(a.x + U(2.5f), b.y - U(2.5f)), ImVec2(c.x - U(1), c.y), ImVec2(c.x + U(3), b.y - U(2.5f)), col);
        dl->AddCircleFilled(ImVec2(b.x - U(4.5f), a.y + U(4.5f)), U(1.6f), col);
        break;
    case Pick_Video:
        dl->AddRect(a, b, col, U(3), 0, 1.2f);
        dl->AddTriangleFilled(ImVec2(c.x - U(2.5f), c.y - U(3.5f)), ImVec2(c.x + U(3.5f), c.y), ImVec2(c.x - U(2.5f), c.y + U(3.5f)), col);
        break;
    case Pick_Shader:
        dl->AddRect(a, b, col, U(2), 0, 1.2f);
        for (int k = 0; k < 3; k++)
        {
            float y = a.y + s * (0.3f + 0.2f * k);
            dl->PathClear();
            for (int i = 0; i <= 8; i++) dl->PathLineTo(ImVec2(a.x + U(2) + (s - U(4)) * i / 8, y + std::sin(i * 0.8f + k) * U(1.2f)));
            dl->PathStroke(col, 0, 1.0f);
        }
        break;
    case Pick_Blend:
        dl->PathClear();
        dl->PathLineTo(ImVec2(c.x, a.y));
        dl->PathBezierCubicCurveTo(ImVec2(c.x + h * 0.6f, c.y - h * 0.2f), ImVec2(c.x + h * 0.85f, c.y + h * 0.4f), ImVec2(c.x, b.y));
        dl->PathBezierCubicCurveTo(ImVec2(c.x - h * 0.85f, c.y + h * 0.4f), ImVec2(c.x - h * 0.6f, c.y - h * 0.2f), ImVec2(c.x, a.y));
        dl->PathStroke(col, 0, 1.2f);
        break;
    case Pick_Contrast:
        dl->AddCircle(c, h, col, 0, 1.2f);
        dl->PathClear();
        dl->PathArcTo(c, h, -IM_PI * 0.5f, IM_PI * 0.5f);
        dl->PathFillConvex(col);
        break;
    }
}

static float Luminance(const ImVec4& c)
{
    auto ch = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); };
    return 0.2126f * ch(c.x) + 0.7152f * ch(c.y) + 0.0722f * ch(c.z);
}

// Background behind a node: the nearest ancestor with a mostly opaque solid fill, else the page color.
static ImVec4 BackgroundOf(const Node* n, const ImVec4& page)
{
    for (const Node* q = n ? n->parent : nullptr; q; q = q->parent)
        if (q->fill.type && q->fill.c0.w > 0.5f) return q->fill.type >= 2 ? ImVec4((q->fill.c0.x + q->fill.c1.x) * 0.5f, (q->fill.c0.y + q->fill.c1.y) * 0.5f, (q->fill.c0.z + q->fill.c1.z) * 0.5f, 1) : q->fill.c0;
    return page;
}

static void AssetThumb(ImDrawList* dl, const Asset* a, ImVec2 p, float s)
{
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), Field, U(6));
    if (!a) return;
    gfx::TexInfo ti = gfx::AssetTexture(*a, s, s, false);
    if (!ti.id) return;
    ImVec2 ia(p.x + U(2), p.y + U(2)), ib(p.x + s - U(2), p.y + s - U(2));
    fim::ContainRect(ImVec2((float)std::max(1, a->width), (float)std::max(1, a->height)), &ia, &ib);
    dl->AddImage((ImTextureID)ti.id, ia, ib);
}

void App::CollectPageColors()
{
    std::vector<ImVec4> out;
    std::function<void(const Node*)> rec = [&](const Node* n) {
        for (const Paint* p : { &n->fill, &n->stroke })
            for (int i = 0; i < (p->type >= 2 ? 2 : p->type == 1 ? 1 : 0); i++)
            {
                ImVec4 c = i ? p->c1 : p->c0;
                ImU32 u = ImGui::ColorConvertFloat4ToU32(c);
                if (std::none_of(out.begin(), out.end(), [&](const ImVec4& q) { return ImGui::ColorConvertFloat4ToU32(q) == u; })) out.push_back(c);
            }
        for (auto& c : n->children) if (out.size() < 32) rec(c.get());
    };
    for (auto& r : doc.roots) rec(r.get());
    ui::SetPageColors(out);
}

// Icon button with a custom glyph (gradient flip / rotate).
static bool GlyphButton(const char* id, const char* tip, void (*glyph)(ImDrawList*, ImVec2, ImU32))
{
    float s = ImGui::GetFrameHeight();
    ImVec2 q = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(s, s));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov) dl->AddRectFilled(q, ImVec2(q.x + s, q.y + s), Hover, U(5));
    glyph(dl, ImVec2(q.x + s * 0.5f, q.y + s * 0.5f), hov ? Text : TextDim);
    if (hov && tip) ImGui::SetTooltip("%s", tip);
    return clicked;
}

static void FlipGlyph(ImDrawList* dl, ImVec2 c, ImU32 col)
{
    float w = U(6), y0 = c.y - U(3), y1 = c.y + U(3), a = U(2.5f);
    dl->AddLine(ImVec2(c.x - w, y0), ImVec2(c.x + w, y0), col, 1.3f);
    dl->AddLine(ImVec2(c.x + w - a, y0 - a), ImVec2(c.x + w, y0), col, 1.3f);
    dl->AddLine(ImVec2(c.x + w - a, y0 + a), ImVec2(c.x + w, y0), col, 1.3f);
    dl->AddLine(ImVec2(c.x + w, y1), ImVec2(c.x - w, y1), col, 1.3f);
    dl->AddLine(ImVec2(c.x - w + a, y1 - a), ImVec2(c.x - w, y1), col, 1.3f);
    dl->AddLine(ImVec2(c.x - w + a, y1 + a), ImVec2(c.x - w, y1), col, 1.3f);
}

static void RotateGlyph(ImDrawList* dl, ImVec2 c, ImU32 col)
{
    float r = U(5.5f);
    dl->PathArcTo(c, r, -IM_PI * 0.35f, IM_PI * 1.25f, 16);
    dl->PathStroke(col, 0, 1.3f);
    ImVec2 e(c.x + std::cos(-IM_PI * 0.35f) * r, c.y + std::sin(-IM_PI * 0.35f) * r);
    dl->AddTriangleFilled(ImVec2(e.x - U(3.5f), e.y - U(1)), ImVec2(e.x + U(1.5f), e.y - U(3.5f)), ImVec2(e.x + U(1.5f), e.y + U(2)), col);
}

// Color of a gradient at t from its stops.
static ImVec4 StopColorAt(const std::vector<GradStop>& st, float t)
{
    if (st.empty()) return ImVec4(1, 1, 1, 1);
    if (t <= st.front().pos) return st.front().color;
    if (t >= st.back().pos) return st.back().color;
    for (size_t i = 0; i + 1 < st.size(); i++)
        if (t <= st[i + 1].pos)
        {
            float span = st[i + 1].pos - st[i].pos, k = span > 1e-5f ? (t - st[i].pos) / span : 1.0f;
            return ImLerp(st[i].color, st[i + 1].color, k);
        }
    return st.back().color;
}

// Figma-style adjustment slider: a pill track centered at 0 with a round knob. Double-click resets.
static bool AdjustSlider(const char* id, float* v, float w)
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float h = ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(w, h));
    bool changed = false;
    float x0 = p.x + h * 0.5f, x1 = p.x + w - h * 0.5f;
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(0, 0.0f))
    {
        float nv = std::clamp(((io.MousePos.x - x0) / (x1 - x0)) * 2.0f - 1.0f, -1.0f, 1.0f);
        if (std::fabs(nv) < 0.03f) nv = 0;   // snaps to the middle
        if (nv != *v) { *v = nv; changed = true; }
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && *v != 0) { *v = 0; changed = true; }
    float cy = p.y + h * 0.5f, th = U(14);
    dl->AddRectFilled(ImVec2(p.x, cy - th * 0.5f), ImVec2(p.x + w, cy + th * 0.5f), Field, th * 0.5f);
    dl->AddRect(ImVec2(p.x, cy - th * 0.5f), ImVec2(p.x + w, cy + th * 0.5f), IM_COL32(255, 255, 255, 20), th * 0.5f);
    float kx = x0 + (x1 - x0) * (*v * 0.5f + 0.5f), mid = (x0 + x1) * 0.5f;
    if (*v != 0) dl->AddRectFilled(ImVec2(std::min(mid, kx), cy - U(2)), ImVec2(std::max(mid, kx), cy + U(2)), Accent, U(2));
    dl->AddCircleFilled(ImVec2(kx, cy), U(8), IM_COL32_WHITE);
    dl->AddCircle(ImVec2(kx, cy), U(8.5f), IM_COL32(0, 0, 0, 70));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetTooltip("%+.0f", *v * 100.0f);
    return changed;
}

static float Contrast(const ImVec4& fg, const ImVec4& bg)
{
    // a translucent color is seen mixed with what is behind it
    ImVec4 mix(fg.x * fg.w + bg.x * (1 - fg.w), fg.y * fg.w + bg.y * (1 - fg.w), fg.z * fg.w + bg.z * (1 - fg.w), 1);
    float l1 = Luminance(mix), l2 = Luminance(bg);
    return (std::max(l1, l2) + 0.05f) / (std::min(l1, l2) + 0.05f);
}

void App::ApplyImageAdjust(Node* n)
{
    TextureFx& tx = n->texture;
    if (tx.source.empty()) tx.source = tx.asset;
    std::string old = tx.asset;
    if (!tx.Adjusted())
    {
        tx.asset = tx.source;
        tx.source.clear();
    }
    else
    {
        std::string id = BakeAdjustedImage(doc, tx.source, tx.adjust, old != tx.source ? old : std::string());
        if (id.empty()) { Toast("Adjustments work on PNG / JPG images", true); return; }
        tx.asset = id;
    }
    // drop an adjusted copy nothing uses any more
    auto it = doc.assets.find(old);
    if (old != tx.asset && it != doc.assets.end() && it->second.name.rfind("adj_", 0) == 0)
    {
        bool used = false;
        doc.ForEach([&](Node* q) { if ((q->texture.on && q->texture.asset == old) || q->asset == old) used = true; });
        if (!used) { gfx::InvalidateAsset(old); doc.assets.erase(it); }
    }
    MarkDirty();
}

int App::FillPickerHeader(Node* n, Paint* p, ImVec4** target, bool mediaFirst)
{
    int res = 0;
    float W = U(240);
    ImGuiIO& io = ImGui::GetIO();
    float fh = ImGui::GetFrameHeight();
    bool rich = n->type == NodeType::Frame || n->type == NodeType::Rect || n->type == NodeType::Ellipse;
    if (ImGui::IsWindowAppearing())
    {
        int kind = -1;
        auto it = doc.assets.find(n->texture.asset);
        if (n->texture.on && it != doc.assets.end()) kind = it->second.kind;
        bool shader = n->texture.on && it != doc.assets.end() && it->second.name.rfind("shader_", 0) == 0;
        bool pattern = n->texture.on && n->texture.fit == 2 && kind == 0;
        pickerTab = mediaFirst && n->texture.on ? (shader ? Pick_Shader : kind == 3 ? Pick_Video : pattern ? Pick_Pattern : Pick_Image) : (p->type >= 2 ? Pick_Gradient : Pick_Solid);
        pickerStop = 0;
        shaderC0 = p->c0;
        shaderC1 = p->type >= 2 ? p->c1 : ImVec4(p->c0.x * 0.3f, p->c0.y * 0.4f, std::min(1.0f, p->c0.z + 0.4f), 1);
    }
    // Tabs: paint kinds on the left, blend mode and contrast check on the right (Figma layout).
    const char* tips[] = { "Solid", "Gradient", "Pattern", "Image", "Video", "Shader", "Blend mode", "Check color contrast" };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float tx0 = ImGui::GetCursorPosX();
    for (int t = 0; t < Pick_Count; t++)
    {
        bool enabled = rich || t == Pick_Solid || t == Pick_Contrast;
        if (!enabled) continue;
        if (t == Pick_Blend) ImGui::SameLine(tx0 + W - fh * 2 - U(2));
        else if (t == Pick_Contrast) ImGui::SameLine(rich ? 0.0f : tx0 + W - fh, rich ? U(2) : 0.0f);
        else if (t) ImGui::SameLine(0, U(2));
        ImGui::PushID(t);
        ImVec2 q = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##ptab", ImVec2(fh, fh)))
        {
            pickerTab = t;
            if (t == Pick_Solid && p->type != 1) { p->type = 1; res |= 1; }
            if (t == Pick_Gradient && p->type < 2)
            {
                p->type = 2;
                p->c1 = ImVec4(p->c0.x * 0.35f, p->c0.y * 0.35f, p->c0.z * 0.35f, p->c0.w);
                p->stops.clear();
                res |= 1;
            }
        }
        bool hov = ImGui::IsItemHovered();
        bool on = pickerTab == t;
        if (on && t == Pick_Contrast) dl->AddRectFilled(q, ImVec2(q.x + fh, q.y + fh), AccentDim, U(5));
        else if (on) dl->AddRectFilled(q, ImVec2(q.x + fh, q.y + fh), IM_COL32(255, 255, 255, 30), U(5));
        else if (hov) dl->AddRectFilled(q, ImVec2(q.x + fh, q.y + fh), Hover, U(5));
        TabGlyph(dl, t, ImVec2(q.x + fh * 0.5f, q.y + fh * 0.5f), U(13), on || hov ? Text : TextDim);
        if (hov) ImGui::SetTooltip("%s", tips[t]);
        ImGui::PopID();
    }
    ImVec2 lp = ImGui::GetCursorScreenPos();
    dl->AddLine(ImVec2(lp.x - U(12), lp.y + U(2)), ImVec2(lp.x + W + U(12), lp.y + U(2)), Divider);
    ImGui::Dummy(ImVec2(W, U(8)));

    TextureFx& tx = n->texture;
    auto setTexture = [&](const std::string& asset, int fit, float opacity) {
        tx.on = true;
        tx.asset = asset;
        tx.source.clear();
        for (float& a : tx.adjust) a = 0;
        tx.fit = fit;
        tx.opacity = opacity;
        MarkDirty();
    };
    // Blend modes in Figma's menu order; returns true on change.
    auto blendCombo = [&](const char* id, int* mode, float w) {
        const char* labels[8];
        int sel = 0;
        for (int i = 0; i < 8; i++) { labels[i] = BlendModeLabel(kBlendMenu[i]); if (kBlendMenu[i] == *mode) sel = i; }
        if (!ui::Combo(id, &sel, labels, 8, w)) return false;
        *mode = kBlendMenu[sel];
        return true;
    };
    auto mediaControls = [&](bool withFit) {
        float half = (W - U(8)) * 0.5f;
        ui::SubLabels("Opacity", "Blend", half + U(8));
        float op = std::round(tx.opacity * 100);
        if (ui::NumField("txop", "", &op, half, 1.0f, 0, 100, "%")) { tx.opacity = op / 100; MarkDirty(); }
        ImGui::SameLine(0, U(8));
        if (blendCombo("##txbl", &tx.blend, half)) MarkDirty();
        (void)withFit;
        if (tx.fit == 2)
        {
            ui::SubLabels("Scale", "Scroll speed", half + U(8));
            float sc = std::round(tx.scale * 100);
            if (ui::NumField("txsc", "", &sc, half, 1.0f, 1, 2000, "%")) { tx.scale = sc / 100; MarkDirty(); }
            ImGui::SameLine(0, U(8));
            float sp = tx.speed.y;
            if (ui::NumField("txsp", "", &sp, half, 1.0f, -2000, 2000, " px/s")) { tx.speed.y = sp; MarkDirty(); }
        }
        if (ImGui::Button("Remove media", ImVec2(W, 0))) { tx.on = false; MarkDirty(); }
    };

    switch (pickerTab)
    {
    case Pick_Gradient:
    {
        // Keep c0 / c1 in step with the end stops (the color body below may have changed a stop).
        {
            ImVec4 o0 = p->c0, o1 = p->c1;
            p->SetStops(p->Stops());
            if (ImGui::ColorConvertFloat4ToU32(o0) != ImGui::ColorConvertFloat4ToU32(p->c0) || ImGui::ColorConvertFloat4ToU32(o1) != ImGui::ColorConvertFloat4ToU32(p->c1)) res |= 1;
        }
        std::vector<GradStop> st = p->Stops();
        pickerStop = std::clamp(pickerStop, 0, (int)st.size() - 1);
        bool stopsChanged = false;
        int remove = -1;
        float x0 = ImGui::GetCursorPosX();

        // kind, flip, rotate
        const char* kinds[] = { "Linear", "Radial", "Angular", "Diamond" };
        int k = std::clamp(p->type - 2, 0, 3);
        if (ui::Combo("##gkind", &k, kinds, 4, U(120))) { p->type = 2 + k; res |= 1; }
        ImGui::SameLine(x0 + W - fh * 2 - U(2));
        if (GlyphButton("##gflip", "Flip gradient", FlipGlyph))
        {
            for (auto& s : st) s.pos = 1.0f - s.pos;
            std::reverse(st.begin(), st.end());
            pickerStop = (int)st.size() - 1 - pickerStop;
            stopsChanged = true;
        }
        ImGui::SameLine(0, U(2));
        if (GlyphButton("##grot", "Rotate 90\xC2\xB0", RotateGlyph))
        {
            p->angle = std::fmod(p->angle + 90.0f, 360.0f);
            res |= 1;
        }

        // the bar, with stop handles above it: drag a handle to move it, click the bar to add a stop
        ImGui::Dummy(ImVec2(0, U(2)));
        float pinH = U(22), bh = U(24), pad = U(9);
        ImVec2 g = ImGui::GetCursorScreenPos();
        float bx0 = g.x + pad, bx1 = g.x + W - pad, by0 = g.y + pinH, by1 = by0 + bh;
        auto pinX = [&](float t) { return bx0 + (bx1 - bx0) * t; };
        ImGui::InvisibleButton("##gbar", ImVec2(W, pinH + bh));
        int* dragIdx = ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("##gdrag"), -1);
        if (ImGui::IsItemActivated())
        {
            ImVec2 m = io.MousePos;
            int hit = -1;
            for (int i = (int)st.size() - 1; i >= 0 && hit < 0; i--)
                if (std::fabs(m.x - pinX(st[i].pos)) < U(9) && m.y < by0 + U(2)) hit = i;
            if (hit < 0)
            {
                float t = ImSaturate((m.x - bx0) / (bx1 - bx0));
                st.push_back({ t, StopColorAt(st, t) });
                hit = (int)st.size() - 1;
                stopsChanged = true;
            }
            pickerStop = hit;
            *dragIdx = hit;
        }
        if (ImGui::IsItemActive() && *dragIdx >= 0 && *dragIdx < (int)st.size() && ImGui::IsMouseDragging(0, 1.0f))
        {
            float t = ImSaturate((io.MousePos.x - bx0) / (bx1 - bx0));
            if (t != st[*dragIdx].pos) { st[*dragIdx].pos = std::round(t * 1000.0f) / 1000.0f; stopsChanged = true; }
        }
        if (!ImGui::IsItemActive()) *dragIdx = -1;
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

        // sort by position, following the selected / dragged stop
        if (stopsChanged)
        {
            std::vector<int> order(st.size());
            for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
            std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return st[a].pos < st[b].pos; });
            std::vector<GradStop> sorted;
            int ns = pickerStop, nd = *dragIdx;
            for (size_t i = 0; i < order.size(); i++)
            {
                sorted.push_back(st[order[i]]);
                if (order[i] == pickerStop) ns = (int)i;
                if (order[i] == *dragIdx) nd = (int)i;
            }
            st = sorted;
            pickerStop = ns;
            *dragIdx = nd;
        }

        ui::Checker(dl, ImVec2(bx0, by0), ImVec2(bx1, by1), U(6));
        auto u32 = [](const ImVec4& c) { return ImGui::ColorConvertFloat4ToU32(c); };
        dl->AddRectFilled(ImVec2(bx0, by0), ImVec2(pinX(st.front().pos), by1), u32(st.front().color));
        for (size_t i = 0; i + 1 < st.size(); i++)
        {
            ImU32 a0 = u32(st[i].color), a1 = u32(st[i + 1].color);
            dl->AddRectFilledMultiColor(ImVec2(pinX(st[i].pos), by0), ImVec2(pinX(st[i + 1].pos), by1), a0, a1, a1, a0);
        }
        dl->AddRectFilled(ImVec2(pinX(st.back().pos), by0), ImVec2(bx1, by1), u32(st.back().color));
        dl->AddRect(ImVec2(bx0, by0), ImVec2(bx1, by1), IM_COL32(255, 255, 255, 40), U(2));
        for (int i = 0; i < (int)st.size(); i++)
        {
            float px = pinX(st[i].pos);
            bool sel = i == pickerStop;
            ImU32 frame = sel ? Accent : IM_COL32(235, 235, 240, 255);
            ImVec2 a(px - U(8), g.y + U(1)), b(px + U(8), g.y + U(17));
            dl->AddTriangleFilled(ImVec2(px - U(4), b.y - U(1)), ImVec2(px + U(4), b.y - U(1)), ImVec2(px, by0 - U(1)), frame);
            dl->AddRectFilled(a, b, frame, U(4));
            ImVec2 ia(a.x + U(2.5f), a.y + U(2.5f)), ib(b.x - U(2.5f), b.y - U(2.5f));
            if (st[i].color.w < 1.0f) ui::Checker(dl, ia, ib, U(3));
            dl->AddRectFilled(ia, ib, u32(st[i].color), U(2));
        }

        // stop list
        ImGui::Dummy(ImVec2(0, U(4)));
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
        ImGui::TextUnformatted("Stops");
        ImGui::PopFont();
        ImGui::SameLine(x0 + W - fh);
        if (IconButton("##gadd", Icon::Plus, false, 0, "Add stop"))
        {
            // in the middle of the widest gap
            float best = -1, at = 0.5f;
            for (size_t i = 0; i + 1 < st.size(); i++)
                if (st[i + 1].pos - st[i].pos > best) { best = st[i + 1].pos - st[i].pos; at = (st[i].pos + st[i + 1].pos) * 0.5f; }
            GradStop ns{ at, StopColorAt(st, at) };
            auto pos = std::upper_bound(st.begin(), st.end(), at, [](float t, const GradStop& s) { return t < s.pos; });
            pickerStop = (int)(pos - st.begin());
            st.insert(pos, ns);
            stopsChanged = true;
        }
        for (int i = 0; i < (int)st.size(); i++)
        {
            ImGui::PushID(i);
            ImVec2 rp = ImGui::GetCursorScreenPos();
            bool sel = i == pickerStop;
            if (sel) dl->AddRectFilled(ImVec2(rp.x - U(12), rp.y - U(2)), ImVec2(rp.x + W + U(12), rp.y + fh + U(2)), IM_COL32(13, 153, 255, 38));
            float pos = std::round(st[i].pos * 100.0f);
            if (ui::NumField("##gp", "", &pos, U(54), 1.0f, 0, 100, "%")) { st[i].pos = pos / 100.0f; stopsChanged = true; }
            if (ImGui::IsItemActivated()) pickerStop = i;
            ImGui::SameLine(0, U(6));
            float rowW = W - U(54) - U(6) - fh - U(4);
            float opW = U(50), hexW = rowW - opW - U(4);
            ImVec2 hp = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(hp, ImVec2(hp.x + hexW, hp.y + fh), Field, ImGui::GetStyle().FrameRounding);
            if (ImGui::InvisibleButton("##gsw", ImVec2(fh, fh))) pickerStop = i;
            ImVec2 sa(hp.x + U(5), hp.y + U(5)), sb(hp.x + fh - U(5), hp.y + fh - U(5));
            if (st[i].color.w < 1.0f) ui::Checker(dl, sa, sb);
            dl->AddRectFilled(sa, sb, u32(st[i].color), U(2));
            dl->AddRect(sa, sb, IM_COL32(255, 255, 255, 40), U(2));
            ImGui::SameLine(0, 0);
            char buf[16];
            snprintf(buf, sizeof(buf), "%s", ColorToHex(ImVec4(st[i].color.x, st[i].color.y, st[i].color.z, 1)).c_str() + 1);
            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
            ImGui::SetNextItemWidth(hexW - fh);
            if (ImGui::InputText("##ghex", buf, sizeof(buf), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsUppercase))
            {
                ImVec4 c;
                if (HexToColor(buf, &c)) { st[i].color = ImVec4(c.x, c.y, c.z, st[i].color.w); stopsChanged = true; }
            }
            if (ImGui::IsItemActivated()) pickerStop = i;
            ImGui::PopStyleColor(3);
            ImGui::SameLine(0, U(4));
            float op = std::round(st[i].color.w * 100.0f);
            if (ui::NumField("##gop", "", &op, opW, 1.0f, 0, 100, "%")) { st[i].color.w = op / 100.0f; stopsChanged = true; }
            if (ImGui::IsItemActivated()) pickerStop = i;
            ImGui::SameLine(x0 + W - fh);
            if (st.size() > 2) { if (IconButton("##gdel", Icon::Minus, false, 0, "Remove stop")) remove = i; }
            else ImGui::Dummy(ImVec2(fh, fh));
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            st.erase(st.begin() + remove);
            if (pickerStop >= remove && pickerStop > 0) pickerStop--;
            stopsChanged = true;
        }
        if (p->type == 2 || p->type == 4)
        {
            ImGui::Dummy(ImVec2(0, U(2)));
            if (ui::NumField("gang", "", &p->angle, (W - U(8)) * 0.5f, 1.0f, -360, 360, "\xC2\xB0", Icon::Radius)) res |= 1;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip(p->type == 2 ? "Direction" : "Start angle");
        }
        if (stopsChanged)
        {
            p->SetStops(st);
            res |= 1;
        }
        ImGui::Dummy(ImVec2(0, U(2)));
        lp = ImGui::GetCursorScreenPos();
        dl->AddLine(ImVec2(lp.x - U(12), lp.y), ImVec2(lp.x + W + U(12), lp.y), Divider);
        ImGui::Dummy(ImVec2(0, U(6)));
        pickerStop = std::clamp(pickerStop, 0, (int)st.size() - 1);
        *target = p->stops.size() >= 2 ? &p->stops[pickerStop].color : (pickerStop == 0 ? &p->c0 : &p->c1);
        break;
    }
    case Pick_Pattern:
    {
        const char* names[] = { "Noise", "Lines", "Grid", "Stripes", "Dots", "Checker" };
        float cw = (W - U(6) * 2) / 3;
        for (int i = 0; i < 6; i++)
        {
            if (i % 3) ImGui::SameLine(0, U(6));
            if (ImGui::Button(names[i], ImVec2(cw, fh))) setTexture(MakeTextureAsset(doc, i), 2, tx.on ? tx.opacity : 0.35f);
        }
        if (tx.on) { ImGui::Dummy(ImVec2(0, U(4))); mediaControls(false); }
        else ui::Label("Pick a pattern - it tiles over the fill.");
        return res | 2;
    }
    case Pick_Image:
    case Pick_Video:
    {
        bool video = pickerTab == Pick_Video;
        auto it = doc.assets.find(tx.asset);
        const Asset* cur = tx.on && it != doc.assets.end() && ((it->second.kind == 3) == video) && it->second.name.rfind("shader_", 0) != 0 ? &it->second : nullptr;
        auto choose = [&]() {
            std::string f = video ? platform::OpenFileDialog("Video fill", "Video|*.mp4;*.mov;*.wmv;*.m4v;*.avi")
                                  : platform::OpenFileDialog("Image fill", "Images|*.png;*.jpg;*.jpeg;*.gif;*.bmp");
            std::vector<unsigned char> bytes;
            if (f.empty() || !platform::ReadFile(f, bytes)) return;
            std::string name = fs::u8path(f).stem().u8string();
            int w = 0, h = 0;
            std::string id;
            if (video) { if (fim::fx::VideoProbe(bytes.data(), bytes.size(), &w, &h)) id = AddAsset(doc, 3, name, bytes); }
            else
            {
                std::vector<unsigned char> rgba;
                if (gfx::DecodeImage(bytes.data(), bytes.size(), rgba, &w, &h)) id = AddAsset(doc, IsGif(bytes) ? 2 : 0, name, bytes);
            }
            if (id.empty()) Toast("Unsupported file", true);
            else setTexture(id, tx.on && cur ? tx.fit : 0, 1.0f);
        };
        // fit mode (Figma: Fill / Fit / Tile)
        const char* fits[] = { "Fill", "Fit", "Tile" };
        if (cur) { if (ui::Combo("##ifit", &tx.fit, fits, 3, U(120))) MarkDirty(); }
        // preview with the upload button
        ImVec2 pa = ImGui::GetCursorScreenPos();
        float ph = U(150);
        ImVec2 pb(pa.x + W, pa.y + ph);
        ui::Checker(dl, pa, pb, U(10));
        dl->AddRectFilled(pa, pb, IM_COL32(0, 0, 0, 90));
        if (cur)
        {
            gfx::TexInfo ti = gfx::AssetTexture(*cur, W, ph, false);
            if (ti.id)
            {
                ImVec2 ia = pa, ib = pb;
                fim::ContainRect(ImVec2((float)std::max(1, cur->width), (float)std::max(1, cur->height)), &ia, &ib);
                dl->AddImage((ImTextureID)ti.id, ia, ib);
            }
        }
        ImGui::InvisibleButton("##ipreview", ImVec2(W, ph));
        bool hov = ImGui::IsItemHovered() || ImGui::IsItemActive();
        if (!cur || hov)
        {
            const char* label = cur ? (video ? "Replace video" : "Replace image") : "Upload from computer";
            ImVec2 ts = ImGui::CalcTextSize(label);
            ImVec2 ba(pa.x + (W - ts.x) * 0.5f - U(12), pa.y + ph * 0.5f - fh * 0.5f);
            ImGui::SetCursorScreenPos(ba);
            ImGui::PushStyleColor(ImGuiCol_Button, Accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.65f, 1.0f, 1)));
            if (ImGui::Button(label, ImVec2(ts.x + U(24), fh))) choose();
            ImGui::PopStyleColor(2);
            ImGui::SetCursorScreenPos(ImVec2(pa.x, pb.y));
        }
        ImGui::Dummy(ImVec2(0, U(4)));
        // media already in the document
        {
            int shown = 0;
            float ts = U(34);
            for (auto& [id, a] : doc.assets)
            {
                if (a.kind == 1 || (a.kind == 3) != video || a.name.rfind("shader_", 0) == 0 || a.name.rfind("adj_", 0) == 0) continue;
                if (shown >= 6) break;
                if (shown) ImGui::SameLine(0, U(6));
                shown++;
                ImGui::PushID(id.c_str());
                ImVec2 q = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##ith", ImVec2(ts, ts))) setTexture(id, tx.on && cur ? tx.fit : 0, 1.0f);
                AssetThumb(dl, &a, q, ts);
                bool on = tx.on && (tx.asset == id || tx.source == id);
                if (on || ImGui::IsItemHovered()) dl->AddRect(q, ImVec2(q.x + ts, q.y + ts), on ? Accent : IM_COL32(255, 255, 255, 90), U(6), 0, 1.5f);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", a.name.c_str());
                ImGui::PopID();
            }
        }
        if (tx.on && cur)
        {
            ImGui::Dummy(ImVec2(0, U(4)));
            mediaControls(false);
            const Asset* src = tx.source.empty() ? cur : (doc.assets.count(tx.source) ? &doc.assets[tx.source] : nullptr);
            if (!video && src && src->kind == 0)
            {
                // Adjustments (Figma's image controls), baked into a copy of the image.
                ImGui::Dummy(ImVec2(0, U(4)));
                static bool pending = false;
                static double lastBake = 0;
                bool any = false;
                float labelW = U(86);
                for (int i = 0; i < 7; i++)
                {
                    ImGui::PushID(i);
                    ImGui::AlignTextToFramePadding();
                    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
                    ImGui::TextUnformatted(kAdjustNames[i]);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(tx0 + labelW);
                    if (AdjustSlider("##adj", &tx.adjust[i], W - labelW)) pending = true;
                    any |= ImGui::IsItemActive();
                    ImGui::PopID();
                }
                if (pending && (!any || ImGui::GetTime() - lastBake > 0.12))
                {
                    ApplyImageAdjust(n);
                    pending = false;
                    lastBake = ImGui::GetTime();
                }
                if (tx.Adjusted() && ImGui::Button("Reset adjustments", ImVec2(W, 0)))
                {
                    for (float& a : tx.adjust) a = 0;
                    ApplyImageAdjust(n);
                }
            }
        }
        return res | 2;
    }
    case Pick_Shader:
    {
        static int style = 0;
        float x0 = ImGui::GetCursorPosX();
        if (ui::Combo("##shstyle", &style, kShaderStyleNames, 5, U(140))) {}
        ImGui::SameLine(x0 + W - fh);
        if (IconButton("##shmore", Icon::Menu, false, 0, "More")) ImGui::OpenPopup("##shmenu");
        if (ImGui::BeginPopup("##shmenu"))
        {
            if (ImGui::MenuItem("Reset colors")) { shaderC0 = ImVec4(0.3f, 0.5f, 1, 1); shaderC1 = ImVec4(0.9f, 0.3f, 0.8f, 1); }
            if (ImGui::MenuItem("Swap colors")) std::swap(shaderC0, shaderC1);
            ImGui::EndPopup();
        }
        ui::SubLabel("Colors");
        ui::ColorRow("##sh0", &shaderC0, W);
        ui::ColorRow("##sh1", &shaderC1, W);
        if (ui::PrimaryButton("Apply shader fill", ImVec2(W, 0)))
        {
            std::string old = tx.on ? tx.asset : std::string();
            setTexture(MakeShaderAsset(doc, style, shaderC0, shaderC1), 0, 1.0f);
            tx.blend = 0;
            // drop the previous baked shader if nothing else uses it
            auto it = doc.assets.find(old);
            if (it != doc.assets.end() && it->second.name.rfind("shader_", 0) == 0)
            {
                bool used = false;
                std::function<void(const Node*)> rec = [&](const Node* q) { if (q->texture.on && q->texture.asset == old) used = true; for (auto& c : q->children) rec(c.get()); };
                for (auto& r : doc.roots) rec(r.get());
                if (!used) doc.assets.erase(it);
            }
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + W);
        ui::Label("Baked into a 256 px texture, so it looks the same in the editor, the C++ and the Java export.");
        ImGui::PopTextWrapPos();
        if (tx.on) { ImGui::Dummy(ImVec2(0, U(4))); mediaControls(false); }
        return res | 2;
    }
    case Pick_Blend:
    {
        ui::SubLabel("Fill blend mode");
        for (int i = 0; i < 8; i++)
        {
            int m = kBlendMenu[i];
            if (i == 1 || i == 4 || i == 7)
            {
                ImVec2 sp = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(sp.x, sp.y + U(3)), ImVec2(sp.x + W, sp.y + U(3)), Divider);
                ImGui::Dummy(ImVec2(W, U(6)));
            }
            ImGui::PushID(i);
            if (ImGui::Selectable(BlendModeLabel(m), n->blend == m, 0, ImVec2(W, 0))) { n->blend = m; MarkDirty(); }
            ImGui::PopID();
        }
        if (tx.on)
        {
            ImGui::Dummy(ImVec2(0, U(4)));
            ui::SubLabel("Media blend mode");
            if (blendCombo("##mblend", &tx.blend, W)) MarkDirty();
        }
        return res | 2;
    }
    case Pick_Contrast:
    {
        // Figma: the ratio and the AA / AAA result on top, the color body below with the AA line on the square.
        ImVec4 bg = BackgroundOf(n, doc.canvasColor);
        ImVec4 fg = **target;
        float ratio = Contrast(fg, bg);
        ImVec2 q = ImGui::GetCursorScreenPos();
        float r = U(8);
        dl->AddCircleFilled(ImVec2(q.x + r, q.y + fh * 0.5f), r, ImGui::ColorConvertFloat4ToU32(ImVec4(bg.x, bg.y, bg.z, 1)));
        dl->PathArcTo(ImVec2(q.x + r, q.y + fh * 0.5f), r, -IM_PI * 0.5f, IM_PI * 0.5f);
        dl->PathFillConvex(ImGui::ColorConvertFloat4ToU32(ImVec4(fg.x, fg.y, fg.z, 1)));
        dl->AddCircle(ImVec2(q.x + r, q.y + fh * 0.5f), r, IM_COL32(255, 255, 255, 50));
        ImGui::Dummy(ImVec2(r * 2, fh));
        ImGui::SameLine(0, U(8));
        ImGui::AlignTextToFramePadding();
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
        ImGui::Text("%.2f : 1", ratio);
        ImGui::PopFont();
        float bx = tx0 + W;
        for (int lvl = 1; lvl >= 0; lvl--)
        {
            const char* name = lvl ? "AAA" : "AA";
            bool ok = ratio >= (lvl ? 7.0f : 4.5f);
            float tw = ImGui::CalcTextSize(name).x + U(18);
            bx -= tw + U(4);
            ImGui::SameLine(bx);
            ImVec2 bp = ImGui::GetCursorScreenPos();
            dl->AddRectFilled(bp, ImVec2(bp.x + tw, bp.y + fh), ok ? IM_COL32(20, 174, 92, 50) : IM_COL32(242, 72, 34, 45), U(5));
            DrawIcon(dl, ok ? Icon::Check : Icon::Close, ImVec2(bp.x + U(8), bp.y + fh * 0.5f), U(9), ok ? Success : Danger);
            dl->AddText(ImVec2(bp.x + U(15), bp.y + (fh - ImGui::GetFontSize()) * 0.5f), Text, name);
            ImGui::Dummy(ImVec2(tw, fh));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(lvl ? "AAA: 7:1 for normal text, 4.5:1 for large text" : "AA: 4.5:1 for normal text, 3:1 for large text");
        }
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::Text("Against #%s (the layer behind)", ColorToHex(bg).c_str() + 1);
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, U(2)));
        // The AA line on the saturation / brightness square: colors on the dimmed side fail 4.5:1.
        float alpha = fg.w;
        ui::SetSvOverlay([bg, alpha](ImDrawList* d, ImVec2 a, ImVec2 b, float hue) {
            const int N = 48;
            float cw = (b.x - a.x) / N, ch = (b.y - a.y) / N;
            auto passes = [&](float s, float v) {
                float rr, gg, bb;
                ImGui::ColorConvertHSVtoRGB(hue, s, v, rr, gg, bb);
                return Contrast(ImVec4(rr, gg, bb, alpha), bg) >= 4.5f;
            };
            std::vector<ImVec2> line;
            for (int i = 0; i < N; i++)
            {
                float s = (i + 0.5f) / N;
                bool prev = passes(s, 1.0f);
                float lineY = -1;
                for (int j = 0; j < N; j++)
                {
                    float v = 1.0f - (j + 0.5f) / N;
                    bool ok = passes(s, v);
                    if (!ok)
                    {
                        // dotted shade like Figma: a dot per cell
                        ImVec2 cc(a.x + (i + 0.5f) * cw, a.y + (j + 0.5f) * ch);
                        d->AddRectFilled(ImVec2(a.x + i * cw, a.y + j * ch), ImVec2(a.x + (i + 1) * cw, a.y + (j + 1) * ch), IM_COL32(0, 0, 0, 70));
                        if ((i + j) % 2 == 0) d->AddCircleFilled(cc, 1.0f, IM_COL32(255, 255, 255, 60), 4);
                    }
                    if (ok != prev && lineY < 0) lineY = a.y + j * ch;
                    prev = ok;
                }
                if (lineY >= 0) line.push_back(ImVec2(a.x + (i + 0.5f) * cw, lineY));
            }
            if (line.size() >= 2) d->AddPolyline(line.data(), (int)line.size(), IM_COL32(255, 255, 255, 230), 0, 1.5f);
        });
        return res;
    }
    default:
        break;
    }
    return res;
}
