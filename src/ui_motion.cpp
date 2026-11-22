#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "exporters.h"
#include "platform.h"
#include "gfx.h"
#include "../runtime/fimgui_runtime.h"
#include "../runtime/fimgui_fx.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <map>

namespace fs = std::filesystem;
using namespace theme;

static double s_playStart = 0;

// ---------------------------------------------------------------- effects

static std::string ImportTextureFile(Document& doc, const std::string& path)
{
    std::vector<unsigned char> bytes;
    if (!platform::ReadFile(path, bytes)) return "";
    std::string ext = fs::u8path(path).extension().u8string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    std::string name = fs::u8path(path).stem().u8string();
    if (ext == ".mp4" || ext == ".mov" || ext == ".wmv" || ext == ".m4v" || ext == ".avi")
    {
        int w = 0, h = 0;
        return fim::fx::VideoProbe(bytes.data(), bytes.size(), &w, &h) ? AddAsset(doc, 3, name, bytes) : "";
    }
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!gfx::DecodeImage(bytes.data(), bytes.size(), rgba, &w, &h)) return "";
    return AddAsset(doc, IsGif(bytes) ? 2 : 0, name, bytes);
}

// Row for an active effect: a pill with the effect name (click to show its settings) and a remove button.
// Returns -1 when removed, 1 when the settings are expanded, 0 when collapsed.
static int EffectHeader(const char* label, const char* id)
{
    static std::map<std::string, bool> expanded;
    float W = ui::Avail();
    float fh = ImGui::GetFrameHeight();
    float pillW = W - fh - U(8);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::PushID(label);
    bool& open = expanded[label];
    if (ImGui::InvisibleButton("##pill", ImVec2(pillW, fh))) open = !open;
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    float r = ImGui::GetStyle().FrameRounding;
    dl->AddRect(p, ImVec2(p.x + pillW, p.y + fh), hov || open ? IM_COL32(255, 255, 255, 60) : Border, r);
    ImVec2 c(p.x + fh * 0.5f + U(2), p.y + fh * 0.5f);
    dl->AddCircle(c, U(6), Text, 20, 1.2f);
    dl->AddCircleFilled(ImVec2(c.x - U(1.5f), c.y - U(1.5f)), U(3), IM_COL32(255, 255, 255, 120));
    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(p.x + fh + U(6), p.y + (fh - ts.y) * 0.5f), Text, label);
    DrawIcon(dl, open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(p.x + pillW - U(14), p.y + fh * 0.5f), U(9), hov ? Text : TextDim);
    ImGui::SameLine(0, U(8));
    bool rm = IconButton(id, Icon::Minus, false, 0, "Remove effect");
    ImGui::PopID();
    if (rm) return -1;
    if (open) ImGui::Dummy(ImVec2(0, U(2)));
    return open ? 1 : 0;
}

