// fimgui_fx_null.cpp - renderer-agnostic fallback for fimgui_fx.h (no blur, no blend modes, no video).
// Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
#include "fimgui_fx.h"
#include "fimgui_runtime.h"

namespace fim
{
    namespace fx
    {
        bool InitDX11(ID3D11Device*, ID3D11DeviceContext*) { return false; }
        void Shutdown() {}
        bool Available() { return false; }

        void DrawBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], const Backdrop& p, float alpha, bool ellipse)
        {
            Paint fill;
            fill.type = Paint_Solid;
            fill.c0 = (p.tint >> IM_COL32_A_SHIFT) ? p.tint : IM_COL32(255, 255, 255, 24);
            if (ellipse) FillEllipse(dl, a, b, fill, alpha);
            else FillRect(dl, a, b, r, fill, alpha);
            if (p.mode == Backdrop_Glass && !ellipse)
                StrokeRect(dl, a, b, r, IM_COL32(255, 255, 255, (int)(90 * p.highlight)), 1.0f, 0, alpha);
        }

        void BeginBlend(ImDrawList*, int) {}
        void EndBlend(ImDrawList*, int) {}
        void DrawTexture(ImDrawList* dl, ImTextureRef tex, ImVec2 a, ImVec2 b, const float r[4], ImVec2 uv0, ImVec2 uv1, ImU32 tint, int, bool, float alpha)
        {
            DrawImage(dl, tex, a, b, r, uv0, uv1, tint, alpha);
        }

        int  VideoOpenMemory(const void*, size_t) { return -1; }
        int  VideoOpenFile(const char*) { return -1; }
        void VideoClose(int) {}
        ImTextureID VideoFrame(int) { return ImTextureID_Invalid; }
        bool VideoProbe(const void*, size_t, int*, int*) { return false; }
    }
}
