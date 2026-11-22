#pragma once
#include "imgui.h"

enum class Icon
{
    Move, Frame, Rect, Ellipse, Line, Text, Hand, Image, Vector, Group,
    Play, Sparkle, Eye, EyeOff, Lock, Unlock, Plus, Minus, ChevronRight, ChevronDown,
    AlignLeft, AlignCenterH, AlignRight, AlignTop, AlignCenterV, AlignBottom,
    ArrowDown, ArrowRight, Wrap, Link, Unlink, Code, Export, Menu, Search, Settings, Close,
    Send, Stop, Attach, Copy, Grid, Radius, Undo, Redo, TextLeft, TextCenter, TextRight,
    Component, Check,
};

void DrawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 col);
// Fimgui logo: "FI" monogram on a rounded gradient square (same geometry as resources/logo.svg).
void DrawLogo(ImDrawList* dl, ImVec2 pos, float size, float alpha = 1.0f);
// Icon-only button in the Figma style (transparent until hovered). Returns true when clicked.
bool IconButton(const char* id, Icon icon, bool selected = false, float size = 0.0f, const char* tooltip = nullptr);