void App::DrawEffects(Node* n, const std::vector<Node*>& nodes)
{
    if (n->type == NodeType::Line || n->type == NodeType::Text || n->type == NodeType::Group) return;
    float W = ui::Avail();
    float half = (W - U(8)) * 0.5f;
    bool backdropOk = n->type == NodeType::Frame || n->type == NodeType::Rect || n->type == NodeType::Ellipse;
    bool textureOk = n->type != NodeType::Vector;

    if (ui::Section("Effects", 1) == 1) ImGui::OpenPopup("##addfx");
    if (ImGui::BeginPopup("##addfx"))
    {
        bool changed = true;
        if (ImGui::MenuItem("Drop shadow", nullptr, false, n->effect.type == 0)) { for (Node* q : nodes) { q->effect = Effect(); q->effect.type = 1; } }
        else if (ImGui::MenuItem("Inner shadow", nullptr, false, n->effect.type == 0)) { for (Node* q : nodes) { q->effect = Effect(); q->effect.type = 2; } }
        else if (ImGui::MenuItem("Background blur", nullptr, false, backdropOk && !n->blur.on && !n->glass.on))
        {
            for (Node* q : nodes)
            {
                q->blur.on = true;
                if (q->fill.type == 1 && q->fill.c0.w > 0.6f) q->fill.c0.w = 0.45f;
            }
        }
        else if (ImGui::MenuItem("Glass", nullptr, false, backdropOk && !n->glass.on))
        {
            for (Node* q : nodes)
            {
                q->glass = GlassFx();
                q->glass.on = true;
                q->blur.on = false;
                // Glass needs to see through its fill.
                if (q->fill.type == 1 && q->fill.c0.w > 0.2f) q->fill.c0.w = 0.04f;
                if (q->fill.type == 2) { q->fill.c0.w = std::min(q->fill.c0.w, 0.08f); q->fill.c1.w = std::min(q->fill.c1.w, 0.08f); }
            }
            Toast("Glass looks best over colorful content");
        }
        else if (ImGui::MenuItem("Texture overlay", nullptr, false, textureOk && !n->texture.on))
        {
            std::string asset;
            for (auto& [id, a] : doc.assets) if (a.kind != 1) { asset = id; break; }
            if (asset.empty()) asset = MakeTextureAsset(doc, 0);
            for (Node* q : nodes) { q->texture = TextureFx(); q->texture.on = true; q->texture.asset = asset; }
        }
        else changed = false;
        if (changed) MarkDirty();
        ImGui::EndPopup();
    }
    if (!n->effect.type && !n->blur.on && !n->glass.on && !n->texture.on)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextUnformatted("Shadows, background blur, glass, texture");
        ImGui::PopStyleColor();
        return;
    }

    // Shadow
    if (n->effect.type)
    {
        ImGui::PushID("shadow");
        int eh = EffectHeader(n->effect.type == 1 ? "Drop shadow" : "Inner shadow", "##rm"); if (eh < 0) { for (Node* q : nodes) q->effect.type = 0; MarkDirty(); }
        else if (eh > 0)
        {
            bool ec = false;
            int t = n->effect.type - 1;
            const char* types[] = { "Drop shadow", "Inner shadow" };
            if (ui::Combo("##etype", &t, types, 2, W)) { n->effect.type = t + 1; ec = true; }
            ec |= ui::NumField("ex", "X", &n->effect.offset.x, half, 1.0f);
            ImGui::SameLine(0, U(8));
            ec |= ui::NumField("ey", "Y", &n->effect.offset.y, half, 1.0f);
            ec |= ui::NumField("eb", "B", &n->effect.blur, half, 1.0f, 0, 500);
            ImGui::SameLine(0, U(8));
            ec |= ui::NumField("es", "S", &n->effect.spread, half, 1.0f, -500, 500);
            ec |= ui::ColorRow("##ecol", &n->effect.color, W);
            if (ec) { for (Node* q : nodes) q->effect = n->effect; MarkDirty(); }
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, U(4)));
    }

    // Background blur
    if (n->blur.on && backdropOk && !n->glass.on)
    {
        ImGui::PushID("blur");
        int eh = EffectHeader("Background blur", "##rm"); if (eh < 0) { for (Node* q : nodes) q->blur.on = false; MarkDirty(); }
        else if (eh > 0)
        {
            if (ui::NumField("radius", "", &n->blur.radius, half, 0.5f, 0, 200, nullptr, Icon::Ellipse)) { for (Node* q : nodes) q->blur = n->blur; MarkDirty(); }
            ImGui::SameLine(0, U(8));
            ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("fill tints the blur");
            ImGui::PopStyleColor();
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, U(4)));
    }

    // Glass
    if (n->glass.on && backdropOk)
    {
        ImGui::PushID("glass");
        int eh = EffectHeader("Glass", "##rm"); if (eh < 0) { for (Node* q : nodes) q->glass.on = false; MarkDirty(); }
        else if (eh > 0)
        {
            GlassFx& g = n->glass;
            bool gc = false;
            struct Preset { const char* name; GlassFx v; };
            auto make = [](float blur, float refr, float thick, float hi, float disp, float sat, ImVec4 tint) {
                GlassFx f; f.on = true; f.blur = blur; f.refraction = refr; f.thickness = thick; f.highlight = hi; f.dispersion = disp; f.saturation = sat; f.tint = tint; return f;
            };
            const Preset presets[] = {
                { "Liquid", make(8, 0.7f, 20, 0.75f, 0.3f, 1.5f, ImVec4(1, 1, 1, 0.05f)) },
                { "Clear", make(2, 0.85f, 24, 0.85f, 0.4f, 1.2f, ImVec4(1, 1, 1, 0.02f)) },
                { "Frosted", make(20, 0.35f, 14, 0.5f, 0.1f, 1.3f, ImVec4(1, 1, 1, 0.10f)) },
                { "Dark", make(16, 0.5f, 16, 0.45f, 0.2f, 1.2f, ImVec4(0, 0, 0, 0.28f)) },
            };
            float pw = (W - U(18)) / 4;
            for (int i = 0; i < 4; i++)
            {
                if (i) ImGui::SameLine(0, U(6));
                if (ImGui::Button(presets[i].name, ImVec2(pw, 0))) { float a = g.lightAngle; g = presets[i].v; g.lightAngle = a; gc = true; }
            }
            gc |= ui::NumField("gb", "Blr", &g.blur, half, 0.5f, 0, 200);
            ImGui::SameLine(0, U(8));
            gc |= ui::NumField("gt", "Dep", &g.thickness, half, 0.5f, 1, 200);
            float refr = std::round(g.refraction * 100), hi = std::round(g.highlight * 100), disp = std::round(g.dispersion * 100), sat = std::round(g.saturation * 100);
            if (ui::NumField("gr", "Ref", &refr, half, 1.0f, 0, 200, "%")) { g.refraction = refr / 100; gc = true; }
            ImGui::SameLine(0, U(8));
            if (ui::NumField("gh", "Hi", &hi, half, 1.0f, 0, 200, "%")) { g.highlight = hi / 100; gc = true; }
            if (ui::NumField("gd", "Dsp", &disp, half, 1.0f, 0, 200, "%")) { g.dispersion = disp / 100; gc = true; }
            ImGui::SameLine(0, U(8));
            if (ui::NumField("gs", "Sat", &sat, half, 1.0f, 0, 300, "%")) { g.saturation = sat / 100; gc = true; }
            gc |= ui::NumField("gl", "", &g.lightAngle, half, 1.0f, -360, 360, "\xC2\xB0", Icon::Sparkle);
            ImGui::SameLine(0, U(8));
            ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("light direction");
            ImGui::PopStyleColor();
            gc |= ui::ColorRow("##gtint", &g.tint, W);
            if (gc) { for (Node* q : nodes) q->glass = g; MarkDirty(); }
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, U(4)));
    }

    // Texture overlay
    if (n->texture.on && textureOk)
    {
        ImGui::PushID("texture");
        int eh = EffectHeader("Texture overlay", "##rm"); if (eh < 0) { for (Node* q : nodes) q->texture.on = false; MarkDirty(); }
        else if (eh > 0)
        {
            TextureFx& t = n->texture;
            bool tc = false;
            // Asset preview + picker
            float th = U(44);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + th, p.y + th), Field, U(6));
            auto it = doc.assets.find(t.asset);
            if (it != doc.assets.end())
            {
                gfx::TexInfo ti = gfx::AssetTexture(it->second, th, th, false);
                if (ti.id)
                {
                    ImVec2 ia(p.x + U(3), p.y + U(3)), ib(p.x + th - U(3), p.y + th - U(3));
                    fim::ContainRect(ImVec2((float)std::max(1, it->second.width), (float)std::max(1, it->second.height)), &ia, &ib);
                    dl->AddImage((ImTextureID)ti.id, ia, ib);
                }
            }
            ImGui::Dummy(ImVec2(th, th));
            ImGui::SameLine(0, U(8));
            ImGui::BeginGroup();
            const char* kinds[] = { "image", "svg", "GIF", "video" };
            std::string cur = it != doc.assets.end() ? it->second.name + "  (" + kinds[it->second.kind & 3] + ")" : std::string("Choose...");
            ImGui::SetNextItemWidth(W - th - U(8));
            if (ImGui::BeginCombo("##tasset", cur.c_str()))
            {
                for (auto& [id, a] : doc.assets)
                {
                    if (a.kind == 1) continue;
                    std::string lbl = a.name + "  (" + kinds[a.kind & 3] + ")";
                    if (ImGui::Selectable(lbl.c_str(), id == t.asset)) { t.asset = id; tc = true; }
                }
                ImGui::EndCombo();
            }
            float bw = (W - th - U(8) - U(6) * 4) / 5;
            if (ImGui::Button("File", ImVec2(bw, 0)))
            {
                std::string f = platform::OpenFileDialog("Texture", "Textures|*.png;*.jpg;*.jpeg;*.gif;*.mp4;*.mov;*.wmv;*.m4v|All files|*.*");
                if (!f.empty())
                {
                    std::string id = ImportTextureFile(doc, f);
                    if (id.empty()) Toast("Unsupported texture file", true); else { t.asset = id; tc = true; }
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("PNG, JPG, animated GIF or video (MP4/MOV/WMV)");
            const char* gens[] = { "Noise", "Lines", "Grid", "Diag" };
            for (int i = 0; i < 4; i++)
            {
                ImGui::SameLine(0, U(6));
                if (ImGui::Button(gens[i], ImVec2(bw, 0))) { t.asset = MakeTextureAsset(doc, i); t.fit = 2; tc = true; }
            }
            ImGui::EndGroup();

            float op = std::round(t.opacity * 100);
            if (ui::NumField("op", "", &op, half, 1.0f, 0, 100, "%", Icon::Eye)) { t.opacity = op / 100; tc = true; }
            ImGui::SameLine(0, U(8));
            const char* blends[] = { "Normal", "Add", "Multiply", "Screen" };
            tc |= ui::Combo("##tblend", &t.blend, blends, 4, half);
            const char* fits[] = { "Fill", "Fit", "Tile" };
            tc |= ui::Segmented("##tfit", &t.fit, fits, 3, W);
            if (t.fit == 2)
            {
                float sc = std::round(t.scale * 100);
                if (ui::NumField("sc", "", &sc, half, 1.0f, 1, 2000, "%", Icon::Grid)) { t.scale = sc / 100; tc = true; }
                ImGui::SameLine(0, U(8));
                ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("tile scale");
                ImGui::PopStyleColor();
                tc |= ui::NumField("vx", "VX", &t.speed.x, half, 1.0f, -2000, 2000);
                ImGui::SameLine(0, U(8));
                tc |= ui::NumField("vy", "VY", &t.speed.y, half, 1.0f, -2000, 2000);
            }
            if (tc) { for (Node* q : nodes) q->texture = t; MarkDirty(); }
        }
        ImGui::PopID();
    }
}

