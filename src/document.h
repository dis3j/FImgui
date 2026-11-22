#pragma once
#include "imgui.h"
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <functional>
#include "../third_party/json/json.hpp"

using json = nlohmann::json;

enum class NodeType { Frame, Group, Rect, Ellipse, Line, Text, Image, Vector };

const char* NodeTypeName(NodeType t);
bool        NodeTypeFromName(const std::string& s, NodeType* out);

struct Paint
{
    int    type = 1;            // 0 none, 1 solid, 2 linear gradient
    ImVec4 c0 = ImVec4(1, 1, 1, 1);
    ImVec4 c1 = ImVec4(0, 0, 0, 1);
    float  angle = 90.0f;
};

struct Effect
{
    int    type = 0;            // 0 none, 1 drop shadow, 2 inner shadow
    ImVec2 offset = ImVec2(0, 4);
    float  blur = 12.0f;
    float  spread = 0.0f;
    ImVec4 color = ImVec4(0, 0, 0, 0.25f);
};

struct BlurFx
{
    bool  on = false;
    float radius = 16.0f;                   // background blur (layer fill tints it)
};

struct GlassFx
{
    bool  on = false;
    float blur = 10.0f;
    float refraction = 0.6f;                // 0..1
    float thickness = 18.0f;                // px rim width
    float highlight = 0.7f;                 // 0..1
    float dispersion = 0.3f;                // 0..1
    float lightAngle = -45.0f;              // degrees
    float saturation = 1.4f;
    ImVec4 tint = ImVec4(1, 1, 1, 0.06f);
};

struct TextureFx
{
    bool  on = false;
    std::string asset;                      // raster, GIF or video asset
    float opacity = 0.35f;
    int   blend = 0;                        // 0 normal, 1 add, 2 multiply, 3 screen
    int   fit = 2;                          // 0 fill, 1 fit, 2 tile
    float scale = 1.0f;                     // tile scale
    ImVec2 speed = ImVec2(0, 0);            // tile scroll, px/s
};

struct AnimFx
{
    int   preset = 0;                       // fim::AnimPreset
    int   trigger = 0;                      // fim::AnimTrigger
    int   easing = 2;                       // fim::Easing
    float duration = 0.45f;
    float delay = 0.0f;
    float distance = 16.0f;
    float scale = 0.9f;
    float intensity = 0.05f;
    float stagger = 0.0f;                   // >0: animate children one after another instead of the node
    float bezier[4] = { 0.25f, 0.1f, 0.25f, 1.0f };
};

const char* AnimPresetKey(int preset);     // "slide-up"
const char* AnimPresetLabel(int preset);   // "Slide up"
const char* EasingKey(int easing);
const char* EasingLabel(int easing);
const char* TriggerKey(int trigger);
AnimFx AnimDefaults(int preset);

// Interactive role, mapped to ImGui behaviour in preview and in generated code.
enum class Role { None, Button, Checkbox, Slider, Tab, Input, Combo, MultiCombo };
const char* RoleName(Role r);
bool        RoleFromName(const std::string& s, Role* out);

struct Node
{
    std::string id;
    std::string name;
    NodeType    type = NodeType::Rect;

    float x = 0, y = 0, w = 100, h = 100;   // relative to parent
    float opacity = 1.0f;
    bool  visible = true;
    bool  locked = false;
    bool  expanded = true;                  // layers panel state

    float radius[4] = { 0, 0, 0, 0 };       // tl, tr, br, bl
    Paint fill;
    Paint stroke{ 0 };
    float strokeWidth = 1.0f;
    int   strokeAlign = 0;                  // 0 inside, 1 center, 2 outside
    Effect effect;
    BlurFx blur;
    GlassFx glass;
    TextureFx texture;
    AnimFx anim;
    bool  clip = true;                      // frames: clip content

    // Text
    std::string text;
    std::string fontFamily = "Inter";
    std::string fontStyle = "Regular";
    float fontSize = 14.0f;
    float lineHeight = 0.0f;                // px, 0 = auto (1.2x)
    int   alignH = 0, alignV = 0;
    int   textSizing = 1;                   // 0 fixed, 1 auto width, 2 auto height

    // Image / Vector
    std::string asset;
    int   imageFit = 0;                     // 0 fill(cover), 1 fit(contain), 2 stretch
    bool  recolor = false;                  // vector: tint with fill color

