#pragma once
#include "imgui.h"
#include "icons.h"
#include <string>

// Figma-style inspector widgets.
namespace ui
{
    // Numeric field with a scrubbable label prefix ("X", "W", icon...). Returns true while the value changes.
    bool NumField(const char* id, const char* label, float* v, float width, float speed = 1.0f, float vmin = -1e9f, float vmax = 1e9f, const char* suffix = nullptr, Icon icon = (Icon)-1);
    // Color swatch + hex + opacity. Returns true on change.
    bool ColorRow(const char* id, ImVec4* c, float width);
    bool ColorSwatch(const char* id, ImVec4* c, float size);
    // Section header with optional +/- button. Returns 1 when + clicked, -1 when - clicked.
    int  Section(const char* title, int button = 0);   // button: 0 none, 1 plus, -1 minus
    bool Segmented(const char* id, int* value, const char* const* labels, int count, float width);
    bool IconToggle(const char* id, Icon icon, bool on, const char* tip = nullptr);
    bool TextField(const char* id, std::string* s, float width, const char* hint = nullptr, bool multiline = false, float height = 0);
    bool Combo(const char* id, int* idx, const char* const* items, int count, float width);
    bool Check(const char* label, bool* v);
    void Label(const char* text, bool dim = true);
    bool PrimaryButton(const char* label, ImVec2 size = ImVec2(0, 0));
    float Avail();
}
