#include "document.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>

// Implemented in fonts.cpp - measures a text node with its font.
ImVec2 MeasureNodeText(const Node* n, float wrapWidth);

static const char* kTypeNames[] = { "frame", "group", "rect", "ellipse", "line", "text", "image", "vector" };
static const char* kRoleNames[] = { "none", "button", "checkbox", "slider", "tab", "input", "combo", "multicombo" };

const char* NodeTypeName(NodeType t) { return kTypeNames[(int)t]; }
bool NodeTypeFromName(const std::string& s, NodeType* out)
{
    for (int i = 0; i < 8; i++)
        if (s == kTypeNames[i]) { *out = (NodeType)i; return true; }
    if (s == "rectangle") { *out = NodeType::Rect; return true; }
    if (s == "svg" || s == "icon") { *out = NodeType::Vector; return true; }
    return false;
}
const char* RoleName(Role r) { return kRoleNames[(int)r]; }
bool RoleFromName(const std::string& s, Role* out)
{
    for (int i = 0; i < 8; i++)
        if (s == kRoleNames[i]) { *out = (Role)i; return true; }
    if (s == "dropdown" || s == "select") { *out = Role::Combo; return true; }
    if (s == "multiselect" || s == "multi-combo" || s == "multi_combo") { *out = Role::MultiCombo; return true; }
    if (s == "toggle" || s == "switch") { *out = Role::Checkbox; return true; }
    return false;
}

static const char* kPresetKeys[] = { "none", "fade", "slide-up", "slide-down", "slide-left", "slide-right", "pop", "zoom", "bounce",
                                     "pulse", "float", "breathe", "heartbeat", "wiggle", "lift", "grow", "press", "nudge", "dim", "keyframes" };
static const char* kPresetLabels[] = { "None", "Fade", "Slide up", "Slide down", "Slide left", "Slide right", "Pop", "Zoom", "Bounce",
                                       "Pulse", "Float", "Breathe", "Heartbeat", "Wiggle", "Lift", "Grow", "Press", "Nudge", "Dim", "Keyframes" };
static const char* kPropKeys[] = { "x", "y", "scale", "opacity" };
static const char* kPropLabels[] = { "Position X", "Position Y", "Scale", "Opacity" };
static const char* kEasingKeys[] = { "linear", "ease-in", "ease-out", "ease-in-out", "back-out", "elastic-out", "bounce-out", "spring", "bezier" };
static const char* kEasingLabels[] = { "Linear", "Ease in", "Ease out", "Ease in-out", "Back out", "Elastic", "Bounce", "Spring", "Custom bezier" };
static const char* kTriggerKeys[] = { "appear", "hover", "press", "active", "loop" };
static const int kPresetCount = (int)(sizeof(kPresetKeys) / sizeof(kPresetKeys[0]));

const char* AnimPresetKey(int p) { return p >= 0 && p < kPresetCount ? kPresetKeys[p] : "none"; }
const char* AnimPresetLabel(int p) { return p >= 0 && p < kPresetCount ? kPresetLabels[p] : "None"; }
const char* EasingKey(int e) { return e >= 0 && e < 9 ? kEasingKeys[e] : "ease-out"; }
const char* EasingLabel(int e) { return e >= 0 && e < 9 ? kEasingLabels[e] : "Ease out"; }
const char* TriggerKey(int t) { return t >= 0 && t < 5 ? kTriggerKeys[t] : "appear"; }
const char* AnimPropKey(int p) { return p >= 0 && p < fim::Prop_Count ? kPropKeys[p] : "x"; }
const char* AnimPropLabel(int p) { return p >= 0 && p < fim::Prop_Count ? kPropLabels[p] : "Position X"; }

KeyTrackFx* AnimFx::Track(int prop, bool create)
{
    for (auto& t : tracks) if (t.prop == prop) return &t;
    if (!create) return nullptr;
    KeyTrackFx t;
    t.prop = prop;
    tracks.push_back(t);
    std::sort(tracks.begin(), tracks.end(), [](const KeyTrackFx& a, const KeyTrackFx& b) { return a.prop < b.prop; });
    for (auto& q : tracks) if (q.prop == prop) return &q;
    return nullptr;
}

const KeyTrackFx* AnimFx::Track(int prop) const
{
    for (auto& t : tracks) if (t.prop == prop) return &t;
    return nullptr;
}

AnimFx AnimDefaults(int p)
{
    AnimFx a;
    a.preset = p;
    switch (p)
    {
    case 1: a.duration = 0.4f; a.easing = 2; break;                                        // fade
    case 2: case 3: case 4: case 5: a.duration = 0.45f; a.easing = 2; a.distance = 16; break;
    case 6: a.duration = 0.45f; a.easing = 4; a.scale = 0.85f; break;                     // pop
    case 7: a.duration = 0.5f; a.easing = 2; a.scale = 1.12f; break;                      // zoom
    case 8: a.duration = 0.8f; a.easing = 6; a.distance = 40; break;                      // bounce
    case 9: a.trigger = 4; a.duration = 1.4f; a.intensity = 0.05f; break;                 // pulse
    case 10: a.trigger = 4; a.duration = 2.4f; a.distance = 6; break;                     // float
    case 11: a.trigger = 4; a.duration = 2.2f; a.intensity = 0.35f; break;                // breathe
    case 12: a.trigger = 4; a.duration = 1.2f; a.intensity = 0.08f; break;                // heartbeat
    case 13: a.trigger = 4; a.duration = 1.6f; a.distance = 4; break;                     // wiggle
    case 14: a.trigger = 1; a.duration = 0.18f; a.easing = 2; a.distance = 3; a.intensity = 0.02f; break;   // lift
    case 15: a.trigger = 1; a.duration = 0.2f; a.easing = 2; a.intensity = 0.04f; break;                     // grow
    case 16: a.trigger = 2; a.duration = 0.12f; a.easing = 2; a.intensity = 0.05f; break;                    // press
    case 17: a.trigger = 1; a.duration = 0.2f; a.easing = 2; a.distance = 4; break;                          // nudge
    case 18: a.trigger = 1; a.duration = 0.2f; a.easing = 2; a.intensity = 0.35f; break;                     // dim
    case 19:                                                                                                  // keyframes: a starter rise-in
    {
        a.trigger = 0; a.duration = 1.0f; a.easing = 2;
        KeyTrackFx y; y.prop = fim::Prop_Y; y.keys = { { 0.0f, 24.0f, fim::Ease_Out }, { 0.6f, 0.0f, fim::Ease_Out } };
        KeyTrackFx o; o.prop = fim::Prop_Opacity; o.keys = { { 0.0f, 0.0f, fim::Ease_Out }, { 0.4f, 1.0f, fim::Ease_Out } };
        a.tracks = { y, o };
        break;
    }
    }
    return a;
}

