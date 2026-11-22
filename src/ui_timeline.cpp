// Keyframe timeline (bottom of the canvas) and the motion path drawn on the canvas.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "widgets.h"
#include "renderer.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <functional>

using namespace theme;

static const float kKeyEps = 1e-3f;

static float PropDefault(int prop) { return fim::AnimPropDefault(prop); }
static bool PropIsPercent(int prop) { return prop == fim::Prop_Scale || prop == fim::Prop_Opacity; }
static float SnapTime(float t) { return std::max(0.0f, std::round(t * 100.0f) / 100.0f); }

static void DrawDiamond(ImDrawList* dl, ImVec2 c, float r, ImU32 fill, ImU32 border)
{
    ImVec2 pts[] = { ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x, c.y + r), ImVec2(c.x - r, c.y) };
    if (fill) dl->AddConvexPolyFilled(pts, 4, fill);
    if (border) dl->AddPolyline(pts, 4, border, ImDrawFlags_Closed, 1.2f);
}

// ---------------------------------------------------------------- keyframe data helpers

float App::SampleProp(const Node* n, int prop, float t) const
{
    const KeyTrackFx* tr = n->anim.Track(prop);
    if (n->anim.preset != fim::Anim_Keyframes || !tr || tr->keys.empty()) return PropDefault(prop);
    fim::KeyTrack view{ prop, tr->keys.data(), (int)tr->keys.size() };
    return fim::KeyTrackSample(view, t, n->anim.bezier);
}

void App::EnableKeyframes(Node* n)
{
    if (n->anim.preset == fim::Anim_Keyframes) return;
    AnimFx a;
    a.preset = fim::Anim_Keyframes;
    a.trigger = fim::Trigger_Appear;
    a.easing = fim::Ease_InOut;
    a.duration = std::max(1.0f, TimelineLength());
    n->anim = a;
}

void App::SetKey(Node* n, int prop, float t, float v)
{
    EnableKeyframes(n);
    KeyTrackFx* tr = n->anim.Track(prop, true);
    bool found = false;
    for (auto& k : tr->keys) if (std::fabs(k.time - t) < kKeyEps) { k.value = v; found = true; }
    if (!found)
    {
        tr->keys.push_back({ t, v, n->anim.easing });
        std::sort(tr->keys.begin(), tr->keys.end(), [](const fim::Keyframe& a, const fim::Keyframe& b) { return a.time < b.time; });
    }
    n->anim.duration = std::max(n->anim.duration, t);
    MarkDirty();
}

bool App::HasKey(const Node* n, int prop, float t) const
{
    const KeyTrackFx* tr = n->anim.Track(prop);
    if (!tr) return false;
    for (auto& k : tr->keys) if (std::fabs(k.time - t) < kKeyEps) return true;
    return false;
}

void App::RemoveKey(Node* n, int prop, float t)
{
    KeyTrackFx* tr = n->anim.Track(prop, false);
    if (!tr) return;
    tr->keys.erase(std::remove_if(tr->keys.begin(), tr->keys.end(), [&](const fim::Keyframe& k) { return std::fabs(k.time - t) < kKeyEps; }), tr->keys.end());
    auto& ts = n->anim.tracks;
    ts.erase(std::remove_if(ts.begin(), ts.end(), [](const KeyTrackFx& q) { return q.keys.empty(); }), ts.end());
    MarkDirty();
}

void App::DeleteSelectedKeys()
{
    for (auto& s : keySel)
        if (Node* n = doc.Find(s.node)) RemoveKey(n, s.prop, s.time);
    keySel.clear();
}

std::vector<Node*> App::TimelineNodes()
{
    std::vector<Node*> out;
    std::function<void(Node*)> rec = [&](Node* n) {
        if (n->anim.preset == fim::Anim_Keyframes) out.push_back(n);
        for (auto& c : n->children) rec(c.get());
    };
    for (auto& r : doc.roots) rec(r.get());
    for (Node* s : SelNodes())
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    return out;
}

