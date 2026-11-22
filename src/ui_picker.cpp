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
        if (q->fill.type && q->fill.c0.w > 0.5f) return q->fill.type == 2 ? ImVec4((q->fill.c0.x + q->fill.c1.x) * 0.5f, (q->fill.c0.y + q->fill.c1.y) * 0.5f, (q->fill.c0.z + q->fill.c1.z) * 0.5f, 1) : q->fill.c0;
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
            for (int i = 0; i < (p->type == 2 ? 2 : p->type == 1 ? 1 : 0); i++)
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

int App::FillPickerHeader(Node* n, Paint* p, ImVec4** target, bool mediaFirst)
{
    int res = 0;
    float W = U(240);
    bool rich = n->type == NodeType::Frame || n->type == NodeType::Rect || n->type == NodeType::Ellipse;
    if (ImGui::IsWindowAppearing())
    {
        int kind = -1;
        auto it = doc.assets.find(n->texture.asset);
        if (n->texture.on && it != doc.assets.end()) kind = it->second.kind;
        bool shader = n->texture.on && it != doc.assets.end() && it->second.name.rfind("shader_", 0) == 0;
        bool pattern = n->texture.on && n->texture.fit == 2 && kind == 0;
        pickerTab = mediaFirst && n->texture.on ? (shader ? Pick_Shader : kind == 3 ? Pick_Video : pattern ? Pick_Pattern : Pick_Image) : (p->type == 2 ? Pick_Gradient : Pick_Solid);
        pickerStop = 0;
        shaderC0 = p->c0;
        shaderC1 = p->type == 2 ? p->c1 : ImVec4(p->c0.x * 0.3f, p->c0.y * 0.4f, std::min(1.0f, p->c0.z + 0.4f), 1);
    }
    const char* tips[] = { "Solid", "Gradient", "Pattern", "Image", "Video", "Shader", "Blend mode", "Check color contrast" };
    float bs = ImGui::GetFrameHeight();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int t = 0; t < Pick_Count; t++)
    {
        bool enabled = rich || t == Pick_Solid || t == Pick_Contrast;
        if (!enabled) continue;
        if (t) ImGui::SameLine(0, t == Pick_Blend ? U(14) : U(2));
        ImGui::PushID(t);
        ImVec2 q = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##ptab", ImVec2(bs, bs)))
        {
            pickerTab = t;
            if (t == Pick_Solid && p->type != 1) { p->type = 1; res |= 1; }
            if (t == Pick_Gradient && p->type != 2)
            {
                p->type = 2;
                p->c1 = ImVec4(p->c0.x * 0.35f, p->c0.y * 0.35f, p->c0.z * 0.35f, p->c0.w);
                res |= 1;
            }
        }
        bool hov = ImGui::IsItemHovered();
        if (pickerTab == t) dl->AddRectFilled(q, ImVec2(q.x + bs, q.y + bs), IM_COL32(255, 255, 255, 30), U(5));
        else if (hov) dl->AddRectFilled(q, ImVec2(q.x + bs, q.y + bs), Hover, U(5));
        TabGlyph(dl, t, ImVec2(q.x + bs * 0.5f, q.y + bs * 0.5f), U(13), pickerTab == t || hov ? Text : TextDim);
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
        tx.fit = fit;
        tx.opacity = opacity;
        MarkDirty();
    };
    auto textureControls = [&](bool tile) {
        float half = (W - U(8)) * 0.5f;
        ui::SubLabels("Opacity", "Blend", half + U(8));
        float op = std::round(tx.opacity * 100);
        if (ui::NumField("txop", "", &op, half, 1.0f, 0, 100, "%")) { tx.opacity = op / 100; MarkDirty(); }
        ImGui::SameLine(0, U(8));
        const char* blends[] = { "Normal", "Plus lighter", "Multiply", "Screen" };
        if (ui::Combo("##txbl", &tx.blend, blends, 4, half)) MarkDirty();
        if (!tile)
        {
            const char* fits[] = { "Fill", "Fit", "Tile" };
            if (ui::Segmented("##txfit", &tx.fit, fits, 3, W)) MarkDirty();
        }
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
        // Gradient bar with two stops; the selected stop is edited by the color body below.
        float bh = U(18);
        ImVec2 g = ImGui::GetCursorScreenPos();
        ImU32 a0 = ImGui::ColorConvertFloat4ToU32(p->c0), a1 = ImGui::ColorConvertFloat4ToU32(p->c1);
        dl->AddRectFilledMultiColor(g, ImVec2(g.x + W, g.y + bh), a0, a1, a1, a0);
        dl->AddRect(g, ImVec2(g.x + W, g.y + bh), IM_COL32(255, 255, 255, 40));
        for (int i = 0; i < 2; i++)
        {
            ImVec2 sp(i ? g.x + W - U(8) : g.x + U(8), g.y + bh + U(8));
            ImGui::SetCursorScreenPos(ImVec2(sp.x - U(8), g.y));
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##stop", ImVec2(U(16), bh + U(18)))) pickerStop = i;
            ImGui::PopID();
            dl->AddRectFilled(ImVec2(sp.x - U(7), sp.y - U(2)), ImVec2(sp.x + U(7), sp.y + U(12)), pickerStop == i ? Accent : IM_COL32_WHITE, U(3));
            dl->AddRectFilled(ImVec2(sp.x - U(5), sp.y), ImVec2(sp.x + U(5), sp.y + U(10)), i ? a1 : a0, U(2));
        }
        ImGui::SetCursorScreenPos(ImVec2(g.x, g.y + bh + U(24)));
        float half = (W - U(8)) * 0.5f;
        if (ui::NumField("gang", "", &p->angle, half, 1.0f, -360, 360, "\xC2\xB0", Icon::Radius)) res |= 1;
        ImGui::SameLine(0, U(8));
        if (ImGui::Button("Swap stops", ImVec2(half, 0))) { std::swap(p->c0, p->c1); res |= 1; }
        ImGui::Dummy(ImVec2(0, U(4)));
        *target = pickerStop ? &p->c1 : &p->c0;
        break;
    }
    case Pick_Pattern:
    {
        const char* names[] = { "Noise", "Lines", "Grid", "Stripes", "Dots", "Checker" };
        float cw = (W - U(6) * 2) / 3, ch = ImGui::GetFrameHeight();
        for (int i = 0; i < 6; i++)
        {
            if (i % 3) ImGui::SameLine(0, U(6));
            if (ImGui::Button(names[i], ImVec2(cw, ch))) setTexture(MakeTextureAsset(doc, i), 2, tx.on ? tx.opacity : 0.35f);
        }
        if (tx.on) { ImGui::Dummy(ImVec2(0, U(4))); textureControls(true); }
        else ui::Label("Pick a pattern - it tiles over the fill.");
        return res | 2;
    }
    case Pick_Image:
    case Pick_Video:
    {
        bool video = pickerTab == Pick_Video;
        auto it = doc.assets.find(tx.asset);
        const Asset* cur = tx.on && it != doc.assets.end() && ((it->second.kind == 3) == video) ? &it->second : nullptr;
        float th = U(64);
        AssetThumb(dl, cur, ImGui::GetCursorScreenPos(), th);
        ImGui::Dummy(ImVec2(th, th));
        ImGui::SameLine(0, U(10));
        ImGui::BeginGroup();
        ImGui::TextUnformatted(cur ? cur->name.c_str() : (video ? "No video" : "No image"));
        if (ImGui::Button(video ? "Choose video..." : "Choose image...", ImVec2(W - th - U(10), 0)))
        {
            std::string f = video ? platform::OpenFileDialog("Video fill", "Video|*.mp4;*.mov;*.wmv;*.m4v;*.avi")
                                  : platform::OpenFileDialog("Image fill", "Images|*.png;*.jpg;*.jpeg;*.gif;*.bmp");
            std::vector<unsigned char> bytes;
            if (!f.empty() && platform::ReadFile(f, bytes))
            {
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
                else setTexture(id, 0, 1.0f);
            }
        }
        // assets already in the document
        int shown = 0;
        for (auto& [id, a] : doc.assets)
        {
            if (a.kind == 1 || (a.kind == 3) != video || a.name.rfind("shader_", 0) == 0) continue;
            if (shown++ >= 6) break;
            if (ImGui::Selectable(a.name.c_str(), tx.on && tx.asset == id)) setTexture(id, 0, 1.0f);
        }
        ImGui::EndGroup();
        if (tx.on && cur) { ImGui::Dummy(ImVec2(0, U(4))); textureControls(false); }
        return res | 2;
    }
    case Pick_Shader:
    {
        ui::SubLabel("Style");
        static int style = 0;
        float cw = (W - U(6) * 2) / 3;
        for (int i = 0; i < 5; i++)
        {
            if (i % 3) ImGui::SameLine(0, U(6));
            bool sel = style == i;
            if (sel) ImGui::PushStyleColor(ImGuiCol_Button, AccentDim);
            if (ImGui::Button(kShaderStyleNames[i], ImVec2(cw, 0))) style = i;
            if (sel) ImGui::PopStyleColor();
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
        if (tx.on) { ImGui::Dummy(ImVec2(0, U(4))); textureControls(false); }
        return res | 2;
    }
    case Pick_Blend:
    {
        ui::SubLabel("Fill blend mode");
        const char* blends[] = { "Normal", "Plus lighter", "Multiply", "Screen" };
        for (int i = 0; i < 4; i++)
            if (ImGui::Selectable(blends[i], n->blend == i, 0, ImVec2(W, 0))) { n->blend = i; MarkDirty(); }
        if (tx.on)
        {
            ImGui::Dummy(ImVec2(0, U(4)));
            ui::SubLabel("Media blend mode");
            if (ui::Combo("##mblend", &tx.blend, blends, 4, W)) MarkDirty();
        }
        return res | 2;
    }
    case Pick_Contrast:
    {
        ImVec4 bg = BackgroundOf(n, doc.canvasColor);
        ImVec4 fg = **target;
        // a translucent color is seen mixed with what is behind it
        ImVec4 mix(fg.x * fg.w + bg.x * (1 - fg.w), fg.y * fg.w + bg.y * (1 - fg.w), fg.z * fg.w + bg.z * (1 - fg.w), 1);
        float l1 = Luminance(mix), l2 = Luminance(bg);
        float ratio = (std::max(l1, l2) + 0.05f) / (std::min(l1, l2) + 0.05f);
        ImVec2 q = ImGui::GetCursorScreenPos();
        float sw = U(44);
        dl->AddRectFilled(q, ImVec2(q.x + sw, q.y + sw), ImGui::ColorConvertFloat4ToU32(bg), U(6));
        dl->AddRect(q, ImVec2(q.x + sw, q.y + sw), IM_COL32(255, 255, 255, 40), U(6));
        dl->AddText(fonts::UI(fonts::UI_SemiBold), U(20), ImVec2(q.x + U(9), q.y + U(9)), ImGui::ColorConvertFloat4ToU32(mix), "Aa");
        ImGui::Dummy(ImVec2(sw, sw));
        ImGui::SameLine(0, U(12));
        ImGui::BeginGroup();
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 18.0f);
        ImGui::Text("%.2f : 1", ratio);
        ImGui::PopFont();
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::Text("against #%s", ColorToHex(bg).c_str() + 1);
        ImGui::PopStyleColor();
        ImGui::EndGroup();
        struct Rule { const char* name; float need; };
        const Rule rules[] = { { "AA  normal text", 4.5f }, { "AA  large text", 3.0f }, { "AAA normal text", 7.0f }, { "AAA large text", 4.5f } };
        for (auto& r : rules)
        {
            bool ok = ratio >= r.need;
            ImVec2 rp = ImGui::GetCursorScreenPos();
            float fh = ImGui::GetFontSize();
            DrawIcon(dl, ok ? Icon::Check : Icon::Close, ImVec2(rp.x + fh * 0.5f, rp.y + fh * 0.5f + U(1)), U(11), ok ? Success : Danger);
            ImGui::SetCursorScreenPos(ImVec2(rp.x + fh + U(6), rp.y));
            ImGui::Text("%s  (%.1f)", r.name, r.need);
        }
        ImGui::Dummy(ImVec2(0, U(4)));
        return res;
    }
    default:
        break;
    }
    return res;
}