const char* AssetKindName(int k) { return k == 1 ? "svg" : k == 2 ? "gif" : k == 3 ? "video" : "raster"; }
int AssetKindFromName(const std::string& s) { return s == "svg" ? 1 : s == "gif" ? 2 : s == "video" ? 3 : 0; }

ImVec2 Node::AbsPos() const
{
    ImVec2 p(x, y);
    for (Node* q = parent; q; q = q->parent) { p.x += q->x; p.y += q->y; }
    return p;
}

// ---------------------------------------------------------------- colors

static int HexDigit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool HexToColor(const std::string& in, ImVec4* out)
{
    std::string s = in;
    s.erase(std::remove_if(s.begin(), s.end(), [](char c) { return c == ' ' || c == '#'; }), s.end());
    if (s.size() == 3 || s.size() == 4)
    {
        std::string e;
        for (char c : s) { e += c; e += c; }
        s = e;
    }
    if (s.size() != 6 && s.size() != 8) return false;
    int v[4] = { 0, 0, 0, 255 };
    for (size_t i = 0; i < s.size() / 2; i++)
    {
        int hi = HexDigit(s[i * 2]), lo = HexDigit(s[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        v[i] = hi * 16 + lo;
    }
    *out = ImVec4(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f);
    return true;
}

std::string ColorToHex(ImVec4 c, bool alwaysAlpha)
{
    auto b = [](float f) { return (int)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
    char buf[16];
    if (alwaysAlpha || b(c.w) != 255)
        snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", b(c.x), b(c.y), b(c.z), b(c.w));
    else
        snprintf(buf, sizeof(buf), "#%02X%02X%02X", b(c.x), b(c.y), b(c.z));
    return buf;
}

static const std::map<std::string, std::string>& NamedColors()
{
    static std::map<std::string, std::string> m = {
        { "white", "#FFFFFF" }, { "black", "#000000" }, { "red", "#FF0000" }, { "green", "#008000" },
        { "blue", "#0000FF" }, { "gray", "#808080" }, { "grey", "#808080" }, { "yellow", "#FFFF00" },
        { "orange", "#FFA500" }, { "purple", "#800080" }, { "transparent", "#00000000" },
    };
    return m;
}

ImVec4 ParseColor(const json& v, ImVec4 def)
{
    if (v.is_string())
    {
        std::string s = v.get<std::string>();
        ImVec4 c;
        auto it = NamedColors().find(s);
        if (it != NamedColors().end()) s = it->second;
        if (HexToColor(s, &c)) return c;
        float r, g, b, a = 1.0f;
        if (sscanf(s.c_str(), "rgba(%f,%f,%f,%f)", &r, &g, &b, &a) >= 3 || sscanf(s.c_str(), "rgb(%f,%f,%f)", &r, &g, &b) == 3)
            return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a);
        return def;
    }
    if (v.is_array() && v.size() >= 3)
    {
        float k = 1.0f;
        for (auto& e : v) if (e.is_number() && e.get<float>() > 1.0f) k = 1.0f / 255.0f;
        ImVec4 c(v[0].get<float>() * k, v[1].get<float>() * k, v[2].get<float>() * k, 1.0f);
        if (v.size() >= 4) c.w = v[3].get<float>() > 1.0f ? v[3].get<float>() / 255.0f : v[3].get<float>();
        return c;
    }
    return def;
}

// ---------------------------------------------------------------- base64

static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Base64Encode(const unsigned char* d, size_t len)
{
    std::string out;
    out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3)
    {
        unsigned v = d[i] << 16;
        if (i + 1 < len) v |= d[i + 1] << 8;
        if (i + 2 < len) v |= d[i + 2];
        out += kB64[(v >> 18) & 63];
        out += kB64[(v >> 12) & 63];
        out += i + 1 < len ? kB64[(v >> 6) & 63] : '=';
        out += i + 2 < len ? kB64[v & 63] : '=';
    }
    return out;
}

std::vector<unsigned char> Base64Decode(const std::string& s)
{
    int T[256];
    std::fill(T, T + 256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)kB64[i]] = i;
    T['-'] = 62; T['_'] = 63;
    std::vector<unsigned char> out;
    out.reserve(s.size() * 3 / 4);
    unsigned v = 0;
    int bits = -8;
    for (unsigned char c : s)
    {
        if (T[c] < 0) continue;
        v = (v << 6) | T[c];
        bits += 6;
        if (bits >= 0) { out.push_back((unsigned char)((v >> bits) & 0xFF)); bits -= 8; }
    }
    return out;
}

// ---------------------------------------------------------------- tree

std::string Document::NewId(const char* prefix)
{
    for (;;)
    {
        std::string id = std::string(prefix) + std::to_string(nextId++);
        if (!Find(id)) return id;
    }
}

static Node* FindIn(std::vector<std::unique_ptr<Node>>& v, const std::function<bool(Node*)>& pred)
{
    for (auto& n : v)
    {
        if (pred(n.get())) return n.get();
        if (Node* r = FindIn(n->children, pred)) return r;
    }
    return nullptr;
}

