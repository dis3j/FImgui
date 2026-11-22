#pragma once
#include "imgui.h"

// Dark editor theme modelled on modern design tools.
namespace theme
{
    extern float dpi;

    constexpr ImU32 Bg        = IM_COL32(44, 44, 44, 255);    // panels
    constexpr ImU32 BgDeep    = IM_COL32(30, 30, 30, 255);    // canvas default
    constexpr ImU32 Field     = IM_COL32(56, 56, 56, 255);    // inputs
    constexpr ImU32 Hover     = IM_COL32(255, 255, 255, 18);
    constexpr ImU32 Border    = IM_COL32(68, 68, 68, 255);
    constexpr ImU32 Divider   = IM_COL32(56, 56, 56, 255);
    constexpr ImU32 Text      = IM_COL32(255, 255, 255, 230);
    constexpr ImU32 TextDim   = IM_COL32(255, 255, 255, 140);
    constexpr ImU32 TextFaint = IM_COL32(255, 255, 255, 80);
    constexpr ImU32 Accent    = IM_COL32(13, 153, 255, 255);
    constexpr ImU32 AccentDim = IM_COL32(13, 153, 255, 60);
    constexpr ImU32 Guide     = IM_COL32(242, 72, 34, 255);
    constexpr ImU32 Purple    = IM_COL32(151, 71, 255, 255);
    constexpr ImU32 Success   = IM_COL32(20, 174, 92, 255);
    constexpr ImU32 Danger    = IM_COL32(242, 72, 34, 255);

    inline float U(float v) { return v * dpi; }
    void Apply(float dpiScale);
}
