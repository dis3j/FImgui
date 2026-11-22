// fimgui_runtime.h - drawing & interaction helpers shared by the Fimgui editor and generated menus.
// Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
// Requires Dear ImGui 1.92+ (dynamic font sizes).
#pragma once
#include "imgui.h"

namespace fim
{
    enum PaintType { Paint_None = 0, Paint_Solid = 1, Paint_Linear = 2, Paint_Radial = 3, Paint_Angular = 4, Paint_Diamond = 5 };

    struct GradientStop { float pos; ImU32 col; };   // pos 0..1, sorted

    struct Paint
    {
        int   type  = Paint_None;
        ImU32 c0    = 0;      // solid color / gradient start
        ImU32 c1    = 0;      // gradient end
        float angle = 90.0f;  // linear: direction in degrees (0 = left->right, 90 = top->bottom); angular: start angle
        const GradientStop* stops = nullptr;   // optional: more than two stops (c0 / c1 are then ignored)
        int   stopCount = 0;
    };
    ImU32 GradientColorAt(const Paint& p, float t);   // gradient color at position 0..1

    enum ShadowType { Shadow_None = 0, Shadow_Drop = 1, Shadow_Inner = 2 };

    struct Shadow
    {
        int    type   = Shadow_None;
        ImVec2 offset = ImVec2(0, 4);
        float  blur   = 8.0f;
        float  spread = 0.0f;
        ImU32  color  = IM_COL32(0, 0, 0, 64);
    };

    enum AlignH { Align_Left = 0, Align_Center = 1, Align_Right = 2 };
    enum AlignV { Align_Top = 0, Align_Middle = 1, Align_Bottom = 2 };

    // Multiplies the alpha channel of 'col' by 'alpha'.
    ImU32 Fade(ImU32 col, float alpha);
    ImU32 LerpColor(ImU32 a, ImU32 b, float t);