// ---------------------------------------------------------------- motion

static fim::AnimValue PreviewSample(const fim::AnimSpec& s, double t, float* progress)
{
    int cat = fim::AnimCategory(s.preset);
    if (cat == 0)
    {
        double cycle = 0.35 + s.delay + s.duration + 0.9;
        float lt = (float)std::fmod(t, cycle) - 0.35f;
        if (progress) *progress = ImSaturate((lt - s.delay) / std::max(0.01f, s.duration));
        return fim::AnimSample(s, std::max(0.0f, lt), 0);
    }
    if (cat == 1)
    {
        if (progress) *progress = (float)std::fmod(t, std::max(0.01, (double)s.duration + s.delay)) / std::max(0.01f, s.duration);
        return fim::AnimSample(s, (float)t, 0);
    }
    double d = std::max(0.05f, s.duration);
    double period = 2 * d + 1.0;
    double ph = std::fmod(t, period);
    float p = ph < d ? (float)(ph / d) : ph < d + 0.5 ? 1.0f : ph < 2 * d + 0.5 ? 1.0f - (float)((ph - d - 0.5) / d) : 0.0f;
    if (progress) *progress = p;
    return fim::AnimSample(s, 0, p);
}

// Draws a small rounded square animated with the given value, centered in [a,b].
static void DrawPreviewShape(ImDrawList* dl, ImVec2 a, ImVec2 b, const fim::AnimValue& v, float size, float distScale, ImU32 col)
{
    ImVec2 c((a.x + b.x) * 0.5f + v.dx * distScale, (a.y + b.y) * 0.5f + v.dy * distScale);
    float h = size * 0.5f * v.scale;
    dl->AddRectFilled(ImVec2(c.x - h, c.y - h), ImVec2(c.x + h, c.y + h), fim::Fade(col, ImSaturate(v.alpha)), h * 0.35f);
}