float App::TimelineLength()
{
    float len = 0;
    std::function<void(Node*)> rec = [&](Node* n) {
        if (n->anim.preset == fim::Anim_Keyframes) len = std::max(len, n->anim.duration + n->anim.delay);
        for (auto& c : n->children) rec(c.get());
    };
    for (auto& r : doc.roots) rec(r.get());
    return len > 0 ? len : 1.0f;
}

void App::TickTimeline()
{
    if (!timelineOpen || !timelinePlaying) return;
    float len = TimelineLength();
    timelineTime += ImGui::GetIO().DeltaTime;
    if (timelineTime > len)
    {
        if (timelineLoop) timelineTime = std::fmod(timelineTime, len);
        else { timelineTime = len; timelinePlaying = false; }
    }
}

// ---------------------------------------------------------------- timeline panel

void App::DrawTimeline()
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 o = ImGui::GetWindowPos();
    float W = ImGui::GetWindowWidth(), H = ImGui::GetWindowHeight();
    dl->AddRectFilled(o, ImVec2(o.x + W, o.y + H), Bg);
    float len = TimelineLength();
    timelineTime = std::clamp(timelineTime, 0.0, (double)len);
    Node* focus = Sel();

    // ---- toolbar
    float tb = U(40);
    dl->AddLine(ImVec2(o.x, o.y + tb), ImVec2(o.x + W, o.y + tb), Divider);
    ImGui::SetCursorScreenPos(ImVec2(o.x + U(10), o.y + (tb - ImGui::GetFrameHeight()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(6), U(6)));
    ImGui::BeginGroup();
    ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), 0.0f);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Timeline");
    ImGui::PopFont();
    ImGui::SameLine(0, U(14));
    if (IconButton("##tlstart", Icon::Undo, false, 0, "Go to start")) { timelineTime = 0; timelinePlaying = false; }
    ImGui::SameLine(0, U(2));
    {
        ImVec2 p = ImGui::GetCursorScreenPos();
        float s = ImGui::GetFrameHeight();
        if (IconButton("##tlplay", timelinePlaying ? Icon::Stop : Icon::Play, timelinePlaying, 0, timelinePlaying ? "Pause" : "Play"))
        {
            if (!timelinePlaying && timelineTime >= len - 1e-3) timelineTime = 0;
            timelinePlaying = !timelinePlaying;
        }
        (void)p; (void)s;
    }
    ImGui::SameLine(0, U(2));
    if (IconButton("##tlloop", Icon::Redo, timelineLoop, 0, "Loop playback")) timelineLoop = !timelineLoop;
    ImGui::SameLine(0, U(10));
    char tbuf[48];
    snprintf(tbuf, sizeof(tbuf), "%.2f s / %.2f s", timelineTime, len);
    ImGui::AlignTextToFramePadding();
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::TextUnformatted(tbuf);
    ImGui::PopStyleColor();

    if (focus && focus->anim.preset == fim::Anim_Keyframes)
    {
        AnimFx& a = focus->anim;
        ImGui::SameLine(0, U(18));
        ui::Label("Length");
        ImGui::SameLine(0, U(6));
        float l = a.duration;
        if (ui::NumField("tllen", "", &l, U(80), 0.01f, 0.05f, 600, " s")) { a.duration = std::max(0.05f, l); MarkDirty(); }
        ImGui::SameLine(0, U(10));
        ui::Label("Plays");
        ImGui::SameLine(0, U(6));
        const char* trigs[] = { "On appear", "While hovered", "While pressed", "While active", "Loop" };
        if (ui::Combo("##tltrig", &a.trigger, trigs, 5, U(130))) MarkDirty();
    }
    if (!keySel.empty())
    {
        ImGui::SameLine(0, U(10));
        ui::Label("Easing");
        ImGui::SameLine(0, U(6));
        int e = -1;
        if (Node* n = doc.Find(keySel[0].node))
            if (const KeyTrackFx* tr = n->anim.Track(keySel[0].prop))
                for (auto& k : tr->keys) if (std::fabs(k.time - keySel[0].time) < kKeyEps) e = k.easing;
        const char* eas[8];
        for (int i = 0; i < 8; i++) eas[i] = EasingLabel(i);
        int sel = std::max(0, e);
        if (ui::Combo("##tlease", &sel, eas, 8, U(120)))
        {
            for (auto& s : keySel)
                if (Node* n = doc.Find(s.node))
                    if (KeyTrackFx* tr = n->anim.Track(s.prop, false))
                        for (auto& k : tr->keys) if (std::fabs(k.time - s.time) < kKeyEps) k.easing = sel;
            MarkDirty();
        }
        ImGui::SameLine(0, U(8));
        char del[32];
        snprintf(del, sizeof(del), keySel.size() == 1 ? "Delete key" : "Delete %d keys", (int)keySel.size());
        if (ImGui::Button(del)) DeleteSelectedKeys();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Del / Backspace");
    }
    ImGui::EndGroup();
    // right side: zoom and close
    float fh = ImGui::GetFrameHeight();
    ImGui::SetCursorScreenPos(ImVec2(o.x + W - fh * 3 - U(18), o.y + (tb - fh) * 0.5f));
    if (IconButton("##tlzout", Icon::Minus, false, 0, "Zoom out (Ctrl+wheel)")) timelineZoom = std::max(0.25f, timelineZoom / 1.5f);
    ImGui::SameLine(0, U(2));
    if (IconButton("##tlzin", Icon::Plus, false, 0, "Zoom in (Ctrl+wheel)")) timelineZoom = std::min(40.0f, timelineZoom * 1.5f);
    ImGui::SameLine(0, U(8));
    if (IconButton("##tlclose", Icon::Close, false, 0, "Close timeline")) { timelineOpen = false; timelinePlaying = false; }
    ImGui::PopStyleVar();

    // ---- geometry
    float L = U(250);                  // layer / property column
    float ru = U(26);                  // ruler height
    float rh = U(28);                  // row height
    float top = o.y + tb;
    float tx0 = o.x + L + U(14);       // x of time 0
    float trackW = std::max(U(80), W - L - U(28));
    float pps = trackW / std::max(0.05f, len) * timelineZoom;   // pixels per second
    timelineScroll = std::clamp(timelineScroll, 0.0f, std::max(0.0f, len * pps - trackW));
    auto X = [&](float t) { return tx0 + t * pps - timelineScroll; };
    auto T = [&](float x) { return (x - tx0 + timelineScroll) / pps; };

    // ---- ruler
    dl->AddRectFilled(ImVec2(o.x, top), ImVec2(o.x + W, top + ru), IM_COL32(38, 38, 38, 255));
    dl->AddLine(ImVec2(o.x, top + ru), ImVec2(o.x + W, top + ru), Divider);
    dl->AddLine(ImVec2(o.x + L, top), ImVec2(o.x + L, o.y + H), Divider);
    {
        float steps[] = { 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f, 1, 2, 5, 10, 30, 60 };
        float step = 60;
        for (float s : steps) if (s * pps >= U(64)) { step = s; break; }
        dl->PushClipRect(ImVec2(o.x + L + 1, top), ImVec2(o.x + W, o.y + H), true);
        float sub = step / 5;
        for (float t = 0; t <= len + 1e-4f; t += sub)
        {
            float x = X(t);
            bool major = std::fabs(std::fmod(t + sub * 0.5f, step) - sub * 0.5f) < sub * 0.25f;
            dl->AddLine(ImVec2(x, top + ru - (major ? U(10) : U(5))), ImVec2(x, top + ru), major ? TextDim : TextFaint);
            if (major)
            {
                char b[16];
                if (step >= 1) snprintf(b, sizeof(b), "%gs", std::round(t * 100) / 100);
                else snprintf(b, sizeof(b), "%.2f", t);
                dl->AddText(ImVec2(x + U(3), top + U(3)), TextDim, b);
            }
        }
        // the part after the end of the timeline is dimmed
        dl->AddRectFilled(ImVec2(X(len), top), ImVec2(o.x + W, o.y + H), IM_COL32(0, 0, 0, 50));
        dl->PopClipRect();
        ImGui::SetCursorScreenPos(ImVec2(o.x + L, top));
        ImGui::InvisibleButton("##ruler", ImVec2(W - L, ru));
        if (ImGui::IsItemActive()) { timelineTime = std::clamp(T(io.MousePos.x), 0.0f, len); timelinePlaying = false; }
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    dl->AddText(ImVec2(o.x + U(12), top + (ru - ImGui::GetFontSize()) * 0.5f), TextFaint, "Layer / property");

    // ---- rows (scroll together)
    ImGui::SetCursorScreenPos(ImVec2(o.x, top + ru));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::BeginChild("##tlrows", ImVec2(W, H - tb - ru), 0, ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar(2);
    ImDrawList* rdl = ImGui::GetWindowDrawList();
    bool rowsHovered = ImGui::IsWindowHovered();
    if (rowsHovered && io.MouseWheel != 0)
    {
        if (io.KeyCtrl)
        {
            float tm = T(io.MousePos.x);
            timelineZoom = std::clamp(timelineZoom * std::pow(1.2f, io.MouseWheel), 0.25f, 40.0f);
            float npps = trackW / std::max(0.05f, len) * timelineZoom;
            timelineScroll = std::max(0.0f, tx0 + tm * npps - io.MousePos.x);
        }
        else if (io.KeyShift) timelineScroll -= io.MouseWheel * U(60);
        else ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseWheel * rh * 2);
    }

    struct HoverKey { std::string node; int prop = -1; float time = 0; bool valid = false; } hk;
    struct DblRow { Node* node = nullptr; int prop = -1; } dblRow;
    struct KeyPos { std::string node; int prop; float time; ImVec2 c; };
    std::vector<KeyPos> keyPos;   // screen position of every visible key (rubber-band selection)
    auto nodes = TimelineNodes();
    if (nodes.empty())
    {
        ImGui::SetCursorPos(ImVec2(U(14), U(12)));
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::TextUnformatted("Select a layer and press \"Animate\" (or set a key with the diamond) to create keyframes for position, scale, opacity and rotation.");
        ImGui::PopStyleColor();
    }
    float y = ImGui::GetCursorScreenPos().y;
    float rx0 = ImGui::GetWindowPos().x;
    for (Node* n : nodes)
    {
        ImGui::PushID(n->id.c_str());
        bool kf = n->anim.preset == fim::Anim_Keyframes;
        bool selected = IsSelected(n->id);
        bool& open = timelineExpanded[n->id];
        if (!timelineExpandedInit.count(n->id)) { open = true; timelineExpandedInit.insert(n->id); }
        // header row
        rdl->AddRectFilled(ImVec2(rx0, y), ImVec2(rx0 + W, y + rh), selected ? IM_COL32(13, 153, 255, 34) : IM_COL32(255, 255, 255, 6));
        ImGui::SetCursorScreenPos(ImVec2(rx0 + U(6), y));
        if (ImGui::InvisibleButton("##chev", ImVec2(U(18), rh)) && kf) open = !open;
        if (kf) DrawIcon(rdl, open ? Icon::ChevronDown : Icon::ChevronRight, ImVec2(rx0 + U(15), y + rh * 0.5f), U(9), TextDim);
        ImGui::SetCursorScreenPos(ImVec2(rx0 + U(26), y));
        if (ImGui::InvisibleButton("##name", ImVec2(L - U(26) - (kf ? 0 : U(84)), rh))) Select(n->id);
        std::string nm = n->name.empty() ? n->id : n->name;
        rdl->PushClipRect(ImVec2(rx0, y), ImVec2(rx0 + L - U(90), y + rh), true);
        rdl->AddText(fonts::UI(fonts::UI_Medium), ImGui::GetFontSize(), ImVec2(rx0 + U(28), y + (rh - ImGui::GetFontSize()) * 0.5f), Text, nm.c_str());
        rdl->PopClipRect();
        if (!kf)
        {
            ImGui::SetCursorScreenPos(ImVec2(rx0 + L - U(88), y + (rh - fh) * 0.5f));
            if (ui::PrimaryButton("Animate", ImVec2(U(80), fh)))
            {
                EnableKeyframes(n);
                SetKey(n, fim::Prop_X, 0, 0);
                SetKey(n, fim::Prop_Y, 0, 0);
                open = true;
            }
        }
        else
        {
            // summary diamonds: every key time of the layer
            rdl->PushClipRect(ImVec2(rx0 + L + 1, y), ImVec2(rx0 + W, y + rh), true);
            for (auto& tr : n->anim.tracks)
                for (auto& k : tr.keys)
                {
                    ImVec2 c(X(k.time + n->anim.delay), y + rh * 0.5f);
                    DrawDiamond(rdl, c, U(4), 0, TextDim);
                    keyPos.push_back({ n->id, tr.prop, k.time, c });   // a band over the layer row takes all its keys
                }
            rdl->PopClipRect();
        }
        y += rh;

        if (kf && open)
        {
            float base = n->anim.delay;   // keys are drawn in document time (delay shifts the whole layer)
            for (int prop = 0; prop < fim::Prop_Count; prop++)
            {
                ImGui::PushID(prop);
                const KeyTrackFx* tr = n->anim.Track(prop);
                float tl = (float)timelineTime - base;
                rdl->AddLine(ImVec2(rx0, y + rh), ImVec2(rx0 + W, y + rh), IM_COL32(255, 255, 255, 10));
                // key at playhead toggle
                bool keyHere = HasKey(n, prop, tl);
                ImGui::SetCursorScreenPos(ImVec2(rx0 + U(30), y));
                if (ImGui::InvisibleButton("##kt", ImVec2(U(20), rh)))
                {
                    if (keyHere) RemoveKey(n, prop, tl);
                    else SetKey(n, prop, std::max(0.0f, tl), SampleProp(n, prop, tl));
                }
                bool kh = ImGui::IsItemHovered();
                DrawDiamond(rdl, ImVec2(rx0 + U(40), y + rh * 0.5f), U(5), keyHere ? Accent : 0, keyHere ? Accent : (kh ? Text : (tr ? TextDim : TextFaint)));
                if (kh) ImGui::SetTooltip(keyHere ? "Remove key at playhead" : "Add key at playhead");
                rdl->AddText(ImVec2(rx0 + U(56), y + (rh - ImGui::GetFontSize()) * 0.5f), tr ? Text : TextDim, AnimPropLabel(prop));
                // value at playhead
                float v = SampleProp(n, prop, tl);
                float shown = PropIsPercent(prop) ? std::round(v * 1000) / 10 : std::round(v * 100) / 100;
                ImGui::SetCursorScreenPos(ImVec2(rx0 + L - U(86), y + (rh - fh) * 0.5f));
                if (ui::NumField("##val", "", &shown, U(78), 1.0f, -100000, 100000, PropIsPercent(prop) ? "%" : prop == fim::Prop_Rotation ? "\xC2\xB0" : nullptr))
                    SetKey(n, prop, std::max(0.0f, tl), PropIsPercent(prop) ? shown / 100.0f : shown);

                // track area
                ImGui::SetCursorScreenPos(ImVec2(rx0 + L, y));
                ImGui::SetNextItemAllowOverlap();
                ImGui::InvisibleButton("##row", ImVec2(W - L, rh));
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) { dblRow.node = n; dblRow.prop = prop; }
                rdl->PushClipRect(ImVec2(rx0 + L + 1, y), ImVec2(rx0 + W, y + rh), true);
                if (tr)
                {
                    for (size_t i = 0; i + 1 < tr->keys.size(); i++)
                        rdl->AddLine(ImVec2(X(tr->keys[i].time + base), y + rh * 0.5f), ImVec2(X(tr->keys[i + 1].time + base), y + rh * 0.5f), IM_COL32(13, 153, 255, 90), U(2));
                    for (auto& k : tr->keys)
                    {
                        ImVec2 c(X(k.time + base), y + rh * 0.5f);
                        bool isSel = false;
                        for (auto& s : keySel) if (s.node == n->id && s.prop == prop && std::fabs(s.time - k.time) < kKeyEps) isSel = true;
                        bool hov = rowsHovered && std::fabs(io.MousePos.x - c.x) <= U(6) && std::fabs(io.MousePos.y - c.y) <= U(8);
                        if (hov) { hk.node = n->id; hk.prop = prop; hk.time = k.time; hk.valid = true; }
                        keyPos.push_back({ n->id, prop, k.time, c });
                        DrawDiamond(rdl, c, hov ? U(6.5f) : U(5.5f), isSel ? Accent : IM_COL32(220, 220, 225, 255), isSel ? IM_COL32_WHITE : IM_COL32(40, 40, 40, 255));
                    }
                }
                rdl->PopClipRect();
                y += rh;
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::SetCursorScreenPos(ImVec2(rx0, y));
    ImGui::Dummy(ImVec2(W, U(8)));

    // ---- key interaction
    if (hk.valid) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    // (the row buttons under the keys become active on the same click, so only text editing blocks key picking)
    if (rowsHovered && ImGui::IsMouseClicked(0) && !io.WantTextInput && io.MousePos.x > rx0 + L)
    {
        if (hk.valid)
        {
            bool isSel = false;
            for (auto& s : keySel) if (s.node == hk.node && s.prop == hk.prop && std::fabs(s.time - hk.time) < kKeyEps) isSel = true;
            if (io.KeyShift || io.KeyCtrl)
            {
                if (isSel) keySel.erase(std::remove_if(keySel.begin(), keySel.end(), [&](const KeySel& s) { return s.node == hk.node && s.prop == hk.prop && std::fabs(s.time - hk.time) < kKeyEps; }), keySel.end());
                else keySel.push_back({ hk.node, hk.prop, hk.time });
            }
            else if (!isSel) keySel = { { hk.node, hk.prop, hk.time } };
            keyDragOrig.clear();
            for (auto& s : keySel) keyDragOrig.push_back(s.time);
            keyDragStartX = io.MousePos.x;
            keyDragging = true;
        }
        else
        {
            // empty track area: rubber-band selection (Shift / Ctrl adds to the current keys)
            if (!io.KeyShift && !io.KeyCtrl) keySel.clear();
            keyMarquee = true;
            keyMarqueeT0 = T(io.MousePos.x);
            keyMarqueeY0 = io.MousePos.y - ImGui::GetWindowPos().y + ImGui::GetScrollY();
            keyMarqueeBase = keySel;
        }
    }
    if (keyMarquee)
    {
        ImVec2 wp0 = ImGui::GetWindowPos();
        ImVec2 m0(X(keyMarqueeT0), wp0.y + keyMarqueeY0 - ImGui::GetScrollY());
        ImVec2 m1 = io.MousePos;
        ImVec2 r0(std::min(m0.x, m1.x), std::min(m0.y, m1.y)), r1(std::max(m0.x, m1.x), std::max(m0.y, m1.y));
        // drag past the edges scrolls the rows and the time axis
        float edge = U(24), sy = 0, sx = 0;
        if (m1.y < wp0.y + edge) sy = -1; else if (m1.y > wp0.y + ImGui::GetWindowHeight() - edge) sy = 1;
        if (m1.x > wp0.x + W - edge) sx = 1; else if (m1.x < wp0.x + L + edge) sx = -1;
        if (sy != 0) ImGui::SetScrollY(ImGui::GetScrollY() + sy * U(600) * io.DeltaTime);
        if (sx != 0) timelineScroll = std::max(0.0f, timelineScroll + sx * U(600) * io.DeltaTime);
        if (!ImGui::IsMouseDown(0)) keyMarquee = false;
        else if (r1.x - r0.x > 2 || r1.y - r0.y > 2)
        {
            keySel = keyMarqueeBase;
            for (auto& k : keyPos)
            {
                if (k.c.x < r0.x || k.c.x > r1.x || k.c.y < r0.y || k.c.y > r1.y) continue;
                bool dup = false;
                for (auto& s : keySel) if (s.node == k.node && s.prop == k.prop && std::fabs(s.time - k.time) < kKeyEps) dup = true;
                if (!dup) keySel.push_back({ k.node, k.prop, k.time });
            }
            rdl->PushClipRect(ImVec2(wp0.x + L + 1, wp0.y), ImVec2(wp0.x + W, wp0.y + ImGui::GetWindowHeight()), true);
            rdl->AddRectFilled(r0, r1, IM_COL32(13, 153, 255, 30));
            rdl->AddRect(r0, r1, IM_COL32(13, 153, 255, 200));
            rdl->PopClipRect();
        }
    }
    if (rowsHovered && ImGui::IsMouseClicked(1) && hk.valid)
    {
        bool isSel = false;
        for (auto& s : keySel) if (s.node == hk.node && s.prop == hk.prop && std::fabs(s.time - hk.time) < kKeyEps) isSel = true;
        if (!isSel) keySel = { { hk.node, hk.prop, hk.time } };
        ImGui::OpenPopup("##keymenu");
    }
    if (keyDragging)
    {
        if (!ImGui::IsMouseDown(0)) keyDragging = false;
        else if (std::fabs(io.MousePos.x - keyDragStartX) > 2)
        {
            float dt = (io.MousePos.x - keyDragStartX) / pps;
            for (size_t i = 0; i < keySel.size() && i < keyDragOrig.size(); i++)
            {
                KeySel& s = keySel[i];
                Node* n = doc.Find(s.node);
                KeyTrackFx* tr = n ? n->anim.Track(s.prop, false) : nullptr;
                if (!tr) continue;
                float nt = io.KeyAlt ? std::max(0.0f, keyDragOrig[i] + dt) : SnapTime(keyDragOrig[i] + dt);
                for (auto& k : tr->keys) if (std::fabs(k.time - s.time) < kKeyEps) { k.time = nt; break; }
                std::sort(tr->keys.begin(), tr->keys.end(), [](const fim::Keyframe& a, const fim::Keyframe& b) { return a.time < b.time; });
                s.time = nt;
                n->anim.duration = std::max(n->anim.duration, nt);
            }
            MarkDirty();
        }
    }
    if (dblRow.node && !hk.valid)
    {
        float t = SnapTime(T(io.MousePos.x) - dblRow.node->anim.delay);
        SetKey(dblRow.node, dblRow.prop, t, SampleProp(dblRow.node, dblRow.prop, t));
        keySel = { { dblRow.node->id, dblRow.prop, t } };
    }
    if (ImGui::BeginPopup("##keymenu"))
    {
        ImGui::TextDisabled("Easing to the next key");
        for (int e = 0; e < 8; e++)
            if (ImGui::MenuItem(EasingLabel(e)))
            {
                for (auto& s : keySel)
                    if (Node* n = doc.Find(s.node))
                        if (KeyTrackFx* tr = n->anim.Track(s.prop, false))
                            for (auto& k : tr->keys) if (std::fabs(k.time - s.time) < kKeyEps) k.easing = e;
                MarkDirty();
            }
        ImGui::Separator();
        if (ImGui::MenuItem("Move playhead here") && !keySel.empty())
            if (Node* n = doc.Find(keySel[0].node)) timelineTime = keySel[0].time + n->anim.delay;
        if (ImGui::MenuItem("Delete keys", "Del")) DeleteSelectedKeys();
        ImGui::EndPopup();
    }

    // ---- playhead (across rows)
    float px = X((float)timelineTime);
    ImVec2 wp = ImGui::GetWindowPos();
    if (px >= wp.x + L && px <= wp.x + W)
        rdl->AddLine(ImVec2(px, wp.y), ImVec2(px, wp.y + ImGui::GetWindowHeight() + ImGui::GetScrollY() + U(2000)), Accent, 1.5f);
    ImGui::EndChild();
    if (px >= o.x + L && px <= o.x + W)
    {
        dl->AddLine(ImVec2(px, top), ImVec2(px, top + ru), Accent, 1.5f);
        dl->AddRectFilled(ImVec2(px - U(5), top + U(2)), ImVec2(px + U(5), top + U(12)), Accent, U(2));
    }
}

// ---------------------------------------------------------------- motion path on the canvas

// Screen position of a node's center offset by the keyframed X / Y at time t.
static ImVec2 PathPoint(App* app, Node* n, ImVec2 baseCenter, float t)
{
    return app->WorldToScreen(ImVec2(baseCenter.x + app->SampleProp(n, fim::Prop_X, t), baseCenter.y + app->SampleProp(n, fim::Prop_Y, t)));
}

static std::vector<float> PathKeyTimes(const Node* n)
{
    std::vector<float> ts;
    for (int p : { (int)fim::Prop_X, (int)fim::Prop_Y })
        if (const KeyTrackFx* tr = n->anim.Track(p))
            for (auto& k : tr->keys)
                if (std::none_of(ts.begin(), ts.end(), [&](float q) { return std::fabs(q - k.time) < kKeyEps; })) ts.push_back(k.time);
    std::sort(ts.begin(), ts.end());
    return ts;
}

bool App::MotionPathInput()
{
    if (!timelineOpen || preview) return false;
    ImGuiIO& io = ImGui::GetIO();
    if (pathDrag.active)
    {
        Node* n = doc.Find(pathDrag.node);
        if (!n || !ImGui::IsMouseDown(0)) { pathDrag.active = false; return true; }
        ImVec2 w = ScreenToWorld(io.MousePos);
        float dx = std::round(w.x - pathDrag.baseCenter.x - pathDrag.grab.x), dy = std::round(w.y - pathDrag.baseCenter.y - pathDrag.grab.y);
        SetKey(n, fim::Prop_X, pathDrag.time, dx);
        SetKey(n, fim::Prop_Y, pathDrag.time, dy);
        timelineTime = pathDrag.time + n->anim.delay;
        return true;
    }
    for (Node* n : SelNodes())
    {
        if (n->anim.preset != fim::Anim_Keyframes) continue;
        ImVec2 a, b;
        NodeWorldRect(n, &vars, &a, &b);
        ImVec2 bc((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        for (float t : PathKeyTimes(n))
        {
            ImVec2 p = PathPoint(this, n, bc, t);
            if (ImLengthSqr(ImVec2(io.MousePos.x - p.x, io.MousePos.y - p.y)) <= U(7) * U(7))
            {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                ImGui::SetTooltip("Key at %.2f s - drag to move", t);
                if (ImGui::IsMouseClicked(0))
                {
                    pathDrag.active = true;
                    pathDrag.node = n->id;
                    pathDrag.time = t;
                    pathDrag.baseCenter = bc;
                    ImVec2 w = ScreenToWorld(io.MousePos);
                    pathDrag.grab = ImVec2(w.x - bc.x - SampleProp(n, fim::Prop_X, t), w.y - bc.y - SampleProp(n, fim::Prop_Y, t));
                }
                return true;
            }
        }
    }
    return false;
}

void App::DrawMotionPath(ImDrawList* dl)
{
    if (!timelineOpen || preview) return;
    for (Node* n : SelNodes())
    {
        if (n->anim.preset != fim::Anim_Keyframes) continue;
        if (!n->anim.Track(fim::Prop_X) && !n->anim.Track(fim::Prop_Y)) continue;
        ImVec2 a, b;
        NodeWorldRect(n, &vars, &a, &b);
        ImVec2 bc((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        float len = std::max(0.05f, n->anim.duration);
        const int N = 96;
        ImVec2 pts[N + 1];
        for (int i = 0; i <= N; i++) pts[i] = PathPoint(this, n, bc, len * i / N);
        // dashed path
        for (int i = 0; i < N; i++)
            if (i % 2 == 0) dl->AddLine(pts[i], pts[i + 1], IM_COL32(13, 153, 255, 200), 1.5f);
        for (float t : PathKeyTimes(n))
        {
            ImVec2 p = PathPoint(this, n, bc, t);
            bool active = pathDrag.active && pathDrag.node == n->id && std::fabs(pathDrag.time - t) < kKeyEps;
            dl->AddCircleFilled(p, U(4.5f), active ? Accent : IM_COL32_WHITE);
            dl->AddCircle(p, U(4.5f), Accent, 0, 1.5f);
        }
        ImVec2 cur = PathPoint(this, n, bc, (float)timelineTime - n->anim.delay);
        DrawDiamond(dl, cur, U(6), Accent, IM_COL32_WHITE);
    }
}