Node* Document::Find(const std::string& id)
{
    if (id.empty()) return nullptr;
    return FindIn(roots, [&](Node* n) { return n->id == id; });
}

Node* Document::FindByName(const std::string& nm)
{
    return FindIn(roots, [&](Node* n) { return n->name == nm; });
}

std::vector<std::unique_ptr<Node>>& Document::SiblingsOf(Node* n)
{
    return n->parent ? n->parent->children : roots;
}

int Document::IndexOf(Node* n)
{
    auto& s = SiblingsOf(n);
    for (int i = 0; i < (int)s.size(); i++)
        if (s[i].get() == n) return i;
    return -1;
}

std::unique_ptr<Node> Document::Detach(Node* n)
{
    auto& s = SiblingsOf(n);
    int i = IndexOf(n);
    if (i < 0) return nullptr;
    std::unique_ptr<Node> u = std::move(s[i]);
    s.erase(s.begin() + i);
    u->parent = nullptr;
    return u;
}

void Document::Insert(std::unique_ptr<Node> n, Node* parent, int index)
{
    auto& s = parent ? parent->children : roots;
    n->parent = parent;
    if (index < 0 || index > (int)s.size()) s.push_back(std::move(n));
    else s.insert(s.begin() + index, std::move(n));
}

static void ForEachIn(std::vector<std::unique_ptr<Node>>& v, const std::function<void(Node*)>& fn)
{
    for (size_t i = 0; i < v.size(); i++)
    {
        fn(v[i].get());
        ForEachIn(v[i]->children, fn);
    }
}

void Document::ForEach(const std::function<void(Node*)>& fn) { ForEachIn(roots, fn); }

void Document::Clear()
{
    roots.clear();
    assets.clear();
    nextId = 1;
}

bool Document::IsAncestor(Node* a, Node* n)
{
    for (Node* q = n ? n->parent : nullptr; q; q = q->parent)
        if (q == a) return true;
    return false;
}

// ---------------------------------------------------------------- serialization

static json ColorJ(ImVec4 c) { return ColorToHex(c); }

static json PaintJ(const Paint& p)
{
    if (p.type == 0) return "none";
    if (p.type == 1) return ColorJ(p.c0);
    return json{ { "type", "linear" }, { "from", ColorJ(p.c0) }, { "to", ColorJ(p.c1) }, { "angle", p.angle } };
}

static Paint PaintFromJ(const json& v, Paint def)
{
    Paint p = def;
    if (v.is_null() || (v.is_string() && (v == "none" || v == ""))) { p.type = 0; return p; }
    if (v.is_object())
    {
        std::string t = v.value("type", "solid");
        if (t == "linear" || t == "gradient" || v.contains("from"))
        {
            p.type = 2;
            p.c0 = ParseColor(v.value("from", json("#FFFFFF")), ImVec4(1, 1, 1, 1));
            p.c1 = ParseColor(v.value("to", json("#000000")), ImVec4(0, 0, 0, 1));
            p.angle = v.value("angle", 90.0f);
        }
        else
        {
            p.type = 1;
            p.c0 = ParseColor(v.value("color", json("#FFFFFF")), p.c0);
        }
        return p;
    }
    p.type = 1;
    p.c0 = ParseColor(v, p.c0);
    return p;
}

static bool PaintEq(const Paint& a, const Paint& b)
{
    if (a.type != b.type) return false;
    if (a.type == 0) return true;
    auto eq = [](ImVec4 x, ImVec4 y) { return ColorToHex(x) == ColorToHex(y); };
    if (a.type == 1) return eq(a.c0, b.c0);
    return eq(a.c0, b.c0) && eq(a.c1, b.c1) && a.angle == b.angle;
}

static Node DefaultFor(NodeType t)
{
    Node d;
    d.type = t;
    if (t == NodeType::Text) { d.fill.c0 = ImVec4(0, 0, 0, 1); }
    if (t == NodeType::Group) { d.fill.type = 0; d.clip = false; }
    if (t == NodeType::Line) { d.fill.type = 0; d.stroke = Paint{ 1, ImVec4(0, 0, 0, 1) }; d.strokeWidth = 1; d.h = 0; }
    if (t == NodeType::Image || t == NodeType::Vector) { d.fill.type = 0; }
    return d;
}

void InitNodeDefaults(Node* n)
{
    Node d = DefaultFor(n->type);
    n->fill = d.fill;
    n->stroke = d.stroke;
    n->clip = d.clip;
    if (n->type == NodeType::Line) n->h = 0;
}