void App::PlayAnimation(Node* n)
{
    if (!n) return;
    Node* root = n;
    if (!n->anim.preset && n->parent && n->parent->anim.preset && n->parent->anim.stagger > 0) root = n->parent;
    animPlayId = root->id;
    float end = 0.3f;
    std::function<void(Node*)> rec = [&](Node* q) {
        animClocks.erase(q->id);
        if (q->anim.preset)
        {
            int cat = fim::AnimCategory(q->anim.preset);
            float e = cat == 0 ? q->anim.delay + q->anim.duration + q->anim.stagger * (float)q->children.size() : 3.2f;
            end = std::max(end, e);
        }
        for (auto& c : q->children) rec(c.get());
    };
    rec(root);
    s_playStart = ImGui::GetTime();
    animPlayUntil = s_playStart + end + 0.5;
}

static bool UsesDistance(int p) { return (p >= fim::Anim_SlideUp && p <= fim::Anim_SlideRight) || p == fim::Anim_Bounce || p == fim::Anim_Float || p == fim::Anim_Wiggle || p == fim::Anim_Lift || p == fim::Anim_Nudge; }
static bool UsesScale(int p) { return p == fim::Anim_Pop || p == fim::Anim_Zoom; }
static bool UsesIntensity(int p) { return p == fim::Anim_Pulse || p == fim::Anim_Breathe || p == fim::Anim_Heartbeat || p == fim::Anim_Lift || p == fim::Anim_Grow || p == fim::Anim_Press || p == fim::Anim_Dim; }