    // Auto layout (frames)
    int   layout = 0;                       // 0 none, 1 horizontal, 2 vertical
    float gap = 8.0f;
    float pad[4] = { 0, 0, 0, 0 };          // left, top, right, bottom
    int   alignMain = 0;                    // 0 start, 1 center, 2 end, 3 space-between
    int   alignCross = 0;                   // 0 start, 1 center, 2 end, 3 stretch
    bool  hugW = false, hugH = false;       // hug contents
    bool  fillW = false, fillH = false;     // child: fill container along axis

    // Interaction
    Role  role = Role::None;
    std::string var;                        // bound state variable
    float value = 0.0f;                     // tab index / default value
    float vmin = 0.0f, vmax = 100.0f;
    bool  hasHover = false; ImVec4 hoverColor = ImVec4(1, 1, 1, 1);
    bool  hasActive = false; ImVec4 activeColor = ImVec4(0.05f, 0.6f, 1, 1);
    std::string showVar;                    // visible only when showVar == showValue (or != when showNot)
    float showValue = 1.0f;
    bool  showNot = false;
    int   sliderPart = 0;                   // 0 none, 1 fill (width follows value), 2 knob (x follows value)
    // Combo / multi-combo dropdown (value = default index, or default bit mask for multi)
    std::vector<std::string> options;
    ImVec4 listFill = ImVec4(0.11f, 0.11f, 0.13f, 0.98f);   // dropdown list background
    ImVec4 listAccent = ImVec4(0.31f, 0.49f, 1.0f, 1.0f);   // selected item / check color
    float itemHeight = 0.0f;                // 0 = same as the box height
    std::string noneLabel = "None";         // multi: text when nothing is selected
    std::string allLabel = "All";           // multi: text when everything is selected + "select all" row ("" hides)

    std::vector<std::unique_ptr<Node>> children;
    Node* parent = nullptr;

    bool IsContainer() const { return type == NodeType::Frame || type == NodeType::Group; }
    ImVec2 AbsPos() const;
};

struct Asset
{
    std::string id;
    std::string name;
    int kind = 0;                           // 0 raster (PNG/JPG bytes), 1 SVG text, 2 GIF, 3 video (MP4/MOV/WMV)
    std::vector<unsigned char> data;
    int width = 0, height = 0;              // intrinsic size
    std::string file;                       // project mode: path relative to the project folder (images/x.png ...)
};

struct Document
{
    std::string name = "Untitled";
    std::vector<std::unique_ptr<Node>> roots;   // top-level nodes on the page
    std::map<std::string, Asset> assets;
    ImVec4 canvasColor = ImVec4(0.118f, 0.118f, 0.118f, 1.0f);
    int nextId = 1;

    std::string NewId(const char* prefix = "n");
    Node* Find(const std::string& id);
    Node* FindByName(const std::string& name);
    std::vector<std::unique_ptr<Node>>& SiblingsOf(Node* n);
    int  IndexOf(Node* n);
    std::unique_ptr<Node> Detach(Node* n);
    void Insert(std::unique_ptr<Node> n, Node* parent, int index = -1);
    void ForEach(const std::function<void(Node*)>& fn);
    void Clear();
    bool IsAncestor(Node* maybeAncestor, Node* n);

    // projectFiles: assets are referenced by their project file ("file") instead of being embedded.
    json ToJson(bool includeAssets = true, bool projectFiles = false) const;
    // baseDir: folder used to resolve assets stored as project files.
    bool FromJson(const json& j, std::string* err = nullptr, const std::string& baseDir = "");
    std::unique_ptr<Node> CloneNode(const Node* n, bool newIds);

    void ApplyLayout();   // recompute auto-layout positions/sizes
    void FitText(Node* n); // auto-size text nodes
};

json NodeToJson(const Node* n);
std::unique_ptr<Node> NodeFromJson(const json& j, Document* doc);
// Applies a partial property object onto a node (used by the inspector paste, AI ops and CSS import).
void ApplyProps(Node* n, const json& props, Document* doc);

ImVec4 ParseColor(const json& v, ImVec4 def);
std::string ColorToHex(ImVec4 c, bool alwaysAlpha = false);
bool HexToColor(const std::string& s, ImVec4* out);

const char* AssetKindName(int kind);
int AssetKindFromName(const std::string& s);

std::string Base64Encode(const unsigned char* data, size_t len);
std::vector<unsigned char> Base64Decode(const std::string& s);
