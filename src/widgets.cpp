#include "widgets.h"
#include "platform.h"
#include "theme.h"
#include "fonts.h"
#include "document.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace theme;

namespace ui
{
    float Avail() { return ImGui::GetContentRegionAvail().x; }

    static std::string FormatNum(float v)
    {
        char buf[32];
        if (std::fabs(v - std::round(v)) < 0.005f) snprintf(buf, sizeof(buf), "%d", (int)std::round(v));
        else
        {
            snprintf(buf, sizeof(buf), "%.2f", v);
            std::string s = buf;
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
            return s;
        }
        return buf;
    }

    // Evaluates simple arithmetic like "120+24" or "200/2".
    static bool ParseExpr(const char* s, float* out)
    {
        char* end = nullptr;
        double acc = strtod(s, &end);
        if (end == s) return false;
        while (*end)
        {
            while (*end == ' ') end++;
            char op = *end;
            if (!op) break;
            if (op != '+' && op != '-' && op != '*' && op != '/') return false;
            char* e2 = nullptr;
            double v = strtod(end + 1, &e2);
            if (e2 == end + 1) return false;
            if (op == '+') acc += v; else if (op == '-') acc -= v; else if (op == '*') acc *= v; else if (v != 0) acc /= v;
            end = e2;
        }
        *out = (float)acc;
        return true;
    }