json NodeToJson(const Node* n)
{
    Node d = DefaultFor(n->type);
    json j;
    j["id"] = n->id;
    j["type"] = NodeTypeName(n->type);
    if (!n->name.empty()) j["name"] = n->name;
    auto r1 = [](float v) { return std::round(v * 100.0f) / 100.0f; };
    j["x"] = r1(n->x); j["y"] = r1(n->y); j["w"] = r1(n->w); j["h"] = r1(n->h);
    if (n->opacity != 1.0f) j["opacity"] = r1(n->opacity);
    if (!n->visible) j["visible"] = false;
    if (n->locked) j["locked"] = true;
    if (!n->expanded) j["expanded"] = false;
    if (n->radius[0] || n->radius[1] || n->radius[2] || n->radius[3])
    {
        if (n->radius[0] == n->radius[1] && n->radius[1] == n->radius[2] && n->radius[2] == n->radius[3]) j["radius"] = n->radius[0];
        else j["radius"] = { n->radius[0], n->radius[1], n->radius[2], n->radius[3] };
    }
    if (n->fillHidden) { Paint p = n->fill; p.type = n->fillHidden; j["fill"] = PaintJ(p); j["fillHidden"] = true; }
    else if (!PaintEq(n->fill, d.fill)) j["fill"] = PaintJ(n->fill);
    if (n->strokeHidden) { Paint p = n->stroke; p.type = n->strokeHidden; j["stroke"] = PaintJ(p); j["strokeHidden"] = true; }
    else if (!PaintEq(n->stroke, d.stroke)) j["stroke"] = PaintJ(n->stroke);
    if (n->blend) j["blend"] = n->blend == 1 ? "plus-lighter" : n->blend == 2 ? "multiply" : "screen";
    if (n->stroke.type && n->strokeWidth != 1.0f) j["strokeWidth"] = n->strokeWidth;
    if (n->stroke.type && n->strokeAlign != 0) j["strokeAlign"] = n->strokeAlign == 1 ? "center" : "outside";
    if (n->effect.type)
        j["shadow"] = { { "type", n->effect.type == 1 ? "drop" : "inner" }, { "x", n->effect.offset.x }, { "y", n->effect.offset.y },
                        { "blur", n->effect.blur }, { "spread", n->effect.spread }, { "color", ColorJ(n->effect.color) } };
    if (n->blur.on) j["blur"] = n->blur.radius;
    if (n->glass.on)
        j["glass"] = { { "blur", n->glass.blur }, { "refraction", n->glass.refraction }, { "thickness", n->glass.thickness }, { "highlight", n->glass.highlight },
                       { "dispersion", n->glass.dispersion }, { "light", n->glass.lightAngle }, { "saturation", n->glass.saturation }, { "tint", ColorJ(n->glass.tint) } };
    if (n->texture.on)
    {
        static const char* blends[] = { "normal", "add", "multiply", "screen" };
        static const char* fits[] = { "fill", "fit", "tile" };
        j["texture"] = { { "asset", n->texture.asset }, { "opacity", n->texture.opacity }, { "blend", blends[n->texture.blend & 3] }, { "fit", fits[std::clamp(n->texture.fit, 0, 2)] },
                         { "scale", n->texture.scale }, { "speed", { n->texture.speed.x, n->texture.speed.y } } };
    }
    if (n->anim.preset)
    {
        const AnimFx& a = n->anim;
        json aj = { { "preset", AnimPresetKey(a.preset) }, { "trigger", TriggerKey(a.trigger) }, { "easing", EasingKey(a.easing) },
                    { "duration", a.duration }, { "delay", a.delay }, { "distance", a.distance }, { "scale", a.scale }, { "intensity", a.intensity } };
        if (a.stagger > 0) aj["stagger"] = a.stagger;
        if (a.easing == 8) aj["bezier"] = { a.bezier[0], a.bezier[1], a.bezier[2], a.bezier[3] };
        if (a.preset == fim::Anim_Keyframes)
        {
            json kj = json::object();
            for (auto& t : a.tracks)
            {
                if (t.keys.empty()) continue;
                json arr = json::array();
                for (auto& k : t.keys) arr.push_back({ { "t", k.time }, { "v", k.value }, { "ease", EasingKey(k.easing) } });
                kj[AnimPropKey(t.prop)] = arr;
            }
            aj["keyframes"] = kj;
        }
        j["animation"] = aj;
    }
    if (n->type == NodeType::Frame && !n->clip) j["clip"] = false;
    if (n->type == NodeType::Text)
    {
        j["text"] = n->text;
        j["font"] = n->fontFamily;
        if (n->fontStyle != "Regular") j["fontStyle"] = n->fontStyle;
        j["fontSize"] = n->fontSize;
        if (n->lineHeight > 0) j["lineHeight"] = n->lineHeight;
        if (n->alignH) j["align"] = n->alignH == 1 ? "center" : "right";
        if (n->alignV) j["valign"] = n->alignV == 1 ? "middle" : "bottom";
        j["textSizing"] = n->textSizing == 0 ? "fixed" : n->textSizing == 1 ? "auto-width" : "auto-height";
    }
    if (n->type == NodeType::Image || n->type == NodeType::Vector)
    {
        j["asset"] = n->asset;
        if (n->imageFit) j["imageFit"] = n->imageFit == 1 ? "fit" : "stretch";
        if (n->recolor) j["recolor"] = true;
    }
    if (n->layout)
    {
        j["layout"] = n->layout == 1 ? "horizontal" : "vertical";
        j["gap"] = n->gap;
        j["padding"] = { n->pad[0], n->pad[1], n->pad[2], n->pad[3] };
        static const char* am[] = { "start", "center", "end", "space-between" };
        static const char* ac[] = { "start", "center", "end", "stretch" };
        if (n->alignMain) j["alignMain"] = am[n->alignMain];
        if (n->alignCross) j["alignCross"] = ac[n->alignCross];
        if (n->hugW) j["hugW"] = true;
        if (n->hugH) j["hugH"] = true;
    }
    if (n->fillW) j["fillW"] = true;
    if (n->fillH) j["fillH"] = true;
    if (n->role != Role::None)
    {
        j["role"] = RoleName(n->role);
        if (!n->var.empty()) j["var"] = n->var;
        if (n->value != 0.0f) j["value"] = n->value;
        if (n->role == Role::Slider) { j["min"] = n->vmin; j["max"] = n->vmax; }
    }
    if (n->hasHover) j["hoverColor"] = ColorJ(n->hoverColor);
    if (n->hasActive) j["activeColor"] = ColorJ(n->activeColor);
    if (!n->showVar.empty()) j["showIf"] = { { "var", n->showVar }, { "value", n->showValue }, { "not", n->showNot } };
    if (n->sliderPart) j["sliderPart"] = n->sliderPart == 1 ? "fill" : "knob";
    if (n->role == Role::Combo || n->role == Role::MultiCombo)
    {
        j["options"] = n->options;
        j["listFill"] = ColorJ(n->listFill);
        j["listAccent"] = ColorJ(n->listAccent);
        if (n->itemHeight > 0) j["itemHeight"] = n->itemHeight;
        if (n->role == Role::MultiCombo) { j["noneLabel"] = n->noneLabel; j["allLabel"] = n->allLabel; }
    }
    if (!n->children.empty())
    {
        json c = json::array();
        for (auto& ch : n->children) c.push_back(NodeToJson(ch.get()));
        j["children"] = c;
    }
    return j;
}