    // Rounded rectangle path with independent corner radii (tl, tr, br, bl).
    void  PathRoundRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4]);

    void  FillRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], const Paint& paint, float alpha = 1.0f);
    void  StrokeRect(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], ImU32 col, float thickness, int align = 0, float alpha = 1.0f); // align: 0 inside, 1 center, 2 outside
    void  FillEllipse(ImDrawList* dl, ImVec2 a, ImVec2 b, const Paint& paint, float alpha = 1.0f);
    void  StrokeEllipse(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float alpha = 1.0f);
    void  DrawShadow(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], const Shadow& s, float scale = 1.0f, float alpha = 1.0f, bool ellipse = false);
    void  DrawLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness, float alpha = 1.0f);
    void  DrawImage(ImDrawList* dl, ImTextureRef tex, ImVec2 a, ImVec2 b, const float r[4], ImVec2 uv0 = ImVec2(0, 0), ImVec2 uv1 = ImVec2(1, 1), ImU32 tint = IM_COL32_WHITE, float alpha = 1.0f);

    // Vector path (pen tool): anchors in box units (0..1 over a-b), handles as offsets from the anchor (0 = sharp corner).
    struct PathPoint { ImVec2 p, in, out; };
    // The outline with its curves flattened, in screen space.
    void  PathOutline(ImVec2 a, ImVec2 b, const PathPoint* pts, int count, bool closed, ImVector<ImVec2>& out);
    // Fill (closed paths) and stroke; thickness 0 = no stroke.
    void  DrawPath(ImDrawList* dl, ImVec2 a, ImVec2 b, const PathPoint* pts, int count, bool closed, const Paint& fill, ImU32 stroke, float thickness, float alpha = 1.0f);

    // Rotation: everything drawn between RotateBegin and RotateEnd turns by 'degrees' (clockwise) around 'center'.
    int   RotateBegin(ImDrawList* dl);
    void  RotateEnd(ImDrawList* dl, int begin, ImVec2 center, float degrees);

    // Auto layout for frames whose children appear and disappear with state ("Show when"): hidden items take no space.
    // Items are in/out: w / h (fill items change), x / y relative to the frame.
    struct LayoutItem { float w, h; bool visible; bool fillMain; bool fillCross; float x, y; };
    struct LayoutSpec
    {
        bool  horizontal = true;
        float gap = 8.0f;
        float pad[4] = { 0, 0, 0, 0 };    // left, top, right, bottom
        int   alignMain = 0;              // 0 start, 1 center, 2 end, 3 space between
        int   alignCross = 0;             // 0 start, 1 center, 2 end, 3 stretch
        bool  hugMain = false, hugCross = false;
    };
    // *w / *h: the frame size; hugging axes are updated.
    void  AutoLayout(LayoutItem* items, int count, const LayoutSpec& spec, float* w, float* h);

    // Text box: lines are wrapped to the box width when 'wrap' is set; lineHeight is in pixels (0 = font default).
    void  DrawText(ImDrawList* dl, ImFont* font, float size, ImVec2 a, ImVec2 b, const char* text, ImU32 col,
                   int alignH = Align_Left, int alignV = Align_Top, float lineHeight = 0.0f, bool wrap = true, float alpha = 1.0f);
    ImVec2 MeasureText(ImFont* font, float size, const char* text, float wrapWidth = -1.0f, float lineHeight = 0.0f);

    // Image UVs for 'cover' fitting of an image of size 'img' into box 'box'.
    void  CoverUV(ImVec2 img, ImVec2 box, ImVec2* uv0, ImVec2* uv1);
    // Inner rect for 'contain' fitting.
    void  ContainRect(ImVec2 img, ImVec2* a, ImVec2* b);

    // Small string builder used for texts bound to state variables ("FOV: {fov}").
    struct TextBuf
    {
        char buf[512];
        int  len = 0;
        TextBuf() { buf[0] = 0; }
        TextBuf& Add(const char* s);
        TextBuf& Num(float v, const char* fmt = nullptr);   // default: integral -> "%.0f", else "%.2f"
        TextBuf& Int(int v, const char* fmt = nullptr);
        // Combo labels: the selected option, or for a multi-combo "A, B" / allLabel / noneLabel.
        TextBuf& Option(int index, const char* const* items, int count);
        TextBuf& Options(unsigned int mask, const char* const* items, int count, const char* noneLabel = "None", const char* allLabel = "All", const char* sep = ", ");
        const char* c_str() const { return buf; }
    };

    // ---- Animation ----
    enum AnimPreset
    {
        Anim_None,
        // Entrance (play when the element appears)
        Anim_Fade, Anim_SlideUp, Anim_SlideDown, Anim_SlideLeft, Anim_SlideRight, Anim_Pop, Anim_Zoom, Anim_Bounce,
        // Loops
        Anim_Pulse, Anim_Float, Anim_Breathe, Anim_Heartbeat, Anim_Wiggle,
        // Interaction (hover / press / active state)
        Anim_Lift, Anim_Grow, Anim_Press, Anim_Nudge, Anim_Dim,
        // Timeline: keyframe tracks; the trigger decides when it plays (appear, loop, hover, press, active)
        Anim_Keyframes,
        Anim_Count
    };
    enum AnimTrigger { Trigger_Appear, Trigger_Hover, Trigger_Press, Trigger_Active, Trigger_Loop };
    enum Easing { Ease_Linear, Ease_In, Ease_Out, Ease_InOut, Ease_BackOut, Ease_ElasticOut, Ease_BounceOut, Ease_Spring, Ease_Bezier, Ease_Count };

    // Keyframe tracks (Anim_Keyframes). X / Y are offsets in px, Scale a factor, Opacity 0..1.
    enum AnimProp { Prop_X, Prop_Y, Prop_Scale, Prop_Opacity, Prop_Rotation, Prop_Count };   // rotation in degrees
    struct Keyframe { float time; float value; int easing; };   // easing shapes the segment that starts at this key
    struct KeyTrack { int prop; const Keyframe* keys; int count; };

    struct AnimSpec
    {
        int   preset = Anim_None;
        int   trigger = Trigger_Appear;
        int   easing = Ease_Out;
        float duration = 0.4f;      // seconds
        float delay = 0.0f;         // seconds
        float distance = 16.0f;     // px for slides / floats / nudges
        float scale = 0.9f;         // start scale for pop / zoom
        float intensity = 0.05f;    // amount for pulse / grow / press / dim
        float bezier[4] = { 0.25f, 0.1f, 0.25f, 1.0f };
        const KeyTrack* tracks = nullptr;   // Anim_Keyframes; duration is the timeline length
        int   trackCount = 0;
    };

    struct AnimValue
    {
        float dx = 0, dy = 0, scale = 1, alpha = 1, rot = 0;
        bool  IsIdentity() const { return dx == 0 && dy == 0 && scale == 1 && alpha == 1 && rot == 0; }
    };
    float     AnimPropDefault(int prop);                                // 1 for scale and opacity, else 0

    int       AnimCategory(int preset);                                 // 0 entrance, 1 loop, 2 interaction
    int       AnimCategoryOf(const AnimSpec& s);                        // same, keyframes resolved by their trigger
    float     KeyTrackSample(const KeyTrack& t, float time, const float bezier[4]);
    AnimValue AnimKeyframesAt(const AnimSpec& s, float time);           // keyframes at timeline time (no delay)
    float     Ease(int easing, const float bezier[4], float t);
    // time: seconds since the trigger (entrance / loop); progress: 0..1 state progress (interaction).
    AnimValue AnimSample(const AnimSpec& s, float time, float progress);
    // Stateful version: keeps its clock in ImGui storage (call inside a window every frame the element is drawn).
    AnimValue AnimUpdate(const char* id, const AnimSpec& s, bool hovered, bool held, bool active);

    // ---- Animated GIF (frames created with your texture factory) ----
    struct GifAnim
    {
        ImVector<ImTextureID> frames;
        ImVector<int> delays;       // ms per frame
        int totalMs = 0;
        int width = 0, height = 0;
    };
    ImTextureID GifFrame(const GifAnim& g, double time = -1.0);   // -1 = ImGui::GetTime()

    // ---- Interaction (use inside an ImGui window) ----
    struct ItemState { bool hovered = false; bool held = false; bool clicked = false; };
    ItemState Item(const char* id, ImVec2 a, ImVec2 b);
    // Smoothly animates towards 'target'. Persistent per id.
    float Animate(const char* id, float target, float speed = 14.0f);
    // Returns true when value changed.
    bool  SliderBehavior(const char* id, ImVec2 a, ImVec2 b, float* v, float vmin, float vmax, ItemState* out = nullptr);
    // Dropdowns. Draw the closed box yourself (it is the designed frame), then call these with its rect:
    // a click opens a popup list styled by ComboStyle. Returns true when the selection changed.
    // out->held is true while the list is open.
    struct ComboStyle
    {
        ImFont* font = nullptr;
        float fontSize = 13.0f;
        ImU32 text = IM_COL32(230, 230, 236, 255);
        ImU32 bg = IM_COL32(28, 28, 34, 250);
        ImU32 hover = IM_COL32(255, 255, 255, 18);
        ImU32 accent = IM_COL32(79, 125, 255, 255);
        ImU32 border = IM_COL32(255, 255, 255, 24);
        float rounding = 8.0f;
        float itemHeight = 32.0f;
        float padX = 12.0f;
        float gap = 4.0f;              // space between the box and the list
        int   maxVisible = 8;          // more items scroll
        const char* allLabel = "All";  // multi-combo: "select all" row (nullptr or "" hides it)
    };
    bool  Combo(const char* id, ImVec2 a, ImVec2 b, int* index, const char* const* items, int count, const ComboStyle& style, ItemState* out = nullptr);
    bool  MultiCombo(const char* id, ImVec2 a, ImVec2 b, unsigned int* mask, const char* const* items, int count, const ComboStyle& style, ItemState* out = nullptr);

    bool  InputBehavior(const char* id, ImVec2 a, ImVec2 b, char* buf, int bufSize, ImFont* font, float fontSize, ImU32 textCol, float padX, bool* active = nullptr);
}