static void EasingGraph(App* app, AnimFx& a, float W, float h, float progress, bool* changed)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(W, h));
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + h), IM_COL32(30, 30, 30, 255), U(6));
    float pad = U(10);
    // y range -0.3 .. 1.3 to show overshoot
    auto P = [&](float x, float y) { return ImVec2(p.x + pad + x * (W - pad * 2), p.y + h - pad - (y + 0.3f) / 1.6f * (h - pad * 2)); };
    dl->AddLine(P(0, 0), P(1, 0), IM_COL32(255, 255, 255, 25));
    dl->AddLine(P(0, 1), P(1, 1), IM_COL32(255, 255, 255, 25));
    ImVec2 pts[65];
    for (int i = 0; i <= 64; i++) { float x = i / 64.0f; pts[i] = P(x, fim::Ease(a.easing, a.bezier, x)); }
    dl->AddPolyline(pts, 65, Accent, 0, 2.0f);
    if (a.easing == fim::Ease_Bezier)
    {
        for (int k = 0; k < 2; k++)
        {
            ImVec2 anchor = k == 0 ? P(0, 0) : P(1, 1);
            ImVec2 hp = P(a.bezier[k * 2], a.bezier[k * 2 + 1]);
            dl->AddLine(anchor, hp, IM_COL32(255, 255, 255, 120), 1.0f);
            ImGui::SetCursorScreenPos(ImVec2(hp.x - U(6), hp.y - U(6)));
            ImGui::PushID(k);
            ImGui::InvisibleButton("##bz", ImVec2(U(12), U(12)));
            if (ImGui::IsItemActive())
            {
                ImVec2 m = ImGui::GetIO().MousePos;
                a.bezier[k * 2] = ImSaturate((m.x - p.x - pad) / (W - pad * 2));
                a.bezier[k * 2 + 1] = ImClamp((p.y + h - pad - m.y) / (h - pad * 2) * 1.6f - 0.3f, -0.3f, 1.3f);
                *changed = true;
            }
            dl->AddCircleFilled(hp, U(5), ImGui::IsItemHovered() || ImGui::IsItemActive() ? IM_COL32_WHITE : IM_COL32(220, 220, 220, 255));
            dl->AddCircle(hp, U(5), Accent, 0, 1.5f);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + ImGui::GetStyle().ItemSpacing.y));
    }
    ImVec2 dot = P(progress, fim::Ease(a.easing, a.bezier, progress));
    dl->AddCircleFilled(dot, U(4), IM_COL32_WHITE);
    (void)app;
}

