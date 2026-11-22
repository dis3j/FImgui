#pragma once
#include "imgui.h"
#include "icons.h"
#include <string>
#include <functional>
#include <vector>

// Figma-style inspector widgets.
namespace ui
{
    // Numeric field with a scrubbable label prefix ("X", "W", icon...). Returns true while the value changes.
    bool NumField(const char* id, const char* label, float* v, float width, float speed = 1.0f, float vmin = -1e9f, float vmax = 1e9f, const char* suffix = nullptr, Icon icon = (Icon)-1);
    // Optional top part of a color picker popup (fill type tabs). It may redirect the edited color through *target.
    // Returns bit 1 when something changed, bit 2 to hide the color picker body.
    using PickerHeader = std::function<int(ImVec4** target)>;
    // Color swatch + hex + opacity. Returns true on change.
    bool ColorRow(const char* id, ImVec4* c, float width, const PickerHeader* header = nullptr);
    bool ColorSwatch(const char* id, ImVec4* c, float size, const PickerHeader* header = nullptr);
    // Figma-style picker: saturation/brightness square, eyedropper, hue and alpha bars, Hex/RGB/HSL/HSB, page colors.
    bool ColorPickerBody(ImVec4* c, float width);
    // Colors used in the document, shown under "On this page" (set once per frame by the app).
    void SetPageColors(const std::vector<ImVec4>& colors);
    // Colors saved with "+" in the picker (Libraries tab); the app persists them in its settings.
    std::vector<ImVec4>& SavedColors();
    // Drawn over the saturation / brightness square of the next color picker body (once): a = top-left, b = bottom-right.
    using SvOverlay = std::function<void(ImDrawList* dl, ImVec2 a, ImVec2 b, float hue)>;
    void SetSvOverlay(SvOverlay fn);
    void Checker(ImDrawList* dl, ImVec2 a, ImVec2 b, float cell = 0.0f);   // transparency checkerboard
    // Section header with optional +/- button. Returns 1 when + clicked, -1 when - clicked.
    // extra: optional icon button left of +/- (returns 2 when clicked).
    int  Section(const char* title, int button = 0, Icon extra = (Icon)-1, const char* extraTip = nullptr);
    // Small dim caption above a field ("Position", "Opacity"...). x: offset inside the row.
    void SubLabel(const char* text, float x = 0.0f);
    // Two captions on one line (above two columns).
    void SubLabels(const char* a, const char* b, float secondX);
    // Rounded field-colored background behind a run of icon buttons.
    void IconGroupBg(ImVec2 pos, float width);
    bool Segmented(const char* id, int* value, const char* const* labels, int count, float width);
    bool IconToggle(const char* id, Icon icon, bool on, const char* tip = nullptr);
    bool TextField(const char* id, std::string* s, float width, const char* hint = nullptr, bool multiline = false, float height = 0);
    bool Combo(const char* id, int* idx, const char* const* items, int count, float width);
    bool Check(const char* label, bool* v);
    void Label(const char* text, bool dim = true);
    bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
    float Avail();
}
