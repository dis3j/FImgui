// fimgui_runtime.cpp - see fimgui_runtime.h
// Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
#include "fimgui_runtime.h"
#include "imgui_internal.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

namespace fim
{
    static const float PI = 3.14159265358979f;

    ImU32 Fade(ImU32 col, float alpha)
    {
        if (alpha >= 1.0f) return col;
        unsigned a = (col >> IM_COL32_A_SHIFT) & 0xFF;
        a = (unsigned)(a * ImClamp(alpha, 0.0f, 1.0f) + 0.5f);
        return (col & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT);
    }

    ImU32 LerpColor(ImU32 a, ImU32 b, float t)
    {
        ImVec4 ca = ImGui::ColorConvertU32ToFloat4(a), cb = ImGui::ColorConvertU32ToFloat4(b);
        return ImGui::ColorConvertFloat4ToU32(ImVec4(ImLerp(ca.x, cb.x, t), ImLerp(ca.y, cb.y, t), ImLerp(ca.z, cb.z, t), ImLerp(ca.w, cb.w, t)));
    }

    static void ClampRadii(ImVec2 a, ImVec2 b, const float in[4], float out[4])
    {
        float maxr = ImMin(b.x - a.x, b.y - a.y) * 0.5f;
        if (maxr < 0) maxr = 0;
        for (int i = 0; i < 4; i++)
            out[i] = ImClamp(in ? in[i] : 0.0f, 0.0f, maxr);
    }