static int IndexOfStr(const std::string& s, std::initializer_list<const char*> opts, int def)
{
    int i = 0;
    for (const char* o : opts) { if (s == o) return i; i++; }
    return def;
}

static float Num(const json& v, float def)
{
    if (v.is_number()) return v.get<float>();
    if (v.is_string()) { try { return std::stof(v.get<std::string>()); } catch (...) {} }
    if (v.is_boolean()) return v.get<bool>() ? 1.0f : 0.0f;
    return def;
}

void ApplyProps(Node* n, const json& p, Document* doc)
{
    if (!p.is_object()) return;
    (void)doc;
    // Setting a paint shows it again unless the same update hides it.
    if (p.contains("fill") || p.contains("color") || p.contains("background")) n->fillHidden = 0;
    if (p.contains("stroke") || p.contains("border")) n->strokeHidden = 0;
    for (auto it = p.begin(); it != p.end(); ++it)
    {
        const std::string& k = it.key();
        const json& v = it.value();
        if (k == "name" && v.is_string()) n->name = v.get<std::string>();
        else if (k == "x") n->x = Num(v, n->x);
        else if (k == "y") n->y = Num(v, n->y);
        else if (k == "w" || k == "width") n->w = std::max(0.0f, Num(v, n->w));
        else if (k == "h" || k == "height") n->h = std::max(0.0f, Num(v, n->h));
        else if (k == "opacity") n->opacity = std::clamp(Num(v, n->opacity), 0.0f, 1.0f);
        else if (k == "visible") n->visible = Num(v, 1) != 0;
        else if (k == "locked") n->locked = Num(v, 0) != 0;
        else if (k == "expanded") n->expanded = Num(v, 1) != 0;
        else if (k == "radius")
        {
            if (v.is_array()) for (int i = 0; i < 4 && i < (int)v.size(); i++) n->radius[i] = Num(v[i], 0);
            else { float r = Num(v, 0); for (float& q : n->radius) q = r; }
        }
        else if (k == "fill" || k == "color" || k == "background") n->fill = PaintFromJ(v, n->fill);
        else if (k == "stroke" || k == "border") { n->stroke = PaintFromJ(v, n->stroke); if (n->stroke.type == 2) n->stroke.type = 1; }
        else if (k == "strokeWidth") n->strokeWidth = Num(v, n->strokeWidth);
        else if (k == "blend" && v.is_string()) n->blend = IndexOfStr(v.get<std::string>(), { "normal", "plus-lighter", "multiply", "screen" }, 0);
        else if (k == "strokeAlign" && v.is_string()) n->strokeAlign = IndexOfStr(v.get<std::string>(), { "inside", "center", "outside" }, 0);
        else if (k == "shadow")
        {
            if (v.is_null() || (v.is_string() && v == "none") || (v.is_boolean() && !v.get<bool>())) n->effect.type = 0;
            else if (v.is_object())
            {
                n->effect.type = v.value("type", std::string("drop")) == "inner" ? 2 : 1;
                n->effect.offset.x = Num(v.value("x", json(n->effect.offset.x)), 0);
                n->effect.offset.y = Num(v.value("y", json(n->effect.offset.y)), 4);
                n->effect.blur = Num(v.value("blur", json(n->effect.blur)), 12);
                n->effect.spread = Num(v.value("spread", json(n->effect.spread)), 0);
                if (v.contains("color")) n->effect.color = ParseColor(v["color"], n->effect.color);
            }
        }
        else if (k == "blur" || k == "backgroundBlur")
        {
            if (v.is_object()) { n->blur.on = true; n->blur.radius = Num(v.value("radius", json(16)), 16); }
            else if (v.is_null() || (v.is_string() && v == "none")) n->blur.on = false;
            else { n->blur.radius = Num(v, 16); n->blur.on = n->blur.radius > 0; }
        }
        else if (k == "glass")
        {
            if (v.is_null() || (v.is_string() && v == "none") || (v.is_boolean() && !v.get<bool>())) n->glass.on = false;
            else
            {
                n->glass.on = true;
                if (v.is_object())
                {
                    GlassFx& g = n->glass;
                    g.blur = Num(v.value("blur", json(g.blur)), g.blur);
                    g.refraction = Num(v.value("refraction", json(g.refraction)), g.refraction);
                    g.thickness = Num(v.value("thickness", json(g.thickness)), g.thickness);
                    g.highlight = Num(v.value("highlight", json(g.highlight)), g.highlight);
                    g.dispersion = Num(v.value("dispersion", json(g.dispersion)), g.dispersion);
                    g.lightAngle = Num(v.value("light", json(g.lightAngle)), g.lightAngle);
                    g.saturation = Num(v.value("saturation", json(g.saturation)), g.saturation);
                    if (v.contains("tint")) g.tint = ParseColor(v["tint"], g.tint);
                }
            }
        }
        else if (k == "texture")
        {
            if (!v.is_object()) n->texture.on = false;
            else
            {
                TextureFx& t = n->texture;
                t.on = true;
                t.asset = v.value("asset", t.asset);
                t.opacity = std::clamp(Num(v.value("opacity", json(t.opacity)), t.opacity), 0.0f, 1.0f);
                if (v.contains("blend") && v["blend"].is_string()) t.blend = IndexOfStr(v["blend"].get<std::string>(), { "normal", "add", "multiply", "screen" }, 0);
                if (v.contains("fit") && v["fit"].is_string()) t.fit = IndexOfStr(v["fit"].get<std::string>(), { "fill", "fit", "tile" }, 2);
                t.scale = std::max(0.01f, Num(v.value("scale", json(t.scale)), t.scale));
                if (v.contains("speed") && v["speed"].is_array() && v["speed"].size() >= 2) t.speed = ImVec2(Num(v["speed"][0], 0), Num(v["speed"][1], 0));
            }
        }
        else if (k == "animation")
        {
            if (!v.is_object()) { n->anim.preset = 0; continue; }
            int preset = n->anim.preset;
            if (v.contains("preset") && v["preset"].is_string())
            {
                std::string ps = v["preset"].get<std::string>();
                preset = 0;
                for (int i = 0; i < kPresetCount; i++) if (ps == kPresetKeys[i]) preset = i;
            }
            if (preset != n->anim.preset) n->anim = AnimDefaults(preset);
            AnimFx& a = n->anim;
            if (v.contains("trigger") && v["trigger"].is_string()) a.trigger = IndexOfStr(v["trigger"].get<std::string>(), { "appear", "hover", "press", "active", "loop" }, a.trigger);
            if (v.contains("easing") && v["easing"].is_string())
            {
                std::string es = v["easing"].get<std::string>();
                float b[4];
                if (sscanf(es.c_str(), "cubic-bezier(%f,%f,%f,%f)", &b[0], &b[1], &b[2], &b[3]) == 4) { a.easing = 8; for (int i = 0; i < 4; i++) a.bezier[i] = b[i]; }
                else for (int i = 0; i < 9; i++) if (es == kEasingKeys[i]) a.easing = i;
            }
            a.duration = std::max(0.01f, Num(v.value("duration", json(a.duration)), a.duration));
            a.delay = std::max(0.0f, Num(v.value("delay", json(a.delay)), a.delay));
            a.distance = Num(v.value("distance", json(a.distance)), a.distance);
            a.scale = Num(v.value("scale", json(a.scale)), a.scale);
            a.intensity = Num(v.value("intensity", json(a.intensity)), a.intensity);
            a.stagger = std::max(0.0f, Num(v.value("stagger", json(a.stagger)), a.stagger));
            if (v.contains("bezier") && v["bezier"].is_array() && v["bezier"].size() == 4) { a.easing = 8; for (int i = 0; i < 4; i++) a.bezier[i] = Num(v["bezier"][i], a.bezier[i]); }
            // "keyframes": { "x": [ { "t": 0, "v": 24, "ease": "ease-out" }, ... ], "y": ..., "scale": ..., "opacity": ... }
            if (v.contains("keyframes") && v["keyframes"].is_object())
            {
                a.tracks.clear();
                for (int p = 0; p < fim::Prop_Count; p++)
                {
                    auto it = v["keyframes"].find(kPropKeys[p]);
                    if (it == v["keyframes"].end() || !it->is_array()) continue;
                    KeyTrackFx t;
                    t.prop = p;
                    for (auto& k : *it)
                    {
                        fim::Keyframe kf{ 0, p >= fim::Prop_Scale ? 1.0f : 0.0f, a.easing };
                        if (k.is_array() && k.size() >= 2) { kf.time = Num(k[0], 0); kf.value = Num(k[1], kf.value); if (k.size() > 2 && k[2].is_string()) kf.easing = IndexOfStr(k[2].get<std::string>(), { "linear", "ease-in", "ease-out", "ease-in-out", "back-out", "elastic-out", "bounce-out", "spring", "bezier" }, kf.easing); }
                        else if (k.is_object())
                        {
                            kf.time = Num(k.value("t", json(0)), 0);
                            kf.value = Num(k.value("v", json(kf.value)), kf.value);
                            if (k.contains("ease") && k["ease"].is_string()) kf.easing = IndexOfStr(k["ease"].get<std::string>(), { "linear", "ease-in", "ease-out", "ease-in-out", "back-out", "elastic-out", "bounce-out", "spring", "bezier" }, kf.easing);
                        }
                        kf.time = std::max(0.0f, kf.time);
                        t.keys.push_back(kf);
                    }
                    std::sort(t.keys.begin(), t.keys.end(), [](const fim::Keyframe& x, const fim::Keyframe& y) { return x.time < y.time; });
                    if (!t.keys.empty()) a.tracks.push_back(t);
                }
                // The timeline is at least as long as its last key.
                for (auto& t : a.tracks) a.duration = std::max(a.duration, t.keys.back().time);
            }
        }
        else if (k == "clip") n->clip = Num(v, 1) != 0;
        else if (k == "text" && v.is_string()) n->text = v.get<std::string>();
        else if ((k == "font" || k == "fontFamily") && v.is_string()) n->fontFamily = v.get<std::string>();
        else if ((k == "fontStyle" || k == "fontWeight") && (v.is_string() || v.is_number()))
        {
            if (v.is_number())
            {
                int w = v.get<int>();
                n->fontStyle = w >= 700 ? "Bold" : w >= 600 ? "SemiBold" : w >= 500 ? "Medium" : "Regular";
            }
            else n->fontStyle = v.get<std::string>();
        }
        else if (k == "fontSize") n->fontSize = std::max(1.0f, Num(v, n->fontSize));
        else if (k == "lineHeight") n->lineHeight = Num(v, 0);
        else if (k == "align" && v.is_string()) n->alignH = IndexOfStr(v.get<std::string>(), { "left", "center", "right" }, 0);
        else if (k == "valign" && v.is_string()) n->alignV = IndexOfStr(v.get<std::string>(), { "top", "middle", "bottom" }, 0);
        else if (k == "textSizing" && v.is_string()) n->textSizing = IndexOfStr(v.get<std::string>(), { "fixed", "auto-width", "auto-height" }, 1);
        else if (k == "asset" && v.is_string()) n->asset = v.get<std::string>();
        else if (k == "imageFit" && v.is_string()) n->imageFit = IndexOfStr(v.get<std::string>(), { "fill", "fit", "stretch" }, 0);
        else if (k == "recolor") n->recolor = Num(v, 0) != 0;
        else if (k == "layout" && v.is_string()) n->layout = IndexOfStr(v.get<std::string>(), { "none", "horizontal", "vertical" }, 0);
        else if (k == "gap") n->gap = Num(v, n->gap);
        else if (k == "padding")
        {
            if (v.is_array() && v.size() == 2) { n->pad[0] = n->pad[2] = Num(v[0], 0); n->pad[1] = n->pad[3] = Num(v[1], 0); }
            else if (v.is_array()) for (int i = 0; i < 4 && i < (int)v.size(); i++) n->pad[i] = Num(v[i], 0);
            else { float q = Num(v, 0); for (float& e : n->pad) e = q; }
        }
        else if (k == "alignMain" && v.is_string()) n->alignMain = IndexOfStr(v.get<std::string>(), { "start", "center", "end", "space-between" }, 0);
        else if (k == "alignCross" && v.is_string()) n->alignCross = IndexOfStr(v.get<std::string>(), { "start", "center", "end", "stretch" }, 0);
        else if (k == "hugW") n->hugW = Num(v, 0) != 0;
        else if (k == "hugH") n->hugH = Num(v, 0) != 0;
        else if (k == "fillW") n->fillW = Num(v, 0) != 0;
        else if (k == "fillH") n->fillH = Num(v, 0) != 0;
        else if (k == "role" && v.is_string()) RoleFromName(v.get<std::string>(), &n->role);
        else if (k == "var" && v.is_string()) n->var = v.get<std::string>();
        else if (k == "value") n->value = Num(v, 0);
        else if (k == "min") n->vmin = Num(v, 0);
        else if (k == "max") n->vmax = Num(v, 100);
        else if (k == "hoverColor") { n->hasHover = !v.is_null(); if (n->hasHover) n->hoverColor = ParseColor(v, n->hoverColor); }
        else if (k == "activeColor") { n->hasActive = !v.is_null(); if (n->hasActive) n->activeColor = ParseColor(v, n->activeColor); }
        else if (k == "showIf")
        {
            if (v.is_object())
            {
                n->showVar = v.value("var", std::string());
                n->showValue = Num(v.value("value", json(1)), 1);
                n->showNot = Num(v.value("not", json(false)), 0) != 0;
            }
            else n->showVar.clear();
        }
        else if (k == "sliderPart" && v.is_string()) n->sliderPart = IndexOfStr(v.get<std::string>(), { "none", "fill", "knob" }, 0);
        else if ((k == "options" || k == "items") && v.is_array())
        {
            n->options.clear();
            for (auto& o : v) if (o.is_string()) n->options.push_back(o.get<std::string>()); else if (o.is_number()) n->options.push_back(o.dump());
            if (n->options.size() > 24) n->options.resize(24);   // multi-combo masks are stored as float: 24 exact bits
        }
        else if (k == "selected" && v.is_array())
        {
            // Multi-combo default selection given as indices.
            unsigned mask = 0;
            for (auto& o : v) if (o.is_number_integer() && o.get<int>() >= 0 && o.get<int>() < 32) mask |= 1u << o.get<int>();
            n->value = (float)mask;
        }
        else if (k == "listFill") n->listFill = ParseColor(v, n->listFill);
        else if (k == "listAccent") n->listAccent = ParseColor(v, n->listAccent);
        else if (k == "itemHeight") n->itemHeight = std::max(0.0f, Num(v, 0));
        else if (k == "noneLabel" && v.is_string()) n->noneLabel = v.get<std::string>();
        else if (k == "allLabel" && v.is_string()) n->allLabel = v.get<std::string>();
    }
    if (p.contains("fillHidden") && Num(p["fillHidden"], 0) != 0 && n->fill.type) { n->fillHidden = n->fill.type; n->fill.type = 0; }
    if (p.contains("strokeHidden") && Num(p["strokeHidden"], 0) != 0 && n->stroke.type) { n->strokeHidden = n->stroke.type; n->stroke.type = 0; }
    if (n->type == NodeType::Line) n->h = 0;
}

