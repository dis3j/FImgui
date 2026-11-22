#include "icons.h"
#include "theme.h"
#include "imgui_internal.h"
#include "../runtime/fimgui_runtime.h"
#include <cmath>

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float s, ImU32 col)
{
    float u = s / 16.0f;                // icons are designed on a 16x16 grid
    float t = std::fmax(1.0f, 1.25f * u);
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 8.0f) * u, c.y + (y - 8.0f) * u); };
    auto Ln = [&](float x0, float y0, float x1, float y1) { dl->AddLine(P(x0, y0), P(x1, y1), col, t); };
    auto Rc = [&](float x0, float y0, float x1, float y1, float r = 0) { dl->AddRect(P(x0, y0), P(x1, y1), col, r * u, 0, t); };
    auto RcF = [&](float x0, float y0, float x1, float y1, float r = 0) { dl->AddRectFilled(P(x0, y0), P(x1, y1), col, r * u); };
    switch (icon)
    {
    case Icon::Move:
    {
        ImVec2 pts[] = { P(4, 2), P(12.5f, 9.5f), P(8.6f, 9.8f), P(10.8f, 14.2f), P(9.2f, 14.9f), P(7.1f, 10.5f), P(4, 13) };
        dl->AddPolyline(pts, 7, col, ImDrawFlags_Closed, t);
        break;
    }
    case Icon::Frame: Ln(5, 2, 5, 14); Ln(11, 2, 11, 14); Ln(2, 5, 14, 5); Ln(2, 11, 14, 11); break;
    case Icon::Rect: Rc(2.5f, 3.5f, 13.5f, 12.5f, 0.5f); break;
    case Icon::Ellipse: dl->AddCircle(c, 5.8f * u, col, 0, t); break;
    case Icon::Line: Ln(3, 13, 13, 3); break;
    case Icon::Text: Ln(3, 3, 13, 3); Ln(8, 3, 8, 13); Ln(6, 13, 10, 13); Ln(3, 3, 3, 4.5f); Ln(13, 3, 13, 4.5f); break;
    case Icon::Hand:
        Rc(4, 7, 12, 14, 3);
        Ln(6, 7, 6, 3); Ln(8, 7, 8, 2); Ln(10, 7, 10, 3); Ln(12, 9, 12, 5);
        break;
    case Icon::Image:
        Rc(2.5f, 3, 13.5f, 13, 1);
        dl->AddCircleFilled(P(6, 6.5f), 1.3f * u, col);
        { ImVec2 pts[] = { P(3, 12), P(7, 8.5f), P(9.5f, 10.5f), P(11, 9), P(13, 11) }; dl->AddPolyline(pts, 5, col, 0, t); }
        break;
    case Icon::Vector:
        Ln(3, 13, 8, 3); Ln(8, 3, 13, 13);
        RcF(6.5f, 1.5f, 9.5f, 4.5f); RcF(1.5f, 11.5f, 4.5f, 14.5f); RcF(11.5f, 11.5f, 14.5f, 14.5f);
        break;
    case Icon::Group:
        for (int i = 0; i < 4; i++) { Ln(3 + i * 3, 3, 4.5f + i * 3, 3); Ln(3 + i * 3, 13, 4.5f + i * 3, 13); Ln(3, 3 + i * 3, 3, 4.5f + i * 3); Ln(13, 3 + i * 3, 13, 4.5f + i * 3); }
        break;
    case Icon::Play: dl->AddTriangleFilled(P(5, 3), P(13, 8), P(5, 13), col); break;
    case Icon::Sparkle:
    {
        auto star = [&](float x, float y, float r) {
            ImVec2 pts[8];
            for (int i = 0; i < 8; i++)
            {
                float a = i * 3.14159265f / 4.0f;
                float rr = (i % 2 == 0) ? r : r * 0.28f;
                pts[i] = P(x + std::cos(a - 1.5708f) * rr, y + std::sin(a - 1.5708f) * rr);
            }
            dl->AddConvexPolyFilled(pts, 8, col);
        };
        star(7, 9, 6.0f);
        star(12.5f, 3.5f, 2.6f);
        break;
    }
    case Icon::Eye:
    case Icon::EyeOff:
    {
        ImVec2 pts[24];
        for (int i = 0; i < 12; i++) { float a = 3.14159265f * i / 11; pts[i] = P(8 - std::cos(a) * 6.5f, 8 - std::sin(a) * 4.0f); }
        for (int i = 0; i < 12; i++) { float a = 3.14159265f * i / 11; pts[12 + i] = P(8 + std::cos(a) * 6.5f, 8 + std::sin(a) * 4.0f); }
        dl->AddPolyline(pts, 24, col, ImDrawFlags_Closed, t);
        dl->AddCircle(c, 2.0f * u, col, 0, t);
        if (icon == Icon::EyeOff) Ln(3, 13, 13, 3);
        break;
    }
    case Icon::Lock:
    case Icon::Unlock:
        RcF(3.5f, 7.5f, 12.5f, 13.5f, 1);
        if (icon == Icon::Lock) { dl->PathArcTo(P(8, 6.5f), 2.8f * u, 3.14159f, 6.28318f); dl->PathStroke(col, 0, t); Ln(5.2f, 6.5f, 5.2f, 7.5f); Ln(10.8f, 6.5f, 10.8f, 7.5f); }
        else { dl->PathArcTo(P(11, 5.5f), 2.8f * u, 3.14159f, 6.28318f); dl->PathStroke(col, 0, t); Ln(8.2f, 5.5f, 8.2f, 7.5f); }
        break;
    case Icon::Plus: Ln(8, 3, 8, 13); Ln(3, 8, 13, 8); break;
    case Icon::Minus: Ln(3, 8, 13, 8); break;
    case Icon::Check: { ImVec2 pts[] = { P(3.5f, 8.5f), P(6.5f, 11.5f), P(12.5f, 4.5f) }; dl->AddPolyline(pts, 3, col, 0, t * 1.2f); break; }
    case Icon::ChevronRight: { ImVec2 pts[] = { P(6.5f, 4.5f), P(10, 8), P(6.5f, 11.5f) }; dl->AddPolyline(pts, 3, col, 0, t); break; }
    case Icon::ChevronDown: { ImVec2 pts[] = { P(4.5f, 6.5f), P(8, 10), P(11.5f, 6.5f) }; dl->AddPolyline(pts, 3, col, 0, t); break; }
    case Icon::AlignLeft: Ln(2.5f, 2, 2.5f, 14); RcF(4.5f, 4, 13, 7); RcF(4.5f, 9, 9.5f, 12); break;
    case Icon::AlignCenterH: Ln(8, 2, 8, 14); RcF(3, 4, 13, 7); RcF(5, 9, 11, 12); break;
    case Icon::AlignRight: Ln(13.5f, 2, 13.5f, 14); RcF(3, 4, 11.5f, 7); RcF(6.5f, 9, 11.5f, 12); break;
    case Icon::AlignTop: Ln(2, 2.5f, 14, 2.5f); RcF(4, 4.5f, 7, 13); RcF(9, 4.5f, 12, 9.5f); break;
    case Icon::AlignCenterV: Ln(2, 8, 14, 8); RcF(4, 3, 7, 13); RcF(9, 5, 12, 11); break;
    case Icon::AlignBottom: Ln(2, 13.5f, 14, 13.5f); RcF(4, 3, 7, 11.5f); RcF(9, 6.5f, 12, 11.5f); break;
    case Icon::ArrowDown: Ln(8, 3, 8, 13); Ln(4.5f, 9.5f, 8, 13); Ln(11.5f, 9.5f, 8, 13); break;
    case Icon::ArrowRight: Ln(3, 8, 13, 8); Ln(9.5f, 4.5f, 13, 8); Ln(9.5f, 11.5f, 13, 8); break;
    case Icon::Wrap: Ln(3, 5, 12, 5); dl->PathArcTo(P(10.5f, 8), 3 * u, -1.5708f, 1.5708f); dl->PathStroke(col, 0, t); Ln(10.5f, 11, 6, 11); Ln(7.5f, 9.5f, 6, 11); Ln(7.5f, 12.5f, 6, 11); break;
    case Icon::Link:
    case Icon::Unlink:
        Rc(2, 5.5f, 8, 10.5f, 2.5f); Rc(8, 5.5f, 14, 10.5f, 2.5f);
        if (icon == Icon::Link) Ln(6, 8, 10, 8);
        break;
    case Icon::Radius: dl->PathArcTo(P(10, 10), 7 * u, 3.14159f, 4.712f); dl->PathStroke(col, 0, t); Ln(3, 10, 3, 14); Ln(10, 3, 14, 3); break;
    case Icon::Code: Ln(5.5f, 4, 2, 8); Ln(2, 8, 5.5f, 12); Ln(10.5f, 4, 14, 8); Ln(14, 8, 10.5f, 12); Ln(9, 3, 7, 13); break;
    case Icon::Export: Ln(8, 2, 8, 10); Ln(5, 5, 8, 2); Ln(11, 5, 8, 2); Ln(3, 9, 3, 14); Ln(3, 14, 13, 14); Ln(13, 14, 13, 9); break;
    case Icon::Menu: Ln(3, 4.5f, 13, 4.5f); Ln(3, 8, 13, 8); Ln(3, 11.5f, 13, 11.5f); break;
    case Icon::Search: dl->AddCircle(P(7, 7), 4.2f * u, col, 0, t); Ln(10, 10, 13.5f, 13.5f); break;
    case Icon::Settings:
        dl->AddCircle(c, 2.2f * u, col, 0, t);
        for (int i = 0; i < 8; i++) { float a = i * 3.14159265f / 4; Ln(8 + std::cos(a) * 4.2f, 8 + std::sin(a) * 4.2f, 8 + std::cos(a) * 6.3f, 8 + std::sin(a) * 6.3f); }
        dl->AddCircle(c, 4.4f * u, col, 0, t);
        break;
    case Icon::Close: Ln(4, 4, 12, 12); Ln(12, 4, 4, 12); break;
    case Icon::Send: { ImVec2 pts[] = { P(2.5f, 3), P(14, 8), P(2.5f, 13), P(5, 8) }; dl->AddConvexPolyFilled(pts, 4, col); break; }
    case Icon::Stop: RcF(4, 4, 12, 12, 1.5f); break;
    case Icon::Attach:
        dl->PathArcTo(P(8, 4.5f), 2.5f * u, 3.14159f, 6.28318f); dl->PathLineTo(P(10.5f, 11)); dl->PathArcTo(P(8, 11), 2.5f * u, 0, 3.14159f); dl->PathLineTo(P(5.5f, 6.5f));
        dl->PathStroke(col, 0, t);
        break;
    case Icon::Copy: Rc(5, 5, 13.5f, 13.5f, 1.5f); Ln(2.5f, 10.5f, 2.5f, 2.5f); Ln(2.5f, 2.5f, 10.5f, 2.5f); break;
    case Icon::Grid: for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) dl->AddCircleFilled(P(4 + i * 4.0f, 4 + j * 4.0f), 1.0f * u, col); break;
    case Icon::Undo: dl->PathArcTo(P(8.5f, 9), 4.5f * u, -2.6f, 1.6f); dl->PathStroke(col, 0, t); Ln(4.2f, 6.2f, 3.5f, 3); Ln(4.2f, 6.2f, 7.3f, 5.5f); break;
    case Icon::Redo: dl->PathArcTo(P(7.5f, 9), 4.5f * u, 1.54f, 5.74f); dl->PathStroke(col, 0, t); Ln(11.8f, 6.2f, 12.5f, 3); Ln(11.8f, 6.2f, 8.7f, 5.5f); break;
    case Icon::TextLeft: Ln(2.5f, 4, 13.5f, 4); Ln(2.5f, 8, 10, 8); Ln(2.5f, 12, 12, 12); break;
    case Icon::TextCenter: Ln(2.5f, 4, 13.5f, 4); Ln(4.5f, 8, 11.5f, 8); Ln(3.5f, 12, 12.5f, 12); break;
    case Icon::TextRight: Ln(2.5f, 4, 13.5f, 4); Ln(6, 8, 13.5f, 8); Ln(4, 12, 13.5f, 12); break;
    case Icon::Component:
    {
        auto d = [&](float x, float y) { ImVec2 pts[] = { P(x, y - 2.3f), P(x + 2.3f, y), P(x, y + 2.3f), P(x - 2.3f, y) }; dl->AddPolyline(pts, 4, col, ImDrawFlags_Closed, t); };
        d(8, 4); d(4, 8); d(12, 8); d(8, 12);
        break;
    }
    }
}

