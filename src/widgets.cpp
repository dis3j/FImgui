#include "widgets.h"
#include "theme.h"
#include "fonts.h"
#include "document.h"
#include "imgui_internal.h"
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

    static void Checker(ImDrawList* dl, ImVec2 a, ImVec2 b)
    {
        float s = std::max(2.0f, (b.x - a.x) / 4);
        dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 255));
        for (float y = a.y; y < b.y; y += s)
            for (float x = a.x; x < b.x; x += s)
                if (((int)((x - a.x) / s) + (int)((y - a.y) / s)) % 2)
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + s, b.x), std::min(y + s, b.y)), IM_COL32(204, 204, 204, 255));
    }

    bool ColorSwatch(const char* id, ImVec4* c, float size)
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
        if (ImGui::BeginPopup("##picker"))
        {
            ImGui::SetNextItemWidth(U(220));
            changed = ImGui::ColorPicker4("##pick", (float*)c, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_NoSidePreview | ImGuiColorEditFlags_DisplayHex |
                                                                 ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_PickerHueBar);
            ImGui::EndPopup();
        }
        ImGui::PopID();
        return changed;
    }

    bool ColorRow(const char* id, ImVec4* c, float width)
    {
        ImGui::PushID(id);
        bool changed = false;
        float h = ImGui::GetFrameHeight();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float opW = U(58);
        float mainW = width - opW - U(4);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + mainW, p.y + h), Field, ImGui::GetStyle().FrameRounding);
        changed |= ColorSwatch("##s", c, h);
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