std::unique_ptr<Node> NodeFromJson(const json& j, Document* doc)
{
    auto n = std::make_unique<Node>();
    NodeTypeFromName(j.value("type", std::string("rect")), &n->type);
    InitNodeDefaults(n.get());
    n->id = j.value("id", std::string());
    if (n->id.empty() || doc->Find(n->id)) n->id = doc->NewId();
    ApplyProps(n.get(), j, doc);
    if (j.contains("children") && j["children"].is_array())
        for (auto& c : j["children"])
        {
            auto ch = NodeFromJson(c, doc);
            ch->parent = n.get();
            n->children.push_back(std::move(ch));
        }
    return n;
}

json Document::ToJson(bool includeAssets, bool projectFiles) const
{
    json j;
    j["format"] = "fimgui";
    j["version"] = 1;
    j["name"] = name;
    j["canvasColor"] = ColorToHex(canvasColor);
    j["nextId"] = nextId;
    json r = json::array();
    for (auto& n : roots) r.push_back(NodeToJson(n.get()));
    j["nodes"] = r;
    json a = json::object();
    for (auto& [id, as] : assets)
    {
        json e = { { "name", as.name }, { "kind", AssetKindName(as.kind) }, { "width", as.width }, { "height", as.height } };
        if (projectFiles && !as.file.empty()) e["file"] = as.file;
        else if (includeAssets) e["data"] = as.kind == 1 ? std::string(as.data.begin(), as.data.end()) : Base64Encode(as.data.data(), as.data.size());
        a[id] = e;
    }
    j["assets"] = a;
    return j;
}

