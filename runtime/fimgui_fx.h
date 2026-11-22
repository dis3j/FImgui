// fimgui_fx.h - GPU effects for Fimgui menus: background blur, liquid glass, texture blend modes and video.
// Part of Fimgui - non-commercial use only, see LICENSE-FIMGUI.txt (https://github.com/dis3j/FImgui).
// fimgui_fx_dx11.cpp implements them for DirectX 11. On other renderers compile fimgui_fx_null.cpp instead:
// effects then fall back to plain translucent fills and videos are not played.
#pragma once
#include "imgui.h"

struct ID3D11Device;
struct ID3D11DeviceContext;

namespace fim
{
    namespace fx
    {
        bool InitDX11(ID3D11Device* device, ID3D11DeviceContext* context);   // call once after ImGui_ImplDX11_Init()
        void Shutdown();
        bool Available();

        enum BackdropMode { Backdrop_Blur = 0, Backdrop_Glass = 1 };

        struct Backdrop
        {
            int   mode = Backdrop_Blur;
            float blur = 16.0f;          // px (already scaled)
            float saturation = 1.0f;     // 1 = unchanged
            ImU32 tint = 0;              // color mixed over the backdrop (alpha = amount)
            // Glass
            float refraction = 0.6f;     // 0..1 bending of the background at the rim
            float thickness = 18.0f;     // px width of the curved rim (already scaled)
            float highlight = 0.7f;      // 0..1 specular edge strength
            float dispersion = 0.3f;     // 0..1 chromatic aberration at the rim
            float lightAngle = -45.0f;   // degrees, direction the light comes from
        };

        // Draws the blurred / refracted content that is already behind the rect.
        void DrawBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, const float radius[4], const Backdrop& p, float alpha = 1.0f, bool ellipse = false);

        enum Blend { Blend_Normal = 0, Blend_Add = 1, Blend_Multiply = 2, Blend_Screen = 3 };
        // Image with a blend mode; wrap = repeat UVs outside 0..1 (tiling).
        void DrawTexture(ImDrawList* dl, ImTextureRef tex, ImVec2 a, ImVec2 b, const float radius[4], ImVec2 uv0, ImVec2 uv1,
                         ImU32 tint, int blend, bool wrap, float alpha = 1.0f);

        // Video playback (MP4/MOV/WMV via Media Foundation). Loops forever. Handles are >= 0, -1 on failure.
        int  VideoOpenMemory(const void* data, size_t size);
        int  VideoOpenFile(const char* utf8Path);
        void VideoClose(int handle);
        ImTextureID VideoFrame(int handle);           // uploads the newest decoded frame, returns its texture
        bool VideoProbe(const void* data, size_t size, int* width, int* height);
    }
}