void App::DrawMotionTab()
{
    float W = ui::Avail();
    float half = (W - U(8)) * 0.5f;
    double t = ImGui::GetTime();
    Node* n = Sel();
    auto nodes = SelNodes();

    if (n)
    {
        ImGui::PushID(n->id.c_str());
        int b = ui::Section("Presets", n->anim.preset ? -1 : 0);
        if (b == -1) { for (Node* q : nodes) q->anim.preset = 0; MarkDirty(); }
        struct Cat { const char* name; int first, last; };
        const Cat cats[] = { { "Entrance", fim::Anim_Fade, fim::Anim_Bounce }, { "Loop", fim::Anim_Pulse, fim::Anim_Wiggle }, { "Hover & press", fim::Anim_Lift, fim::Anim_Dim } };
        float gap = U(6);
        float cw = (W - gap * 2) / 3, ch = U(64);
        for (const Cat& c : cats)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
            ImGui::TextUnformatted(c.name);
            ImGui::PopStyleColor();
            int col = 0;
            for (int pr = c.first; pr <= c.last; pr++)
            {
                if (col) ImGui::SameLine(0, gap);
                ImGui::PushID(pr);
                ImVec2 p = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::InvisibleButton("##card", ImVec2(cw, ch));
                bool hov = ImGui::IsItemHovered();
                bool sel = n->anim.preset == pr;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(p, ImVec2(p.x + cw, p.y + ch), hov ? IM_COL32(66, 66, 66, 255) : Field, U(8));
                if (sel) dl->AddRect(p, ImVec2(p.x + cw, p.y + ch), Accent, U(8), 0, 1.5f);
                fim::AnimSpec spec = ToAnimSpec(AnimDefaults(pr));
                fim::AnimValue v = PreviewSample(spec, t + pr * 0.13, nullptr);
                dl->PushClipRect(p, ImVec2(p.x + cw, p.y + ch - U(16)), true);
                DrawPreviewShape(dl, p, ImVec2(p.x + cw, p.y + ch - U(14)), v, U(18), 0.6f * dpi, sel ? Accent : IM_COL32(200, 200, 210, 255));
                dl->PopClipRect();
                const char* label = AnimPresetLabel(pr);
                float fs = ImGui::GetFontSize() * 0.92f;
                ImVec2 ts = fonts::UI(fonts::UI_Regular)->CalcTextSizeA(fs, FLT_MAX, 0, label);
                dl->AddText(fonts::UI(fonts::UI_Regular), fs, ImVec2(p.x + (cw - ts.x) * 0.5f, p.y + ch - ts.y - U(5)), sel ? Text : TextDim, label);
                if (clicked)
                {
                    for (Node* q : nodes)
                    {
                        float stagger = q->anim.stagger;
                        q->anim = AnimDefaults(pr);
                        if (fim::AnimCategory(pr) == 0) q->anim.stagger = stagger;
                    }
                    MarkDirty();
                    PlayAnimation(n);
                }
                ImGui::PopID();
                col = (col + 1) % 3;
                if (col == 0 && pr != c.last) { /* next row */ }
            }
            ImGui::Dummy(ImVec2(0, U(2)));
        }

        if (n->anim.preset)
        {
            AnimFx& a = n->anim;
            bool ch2 = false;
            int cat = fim::AnimCategory(a.preset);

            ui::Section("Live preview");
            float ph = U(96);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + W, p.y + ph), IM_COL32(30, 30, 30, 255), U(8));
            float progress = 0;
            fim::AnimValue v = PreviewSample(ToAnimSpec(a), t - s_playStart, &progress);
            // A card-like shape that uses the node's fill when it has one.
            bool useFill = n->fill.type && n->fill.c0.w > 0.3f && (n->fill.c0.x + n->fill.c0.y + n->fill.c0.z) < 2.4f;
            ImU32 col = useFill ? ImGui::ColorConvertFloat4ToU32(ImVec4(n->fill.c0.x, n->fill.c0.y, n->fill.c0.z, 1)) : Accent;
            ImVec2 c(p.x + W * 0.5f + v.dx * dpi, p.y + ph * 0.5f + v.dy * dpi);
            float hw = std::min(W * 0.32f, U(90)) * v.scale, hh = U(26) * v.scale;
            dl->PushClipRect(p, ImVec2(p.x + W, p.y + ph), true);
            dl->AddRectFilled(ImVec2(c.x - hw, c.y - hh), ImVec2(c.x + hw, c.y + hh), fim::Fade(col, ImSaturate(v.alpha)), U(8) * v.scale);
            dl->AddRect(ImVec2(c.x - hw, c.y - hh), ImVec2(c.x + hw, c.y + hh), IM_COL32(255, 255, 255, (int)(40 * ImSaturate(v.alpha))), U(8) * v.scale);
            std::string nm = n->name.empty() ? "Layer" : n->name;
            ImVec2 ts = ImGui::CalcTextSize(nm.c_str());
            dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), fim::Fade(IM_COL32_WHITE, ImSaturate(v.alpha)), nm.c_str());
            if (cat == 2)
            {
                // Show the pointer that triggers the state.
                ImVec2 cur(c.x + hw * 0.4f, c.y + hh * 0.3f + (1 - progress) * U(24));
                DrawIcon(dl, Icon::Move, cur, U(14), IM_COL32(255, 255, 255, 200));
            }
            dl->PopClipRect();
            ImGui::Dummy(ImVec2(W, ph));
            if (ui::PrimaryButton("Play on canvas", ImVec2(half, 0))) PlayAnimation(n);
            ImGui::SameLine(0, U(8));
            if (ImGui::Button("Preview menu", ImVec2(half, 0))) { preview = true; animClocks.clear(); }

            ui::Section("Timing");
            if (cat == 0) ui::Label("Plays when the layer appears (menu opens, tab or page shows).");
            else if (cat == 1) ui::Label("Loops while the layer is visible.");
            else
            {
                int trig = std::clamp(a.trigger - 1, 0, 2);
                const char* trigs[] = { "While hovered", "While pressed", "While active (checked / selected)" };
                if (ui::Combo("##trig", &trig, trigs, 3, W)) { a.trigger = trig + 1; ch2 = true; }
            }
            float dur = std::round(a.duration * 1000), del = std::round(a.delay * 1000);
            if (ui::NumField("dur", "", &dur, half, 10.0f, 10, 20000, " ms", Icon::Play)) { a.duration = dur / 1000; ch2 = true; }
            ImGui::SameLine(0, U(8));
            if (ui::NumField("del", "", &del, half, 10.0f, 0, 20000, " ms", Icon::Redo)) { a.delay = del / 1000; ch2 = true; }
            if (cat != 1)
            {
                const char* easings[9];
                for (int i = 0; i < 9; i++) easings[i] = EasingLabel(i);
                ch2 |= ui::Combo("##ease", &a.easing, easings, 9, W);
                EasingGraph(this, a, W, U(92), progress, &ch2);
                if (a.easing == fim::Ease_Bezier)
                {
                    float q4 = (W - U(18)) / 4;
                    for (int i = 0; i < 4; i++)
                    {
                        if (i) ImGui::SameLine(0, U(6));
                        ImGui::PushID(i);
                        ch2 |= ui::NumField("bz", "", &a.bezier[i], q4, 0.01f, -1, 2);
                        ImGui::PopID();
                    }
                }
            }

            if (UsesDistance(a.preset) || UsesScale(a.preset) || UsesIntensity(a.preset) || cat == 0)
            {
                ui::Section("Motion");
                if (UsesDistance(a.preset)) ch2 |= ui::NumField("dist", "", &a.distance, half, 0.5f, -1000, 1000, " px", Icon::ArrowRight);
                if (UsesScale(a.preset))
                {
                    if (UsesDistance(a.preset)) ImGui::SameLine(0, U(8));
                    float sc = std::round(a.scale * 100);
                    if (ui::NumField("scl", "", &sc, half, 1.0f, 1, 500, "%", Icon::Radius)) { a.scale = sc / 100; ch2 = true; }
                }
                if (UsesIntensity(a.preset))
                {
                    if (UsesDistance(a.preset) || UsesScale(a.preset)) ImGui::SameLine(0, U(8));
                    float in = std::round(a.intensity * 100);
                    if (ui::NumField("int", "", &in, half, 0.5f, 0, 100, "%", Icon::Sparkle)) { a.intensity = in / 100; ch2 = true; }
                }
                if (cat == 0 && !n->children.empty())
                {
                    bool st = a.stagger > 0;
                    if (ui::Check("Stagger children", &st)) { a.stagger = st ? 0.06f : 0.0f; ch2 = true; }
                    if (a.stagger > 0)
                    {
                        ImGui::SameLine(0, U(8));
                        float sg = std::round(a.stagger * 1000);
                        if (ui::NumField("stg", "", &sg, half, 5.0f, 1, 5000, " ms", Icon::AlignCenterH)) { a.stagger = sg / 1000; ch2 = true; }
                    }
                }
            }
            if (ch2)
            {
                for (Node* q : nodes) if (q != n && q->anim.preset == a.preset) q->anim = a;
                MarkDirty();
            }
        }
        ImGui::PopID();
    }
    else
    {
        ui::Section("Motion");
        ImGui::PushTextWrapPos(0);
        ui::Label("Select a layer to animate it. Entrance presets play when a menu or tab appears, loops run while visible, hover & press presets react to the pointer.");
        ImGui::PopTextWrapPos();
        if (ui::PrimaryButton("Preview menu (Ctrl+Enter)", ImVec2(W, 0))) { preview = true; animClocks.clear(); }
    }

    // Timeline of animated layers in the current top-level frame (or the whole page).
    ui::Section("Timeline");
    Node* scope = n;
    while (scope && scope->parent) scope = scope->parent;
    struct Row { Node* node; float start, end; int cat; };
    std::vector<Row> rows;
    std::function<void(Node*, const AnimFx*, float)> collect = [&](Node* q, const AnimFx* inh, float extra) {
        const AnimFx* spec = (q->anim.preset && q->anim.stagger <= 0) ? &q->anim : (q->anim.preset ? nullptr : inh);
        float d = spec == inh ? extra : 0.0f;
        if (spec) rows.push_back({ q, spec->delay + d, spec->delay + d + spec->duration, fim::AnimCategory(spec->preset) });
        bool stagger = q->anim.preset && q->anim.stagger > 0;
        for (size_t i = 0; i < q->children.size(); i++)
            collect(q->children[i].get(), stagger ? &q->anim : nullptr, stagger ? q->anim.stagger * (float)i : 0.0f);
    };
    if (scope) collect(scope, nullptr, 0);
    else for (auto& r : doc.roots) collect(r.get(), nullptr, 0);
    if (rows.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, TextFaint);
        ImGui::TextUnformatted("No animated layers yet.");
        ImGui::PopStyleColor();
        return;
    }
    float tmax = 1.0f;
    for (auto& r : rows) if (r.cat == 0) tmax = std::max(tmax, r.end);
    tmax = std::ceil(tmax * 2) / 2;
    float nameW = W * 0.38f, barW = W - nameW - U(4), rowH = U(20);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 top = ImGui::GetCursorScreenPos();
    // time ruler
    for (float s = 0; s <= tmax + 0.001f; s += 0.5f)
    {
        float x = top.x + nameW + U(4) + s / tmax * barW;
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%.1fs", s);
        dl->AddText(fonts::UI(fonts::UI_Regular), ImGui::GetFontSize() * 0.8f, ImVec2(x - U(8), top.y), TextFaint, lbl);
    }
    ImGui::Dummy(ImVec2(W, U(14)));
    ImVec2 body = ImGui::GetCursorScreenPos();
    for (size_t i = 0; i < rows.size() && i < 40; i++)
    {
        const Row& r = rows[i];
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::InvisibleButton("##row", ImVec2(W, rowH))) Select(r.node->id);
        bool sel = IsSelected(r.node->id);
        if (sel || ImGui::IsItemHovered()) dl->AddRectFilled(p, ImVec2(p.x + W, p.y + rowH), sel ? IM_COL32(13, 153, 255, 40) : Hover, U(4));
        std::string nm = r.node->name.empty() ? r.node->id : r.node->name;
        dl->PushClipRect(p, ImVec2(p.x + nameW, p.y + rowH), true);
        dl->AddText(ImVec2(p.x + U(4), p.y + (rowH - ImGui::GetFontSize()) * 0.5f), sel ? Text : TextDim, nm.c_str());
        dl->PopClipRect();
        float bx = p.x + nameW + U(4);
        dl->AddRectFilled(ImVec2(bx, p.y + rowH * 0.5f - 1), ImVec2(bx + barW, p.y + rowH * 0.5f + 1), IM_COL32(255, 255, 255, 18));
        if (r.cat == 0)
        {
            float x0 = bx + r.start / tmax * barW, x1 = bx + std::min(r.end, tmax) / tmax * barW;
            dl->AddRectFilled(ImVec2(x0, p.y + U(4)), ImVec2(std::max(x1, x0 + U(3)), p.y + rowH - U(4)), Accent, U(3));
        }
        else
        {
            ImU32 c = r.cat == 1 ? Purple : IM_COL32(120, 120, 130, 255);
            for (float x = bx; x < bx + barW; x += U(8)) dl->AddRectFilled(ImVec2(x, p.y + U(6)), ImVec2(std::min(x + U(5), bx + barW), p.y + rowH - U(6)), c, U(2));
            const char* tag = r.cat == 1 ? "loop" : r.node->anim.trigger == fim::Trigger_Press ? "press" : r.node->anim.trigger == fim::Trigger_Active ? "active" : "hover";
            dl->AddText(fonts::UI(fonts::UI_Medium), ImGui::GetFontSize() * 0.8f, ImVec2(bx + U(4), p.y + U(3)), IM_COL32_WHITE, tag);
        }
        ImGui::PopID();
    }
    // Playhead while playing on the canvas
    if (t < animPlayUntil)
    {
        float x = body.x + nameW + U(4) + std::min((float)(t - s_playStart), tmax) / tmax * barW;
        dl->AddLine(ImVec2(x, body.y - U(2)), ImVec2(x, ImGui::GetCursorScreenPos().y), theme::Guide, 1.5f);
    }
}
