#pragma once
#include "imgui.h"
#include <string>

struct Node;

// Screen presets for web pages, in CSS px (the browser viewport) and their usual orientation.
enum class DeviceKind { Desktop, Tablet, Phone, Foldable };

struct WebDevice
{
    const char* key;
    const char* group;      // menu group: "Desktop", "iPhone", "Android foldables"...
    const char* name;
    float w, h;
    DeviceKind kind;
    int fold;               // 0 none, 1 book-style fold (the hinge splits the width), 2 flip phone (the hinge splits the height)
    const char* other;      // the same device folded / unfolded
};

extern const WebDevice kWebDevices[];
extern const int kWebDeviceCount;
extern const char* const kWebDeviceGroups[];
extern const int kWebDeviceGroupCount;

const WebDevice* FindWebDevice(const std::string& key);
// The preset a page is designed for while its width still matches it (rotated: the other orientation).
// The height is free: a page longer than the screen scrolls.
const WebDevice* WebDeviceOf(const Node* frame, bool* rotated = nullptr);
inline ImVec2 WebScreenOf(const WebDevice* d, bool rotated) { return rotated ? ImVec2(d->h, d->w) : ImVec2(d->w, d->h); }