void DrawLogo(ImDrawList* dl, ImVec2 p, float size, float alpha)
{
    float k = size / 256.0f;
    float r[4] = { 58 * k, 58 * k, 58 * k, 58 * k };
    fim::Paint bg;
    bg.type = fim::Paint_Linear;
    bg.c0 = IM_COL32(43, 140, 255, 255);
    bg.c1 = IM_COL32(138, 92, 255, 255);
    bg.angle = 45.0f;
    fim::FillRect(dl, p, ImVec2(p.x + size, p.y + size), r, bg, alpha);
    ImU32 white = fim::Fade(IM_COL32_WHITE, alpha);
    auto P = [&](float x, float y) { return ImVec2(p.x + x * k, p.y + y * k); };
    // "F"
    const float bars[3][4] = { { 58, 60, 88, 196 }, { 58, 60, 146, 88 }, { 58, 114, 130, 140 } };
    for (auto& b : bars) dl->AddRectFilled(P(b[0], b[1]), P(b[2], b[3]), white, 5 * k);
    // "i" as a paint brush, bristles down: a paint drop for the dot, handle, ferrule, tapered tip.
    dl->AddCircleFilled(P(182, 72), 13 * k, white, 24);
    dl->AddRectFilled(P(175, 98), P(189, 140), white, 7 * k);
    dl->AddRectFilled(P(169, 142), P(195, 156), fim::Fade(IM_COL32_WHITE, alpha * 0.72f), 3 * k);
    dl->PathLineTo(P(169, 158));
    dl->PathLineTo(P(195, 158));
    dl->PathBezierCubicCurveTo(P(196, 176), P(190, 188), P(182, 198));
    dl->PathBezierCubicCurveTo(P(174, 188), P(168, 176), P(169, 158));
    dl->PathFillConvex(white);
}

bool IconButton(const char* id, Icon icon, bool selected, float size, const char* tooltip)
{
    float s = size > 0 ? size : ImGui::GetFrameHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(s, s));
    bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected) dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), theme::Accent, 5.0f * theme::dpi);
    else if (hov) dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), theme::Hover, 5.0f * theme::dpi);
    DrawIcon(dl, icon, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), std::fmin(16.0f * theme::dpi, s * 0.7f), selected ? IM_COL32_WHITE : theme::Text);
    if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}