bool Document::FromJson(const json& j, std::string* err, const std::string& baseDir)
{
    if (!j.is_object() || !j.contains("nodes")) { if (err) *err = "Not a Fimgui document"; return false; }
    Clear();
    name = j.value("name", std::string("Untitled"));
    canvasColor = ParseColor(j.value("canvasColor", json("#1E1E1E")), canvasColor);
    nextId = j.value("nextId", 1);
    if (j.contains("assets") && j["assets"].is_object())
        for (auto it = j["assets"].begin(); it != j["assets"].end(); ++it)
        {
            Asset as;
            as.id = it.key();
            as.name = it.value().value("name", as.id);
            as.kind = AssetKindFromName(it.value().value("kind", std::string("raster")));
            as.width = it.value().value("width", 0);
            as.height = it.value().value("height", 0);
            as.file = it.value().value("file", std::string());
            if (!as.file.empty() && !baseDir.empty())
            {
                // Project asset stored as a file next to the design.
                std::ifstream f(std::filesystem::u8path(baseDir) / std::filesystem::u8path(as.file), std::ios::binary);
                if (f) as.data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
                else if (err) *err = "Missing asset file " + as.file;
            }
            else
            {
                std::string data = it.value().value("data", std::string());
                if (as.kind == 1) as.data.assign(data.begin(), data.end());
                else as.data = Base64Decode(data);
            }
            assets[as.id] = std::move(as);
        }
    for (auto& nj : j["nodes"]) roots.push_back(NodeFromJson(nj, this));
    return true;
}

