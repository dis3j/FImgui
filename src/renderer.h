#pragma once
#include "document.h"
#include "../runtime/fimgui_runtime.h"
#include <map>
#include <string>
#include <unordered_map>

enum class VarKind { Bool, Int, Float, String, Mask };   // Mask: multi-combo selection bits

struct VarInfo
{
    std::string name;
    VarKind kind = VarKind::Float;
    float def = 0.0f;
};

// Values of interactive state variables (preview mode and edit-mode state previews).
struct VarStore
{
    std::map<std::string, float> num;
    std::map<std::string, std::string> str;
    std::map<std::string, float> defaults;   // last default seen per variable
    struct ComboInfo { std::vector<std::string> options; bool multi = false; std::string none, all; };
    std::map<std::string, ComboInfo> combos; // option labels for "{var:label}" texts
    float Get(const std::string& k, float def = 0.0f) const
    {
        auto it = num.find(k);
        return it == num.end() ? def : it->second;
    }
};

// Values used to measure auto-sized texts with {var} placeholders.
extern const VarStore* g_textVars;

std::vector<VarInfo> CollectVars(Document& doc);
void EnsureVars(Document& doc, VarStore& vars);
bool NodeConditionVisible(const Node* n, const VarStore& vars);
// Expands "{var}" / "{var:%.1f}" placeholders.
std::string ExpandText(const std::string& text, const VarStore& vars);
// Dropdown list style for a combo node at the given zoom (font/colors come from its first text child).
fim::ComboStyle ComboStyleFor(const Node* n, float zoom);
// Finds the slider that owns a sliderPart node.
Node* SliderOwner(Node* n);

struct AnimClock { int lastFrame = -100; double start = 0; float progress = 0; };
using AnimClocks = std::unordered_map<std::string, AnimClock>;

fim::AnimSpec ToAnimSpec(const AnimFx& a, float extraDelay = 0.0f);

struct RenderCtx
{
    ImDrawList* dl = nullptr;
    ImVec2 origin = ImVec2(0, 0);   // screen position of world (0,0)
    float zoom = 1.0f;
    bool interactive = false;       // preview: run ImGui interaction (must be inside a window)
    bool ignoreConditions = false;
    VarStore* vars = nullptr;
    Document* doc = nullptr;
    const std::string* hideTextOf = nullptr;   // text node currently edited inline
    // Animation
    AnimClocks* clocks = nullptr;              // null = render the resting state
    bool animate = false;                      // run animations everywhere (preview)
    const Node* playRoot = nullptr;            // or only inside this subtree (editor "Play")
    double time = 0.0;
    int frame = 0;
    double timeline = -1.0;                    // >= 0: keyframe animations show this timeline time (seconds)
};

void RenderNode(RenderCtx& ctx, Node* n, ImVec2 parentWorld, float parentAlpha = 1.0f, const AnimFx* inherited = nullptr, float extraDelay = 0.0f);
void RenderDocument(RenderCtx& ctx);
// Effective world rect of a node after slider-part adjustments.
void NodeWorldRect(Node* n, const VarStore* vars, ImVec2* a, ImVec2* b);