    void PathRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const float rin[4])
    {
        float r[4];
        ClampRadii(a, b, rin, r);
        if (r[0] < 0.5f) dl->PathLineTo(a); else dl->PathArcToFast(ImVec2(a.x + r[0], a.y + r[0]), r[0], 6, 9);
        if (r[1] < 0.5f) dl->PathLineTo(ImVec2(b.x, a.y)); else dl->PathArcToFast(ImVec2(b.x - r[1], a.y + r[1]), r[1], 9, 12);
        if (r[2] < 0.5f) dl->PathLineTo(b); else dl->PathArcToFast(ImVec2(b.x - r[2], b.y - r[2]), r[2], 0, 3);
        if (r[3] < 0.5f) dl->PathLineTo(ImVec2(a.x, b.y)); else dl->PathArcToFast(ImVec2(a.x + r[3], b.y - r[3]), r[3], 3, 6);
    }

    // Recolors vertices [start,end) with a linear gradient, multiplying by the existing vertex alpha (keeps AA fringes).
    static void ShadeGradient(ImDrawList* dl, int start, int end, ImVec2 a, ImVec2 b, const Paint& p, float alpha)
    {
        float ang = p.angle * PI / 180.0f;
        ImVec2 dir(cosf(ang), sinf(ang));
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        float half = (fabsf(dir.x) * (b.x - a.x) + fabsf(dir.y) * (b.y - a.y)) * 0.5f;
        if (half < 1e-3f) half = 1e-3f;
        bool st = p.stops && p.stopCount >= 2;
        ImVec4 c0 = ImGui::ColorConvertU32ToFloat4(st ? p.stops[0].col : p.c0), c1 = ImGui::ColorConvertU32ToFloat4(st ? p.stops[p.stopCount - 1].col : p.c1);
        for (int i = start; i < end; i++)
        {
            ImDrawVert& v = dl->VtxBuffer[i];
            float t = ImClamp(((v.pos.x - c.x) * dir.x + (v.pos.y - c.y) * dir.y) / (2.0f * half) + 0.5f, 0.0f, 1.0f);
            float va = ((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            ImVec4 col(ImLerp(c0.x, c1.x, t), ImLerp(c0.y, c1.y, t), ImLerp(c0.z, c1.z, t), ImLerp(c0.w, c1.w, t) * va * alpha);
            v.col = ImGui::ColorConvertFloat4ToU32(col);
        }
    }

    // Outline of a rounded rect / ellipse grown by 'grow' px. The point count is fixed per shape, so rings line up.
    static const int kCornerSegs = 10, kEdgeSegs = 12, kEllipsePts = 96, kMaxOutline = 96;
    static int ShapeOutline(ImVec2 a, ImVec2 b, const float rin[4], bool ellipse, float grow, ImVec2* out)
    {
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        if (ellipse)
        {
            float rx = ImMax(0.0f, (b.x - a.x) * 0.5f + grow), ry = ImMax(0.0f, (b.y - a.y) * 0.5f + grow);
            for (int i = 0; i < kEllipsePts; i++)
            {
                float t = 2 * PI * i / kEllipsePts;
                out[i] = ImVec2(c.x + cosf(t) * rx, c.y + sinf(t) * ry);
            }
            return kEllipsePts;
        }
        float r[4];
        ClampRadii(a, b, rin, r);
        ImVec2 ga(a.x - grow, a.y - grow), gb(b.x + grow, b.y + grow);
        if (gb.x < ga.x) ga.x = gb.x = c.x;
        if (gb.y < ga.y) ga.y = gb.y = c.y;
        ImVec2 centers[4] = { ImVec2(a.x + r[0], a.y + r[0]), ImVec2(b.x - r[1], a.y + r[1]), ImVec2(b.x - r[2], b.y - r[2]), ImVec2(a.x + r[3], b.y - r[3]) };
        // corner arcs, then the straight edges are split too, so colors interpolate smoothly along them
        ImVec2 arc[4][kCornerSegs + 1];
        for (int k = 0; k < 4; k++)
        {
            float rr = ImMax(0.0f, r[k] + grow);
            float a0 = PI * (1.0f + 0.5f * k);   // the top-left corner starts at 180 degrees
            for (int i = 0; i <= kCornerSegs; i++)
            {
                float t = a0 + 0.5f * PI * i / kCornerSegs;
                ImVec2 p(centers[k].x + cosf(t) * rr, centers[k].y + sinf(t) * rr);
                arc[k][i] = ImVec2(ImClamp(p.x, ga.x, gb.x), ImClamp(p.y, ga.y, gb.y));
            }
        }
        int n = 0;
        for (int k = 0; k < 4; k++)
        {
            for (int i = 0; i <= kCornerSegs; i++) out[n++] = arc[k][i];
            ImVec2 e0 = arc[k][kCornerSegs], e1 = arc[(k + 1) % 4][0];
            for (int i = 1; i < kEdgeSegs; i++) out[n++] = ImLerp(e0, e1, (float)i / kEdgeSegs);
        }
        return n;
    }
    static_assert(4 * (kCornerSegs + kEdgeSegs) <= kMaxOutline, "outline buffer");

    // Signed distance from p to a rounded rect (corner radii tl, tr, br, bl) or an ellipse; negative inside.
    static float ShapeDistance(ImVec2 a, ImVec2 b, const float rin[4], bool ellipse, ImVec2 p)
    {
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), h(ImMax(0.0f, (b.x - a.x) * 0.5f), ImMax(0.0f, (b.y - a.y) * 0.5f));
        float px = p.x - c.x, py = p.y - c.y;
        if (ellipse)
        {
            // approximation from the gradient of the implicit function
            float rx = ImMax(1e-3f, h.x), ry = ImMax(1e-3f, h.y);
            float k0 = sqrtf((px / rx) * (px / rx) + (py / ry) * (py / ry));
            float k1 = sqrtf((px / (rx * rx)) * (px / (rx * rx)) + (py / (ry * ry)) * (py / (ry * ry)));
            return k1 > 1e-6f ? k0 * (k0 - 1.0f) / k1 : -ImMin(rx, ry);
        }
        float r[4];
        ClampRadii(a, b, rin, r);
        float rr = px < 0 ? (py < 0 ? r[0] : r[3]) : (py < 0 ? r[1] : r[2]);
        float qx = fabsf(px) - h.x + rr, qy = fabsf(py) - h.y + rr;
        float ox = ImMax(qx, 0.0f), oy = ImMax(qy, 0.0f);
        return sqrtf(ox * ox + oy * oy) + ImMin(ImMax(qx, qy), 0.0f) - rr;
    }

    // Position 0..1 of point q in a gradient over the box a-b.
    static float GradientT(const Paint& p, ImVec2 a, ImVec2 b, ImVec2 q)
    {
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        if (p.type == Paint_Linear)
        {
            float ang = p.angle * PI / 180.0f;
            ImVec2 dir(cosf(ang), sinf(ang));
            float half = ImMax(1e-3f, (fabsf(dir.x) * (b.x - a.x) + fabsf(dir.y) * (b.y - a.y)) * 0.5f);
            return ImSaturate(((q.x - c.x) * dir.x + (q.y - c.y) * dir.y) / (2.0f * half) + 0.5f);
        }
        float hx = ImMax(1e-3f, (b.x - a.x) * 0.5f), hy = ImMax(1e-3f, (b.y - a.y) * 0.5f);
        float nx = (q.x - c.x) / hx, ny = (q.y - c.y) / hy;
        if (p.type == Paint_Radial) return ImSaturate(sqrtf(nx * nx + ny * ny));
        if (p.type == Paint_Diamond) return ImSaturate(fabsf(nx) + fabsf(ny));
        float t = (atan2f(ny, nx) * 180.0f / PI - p.angle) / 360.0f;
        return t - floorf(t);
    }

    ImU32 GradientColorAt(const Paint& p, float t)
    {
        ImU32 a = p.c0, b = p.c1;
        if (p.stops && p.stopCount > 0)
        {
            const GradientStop* s = p.stops;
            int n = p.stopCount;
            if (t <= s[0].pos) return s[0].col;
            if (t >= s[n - 1].pos) return s[n - 1].col;
            for (int i = 0; i + 1 < n; i++)
                if (t <= s[i + 1].pos)
                {
                    float span = s[i + 1].pos - s[i].pos;
                    a = s[i].col; b = s[i + 1].col;
                    t = span > 1e-5f ? (t - s[i].pos) / span : 1.0f;
                    break;
                }
        }
        return LerpColor(a, b, t);
    }

    static ImU32 GradientColor(const Paint& p, float t, float alpha) { return Fade(GradientColorAt(p, t), alpha); }

    // A linear gradient with exactly two end stops is affine in position, so the plain convex fill can be recolored.
    static bool SimpleLinear(const Paint& p)
    {
        return p.type == Paint_Linear && (!p.stops || p.stopCount < 2 || (p.stopCount == 2 && p.stops[0].pos <= 0.0f && p.stops[1].pos >= 1.0f));
    }

    // Writes one quad with its own four vertices.
    static void Quad(ImDrawList* dl, const ImVec2 v[4], const ImU32 col[4])
    {
        ImVec2 uv = dl->_Data->TexUvWhitePixel;
        ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
        for (int j = 0; j < 4; j++) dl->PrimWriteVtx(v[j], uv, col[j]);
        dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + 1)); dl->PrimWriteIdx((ImDrawIdx)(base + 2));
        dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + 2)); dl->PrimWriteIdx((ImDrawIdx)(base + 3));
    }

    // Radial / angular / diamond fill: concentric rings of the shape carry the colors, a thin AA stroke smooths the edge.
    static void FillShapeGradient(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], bool ellipse, const Paint& p, float alpha)
    {
        ImVec2 ring[kMaxOutline];
        int n = ShapeOutline(a, b, r, ellipse, -0.5f, ring);
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const int rings = 24;
        for (int k = 0; k < rings; k++)
        {
            float s0 = ImMax(1e-3f, (float)k / rings), s1 = (float)(k + 1) / rings;
            dl->PrimReserve(n * 6, n * 4);
            for (int i = 0; i < n; i++)
            {
                ImVec2 q0 = ring[i], q1 = ring[(i + 1) % n];
                ImVec2 v[4] = { ImVec2(c.x + (q0.x - c.x) * s0, c.y + (q0.y - c.y) * s0), ImVec2(c.x + (q1.x - c.x) * s0, c.y + (q1.y - c.y) * s0),
                                ImVec2(c.x + (q1.x - c.x) * s1, c.y + (q1.y - c.y) * s1), ImVec2(c.x + (q0.x - c.x) * s1, c.y + (q0.y - c.y) * s1) };
                float t[4];
                for (int j = 0; j < 4; j++) t[j] = GradientT(p, a, b, v[j]);
                // angular: a quad across the seam keeps the colors of its own side instead of sweeping the whole range
                if (p.type == Paint_Angular)
                    for (int j = 1; j < 4; j++) if (fabsf(t[j] - t[0]) > 0.5f) t[j] = t[0] > 0.5f ? 1.0f : 0.0f;
                ImU32 col[4];
                for (int j = 0; j < 4; j++) col[j] = GradientColor(p, t[j], alpha);
                Quad(dl, v, col);
            }
        }
        int start = dl->VtxBuffer.Size;
        for (int i = 0; i < n; i++) dl->PathLineTo(ring[i]);
        dl->PathStroke(IM_COL32_WHITE, ImDrawFlags_Closed, 1.0f);
        for (int i = start; i < dl->VtxBuffer.Size; i++)
        {
            ImDrawVert& v = dl->VtxBuffer[i];
            float va = ((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
            v.col = GradientColor(p, GradientT(p, a, b, v.pos), alpha * va);
        }
    }

    void FillRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], const Paint& p, float alpha)
    {
        if (p.type == Paint_None || b.x <= a.x || b.y <= a.y) return;
        if (p.type >= Paint_Radial || (p.type == Paint_Linear && !SimpleLinear(p))) { FillShapeGradient(dl, a, b, r, false, p, alpha); return; }
        int start = dl->VtxBuffer.Size;
        PathRoundRect(dl, a, b, r);
        if (p.type == Paint_Solid) { dl->PathFillConvex(Fade(p.c0, alpha)); return; }
        dl->PathFillConvex(IM_COL32_WHITE);
        ShadeGradient(dl, start, dl->VtxBuffer.Size, a, b, p, alpha);
    }

    void StrokeRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], ImU32 col, float t, int align, float alpha)
    {
        if (t <= 0.0f || ((col >> IM_COL32_A_SHIFT) & 0xFF) == 0) return;
        float inset = align == 0 ? t * 0.5f : align == 2 ? -t * 0.5f : 0.0f;
        float rr[4];
        for (int i = 0; i < 4; i++) rr[i] = r ? ImMax(0.0f, r[i] - inset) : 0.0f;
        PathRoundRect(dl, ImVec2(a.x + inset, a.y + inset), ImVec2(b.x - inset, b.y - inset), rr);
        dl->PathStroke(Fade(col, alpha), ImDrawFlags_Closed, t);
    }

    void FillEllipse(ImDrawList* dl, ImVec2 a, ImVec2 b, const Paint& p, float alpha)
    {
        if (p.type == Paint_None) return;
        if (p.type >= Paint_Radial || (p.type == Paint_Linear && !SimpleLinear(p))) { FillShapeGradient(dl, a, b, nullptr, true, p, alpha); return; }
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), rad((b.x - a.x) * 0.5f, (b.y - a.y) * 0.5f);
        int start = dl->VtxBuffer.Size;
        dl->PathEllipticalArcTo(c, rad, 0.0f, 0.0f, 2.0f * PI);
        if (p.type == Paint_Solid) { dl->PathFillConvex(Fade(p.c0, alpha)); return; }
        dl->PathFillConvex(IM_COL32_WHITE);
        ShadeGradient(dl, start, dl->VtxBuffer.Size, a, b, p, alpha);
    }

    void StrokeEllipse(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float t, float alpha)
    {
        if (t <= 0.0f) return;
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), rad((b.x - a.x - t) * 0.5f, (b.y - a.y - t) * 0.5f);
        dl->PathEllipticalArcTo(c, rad, 0.0f, 0.0f, 2.0f * PI);
        dl->PathStroke(Fade(col, alpha), ImDrawFlags_Closed, t);
    }

    void DrawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], const Shadow& s, float scale, float alpha, bool ellipse)
    {
        if (s.type == Shadow_None) return;
        float blur = s.blur * scale, spread = s.spread * scale;
        ImVec2 off(s.offset.x * scale, s.offset.y * scale);
        ImVec4 col = ImGui::ColorConvertU32ToFloat4(s.color);
        col.w *= alpha;
        if (col.w <= 0.0f) return;
        // Gaussian blur of the shape (sigma = blur / 2, as in Figma and CSS): across the edge the alpha follows
        // erfc of the distance. Rings of the shape carry that profile as vertex colors.
        float sigma = ImMax(0.25f, blur * 0.5f), ext = ImMax(0.75f, sigma * 3.0f);
        auto cover = [&](float d) { return 0.5f * erfcf(d / (sigma * 1.41421356f)); };   // d: distance outside the edge
        auto rgba = [&](float a) { return ImGui::ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, a)); };
        ImVec2 ra[kMaxOutline], rb[kMaxOutline];
        ImVec2 uv = dl->_Data->TexUvWhitePixel;
        auto strip = [&](int n, ImU32 c0, ImU32 c1) {
            dl->PrimReserve(n * 6, n * 4);
            for (int i = 0; i < n; i++)
            {
                int j = (i + 1) % n;
                ImVec2 v[4] = { ra[i], ra[j], rb[j], rb[i] };
                ImU32 cc[4] = { c0, c0, c1, c1 };
                Quad(dl, v, cc);
            }
        };
        auto fan = [&](const ImVec2* pts, const ImU32* cols, int n) {
            dl->PrimReserve((n - 2) * 3, n);
            ImDrawIdx base = (ImDrawIdx)dl->_VtxCurrentIdx;
            for (int i = 0; i < n; i++) dl->PrimWriteVtx(pts[i], uv, cols[i]);
            for (int i = 2; i < n; i++) { dl->PrimWriteIdx(base); dl->PrimWriteIdx((ImDrawIdx)(base + i - 1)); dl->PrimWriteIdx((ImDrawIdx)(base + i)); }
        };
        if (s.type == Shadow_Drop)
        {
            // rings of the offset shape grown by spread, from 3 sigma inside to 3 sigma outside, then a solid core
            ImVec2 sa(a.x + off.x, a.y + off.y), sb(b.x + off.x, b.y + off.y);
            const int rings = 14;
            int n = 0;
            for (int k = 0; k < rings; k++)
            {
                float d0 = -ext + 2 * ext * k / rings, d1 = -ext + 2 * ext * (k + 1) / rings;
                n = ShapeOutline(sa, sb, r, ellipse, spread + d0, ra);
                ShapeOutline(sa, sb, r, ellipse, spread + d1, rb);
                strip(n, rgba(col.w * cover(d0)), rgba(col.w * cover(d1)));
            }
            n = ShapeOutline(sa, sb, r, ellipse, spread - ext, ra);
            ImU32 cols[kMaxOutline];
            for (int i = 0; i < n; i++) cols[i] = rgba(col.w * cover(-ext));
            fan(ra, cols, n);
            return;
        }
        // Inner shadow: drawn only inside the shape, in inset rings; every vertex gets its alpha from the
        // signed distance to the offset shape shrunk by spread (rounded corners included, nothing leaks out).
        ImVec2 ia(a.x + off.x + spread, a.y + off.y + spread), ib(b.x + off.x - spread, b.y + off.y - spread);
        float ir[4];
        for (int i = 0; i < 4; i++) ir[i] = ImMax(0.0f, (r ? r[i] : 0.0f) - spread);
        float maxInset = ImMin(b.x - a.x, b.y - a.y) * 0.5f;
        float depth = ImMin(maxInset, ext + fabsf(spread) + ImMax(fabsf(off.x), fabsf(off.y)));
        int rings = (int)ImClamp(depth / ImMax(0.5f, sigma * 0.5f), 6.0f, 40.0f);
        auto vertexCol = [&](ImVec2 p) { return rgba(col.w * (1.0f - cover(ShapeDistance(ia, ib, ir, ellipse, p)))); };
        int n = 0;
        for (int k = 0; k < rings; k++)
        {
            n = ShapeOutline(a, b, r, ellipse, -depth * k / rings, ra);
            ShapeOutline(a, b, r, ellipse, -depth * (k + 1) / rings, rb);
            dl->PrimReserve(n * 6, n * 4);
            for (int i = 0; i < n; i++)
            {
                int j = (i + 1) % n;
                ImVec2 v[4] = { ra[i], ra[j], rb[j], rb[i] };
                ImU32 cc[4] = { vertexCol(v[0]), vertexCol(v[1]), vertexCol(v[2]), vertexCol(v[3]) };
                Quad(dl, v, cc);
            }
        }
        n = ShapeOutline(a, b, r, ellipse, -depth, ra);
        ImU32 cols[kMaxOutline];
        for (int i = 0; i < n; i++) cols[i] = vertexCol(ra[i]);
        fan(ra, cols, n);
    }

    void DrawLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float t, float alpha)
    {
        dl->AddLine(a, b, Fade(col, alpha), t);
    }

    void DrawImage(ImDrawList* dl, ImTextureRef tex, ImVec2 a, ImVec2 b, const float r[4], ImVec2 uv0, ImVec2 uv1, ImU32 tint, float alpha)
    {
        if (b.x <= a.x || b.y <= a.y) return;
        bool rounded = r && (r[0] > 0.5f || r[1] > 0.5f || r[2] > 0.5f || r[3] > 0.5f);
        if (!rounded) { dl->AddImage(tex, a, b, uv0, uv1, Fade(tint, alpha)); return; }
        dl->PushTexture(tex);
        int start = dl->VtxBuffer.Size;
        PathRoundRect(dl, a, b, r);
        dl->PathFillConvex(Fade(tint, alpha));
        ImGui::ShadeVertsLinearUV(dl, start, dl->VtxBuffer.Size, a, b, uv0, uv1, true);
        dl->PopTexture();
    }

    void PathOutline(ImVec2 a, ImVec2 b, const PathPoint* pts, int count, bool closed, ImVector<ImVec2>& out)
    {
        out.resize(0);
        if (count <= 0) return;
        ImVec2 sz(b.x - a.x, b.y - a.y);
        auto P = [&](ImVec2 q) { return ImVec2(a.x + q.x * sz.x, a.y + q.y * sz.y); };
        auto push = [&](ImVec2 q) { if (out.Size == 0 || ImLengthSqr(ImVec2(q.x - out.back().x, q.y - out.back().y)) > 0.01f) out.push_back(q); };
        int segs = closed ? count : count - 1;
        push(P(pts[0].p));
        for (int i = 0; i < segs; i++)
        {
            const PathPoint& s = pts[i];
            const PathPoint& e = pts[(i + 1) % count];
            ImVec2 p0 = P(s.p), p3 = P(e.p);
            bool curve = s.out.x != 0 || s.out.y != 0 || e.in.x != 0 || e.in.y != 0;
            if (!curve) { push(p3); continue; }
            ImVec2 p1 = P(ImVec2(s.p.x + s.out.x, s.p.y + s.out.y)), p2 = P(ImVec2(e.p.x + e.in.x, e.p.y + e.in.y));
            float len = sqrtf(ImLengthSqr(ImVec2(p1.x - p0.x, p1.y - p0.y))) + sqrtf(ImLengthSqr(ImVec2(p2.x - p1.x, p2.y - p1.y))) + sqrtf(ImLengthSqr(ImVec2(p3.x - p2.x, p3.y - p2.y)));
            int n = (int)ImClamp(len / 4.0f, 6.0f, 64.0f);
            for (int k = 1; k <= n; k++)
            {
                float t = (float)k / n, u = 1 - t;
                push(ImVec2(u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
                            u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y));
            }
        }
        if (closed && out.Size > 1 && ImLengthSqr(ImVec2(out[0].x - out.back().x, out[0].y - out.back().y)) < 0.01f) out.pop_back();
    }

    void DrawPath(ImDrawList* dl, ImVec2 a, ImVec2 b, const PathPoint* pts, int count, bool closed, const Paint& p, ImU32 stroke, float thickness, float alpha)
    {
        ImVector<ImVec2> line;
        PathOutline(a, b, pts, count, closed, line);
        if (line.Size < 2) return;
        if (closed && line.Size >= 3 && p.type != Paint_None)
        {
            int start = dl->VtxBuffer.Size;
            dl->AddConcavePolyFilled(line.Data, line.Size, p.type == Paint_Solid ? Fade(p.c0, alpha) : IM_COL32_WHITE);
            if (p.type != Paint_Solid)
                for (int i = start; i < dl->VtxBuffer.Size; i++)
                {
                    ImDrawVert& v = dl->VtxBuffer[i];
                    float va = ((v.col >> IM_COL32_A_SHIFT) & 0xFF) / 255.0f;
                    v.col = GradientColor(p, GradientT(p, a, b, v.pos), alpha * va);
                }
        }
        if (thickness > 0.0f && ((stroke >> IM_COL32_A_SHIFT) & 0xFF))
            dl->AddPolyline(line.Data, line.Size, Fade(stroke, alpha), closed ? ImDrawFlags_Closed : 0, thickness);
    }

    int RotateBegin(ImDrawList* dl) { return dl->VtxBuffer.Size; }

    void RotateEnd(ImDrawList* dl, int begin, ImVec2 c, float degrees)
    {
        if (degrees == 0.0f) return;
        float s = sinf(degrees * PI / 180.0f), k = cosf(degrees * PI / 180.0f);
        for (int i = begin; i < dl->VtxBuffer.Size; i++)
        {
            ImVec2& p = dl->VtxBuffer[i].pos;
            float x = p.x - c.x, y = p.y - c.y;
            p = ImVec2(c.x + x * k - y * s, c.y + x * s + y * k);
        }
    }

    void AutoLayout(LayoutItem* items, int count, const LayoutSpec& sp, float* w, float* h)
    {
        bool hz = sp.horizontal;
        auto mainSize = [&](LayoutItem& c) -> float& { return hz ? c.w : c.h; };
        auto crossSize = [&](LayoutItem& c) -> float& { return hz ? c.h : c.w; };
        float& fMain = hz ? *w : *h;
        float& fCross = hz ? *h : *w;
        float padMain0 = hz ? sp.pad[0] : sp.pad[1], padMain1 = hz ? sp.pad[2] : sp.pad[3];
        float padCross0 = hz ? sp.pad[1] : sp.pad[0], padCross1 = hz ? sp.pad[3] : sp.pad[2];

        float fixed = 0, maxCross = 0;
        int fills = 0, shown = 0;
        for (int i = 0; i < count; i++)
        {
            LayoutItem& c = items[i];
            if (!c.visible) continue;
            shown++;
            if (c.fillMain && !sp.hugMain) fills++;
            else fixed += mainSize(c);
            if (!c.fillCross && sp.alignCross != 3) maxCross = ImMax(maxCross, crossSize(c));
        }
        float gaps = shown > 1 ? sp.gap * (shown - 1) : 0.0f;
        if (sp.hugMain) fMain = padMain0 + fixed + gaps + padMain1;
        if (sp.hugCross) fCross = padCross0 + maxCross + padCross1;

        float avail = fMain - padMain0 - padMain1;
        float innerCross = fCross - padCross0 - padCross1;
        float freeSpace = avail - fixed - gaps;
        float gap = sp.gap, pos = padMain0;
        if (fills > 0)
        {
            float each = ImMax(0.0f, freeSpace / fills);
            for (int i = 0; i < count; i++) if (items[i].visible && items[i].fillMain) mainSize(items[i]) = each;
        }
        else if (sp.alignMain == 1) pos += freeSpace * 0.5f;
        else if (sp.alignMain == 2) pos += freeSpace;
        else if (sp.alignMain == 3 && shown > 1) gap = (avail - fixed) / (shown - 1);

        for (int i = 0; i < count; i++)
        {
            LayoutItem& c = items[i];
            if (c.fillCross || sp.alignCross == 3) crossSize(c) = innerCross;
            float cp = padCross0;
            if (sp.alignCross == 1) cp += (innerCross - crossSize(c)) * 0.5f;
            else if (sp.alignCross == 2) cp += innerCross - crossSize(c);
            float mp = IM_ROUND(pos);
            (hz ? c.x : c.y) = mp;
            (hz ? c.y : c.x) = IM_ROUND(cp);
            if (c.visible) pos += mainSize(c) + gap;
        }
    }

    static float LineH(ImFont* font, float size, float lineHeight)
    {
        (void)font;
        return lineHeight > 0.0f ? lineHeight : size * 1.2f;
    }

    // Splits text into lines (explicit '\n' and optional word wrap). Calls fn(begin, end) per line.
    template <typename F>
    static void ForEachLine(ImFont* font, float size, const char* text, float wrapWidth, F fn)
    {
        const char* s = text;
        const char* end = text + strlen(text);
        while (s <= end)
        {
            const char* nl = (const char*)memchr(s, '\n', end - s);
            const char* le = nl ? nl : end;
            if (wrapWidth > 0.0f && le > s)
            {
                const char* p = s;
                while (p < le)
                {
                    const char* w = font->CalcWordWrapPosition(size, p, le, wrapWidth);
                    if (w <= p) w = p + 1;
                    const char* e = w;
                    while (e > p && (e[-1] == ' ' || e[-1] == '\t')) e--;
                    fn(p, e);
                    p = w;
                    while (p < le && (*p == ' ' || *p == '\t')) p++;
                }
            }
            else
                fn(s, le);
            if (!nl) break;
            s = nl + 1;
        }
    }

    ImVec2 MeasureText(ImFont* font, float size, const char* text, float wrapWidth, float lineHeight)
    {
        if (!font) font = ImGui::GetFont();
        float lh = LineH(font, size, lineHeight);
        float w = 0.0f;
        int n = 0;
        ForEachLine(font, size, text, wrapWidth, [&](const char* b, const char* e) {
            w = ImMax(w, font->CalcTextSizeA(size, FLT_MAX, 0.0f, b, e).x);
            n++;
        });
        return ImVec2(w, n * lh);
    }

    void DrawText(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, const char* text, ImU32 col,
                  int alignH, int alignV, float lineHeight, bool wrap, float alpha)
    {
        if (!font) font = ImGui::GetFont();
        if (!text || !*text || size < 1.0f) return;
        float lh = LineH(font, size, lineHeight);
        float wrapW = wrap ? (b.x - a.x) + 0.5f : -1.0f;
        ImVec2 total = MeasureText(font, size, text, wrapW, lineHeight);
        float y = a.y;
        if (alignV == Align_Middle) y = (a.y + b.y - total.y) * 0.5f;
        else if (alignV == Align_Bottom) y = b.y - total.y;
        float pad = (lh - size) * 0.5f;
        ImU32 c = Fade(col, alpha);
        ForEachLine(font, size, text, wrapW, [&](const char* lb, const char* le) {
            float w = font->CalcTextSizeA(size, FLT_MAX, 0.0f, lb, le).x;
            float x = a.x;
            if (alignH == Align_Center) x = (a.x + b.x - w) * 0.5f;
            else if (alignH == Align_Right) x = b.x - w;
            dl->AddText(font, size, ImVec2(IM_ROUND(x), IM_ROUND(y + pad)), c, lb, le);
            y += lh;
        });
    }

    void CoverUV(ImVec2 img, ImVec2 box, ImVec2* uv0, ImVec2* uv1)
    {
        *uv0 = ImVec2(0, 0); *uv1 = ImVec2(1, 1);
        if (img.x <= 0 || img.y <= 0 || box.x <= 0 || box.y <= 0) return;
        float ia = img.x / img.y, ba = box.x / box.y;
        if (ia > ba) { float k = ba / ia; uv0->x = (1 - k) * 0.5f; uv1->x = 1 - uv0->x; }
        else         { float k = ia / ba; uv0->y = (1 - k) * 0.5f; uv1->y = 1 - uv0->y; }
    }

    void ContainRect(ImVec2 img, ImVec2* a, ImVec2* b)
    {
        ImVec2 box(b->x - a->x, b->y - a->y);
        if (img.x <= 0 || img.y <= 0 || box.x <= 0 || box.y <= 0) return;
        float k = ImMin(box.x / img.x, box.y / img.y);
        ImVec2 sz(img.x * k, img.y * k);
        ImVec2 o(a->x + (box.x - sz.x) * 0.5f, a->y + (box.y - sz.y) * 0.5f);
        *a = o; *b = ImVec2(o.x + sz.x, o.y + sz.y);
    }

    // ------------------------------------------------------------ animation

    int AnimCategory(int p)
    {
        if (p >= Anim_Pulse && p <= Anim_Wiggle) return 1;
        if (p >= Anim_Lift && p <= Anim_Dim) return 2;
        return 0;
    }

    int AnimCategoryOf(const AnimSpec& s)
    {
        if (s.preset != Anim_Keyframes) return AnimCategory(s.preset);
        return s.trigger == Trigger_Loop ? 1 : s.trigger == Trigger_Appear ? 0 : 2;
    }

    static float BezierCoord(float t, float p1, float p2)
    {
        float u = 1 - t;
        return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t;
    }

    static float BezierEase(const float b[4], float x)
    {
        // Solve x(t) = x by bisection, then evaluate y(t).
        float lo = 0, hi = 1, t = x;
        for (int i = 0; i < 24; i++)
        {
            t = (lo + hi) * 0.5f;
            if (BezierCoord(t, b[0], b[2]) < x) lo = t; else hi = t;
        }
        return BezierCoord(t, b[1], b[3]);
    }

    float Ease(int e, const float bz[4], float t)
    {
        t = ImSaturate(t);
        switch (e)
        {
        case Ease_Linear: return t;
        case Ease_In: return t * t * t;
        case Ease_Out: { float u = 1 - t; return 1 - u * u * u; }
        case Ease_InOut: return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
        case Ease_BackOut: { const float c1 = 1.70158f, c3 = c1 + 1; float u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }
        case Ease_ElasticOut:
            if (t <= 0 || t >= 1) return t;
            return powf(2, -10 * t) * sinf((t * 10 - 0.75f) * (2 * PI / 3)) + 1;
        case Ease_BounceOut:
        {
            const float n1 = 7.5625f, d1 = 2.75f;
            if (t < 1 / d1) return n1 * t * t;
            if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
            if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
            t -= 2.625f / d1;
            return n1 * t * t + 0.984375f;
        }
        case Ease_Spring: return t >= 1 ? 1 : 1 - expf(-6 * t) * cosf(12 * t);
        case Ease_Bezier: return bz ? BezierEase(bz, t) : t;
        }
        return t;
    }

    float AnimPropDefault(int prop) { return prop == Prop_Scale || prop == Prop_Opacity ? 1.0f : 0.0f; }

    float KeyTrackSample(const KeyTrack& t, float time, const float bezier[4])
    {
        if (t.count <= 0) return AnimPropDefault(t.prop);
        if (time <= t.keys[0].time) return t.keys[0].value;
        for (int i = 0; i + 1 < t.count; i++)
        {
            const Keyframe& a = t.keys[i];
            const Keyframe& b = t.keys[i + 1];
            if (time > b.time) continue;
            float span = b.time - a.time;
            float p = span > 1e-5f ? (time - a.time) / span : 1.0f;
            return a.value + (b.value - a.value) * Ease(a.easing, bezier, p);
        }
        return t.keys[t.count - 1].value;
    }

    AnimValue AnimKeyframesAt(const AnimSpec& s, float time)
    {
        AnimValue v;
        for (int i = 0; i < s.trackCount; i++)
        {
            float x = KeyTrackSample(s.tracks[i], time, s.bezier);
            switch (s.tracks[i].prop)
            {
            case Prop_X: v.dx = x; break;
            case Prop_Y: v.dy = x; break;
            case Prop_Scale: v.scale = x; break;
            case Prop_Opacity: v.alpha = ImSaturate(x); break;
            case Prop_Rotation: v.rot = x; break;
            }
        }
        return v;
    }

    AnimValue AnimSample(const AnimSpec& s, float time, float progress)
    {
        AnimValue v;
        if (s.preset == Anim_None) return v;
        if (s.preset == Anim_Keyframes)
        {
            float len = ImMax(0.01f, s.duration);
            int kc = AnimCategoryOf(s);
            float t = kc == 2 ? ImSaturate(progress) * len
                    : kc == 1 ? fmodf(ImMax(time - s.delay, 0.0f), len)
                    : ImClamp(time - s.delay, 0.0f, len);
            return AnimKeyframesAt(s, t);
        }
        int cat = AnimCategory(s.preset);
        if (cat == 0)
        {
            float raw = s.duration > 0 ? ImSaturate((time - s.delay) / s.duration) : 1.0f;
            float p = Ease(s.easing, s.bezier, raw), q = 1 - p;
            switch (s.preset)
            {
            case Anim_Fade: v.alpha = ImSaturate(p); break;
            case Anim_SlideUp: v.dy = s.distance * q; v.alpha = ImSaturate(raw * 1.6f); break;
            case Anim_SlideDown: v.dy = -s.distance * q; v.alpha = ImSaturate(raw * 1.6f); break;
            case Anim_SlideLeft: v.dx = s.distance * q; v.alpha = ImSaturate(raw * 1.6f); break;
            case Anim_SlideRight: v.dx = -s.distance * q; v.alpha = ImSaturate(raw * 1.6f); break;
            case Anim_Pop:
            case Anim_Zoom: v.scale = s.scale + (1 - s.scale) * p; v.alpha = ImSaturate(raw * 2.0f); break;
            case Anim_Bounce: v.dy = -s.distance * q; v.alpha = ImSaturate(raw * 3.0f); break;
            }
        }
        else if (cat == 1)
        {
            float cycle = ImMax(0.01f, s.duration + s.delay);
            float lt = fmodf(ImMax(time, 0.0f), cycle);
            float ph = lt < s.duration ? lt / ImMax(0.01f, s.duration) : 0.0f;
            float w = 0.5f - 0.5f * cosf(ph * 2 * PI);
            switch (s.preset)
            {
            case Anim_Pulse: v.scale = 1 + s.intensity * w; break;
            case Anim_Float: v.dy = -s.distance * w; break;
            case Anim_Breathe: v.alpha = 1 - s.intensity * w; break;
            case Anim_Heartbeat:
            {
                float b = expf(-powf((ph - 0.15f) / 0.06f, 2)) + 0.7f * expf(-powf((ph - 0.38f) / 0.07f, 2));
                v.scale = 1 + s.intensity * b;
                break;
            }
            case Anim_Wiggle: v.dx = ph < 0.5f ? s.distance * sinf(ph * 2 * PI * 4) * (1 - ph * 2) : 0.0f; break;
            }
        }
        else
        {
            float p = Ease(s.easing, s.bezier, ImSaturate(progress));
            switch (s.preset)
            {
            case Anim_Lift: v.dy = -s.distance * p; v.scale = 1 + s.intensity * p; break;
            case Anim_Grow: v.scale = 1 + s.intensity * p; break;
            case Anim_Press: v.scale = 1 - s.intensity * p; break;
            case Anim_Nudge: v.dx = s.distance * p; break;
            case Anim_Dim: v.alpha = 1 - s.intensity * p; break;
            }
        }
        return v;
    }

    AnimValue AnimUpdate(const char* id, const AnimSpec& s, bool hovered, bool held, bool active)
    {
        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID key = ImGui::GetID(id);
        int frame = ImGui::GetFrameCount();
        float now = (float)ImGui::GetTime();
        int* last = st->GetIntRef(key ^ 0x0A11u, -100);
        float* start = st->GetFloatRef(key ^ 0x0A12u, now);
        float* prog = st->GetFloatRef(key ^ 0x0A13u, 0.0f);
        if (*last < frame - 1) *start = now;   // not drawn last frame -> (re)appeared
        *last = frame;
        if (AnimCategoryOf(s) == 2)
        {
            bool on = s.trigger == Trigger_Press ? held : s.trigger == Trigger_Active ? active : hovered;
            float step = ImGui::GetIO().DeltaTime / ImMax(0.01f, s.duration);
            *prog = on ? ImMin(1.0f, *prog + step) : ImMax(0.0f, *prog - step);
            return AnimSample(s, 0, *prog);
        }
        return AnimSample(s, now - *start, 0);
    }

    ImTextureID GifFrame(const GifAnim& g, double time)
    {
        if (g.frames.Size == 0) return ImTextureID_Invalid;
        if (g.totalMs <= 0 || g.frames.Size == 1) return g.frames[0];
        if (time < 0) time = ImGui::GetTime();
        int t = (int)(fmod(time * 1000.0, (double)g.totalMs));
        for (int i = 0; i < g.frames.Size; i++)
        {
            t -= g.delays[i];
            if (t < 0) return g.frames[i];
        }
        return g.frames[g.frames.Size - 1];
    }

    TextBuf& TextBuf::Add(const char* s)
    {
        int n = (int)strlen(s);
        if (len + n >= (int)sizeof(buf)) n = (int)sizeof(buf) - 1 - len;
        if (n > 0) { memcpy(buf + len, s, n); len += n; buf[len] = 0; }
        return *this;
    }

    TextBuf& TextBuf::Num(float v, const char* fmt)
    {
        char t[64];
        if (!fmt) fmt = v == floorf(v) ? "%.0f" : "%.2f";
        snprintf(t, sizeof(t), fmt, (double)v);
        return Add(t);
    }

    TextBuf& TextBuf::Int(int v, const char* fmt)
    {
        char t[64];
        snprintf(t, sizeof(t), fmt ? fmt : "%d", v);
        return Add(t);
    }

    TextBuf& TextBuf::Option(int index, const char* const* items, int count)
    {
        if (index >= 0 && index < count) Add(items[index]);
        return *this;
    }

    TextBuf& TextBuf::Options(unsigned int mask, const char* const* items, int count, const char* noneLabel, const char* allLabel, const char* sep)
    {
        int n = 0;
        for (int i = 0; i < count && i < 32; i++) if (mask & (1u << i)) n++;
        if (n == 0) return Add(noneLabel ? noneLabel : "");
        if (n == count && count > 1 && allLabel && *allLabel) return Add(allLabel);
        bool first = true;
        for (int i = 0; i < count && i < 32; i++)
            if (mask & (1u << i))
            {
                if (!first) Add(sep);
                Add(items[i]);
                first = false;
            }
        return *this;
    }

    static void CheckMark(ImDrawList* dl, ImVec2 c, float s, ImU32 col)
    {
        ImVec2 pts[3] = { ImVec2(c.x - s * 0.45f, c.y), ImVec2(c.x - s * 0.1f, c.y + s * 0.35f), ImVec2(c.x + s * 0.5f, c.y - s * 0.35f) };
        dl->AddPolyline(pts, 3, col, 0, ImMax(1.0f, s * 0.16f));
    }

    static bool ComboImpl(const char* id, ImVec2 a, ImVec2 b, int* index, unsigned int* mask, const char* const* items, int count, const ComboStyle& st, ItemState* out)
    {
        ImGui::PushID(id);
        ImGuiStorage* storage = ImGui::GetStateStorage();
        ImGuiID popupId = ImGui::GetID("##list");
        bool wasOpen = ImGui::IsPopupOpen(popupId, 0);
        int* openAtPress = storage->GetIntRef(popupId ^ 0xC10Fu, 0);
        ItemState box = Item("##box", a, b);
        // Clicking the box while the list is open closes it (ImGui closes popups on mouse down): don't reopen on release.
        if (ImGui::IsItemActivated()) *openAtPress = wasOpen ? 1 : 0;
        if (box.clicked && !*openAtPress) ImGui::OpenPopup(popupId);

        bool changed = false;
        float pad = 4.0f;
        bool multi = mask != nullptr;
        bool allRow = multi && st.allLabel && *st.allLabel && count > 1;
        int rows = count + (allRow ? 1 : 0);
        int vis = ImMin(rows, ImMax(1, st.maxVisible));
        float listH = vis * st.itemHeight + pad * 2 + (allRow ? 5.0f : 0.0f);
        ImVec2 pos(a.x, b.y + st.gap);
        ImGuiViewport* vp = ImGui::GetWindowViewport();
        if (pos.y + listH > vp->WorkPos.y + vp->WorkSize.y && a.y - st.gap - listH > vp->WorkPos.y) pos.y = a.y - st.gap - listH;   // flip up
        ImGui::SetNextWindowPos(pos);
        ImGui::SetNextWindowSize(ImVec2(ImMax(b.x - a.x, st.itemHeight * 3), listH));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad, pad));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, st.rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, st.bg);
        ImGui::PushStyleColor(ImGuiCol_Border, st.border);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0, 0, 0, 0));
        bool open = ImGui::BeginPopupEx(popupId, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        if (open)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* font = st.font ? st.font : ImGui::GetFont();
            if (ImGui::IsWindowAppearing()) ImGui::GetStateStorage()->SetFloat(ImGui::GetID("##fade") ^ 0x5A17u, 0.0f);
            float fade = Animate("##fade", 1.0f, 18.0f);
            ImU32 textCol = Fade(st.text, fade);
            float box = st.fontSize * 1.05f;
            auto row = [&](int i, const char* label, bool selected, bool isAll) -> bool {
                ImVec2 p = ImGui::GetCursorScreenPos();
                float w = ImGui::GetContentRegionAvail().x;
                ImGui::PushID(i);
                bool clicked = ImGui::InvisibleButton("##o", ImVec2(w, st.itemHeight));
                bool hov = ImGui::IsItemHovered();
                ImGui::PopID();
                if (hov) dl->AddRectFilled(p, ImVec2(p.x + w, p.y + st.itemHeight), Fade(st.hover, fade), st.rounding * 0.6f);
                float x = p.x + st.padX, cy = p.y + st.itemHeight * 0.5f;
                if (multi)
                {
                    ImVec2 ba(x, cy - box * 0.5f), bb(x + box, cy + box * 0.5f);
                    if (selected)
                    {
                        dl->AddRectFilled(ba, bb, Fade(st.accent, fade), box * 0.25f);
                        CheckMark(dl, ImVec2(x + box * 0.5f, cy), box * 0.7f, Fade(IM_COL32_WHITE, fade));
                    }
                    else dl->AddRect(ba, bb, Fade(st.text, fade * 0.45f), box * 0.25f, 0, 1.0f);
                    x += box + st.padX * 0.7f;
                }
                ImU32 col = (!multi && selected) ? Fade(st.accent, fade) : textCol;
                ImVec2 ts = font->CalcTextSizeA(st.fontSize, FLT_MAX, 0.0f, label);
                dl->PushClipRect(p, ImVec2(p.x + w - (multi ? 0 : st.padX + box), p.y + st.itemHeight), true);
                dl->AddText(font, st.fontSize, ImVec2(IM_ROUND(x), IM_ROUND(cy - ts.y * 0.5f)), col, label);
                dl->PopClipRect();
                if (!multi && selected) CheckMark(dl, ImVec2(p.x + w - st.padX - box * 0.5f, cy), box * 0.8f, Fade(st.accent, fade));
                (void)isAll;
                return clicked;
            };
            if (allRow)
            {
                unsigned full = count >= 32 ? 0xFFFFFFFFu : ((1u << count) - 1u);
                bool all = (*mask & full) == full;
                if (row(-1, st.allLabel, all, true)) { *mask = all ? 0u : full; changed = true; }
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddLine(ImVec2(p.x + st.padX * 0.5f, p.y + 2), ImVec2(p.x + ImGui::GetContentRegionAvail().x - st.padX * 0.5f, p.y + 2), Fade(st.border, fade));
                ImGui::Dummy(ImVec2(0, 5));
            }
            for (int i = 0; i < count; i++)
            {
                bool selected = multi ? (i < 32 && (*mask & (1u << i)) != 0) : *index == i;
                if (row(i, items[i], selected, false))
                {
                    if (multi) { if (i < 32) *mask ^= (1u << i); }
                    else { *index = i; ImGui::CloseCurrentPopup(); }
                    changed = true;
                }
            }
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(5);
        if (out)
        {
            out->hovered = box.hovered;
            out->held = open;
            out->clicked = changed;
        }
        ImGui::PopID();
        return changed;
    }

    bool Combo(const char* id, ImVec2 a, ImVec2 b, int* index, const char* const* items, int count, const ComboStyle& style, ItemState* out)
    {
        return ComboImpl(id, a, b, index, nullptr, items, count, style, out);
    }

    bool MultiCombo(const char* id, ImVec2 a, ImVec2 b, unsigned int* mask, const char* const* items, int count, const ComboStyle& style, ItemState* out)
    {
        return ComboImpl(id, a, b, nullptr, mask, items, count, style, out);
    }

    ItemState Item(const char* id, ImVec2 a, ImVec2 b)
    {
        ItemState st;
        ImVec2 size(ImMax(1.0f, b.x - a.x), ImMax(1.0f, b.y - a.y));
        ImVec2 backup = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(a);
        st.clicked = ImGui::InvisibleButton(id, size);
        st.hovered = ImGui::IsItemHovered();
        st.held = ImGui::IsItemActive();
        if (st.hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetCursorScreenPos(backup);
        return st;
    }

    float Animate(const char* id, float target, float speed)
    {
        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID key = ImGui::GetID(id) ^ 0x5A17u;
        float* v = st->GetFloatRef(key, target);
        float dt = ImGui::GetIO().DeltaTime;
        *v = ImLerp(*v, target, ImClamp(dt * speed, 0.0f, 1.0f));
        if (fabsf(*v - target) < 0.001f) *v = target;
        return *v;
    }

    bool SliderBehavior(const char* id, ImVec2 a, ImVec2 b, float* v, float vmin, float vmax, ItemState* out)
    {
        ItemState st = Item(id, a, b);
        if (out) *out = st;
        if (st.held && b.x > a.x && vmax != vmin)
        {
            float t = ImClamp((ImGui::GetIO().MousePos.x - a.x) / (b.x - a.x), 0.0f, 1.0f);
            float nv = vmin + (vmax - vmin) * t;
            if (nv != *v) { *v = nv; return true; }
        }
        return false;
    }

    bool InputBehavior(const char* id, ImVec2 a, ImVec2 b, char* buf, int bufSize, ImFont* font, float fontSize, ImU32 textCol, float padX, bool* active)
    {
        ImVec2 backup = ImGui::GetCursorScreenPos();
        float h = b.y - a.y;
        ImGui::PushFont(font, fontSize);
        float pady = ImMax(0.0f, (h - ImGui::GetFontSize()) * 0.5f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(padX, pady));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, textCol);
        ImGui::SetCursorScreenPos(a);
        ImGui::SetNextItemWidth(b.x - a.x);
        bool changed = ImGui::InputText(id, buf, (size_t)bufSize);
        if (active) *active = ImGui::IsItemActive();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        ImGui::SetCursorScreenPos(backup);
        return changed;
    }
}