std::unique_ptr<Node> Document::CloneNode(const Node* n, bool newIds)
{
    json j = NodeToJson(n);
    if (newIds)
    {
        std::function<void(json&)> strip = [&](json& e) {
            e["id"] = NewId();
            if (e.contains("children")) for (auto& c : e["children"]) strip(c);
        };
        strip(j);
    }
    return NodeFromJson(j, this);
}

// ---------------------------------------------------------------- layout

void Document::FitText(Node* n)
{
    if (n->type != NodeType::Text || n->textSizing == 0) return;
    if (n->textSizing == 1)
    {
        ImVec2 s = MeasureNodeText(n, -1.0f);
        n->w = std::ceil(s.x);
        n->h = std::ceil(s.y);
    }
    else
    {
        ImVec2 s = MeasureNodeText(n, n->w);
        n->h = std::ceil(s.y);
    }
}

static void Arrange(Document* doc, Node* n);

static void LayoutRec(Document* doc, Node* n)
{
    doc->FitText(n);
    for (auto& c : n->children) LayoutRec(doc, c.get());
    Arrange(doc, n);
}

static void Arrange(Document* doc, Node* n)
{
    if (n->type == NodeType::Group && !n->children.empty())
    {
        // Groups wrap their children: normalize so the group bounds = children bounds.
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (auto& c : n->children)
        {
            x0 = std::min(x0, c->x); y0 = std::min(y0, c->y);
            x1 = std::max(x1, c->x + c->w); y1 = std::max(y1, c->y + c->h);
        }
        if (x0 != 0 || y0 != 0)
        {
            for (auto& c : n->children) { c->x -= x0; c->y -= y0; }
            n->x += x0; n->y += y0;
        }
        n->w = x1 - x0; n->h = y1 - y0;
        return;
    }
    if (n->type != NodeType::Frame || n->layout == 0) return;

    bool horiz = n->layout == 1;
    std::vector<Node*> items;
    for (auto& c : n->children) if (c->visible) items.push_back(c.get());
    auto mainSize = [&](Node* c) -> float& { return horiz ? c->w : c->h; };
    auto crossSize = [&](Node* c) -> float& { return horiz ? c->h : c->w; };
    auto mainPos = [&](Node* c) -> float& { return horiz ? c->x : c->y; };
    auto crossPos = [&](Node* c) -> float& { return horiz ? c->y : c->x; };
    auto fillMain = [&](Node* c) { return horiz ? c->fillW : c->fillH; };
    auto fillCross = [&](Node* c) { return horiz ? c->fillH : c->fillW; };

    float padMain0 = horiz ? n->pad[0] : n->pad[1], padMain1 = horiz ? n->pad[2] : n->pad[3];
    float padCross0 = horiz ? n->pad[1] : n->pad[0], padCross1 = horiz ? n->pad[3] : n->pad[2];
    bool hugMain = horiz ? n->hugW : n->hugH;
    bool hugCross = horiz ? n->hugH : n->hugW;

    float fixed = 0, maxCross = 0;
    int fills = 0;
    for (Node* c : items)
    {
        if (fillMain(c) && !hugMain) fills++;
        else fixed += mainSize(c);
        if (!fillCross(c) && n->alignCross != 3) maxCross = std::max(maxCross, crossSize(c));
    }
    int count = (int)items.size();
    float gaps = count > 1 ? n->gap * (count - 1) : 0.0f;
    if (hugMain) mainSize(n) = padMain0 + fixed + gaps + padMain1;
    if (hugCross) crossSize(n) = padCross0 + maxCross + padCross1;

    float avail = mainSize(n) - padMain0 - padMain1;
    float innerCross = crossSize(n) - padCross0 - padCross1;
    float free = avail - fixed - gaps;
    float gap = n->gap;
    float pos = padMain0;
    if (fills > 0)
    {
        float each = std::max(0.0f, free / fills);
        for (Node* c : items)
            if (fillMain(c)) { mainSize(c) = each; if (c->type == NodeType::Text) doc->FitText(c); }
    }
    else if (n->alignMain == 1) pos += free * 0.5f;
    else if (n->alignMain == 2) pos += free;
    else if (n->alignMain == 3 && count > 1) gap = (avail - fixed) / (count - 1);

    for (Node* c : items)
    {
        float oldW = c->w, oldH = c->h;
        mainPos(c) = std::round(pos);
        if (fillCross(c) || n->alignCross == 3) crossSize(c) = innerCross;
        float cs = crossSize(c);
        float cp = padCross0;
        if (n->alignCross == 1) cp += (innerCross - cs) * 0.5f;
        else if (n->alignCross == 2) cp += innerCross - cs;
        crossPos(c) = std::round(cp);
        pos += mainSize(c) + gap;
        if ((oldW != c->w || oldH != c->h) && c->layout) Arrange(doc, c);
    }
}

void Document::ApplyLayout()
{
    for (auto& n : roots) LayoutRec(this, n.get());
}