    bool NumField(const char* id, const char* label, float* v, float width, float speed, float vmin, float vmax, const char* suffix, Icon icon)
    {
        ImGui::PushID(id);
        bool changed = false;
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float labelW = (label && *label) || (int)icon >= 0 ? U(22) : U(6);
        ImGuiID inputId = ImGui::GetID("##v");
        bool active = ImGui::GetActiveID() == inputId;
        bool hovered = ImGui::IsMouseHoveringRect(p, ImVec2(p.x + width, p.y + h));
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), active ? IM_COL32(30, 30, 30, 255) : Field, ImGui::GetStyle().FrameRounding);
        if (active) dl->AddRect(p, ImVec2(p.x + width, p.y + h), Accent, ImGui::GetStyle().FrameRounding);
        else if (hovered) dl->AddRect(p, ImVec2(p.x + width, p.y + h), IM_COL32(255, 255, 255, 30), ImGui::GetStyle().FrameRounding);

        // Scrub label
        ImGui::InvisibleButton("##lbl", ImVec2(labelW, h));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.x != 0)
        {
            float k = ImGui::GetIO().KeyShift ? 10.0f : 1.0f;
            *v = std::clamp(*v + ImGui::GetIO().MouseDelta.x * speed * k, vmin, vmax);
            if (speed >= 1.0f) *v = std::round(*v);
            changed = true;
        }
        if ((int)icon >= 0) DrawIcon(dl, icon, ImVec2(p.x + labelW * 0.5f + U(1), p.y + h * 0.5f), U(12), TextDim);
        else if (label && *label)
        {
            ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(p.x + (labelW - ts.x) * 0.5f + U(1), p.y + (h - ts.y) * 0.5f), TextFaint, label);
        }

        ImGui::SameLine(0, 0);
        char buf[64];
        std::string s = FormatNum(*v);
        if (suffix && !active) s += suffix;
        snprintf(buf, sizeof(buf), "%s", s.c_str());
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(U(2), ImGui::GetStyle().FramePadding.y));
        ImGui::SetNextItemWidth(width - labelW);
        if (ImGui::InputText("##v", buf, sizeof(buf), ImGuiInputTextFlags_AutoSelectAll))
        {
            float nv;
            if (ParseExpr(buf, &nv)) { *v = std::clamp(nv, vmin, vmax); changed = true; }
        }
        if (ImGui::IsItemActive())
        {
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { *v = std::clamp(*v + (ImGui::GetIO().KeyShift ? 10.0f : 1.0f), vmin, vmax); changed = true; }
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { *v = std::clamp(*v - (ImGui::GetIO().KeyShift ? 10.0f : 1.0f), vmin, vmax); changed = true; }
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        ImGui::PopID();
        return changed;
    }

    void Checker(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell)
    {
        float s = cell > 0 ? cell : std::max(2.0f, (b.x - a.x) / 4);
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
        for (float y = a.y; y < b.y; y += s)
            for (float x = a.x; x < b.x; x += s)
                if (((int)((x - a.x) / s) + (int)((y - a.y) / s)) % 2)
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + s, b.x), std::min(y + s, b.y)), IM_COL32(204, 204, 204, 255));
    }

    static std::vector<ImVec4> s_pageColors;
    void SetPageColors(const std::vector<ImVec4>& colors) { s_pageColors = colors; }
    std::vector<ImVec4>& SavedColors() { static std::vector<ImVec4> s; return s; }
    static SvOverlay s_svOverlay;
    void SetSvOverlay(SvOverlay fn) { s_svOverlay = std::move(fn); }

    // A grid of color swatches; returns the clicked index or -1.
    static int SwatchGrid(const char* id, const std::vector<ImVec4>& colors, float w, int max = 64)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float sw = U(20), gap = U(6);
        int perRow = std::max(1, (int)((w + gap) / (sw + gap)));
        int hit = -1;
        ImGui::PushID(id);
        for (size_t i = 0; i < colors.size() && (int)i < max; i++)
        {
            if (i % perRow) ImGui::SameLine(0, gap);
            ImGui::PushID((int)i);
            ImVec2 q = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##pc", ImVec2(sw, sw))) hit = (int)i;
            const ImVec4& pc = colors[i];
            if (pc.w < 1.0f) Checker(dl, q, ImVec2(q.x + sw, q.y + sw));
            dl->AddRectFilled(q, ImVec2(q.x + sw, q.y + sw), ImGui::ColorConvertFloat4ToU32(pc), U(3));
            dl->AddRect(q, ImVec2(q.x + sw, q.y + sw), ImGui::IsItemHovered() ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 40), U(3));
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("#%s", ColorToHex(pc).c_str() + 1);
            ImGui::PopID();
        }
        ImGui::PopID();
        return hit;
    }

    static void EyedropperGlyph(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
    {
        // pipette: tilted barrel + bulb
        ImVec2 tip(c.x - s * 0.42f, c.y + s * 0.42f), mid(c.x + s * 0.12f, c.y - s * 0.12f);
        dl->AddLine(tip, mid, col, 1.4f);
        dl->AddLine(ImVec2(mid.x - s * 0.16f, mid.y - s * 0.16f), ImVec2(mid.x + s * 0.16f, mid.y + s * 0.16f), col, 1.4f);
        dl->AddCircleFilled(ImVec2(c.x + s * 0.28f, c.y - s * 0.28f), s * 0.17f, col);
    }

    bool ColorPickerBody(ImVec4* c, float w)
    {
        bool changed = false;
        ImGuiIO& io = ImGui::GetIO();
        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID sid = ImGui::GetID("##cpbody");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float h, s, v;
        ImGui::ColorConvertRGBtoHSV(c->x, c->y, c->z, h, s, v);
        // Keep hue (and saturation) when they are undefined for greys / black.
        float* keepH = st->GetFloatRef(sid ^ 0x51u, h);
        float* keepS = st->GetFloatRef(sid ^ 0x52u, s);
        if (s > 0.002f && v > 0.002f) *keepH = h; else h = *keepH;
        if (v > 0.002f) *keepS = s; else s = *keepS;
        auto apply = [&](float hh, float ss, float vv) {
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(hh, ss, vv, r, g, b);
            c->x = r; c->y = g; c->z = b;
            *keepH = hh; *keepS = ss;
            changed = true;
        };

        // Saturation / brightness square
        float sqH = w * 0.82f;
        ImVec2 p = ImGui::GetCursorScreenPos();
        float hr, hg, hb;
        ImGui::ColorConvertHSVtoRGB(h, 1, 1, hr, hg, hb);
        ImU32 hueCol = ImGui::ColorConvertFloat4ToU32(ImVec4(hr, hg, hb, 1));
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + sqH), IM_COL32_WHITE, hueCol, hueCol, IM_COL32_WHITE);
        dl->AddRectFilledMultiColor(p, ImVec2(p.x + w, p.y + sqH), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32_BLACK, IM_COL32_BLACK);
        if (s_svOverlay)
        {
            dl->PushClipRect(p, ImVec2(p.x + w, p.y + sqH), true);
            s_svOverlay(dl, p, ImVec2(p.x + w, p.y + sqH), h);
            dl->PopClipRect();
            s_svOverlay = nullptr;
        }
        ImGui::InvisibleButton("##sv", ImVec2(w, sqH));
        if (ImGui::IsItemActive())
            apply(h, ImSaturate((io.MousePos.x - p.x) / w), 1 - ImSaturate((io.MousePos.y - p.y) / sqH));
        ImVec2 knob(p.x + s * w, p.y + (1 - v) * sqH);
        dl->AddCircleFilled(knob, U(7), IM_COL32_WHITE);
        dl->AddCircleFilled(knob, U(5), ImGui::ColorConvertFloat4ToU32(ImVec4(c->x, c->y, c->z, 1)));
        dl->AddCircle(knob, U(7.5f), IM_COL32(0, 0, 0, 80));
        ImGui::Dummy(ImVec2(0, U(4)));

        // Eyedropper + hue / alpha bars
        float fh = ImGui::GetFrameHeight();
        ImVec2 rp = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##eyedrop", ImVec2(fh, fh * 1.6f));
        bool eyeHov = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked()) ImGui::OpenPopup("##eyedropper");
        if (eyeHov) dl->AddRectFilled(rp, ImVec2(rp.x + fh, rp.y + fh * 1.6f), Hover, U(5));
        EyedropperGlyph(dl, ImVec2(rp.x + fh * 0.5f, rp.y + fh * 0.8f), U(14), Text);
        if (eyeHov) ImGui::SetTooltip("Pick a color from the screen");
        float bx = rp.x + fh + U(10), bw = w - fh - U(10), bh = U(12);
        float hy = rp.y + U(2), ay = rp.y + fh * 1.6f - bh - U(2);
        // hue bar
        const ImU32 hues[7] = { IM_COL32(255, 0, 0, 255), IM_COL32(255, 255, 0, 255), IM_COL32(0, 255, 0, 255), IM_COL32(0, 255, 255, 255),
                                IM_COL32(0, 0, 255, 255), IM_COL32(255, 0, 255, 255), IM_COL32(255, 0, 0, 255) };
        for (int i = 0; i < 6; i++)
        {
            float x0 = bx + bw * i / 6, x1 = bx + bw * (i + 1) / 6;
            dl->AddRectFilledMultiColor(ImVec2(x0, hy), ImVec2(x1, hy + bh), hues[i], hues[i + 1], hues[i + 1], hues[i]);
        }
        ImGui::SetCursorScreenPos(ImVec2(bx - U(6), hy - U(3)));
        ImGui::InvisibleButton("##hue", ImVec2(bw + U(12), bh + U(6)));
        if (ImGui::IsItemActive()) apply(ImSaturate((io.MousePos.x - bx) / bw) * 0.9999f, s, v);
        ImVec2 hk(bx + h * bw, hy + bh * 0.5f);
        dl->AddCircleFilled(hk, bh * 0.62f, IM_COL32_WHITE);
        dl->AddCircleFilled(hk, bh * 0.42f, hueCol);
        // alpha bar
        Checker(dl, ImVec2(bx, ay), ImVec2(bx + bw, ay + bh));
        ImU32 c0 = ImGui::ColorConvertFloat4ToU32(ImVec4(c->x, c->y, c->z, 0)), c1 = ImGui::ColorConvertFloat4ToU32(ImVec4(c->x, c->y, c->z, 1));
        dl->AddRectFilledMultiColor(ImVec2(bx, ay), ImVec2(bx + bw, ay + bh), c0, c1, c1, c0);
        ImGui::SetCursorScreenPos(ImVec2(bx - U(6), ay - U(3)));
        ImGui::InvisibleButton("##alpha", ImVec2(bw + U(12), bh + U(6)));
        if (ImGui::IsItemActive()) { c->w = ImSaturate((io.MousePos.x - bx) / bw); changed = true; }
        ImVec2 ak(bx + c->w * bw, ay + bh * 0.5f);
        dl->AddCircleFilled(ak, bh * 0.62f, IM_COL32_WHITE);
        dl->AddCircleFilled(ak, bh * 0.42f, c1);
        ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + fh * 1.6f + U(8)));

        // Format + value + opacity
        int* fmt = st->GetIntRef(ImGui::GetID("##cpfmt"), 0);
        const char* fmts[] = { "Hex", "RGB", "HSL", "HSB" };
        ImGui::SetNextItemWidth(U(66));
        if (ImGui::BeginCombo("##fmt", fmts[*fmt]))
        {
            for (int i = 0; i < 4; i++) if (ImGui::Selectable(fmts[i], *fmt == i)) *fmt = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine(0, U(4));
        float opW = U(58);
        float valW = w - U(66) - U(4) - opW - U(4);
        if (*fmt == 0)
        {
            char buf[16];
            std::string hex = ColorToHex(ImVec4(c->x, c->y, c->z, 1.0f));
            snprintf(buf, sizeof(buf), "%s", hex.c_str() + 1);
            ImGui::SetNextItemWidth(valW);
            if (ImGui::InputText("##hexv", buf, sizeof(buf), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsUppercase))
            {
                ImVec4 nc;
                if (HexToColor(buf, &nc)) { c->x = nc.x; c->y = nc.y; c->z = nc.z; changed = true; }
            }
        }
        else
        {
            float a3[3];
            const char* lbl[3];
            if (*fmt == 1) { a3[0] = c->x * 255; a3[1] = c->y * 255; a3[2] = c->z * 255; lbl[0] = "R"; lbl[1] = "G"; lbl[2] = "B"; }
            else if (*fmt == 2)
            {
                // HSL from HSV
                float l = v * (1 - s / 2), sl = (l <= 0 || l >= 1) ? 0 : (v - l) / std::min(l, 1 - l);
                a3[0] = h * 360; a3[1] = sl * 100; a3[2] = l * 100; lbl[0] = "H"; lbl[1] = "S"; lbl[2] = "L";
            }
            else { a3[0] = h * 360; a3[1] = s * 100; a3[2] = v * 100; lbl[0] = "H"; lbl[1] = "S"; lbl[2] = "B"; }
            for (int i = 0; i < 3; i++) a3[i] = std::round(a3[i]);
            float fw = (valW - U(4) * 2) / 3;
            bool ch3 = false;
            for (int i = 0; i < 3; i++)
            {
                if (i) ImGui::SameLine(0, U(4));
                ImGui::PushID(i);
                float mx = *fmt == 1 ? 255.0f : (i == 0 ? 360.0f : 100.0f);
                ch3 |= NumField("##c3", "", &a3[i], fw, 1.0f, 0, mx);
                ImGui::PopID();
            }
            if (ch3)
            {
                if (*fmt == 1) { c->x = a3[0] / 255; c->y = a3[1] / 255; c->z = a3[2] / 255; changed = true; }
                else if (*fmt == 2)
                {
                    float hh = a3[0] / 360, sl = a3[1] / 100, l = a3[2] / 100;
                    float vv = l + sl * std::min(l, 1 - l), ss = vv <= 0 ? 0 : 2 * (1 - l / vv);
                    apply(std::min(hh, 0.9999f), ss, vv);
                }
                else apply(std::min(a3[0] / 360, 0.9999f), a3[1] / 100, a3[2] / 100);
            }
        }
        ImGui::SameLine(0, U(4));
        float pct = std::round(c->w * 100.0f);
        if (NumField("##cpop", "", &pct, opW, 1.0f, 0, 100, "%")) { c->w = pct / 100.0f; changed = true; }

        // On this page
        if (!s_pageColors.empty())
        {
            ImGui::Dummy(ImVec2(0, U(4)));
            ImVec2 lp = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(lp.x - U(12), lp.y), ImVec2(lp.x + w + U(12), lp.y), Divider);
            ImGui::Dummy(ImVec2(0, U(6)));
            ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
            ImGui::TextUnformatted("On this page");
            ImGui::PopStyleColor();
            int hit = SwatchGrid("##page", s_pageColors, w, 32);
            if (hit >= 0) { *c = s_pageColors[hit]; changed = true; }
        }

        // Eyedropper: a transparent full-screen popup above everything; click picks the pixel under the pointer.
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
        if (ImGui::BeginPopup("##eyedropper", ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings))
        {
            float rgb[3] = { c->x, c->y, c->z };
            bool ok = platform::ScreenPixel(io.MousePos.x, io.MousePos.y, rgb);
            ImDrawList* fg = ImGui::GetForegroundDrawList();
            ImVec2 m = io.MousePos;
            ImVec2 bc(m.x + U(34), m.y - U(34));
            fg->AddCircleFilled(bc, U(22), ImGui::ColorConvertFloat4ToU32(ImVec4(rgb[0], rgb[1], rgb[2], 1)));
            fg->AddCircle(bc, U(22), IM_COL32_WHITE, 0, 2.0f);
            fg->AddCircle(bc, U(23.5f), IM_COL32(0, 0, 0, 120), 0, 1.0f);
            fg->AddLine(ImVec2(m.x - U(8), m.y), ImVec2(m.x + U(8), m.y), IM_COL32_WHITE);
            fg->AddLine(ImVec2(m.x, m.y - U(8)), ImVec2(m.x, m.y + U(8)), IM_COL32_WHITE);
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
            if (ImGui::IsMouseClicked(0) && ok) { c->x = rgb[0]; c->y = rgb[1]; c->z = rgb[2]; changed = true; ImGui::CloseCurrentPopup(); }
            if (ImGui::IsMouseClicked(1) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        return changed;
    }

    bool ColorSwatch(const char* id, ImVec4* c, float size, const PickerHeader* header)
    {
        ImGui::PushID(id);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        bool changed = false;
        if (ImGui::InvisibleButton("##sw", ImVec2(size, size))) ImGui::OpenPopup("##picker");
        ImVec2 a(p.x + U(2), p.y + U(2)), b(p.x + size - U(2), p.y + size - U(2));
        if (c->w < 1.0f) Checker(dl, a, b);
        dl->AddRectFilled(a, b, ImGui::ColorConvertFloat4ToU32(*c), U(2));
        dl->AddRect(a, b, IM_COL32(255, 255, 255, 40), U(2));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(12), U(12)));
        // Open to the left of the inspector, like design tools do.
        // Kept inside the window using last frame's height; taller than the window scrolls.
        ImGuiViewport* vp = ImGui::GetMainViewport();
        float* lastH = ImGui::GetStateStorage()->GetFloatRef(ImGui::GetID("##pickerh"), U(470));
        float py = std::max(vp->WorkPos.y + U(8), std::min(p.y - U(40), vp->WorkPos.y + vp->WorkSize.y - *lastH - U(8)));
        ImGui::SetNextWindowPos(ImVec2(p.x - U(28), py), ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, vp->WorkSize.y - U(16)));
        if (ImGui::BeginPopup("##picker"))
        {
            *lastH = ImGui::GetWindowSize().y;
            // Top bar (Figma): Custom / Libraries, save the color, close.
            float pw = U(240), fh = ImGui::GetFrameHeight();
            int* lib = ImGui::GetStateStorage()->GetIntRef(ImGui::GetID("##lib"), 0);
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            const char* tabs[2] = { "Custom", "Libraries" };
            for (int t = 0; t < 2; t++)
            {
                if (t) ImGui::SameLine(0, U(2));
                ImVec2 q = ImGui::GetCursorScreenPos();
                ImVec2 ts = ImGui::CalcTextSize(tabs[t]);
                ImVec2 sz(ts.x + U(16), fh);
                ImGui::PushID(t);
                if (ImGui::InvisibleButton("##ptab", sz)) *lib = t;
                bool hov = ImGui::IsItemHovered();
                ImGui::PopID();
                if (*lib == t) pdl->AddRectFilled(q, ImVec2(q.x + sz.x, q.y + sz.y), IM_COL32(255, 255, 255, 22), U(5));
                pdl->AddText(ImVec2(q.x + U(8), q.y + (fh - ts.y) * 0.5f), *lib == t || hov ? Text : TextDim, tabs[t]);
            }
            ImVec4* target = c;
            float rx = ImGui::GetWindowContentRegionMin().x + pw - fh * 2 - U(2);
            ImGui::SameLine(rx);
            bool save = IconButton("##psave", Icon::Plus, false, 0, "Save color to Libraries");
            ImGui::SameLine(0, U(2));
            if (IconButton("##pclose", Icon::Close, false, 0, "Close")) ImGui::CloseCurrentPopup();
            ImVec2 lp = ImGui::GetCursorScreenPos();
            pdl->AddLine(ImVec2(lp.x - U(12), lp.y + U(2)), ImVec2(lp.x + pw + U(12), lp.y + U(2)), Divider);
            ImGui::Dummy(ImVec2(pw, U(6)));
            if (*lib == 0)
            {
                int hr = header && *header ? (*header)(&target) : 0;
                changed |= (hr & 1) != 0;
                if (!(hr & 2)) changed |= ColorPickerBody(target, pw);
            }
            else
            {
                std::vector<ImVec4>& saved = SavedColors();
                ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
                ImGui::TextUnformatted("Saved colors");
                ImGui::PopStyleColor();
                if (saved.empty()) Label("Press + to save the current color here.");
                int hit = SwatchGrid("##saved", saved, pw);
                if (hit >= 0) { *c = saved[hit]; changed = true; }
                if (!s_pageColors.empty())
                {
                    ImGui::Dummy(ImVec2(0, U(6)));
                    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
                    ImGui::TextUnformatted("On this page");
                    ImGui::PopStyleColor();
                    int ph = SwatchGrid("##libpage", s_pageColors, pw);
                    if (ph >= 0) { *c = s_pageColors[ph]; changed = true; }
                }
                if (!saved.empty())
                {
                    ImGui::Dummy(ImVec2(0, U(4)));
                    if (ImGui::Button("Clear saved colors", ImVec2(pw, 0))) saved.clear();
                }
            }
            if (save)
            {
                std::vector<ImVec4>& saved = SavedColors();
                ImU32 u = ImGui::ColorConvertFloat4ToU32(*target);
                saved.erase(std::remove_if(saved.begin(), saved.end(), [&](const ImVec4& q) { return ImGui::ColorConvertFloat4ToU32(q) == u; }), saved.end());
                saved.insert(saved.begin(), *target);
                if (saved.size() > 48) saved.resize(48);
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();
        ImGui::PopID();
        return changed;
    }

    bool ColorRow(const char* id, ImVec4* c, float width, const PickerHeader* header)
    {
        ImGui::PushID(id);
        bool changed = false;
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float opW = U(58);
        float mainW = width - opW - U(4);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + mainW, p.y + h), Field, ImGui::GetStyle().FrameRounding);
        changed |= ColorSwatch("##s", c, h, header);
        ImGui::SameLine(0, 0);
        char buf[16];
        std::string hex = ColorToHex(ImVec4(c->x, c->y, c->z, 1.0f));
        snprintf(buf, sizeof(buf), "%s", hex.c_str() + 1);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::SetNextItemWidth(mainW - h);
        if (ImGui::InputText("##hex", buf, sizeof(buf), ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_CharsUppercase))
        {
            ImVec4 nc;
            if (HexToColor(buf, &nc)) { c->x = nc.x; c->y = nc.y; c->z = nc.z; if (strlen(buf) == 8) c->w = nc.w; changed = true; }
        }
        ImGui::PopStyleColor(3);
        ImGui::SameLine(0, U(4));
        float pct = std::round(c->w * 100.0f);
        if (NumField("##op", "", &pct, opW, 1.0f, 0, 100, "%")) { c->w = pct / 100.0f; changed = true; }
        ImGui::PopID();
        return changed;
    }

    int Section(const char* title, int button, Icon extra, const char* extraTip)
    {
        int res = 0;
        ImGui::Dummy(ImVec2(0, U(6)));
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x - U(16), p.y), ImVec2(p.x + w + U(16), p.y), Divider);
        ImGui::Dummy(ImVec2(0, U(10)));
        float x0 = ImGui::GetCursorPosX();
        ImGui::PushFont(fonts::UI(fonts::UI_SemiBold), ImGui::GetStyle().FontSizeBase + 1.0f);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        float fh = ImGui::GetFrameHeight();
        ImGui::PushID(title);
        if ((int)extra >= 0)
        {
            ImGui::SameLine(x0 + w - fh * (button ? 2 : 1) - (button ? U(4) : 0));
            if (IconButton("##secextra", extra, false, 0, extraTip)) res = 2;
        }
        if (button)
        {
            ImGui::SameLine(x0 + w - fh);
            if (IconButton("##secbtn", button > 0 ? Icon::Plus : Icon::Minus)) res = button;
        }
        ImGui::PopID();
        ImGui::Dummy(ImVec2(0, U(2)));
        return res;
    }

    void SubLabel(const char* text, float x)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::PushFont(fonts::UI(fonts::UI_Medium), ImGui::GetStyle().FontSizeBase - 1.0f);
        if (x > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    void SubLabels(const char* a, const char* b, float secondX)
    {
        float x0 = ImGui::GetCursorPosX();
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::PushFont(fonts::UI(fonts::UI_Medium), ImGui::GetStyle().FontSizeBase - 1.0f);
        ImGui::TextUnformatted(a);
        ImGui::SameLine(x0 + secondX);
        ImGui::TextUnformatted(b);
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }

    void IconGroupBg(ImVec2 pos, float width)
    {
        ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + ImGui::GetFrameHeight()), Field, ImGui::GetStyle().FrameRounding);
    }

    bool Segmented(const char* id, int* value, const char* const* labels, int count, float width)
    {
        ImGui::PushID(id);
        bool changed = false;
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + h), Field, ImGui::GetStyle().FrameRounding);
        float segW = width / count;
        for (int i = 0; i < count; i++)
        {
            ImGui::PushID(i);
            ImVec2 a(p.x + segW * i, p.y);
            ImGui::SetCursorScreenPos(a);
            if (ImGui::InvisibleButton("##seg", ImVec2(segW, h))) { *value = i; changed = true; }
            bool hov = ImGui::IsItemHovered();
            if (*value == i) dl->AddRectFilled(ImVec2(a.x + U(2), a.y + U(2)), ImVec2(a.x + segW - U(2), a.y + h - U(2)), IM_COL32(255, 255, 255, 28), ImGui::GetStyle().FrameRounding - 1);
            ImVec2 ts = ImGui::CalcTextSize(labels[i]);
            dl->AddText(ImVec2(a.x + (segW - ts.x) * 0.5f, a.y + (h - ts.y) * 0.5f), *value == i || hov ? Text : TextDim, labels[i]);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h));
        ImGui::Dummy(ImVec2(width, 0));
        ImGui::PopID();
        return changed;
    }

    bool IconToggle(const char* id, Icon icon, bool on, const char* tip)
    {
        return IconButton(id, icon, on, 0.0f, tip);
    }

    struct StrCb { std::string* s; };
    static int ResizeCb(ImGuiInputTextCallbackData* d)
    {
        if (d->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            std::string* s = (std::string*)d->UserData;
            s->resize(d->BufTextLen);
            d->Buf = s->data();
        }
        return 0;
    }

    bool TextField(const char* id, std::string* s, float width, const char* hint, bool multiline, float height)
    {
        ImGui::SetNextItemWidth(width);
        if (multiline)
            return ImGui::InputTextMultiline(id, s->data(), s->capacity() + 1, ImVec2(width, height), ImGuiInputTextFlags_CallbackResize, ResizeCb, s);
        if (hint) return ImGui::InputTextWithHint(id, hint, s->data(), s->capacity() + 1, ImGuiInputTextFlags_CallbackResize, ResizeCb, s);
        return ImGui::InputText(id, s->data(), s->capacity() + 1, ImGuiInputTextFlags_CallbackResize, ResizeCb, s);
    }

    bool Combo(const char* id, int* idx, const char* const* items, int count, float width)
    {
        ImGui::SetNextItemWidth(width);
        return ImGui::Combo(id, idx, items, count);
    }

    bool Check(const char* label, bool* v) { return ImGui::Checkbox(label, v); }

    void Label(const char* text, bool dim)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, dim ? TextDim : Text);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        ImGui::PopStyleColor();
    }

    bool PrimaryButton(const char* label, ImVec2 size)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, Accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(35, 165, 255, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(10, 130, 220, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32_WHITE);
        ImGui::PushFont(fonts::UI(fonts::UI_Medium), 0.0f);
        bool r = ImGui::Button(label, size);
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
        return r;
    }
}
