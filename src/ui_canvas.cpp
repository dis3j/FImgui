#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "icons.h"
#include "exporters.h"
#include "gfx.h"
#include "../runtime/fimgui_runtime.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <functional>

using namespace theme;

static ImVec2 operator+(ImVec2 a, ImVec2 b) { return ImVec2(a.x + b.x, a.y + b.y); }
static ImVec2 operator-(ImVec2 a, ImVec2 b) { return ImVec2(a.x - b.x, a.y - b.y); }

static bool PointIn(ImVec2 p, ImVec2 a, ImVec2 b, float pad = 0) { return p.x >= a.x - pad && p.x <= b.x + pad && p.y >= a.y - pad && p.y <= b.y + pad; }

// ---------------------------------------------------------------- hit testing

static bool HitPath(App* app, std::vector<std::unique_ptr<Node>>& list, ImVec2 w, float tol, std::vector<Node*>& path)
{
    for (int i = (int)list.size() - 1; i >= 0; i--)
    {
        Node* n = list[i].get();
        if (!n->visible || n->locked) continue;
        if (!app->ignoreConditions && !NodeConditionVisible(n, app->vars)) continue;
        ImVec2 a, b;
        NodeWorldRect(n, &app->vars, &a, &b);
        bool inside = n->type == NodeType::Line ? PointIn(w, a, ImVec2(b.x, a.y), tol) : PointIn(w, a, b);
        bool clipped = n->type == NodeType::Frame && n->clip;
        if (!n->children.empty() && (inside || !clipped))
        {
            std::vector<Node*> sub;
            if (HitPath(app, n->children, w, tol, sub))
            {
                path.push_back(n);
                path.insert(path.end(), sub.begin(), sub.end());
                return true;
            }
        }
        if (inside && n->type != NodeType::Group) { path.push_back(n); return true; }
    }
    return false;
}

std::string App::HitTest(ImVec2 w, bool deep)
{
    std::vector<Node*> path;
    if (!HitPath(this, doc.roots, w, 4.0f / zoom, path) || path.empty()) return "";
    if (deep) return path.back()->id;
    // Stay at the depth of the current selection when clicking on its siblings / itself.
    for (Node* s : SelNodes())
        for (Node* p : path)
            if (p == s || (p->parent == s->parent && p->parent)) return p->id;
    Node* root = path[0];
    if (root->type == NodeType::Frame && !root->children.empty())
        return path.size() > 1 ? path[1]->id : "";   // empty area of a top-level frame: start marquee
    return root->id;
}

// Top-level frame title labels (select & drag the frame).
static bool LabelRect(App* app, Node* n, ImVec2* a, ImVec2* b)
{
    if (n->type != NodeType::Frame || n->parent) return false;
    ImVec2 p = app->WorldToScreen(ImVec2(n->x, n->y));
    float fs = 11.0f * dpi;
    ImVec2 ts = fonts::UI(fonts::UI_Regular)->CalcTextSizeA(fs, FLT_MAX, 0, n->name.c_str());
    float maxW = n->w * app->zoom;
    *a = ImVec2(p.x, p.y - fs - U(6));
    *b = ImVec2(p.x + std::min(ts.x, std::max(maxW, U(20))), p.y - U(2));
    return true;
}

// ---------------------------------------------------------------- selection bounds & handles

static bool SelectionBounds(App* app, ImVec2* a, ImVec2* b)
{
    auto nodes = app->SelNodes();
    if (nodes.empty()) return false;
    *a = ImVec2(1e9f, 1e9f);
    *b = ImVec2(-1e9f, -1e9f);
    for (Node* n : nodes)
    {
        ImVec2 na, nb;
        NodeWorldRect(n, &app->vars, &na, &nb);
        a->x = std::min(a->x, na.x); a->y = std::min(a->y, na.y);
        b->x = std::max(b->x, nb.x); b->y = std::max(b->y, nb.y);
    }
    return true;
}

static ImVec2 HandlePos(int h, ImVec2 a, ImVec2 b)
{
    float mx = (a.x + b.x) * 0.5f, my = (a.y + b.y) * 0.5f;
    switch (h)
    {
    case 0: return a;
    case 1: return ImVec2(mx, a.y);
    case 2: return ImVec2(b.x, a.y);
    case 3: return ImVec2(b.x, my);
    case 4: return b;
    case 5: return ImVec2(mx, b.y);
    case 6: return ImVec2(a.x, b.y);
    default: return ImVec2(a.x, my);
    }
}

static int HandleAt(App* app, ImVec2 mouse)
{
    ImVec2 wa, wb;
    if (!SelectionBounds(app, &wa, &wb)) return -1;
    for (Node* n : app->SelNodes()) if (n->locked) return -1;
    ImVec2 a = app->WorldToScreen(wa), b = app->WorldToScreen(wb);
    float r = U(5);
    bool line = app->selection.size() == 1 && app->Sel()->type == NodeType::Line;
    for (int h : { 0, 2, 4, 6 })
    {
        if (line && (h == 0 || h == 2 || h == 4 || h == 6)) { ImVec2 p = h == 0 || h == 6 ? ImVec2(a.x, a.y) : ImVec2(b.x, a.y); if (std::fabs(mouse.x - p.x) < r && std::fabs(mouse.y - p.y) < r) return h == 0 || h == 6 ? 7 : 3; continue; }
        ImVec2 p = HandlePos(h, a, b);
        if (std::fabs(mouse.x - p.x) < r && std::fabs(mouse.y - p.y) < r) return h;
    }
    if (line) return -1;
    float e = U(4);
    if (mouse.x > a.x + e && mouse.x < b.x - e)
    {
        if (std::fabs(mouse.y - a.y) < e) return 1;
        if (std::fabs(mouse.y - b.y) < e) return 5;
    }
    if (mouse.y > a.y + e && mouse.y < b.y - e)
    {
        if (std::fabs(mouse.x - b.x) < e) return 3;
        if (std::fabs(mouse.x - a.x) < e) return 7;
    }
    return -1;
}

static ImGuiMouseCursor HandleCursor(int h)
{
    switch (h)
    {
    case 0: case 4: return ImGuiMouseCursor_ResizeNWSE;
    case 2: case 6: return ImGuiMouseCursor_ResizeNESW;
    case 1: case 5: return ImGuiMouseCursor_ResizeNS;
    default: return ImGuiMouseCursor_ResizeEW;
    }
}

// ---------------------------------------------------------------- snapping

static void CollectSnapLines(App* app, const std::vector<Node*>& moving, std::vector<float>& xs, std::vector<float>& ys)
{
    if (moving.empty()) return;
    Node* parent = moving[0]->parent;
    auto& sib = parent ? parent->children : app->doc.roots;
    auto add = [&](ImVec2 a, ImVec2 b) {
        xs.push_back(a.x); xs.push_back((a.x + b.x) * 0.5f); xs.push_back(b.x);
        ys.push_back(a.y); ys.push_back((a.y + b.y) * 0.5f); ys.push_back(b.y);
    };
    for (auto& s : sib)
    {
        if (std::find(moving.begin(), moving.end(), s.get()) != moving.end() || !s->visible) continue;
        if (!NodeConditionVisible(s.get(), app->vars)) continue;
        ImVec2 a, b;
        NodeWorldRect(s.get(), &app->vars, &a, &b);
        add(a, b);
    }
    if (parent)
    {
        ImVec2 a, b;
        NodeWorldRect(parent, &app->vars, &a, &b);
        add(a, b);
    }
}

static float SnapAxis(const std::vector<float>& moving, const std::vector<float>& targets, float tol, float* matched)
{
    float best = tol + 1;
    float delta = 0;
    for (float m : moving)
        for (float t : targets)
        {
            float d = t - m;
            if (std::fabs(d) < std::fabs(best)) { best = d; delta = d; *matched = t; }
        }
    return std::fabs(best) <= tol ? delta : 0.0f;
}

void App::ApplySnapping(ImVec2& delta, const std::vector<Node*>& moving, bool resize, int handle)
{
    m_guides.clear();
    if (ImGui::GetIO().KeyCtrl) return;
    std::vector<float> xs, ys;
    CollectSnapLines(this, moving, xs, ys);
    float tol = 5.0f / zoom;
    // Moving rect (world) with the current delta applied
    ImVec2 a(1e9f, 1e9f), b(-1e9f, -1e9f);
    for (auto& [id, r] : m_dragOrig)
    {
        Node* n = doc.Find(id);
        if (!n) continue;
        ImVec2 base = n->parent ? n->parent->AbsPos() : ImVec2(0, 0);
        a.x = std::min(a.x, base.x + r.x); a.y = std::min(a.y, base.y + r.y);
        b.x = std::max(b.x, base.x + r.x + r.z); b.y = std::max(b.y, base.y + r.y + r.w);
    }
    std::vector<float> mx, my;
    if (!resize)
    {
        mx = { a.x + delta.x, (a.x + b.x) * 0.5f + delta.x, b.x + delta.x };
        my = { a.y + delta.y, (a.y + b.y) * 0.5f + delta.y, b.y + delta.y };
    }
    else
    {
        if (handle == 2 || handle == 3 || handle == 4) mx = { b.x + delta.x };
        if (handle == 0 || handle == 6 || handle == 7) mx = { a.x + delta.x };
        if (handle == 4 || handle == 5 || handle == 6) my = { b.y + delta.y };
        if (handle == 0 || handle == 1 || handle == 2) my = { a.y + delta.y };
    }
    float gx = 0, gy = 0;
    float dx = mx.empty() ? 0 : SnapAxis(mx, xs, tol, &gx);
    float dy = my.empty() ? 0 : SnapAxis(my, ys, tol, &gy);
    bool sx = !mx.empty() && dx != 0.0f, sy = !my.empty() && dy != 0.0f;
    // Exact matches (delta 0) still show a guide.
    if (!sx && !mx.empty()) for (float m : mx) for (float t : xs) if (std::fabs(m - t) < 0.01f) { sx = true; gx = t; }
    if (!sy && !my.empty()) for (float m : my) for (float t : ys) if (std::fabs(m - t) < 0.01f) { sy = true; gy = t; }
    delta.x += dx;
    delta.y += dy;
    ImVec2 na = resize ? a : ImVec2(a.x + delta.x, a.y + delta.y), nb = resize ? b : ImVec2(b.x + delta.x, b.y + delta.y);
    if (sx) m_guides.push_back({ true, gx, std::min(na.y, -1e9f), 0 });
    if (sy) m_guides.push_back({ false, gy, 0, 0 });
    // Extent of guides: from moving rect to the matched sibling lines
    for (auto& g : m_guides)
    {
        float lo = g.vertical ? na.y : na.x, hi = g.vertical ? nb.y : nb.x;
        auto& sib = moving[0]->parent ? moving[0]->parent->children : doc.roots;
        for (auto& s : sib)
        {
            if (std::find(moving.begin(), moving.end(), s.get()) != moving.end()) continue;
            ImVec2 sa, sb;
            NodeWorldRect(s.get(), &vars, &sa, &sb);
            float c = g.vertical ? (sa.x + sb.x) * 0.5f : (sa.y + sb.y) * 0.5f;
            bool on = g.vertical ? (std::fabs(sa.x - g.pos) < 0.01f || std::fabs(sb.x - g.pos) < 0.01f || std::fabs(c - g.pos) < 0.01f)
                                 : (std::fabs(sa.y - g.pos) < 0.01f || std::fabs(sb.y - g.pos) < 0.01f || std::fabs(c - g.pos) < 0.01f);
            if (on) { lo = std::min(lo, g.vertical ? sa.y : sa.x); hi = std::max(hi, g.vertical ? sb.y : sb.x); }
        }
        g.from = lo; g.to = hi;
    }
}

// ---------------------------------------------------------------- move / resize

void App::BeginMove()
{
    m_dragOrig.clear();
    m_dragOrigChildren.clear();
    for (Node* n : SelNodes())
    {
        bool childOfSelected = false;
        for (Node* q = n->parent; q; q = q->parent) if (IsSelected(q->id)) childOfSelected = true;
        if (childOfSelected || n->locked) continue;
        m_dragOrig[n->id] = ImVec4(n->x, n->y, n->w, n->h);
        if (n->type == NodeType::Group || m_drag == Drag::Resize)
        {
            std::vector<std::pair<std::string, ImVec4>> ch;
            std::function<void(Node*)> rec = [&](Node* q) {
                for (auto& c : q->children) { ch.push_back({ c->id, ImVec4(c->x, c->y, c->w, c->h) }); if (q->type == NodeType::Group) rec(c.get()); }
            };
            rec(n);
            m_dragOrigChildren[n->id] = ch;
        }
    }
}

void App::UpdateMove(ImVec2 world)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 delta = world - m_dragStartWorld;
    if (io.KeyShift)
    {
        if (std::fabs(delta.x) > std::fabs(delta.y)) delta.y = 0; else delta.x = 0;
    }
    delta.x = Snap(delta.x);
    delta.y = Snap(delta.y);
    std::vector<Node*> moving;
    for (auto& [id, r] : m_dragOrig) if (Node* n = doc.Find(id)) moving.push_back(n);
    if (moving.empty()) return;
    ApplySnapping(delta, moving, false, -1);
    for (Node* n : moving)
    {
        ImVec4 r = m_dragOrig[n->id];
        n->x = Snap(r.x + delta.x);
        n->y = Snap(r.y + delta.y);
    }
    MarkDirty();
}

void App::UpdateResize(ImVec2 world)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 delta = world - m_dragStartWorld;
    std::vector<Node*> nodes;
    for (auto& [id, r] : m_dragOrig) if (Node* n = doc.Find(id)) nodes.push_back(n);
    if (nodes.empty()) return;
    ApplySnapping(delta, nodes, true, m_handle);

    // Bounding box of the originals (parent-relative coords are converted to world).
    ImVec2 a(1e9f, 1e9f), b(-1e9f, -1e9f);
    for (Node* n : nodes)
    {
        ImVec4 r = m_dragOrig[n->id];
        ImVec2 base = n->parent ? n->parent->AbsPos() : ImVec2(0, 0);
        a.x = std::min(a.x, base.x + r.x); a.y = std::min(a.y, base.y + r.y);
        b.x = std::max(b.x, base.x + r.x + r.z); b.y = std::max(b.y, base.y + r.y + r.w);
    }
    ImVec2 na = a, nb = b;
    int h = m_handle;
    bool left = h == 0 || h == 6 || h == 7, right = h == 2 || h == 3 || h == 4;
    bool top = h == 0 || h == 1 || h == 2, bottom = h == 4 || h == 5 || h == 6;
    if (left) na.x += delta.x;
    if (right) nb.x += delta.x;
    if (top) na.y += delta.y;
    if (bottom) nb.y += delta.y;
    if (io.KeyAlt)
    {
        if (left) nb.x -= delta.x;
        if (right) na.x -= delta.x;
        if (top) nb.y -= delta.y;
        if (bottom) na.y -= delta.y;
    }
    if (io.KeyShift && (b.x - a.x) > 0 && (b.y - a.y) > 0)
    {
        float aspect = (b.x - a.x) / (b.y - a.y);
        float w = nb.x - na.x, hh = nb.y - na.y;
        if ((left || right) && (top || bottom)) { if (std::fabs(w / aspect) > std::fabs(hh)) hh = w / aspect; else w = hh * aspect; }
        else if (left || right) hh = w / aspect;
        else w = hh * aspect;
        if (left) na.x = nb.x - w; else nb.x = na.x + w;
        if (top) na.y = nb.y - hh; else nb.y = na.y + hh;
    }
    if (nb.x < na.x) std::swap(na.x, nb.x);
    if (nb.y < na.y) std::swap(na.y, nb.y);
    na.x = Snap(na.x); na.y = Snap(na.y); nb.x = Snap(nb.x); nb.y = Snap(nb.y);
    float sx = (b.x - a.x) > 0 ? (nb.x - na.x) / (b.x - a.x) : 1, sy = (b.y - a.y) > 0 ? (nb.y - na.y) / (b.y - a.y) : 1;

    for (Node* n : nodes)
    {
        ImVec4 r = m_dragOrig[n->id];
        ImVec2 base = n->parent ? n->parent->AbsPos() : ImVec2(0, 0);
        float wx = base.x + r.x, wy = base.y + r.y;
        float nx = na.x + (wx - a.x) * sx, ny = na.y + (wy - a.y) * sy;
        n->x = Snap(nx - base.x);
        n->y = Snap(ny - base.y);
        n->w = std::max(n->type == NodeType::Line ? 1.0f : 1.0f, Snap(r.z * sx));
        if (n->type != NodeType::Line) n->h = std::max(1.0f, Snap(r.w * sy));
        if (n->type == NodeType::Text && n->textSizing != 0) n->textSizing = (top || bottom) ? 0 : 2;
        if (n->layout) { if (left || right) n->hugW = false; if (top || bottom) n->hugH = false; }
        if (n->type == NodeType::Group)
        {
            float gx = n->w / std::max(1.0f, r.z), gy = n->h / std::max(1.0f, r.w);
            for (auto& [cid, cr] : m_dragOrigChildren[n->id])
                if (Node* c = doc.Find(cid))
                {
                    c->x = Snap(cr.x * gx); c->y = Snap(cr.y * gy);
                    c->w = std::max(1.0f, Snap(cr.z * gx)); if (c->type != NodeType::Line) c->h = std::max(1.0f, Snap(cr.w * gy));
                    if (c->type == NodeType::Text) c->fontSize = std::max(1.0f, c->fontSize);
                }
        }
    }
    MarkDirty();
}

// ---------------------------------------------------------------- text editing

void App::StartTextEdit(Node* n)
{
    if (!n || n->type != NodeType::Text) return;
    editingText = n->id;
    snprintf(textBuf, sizeof(textBuf), "%s", n->text.c_str());
    textEditFocus = true;
    SetSelection({ n->id });
}

void App::CommitTextEdit()
{
    if (editingText.empty()) return;
    Node* n = doc.Find(editingText);
    editingText.clear();
    if (!n) return;
    n->text = textBuf;
    if (n->text.empty()) { doc.Detach(n); selection.clear(); }
    MarkDirty();
}

// ---------------------------------------------------------------- input

void App::CanvasInput()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    ImVec2 world = ScreenToWorld(mouse);
    bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) || m_drag != Drag::None;
    bool canvasHovered = ImGui::IsItemHovered();

    // Panning
    bool panTool = tool == Tool::Hand || m_spaceHeld;
    if (canvasHovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (panTool && ImGui::IsMouseClicked(0))))
        m_drag = Drag::Pan;
    if (m_drag == Drag::Pan)
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
        pan = pan + io.MouseDelta;
        if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle)) m_drag = Drag::None;
        return;
    }
    if (panTool && canvasHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

    // Hover
    m_hoverId = (canvasHovered && m_drag == Drag::None) ? HitTest(world, io.KeyCtrl) : "";
    if (canvasHovered && m_drag == Drag::None && m_hoverId.empty())
        for (auto& r : doc.roots)
        {
            ImVec2 la, lb;
            if (LabelRect(this, r.get(), &la, &lb) && PointIn(mouse, la, lb)) m_hoverId = r->id;
        }

    int handle = (canvasHovered && tool == Tool::Move && m_drag == Drag::None) ? HandleAt(this, mouse) : -1;
    if (handle >= 0) ImGui::SetMouseCursor(HandleCursor(handle));
    if (m_drag == Drag::Resize) ImGui::SetMouseCursor(HandleCursor(m_handle));
    if (tool != Tool::Move && tool != Tool::Hand && canvasHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);

    // Mouse down
    if (canvasHovered && ImGui::IsMouseClicked(0) && !panTool)
    {
        if (!editingText.empty()) CommitTextEdit();
        m_dragStartWorld = world;
        m_dragStartScreen = mouse;
        if (tool != Tool::Move)
        {
            m_drag = Drag::Create;
            m_createdId.clear();
        }
        else if (handle >= 0)
        {
            m_drag = Drag::Resize;
            m_handle = handle;
            BeginMove();
        }
        else
        {
            std::string hit = m_hoverId;
            if (!hit.empty())
            {
                if (io.KeyShift) { Select(hit, true); m_drag = Drag::None; }
                else
                {
                    m_pendingClickId = IsSelected(hit) && selection.size() > 1 ? hit : "";
                    if (!IsSelected(hit)) Select(hit);
                    m_drag = Drag::Pending;
                }
            }
            else
            {
                if (!io.KeyShift) selection.clear();
                m_drag = Drag::Marquee;
            }
        }
    }

    // Double click
    if (canvasHovered && ImGui::IsMouseDoubleClicked(0) && tool == Tool::Move)
    {
        Node* n = doc.Find(m_hoverId.empty() ? (selection.empty() ? "" : selection[0]) : m_hoverId);
        if (n && n->type == NodeType::Text) { StartTextEdit(n); m_drag = Drag::None; }
        else if (n && !n->children.empty())
        {
            std::vector<Node*> path;
            std::vector<std::unique_ptr<Node>>& list = n->children;
            if (HitPath(this, list, world, 4.0f / zoom, path) && !path.empty()) { Select(path[0]->id); m_drag = Drag::None; }
        }
    }

    // Dragging
    if (m_drag == Drag::Pending && ImGui::IsMouseDragging(0, 3.0f))
    {
        m_drag = Drag::Move;
        m_pendingClickId.clear();
        if (io.KeyAlt)
        {
            // Alt-drag duplicates and moves the copies
            std::vector<std::string> ids;
            for (Node* n : SelNodes())
            {
                auto c = doc.CloneNode(n, true);
                ids.push_back(c->id);
                doc.Insert(std::move(c), n->parent, doc.IndexOf(n) + 1);
            }
            SetSelection(ids);
        }
        BeginMove();
    }
    if (m_drag == Drag::Move) UpdateMove(world);
    if (m_drag == Drag::Resize) UpdateResize(world);
    if (m_drag == Drag::Create && ImGui::IsMouseDragging(0, 2.0f))
    {
        ImVec2 a(std::min(m_dragStartWorld.x, world.x), std::min(m_dragStartWorld.y, world.y));
        ImVec2 b(std::max(m_dragStartWorld.x, world.x), std::max(m_dragStartWorld.y, world.y));
        if (io.KeyShift && tool != Tool::Line)
        {
            float s = std::max(b.x - a.x, b.y - a.y);
            if (world.x < m_dragStartWorld.x) a.x = m_dragStartWorld.x - s; else b.x = a.x + s;
            if (world.y < m_dragStartWorld.y) a.y = m_dragStartWorld.y - s; else b.y = a.y + s;
        }
        Node* n = doc.Find(m_createdId);
        if (!n)
        {
            NodeType t = tool == Tool::Frame ? NodeType::Frame : tool == Tool::Rect ? NodeType::Rect : tool == Tool::Ellipse ? NodeType::Ellipse
                       : tool == Tool::Line ? NodeType::Line : NodeType::Text;
            Node* parent = ContainerAt(m_dragStartWorld);
            n = CreateNode(t, a, ImVec2(1, 1), parent);
            if (t == NodeType::Text) { n->textSizing = 2; }
            m_createdId = n->id;
            SetSelection({ n->id });
        }
        ImVec2 base = n->parent ? n->parent->AbsPos() : ImVec2(0, 0);
        if (n->type == NodeType::Line)
        {
            n->x = Snap(std::min(m_dragStartWorld.x, world.x) - base.x);
            n->y = Snap(m_dragStartWorld.y - base.y);
            n->w = std::max(1.0f, Snap(std::fabs(world.x - m_dragStartWorld.x)));
        }
        else
        {
            n->x = Snap(a.x - base.x); n->y = Snap(a.y - base.y);
            n->w = std::max(1.0f, Snap(b.x - a.x)); n->h = std::max(1.0f, Snap(b.y - a.y));
        }
        MarkDirty();
    }

    // Mouse up
    if (ImGui::IsMouseReleased(0))
    {
        if (m_drag == Drag::Pending && !m_pendingClickId.empty()) Select(m_pendingClickId);
        if (m_drag == Drag::Create)
        {
            Node* n = doc.Find(m_createdId);
            if (!n)
            {
                // Click without drag: default-size shape
                NodeType t = tool == Tool::Frame ? NodeType::Frame : tool == Tool::Rect ? NodeType::Rect : tool == Tool::Ellipse ? NodeType::Ellipse
                           : tool == Tool::Line ? NodeType::Line : NodeType::Text;
                Node* parent = ContainerAt(m_dragStartWorld);
                ImVec2 size = t == NodeType::Text ? ImVec2(10, 10) : t == NodeType::Line ? ImVec2(100, 0) : ImVec2(100, 100);
                n = CreateNode(t, m_dragStartWorld, size, parent);
                SetSelection({ n->id });
            }
            if (n->type == NodeType::Text) StartTextEdit(n);
            tool = Tool::Move;
        }
        if (m_drag == Drag::Move)
        {
            // Reparent / reorder on drop
            for (auto& [id, r] : m_dragOrig)
            {
                Node* n = doc.Find(id);
                if (!n) continue;
                Node* target = ContainerAt(world, n);
                if (target && (target == n || doc.IsAncestor(n, target))) target = nullptr;
                if (target == n->parent)
                {
                    if (target && target->layout)
                    {
                        // Reorder within auto layout by the drop position
                        bool horiz = target->layout == 1;
                        ImVec2 base = target->AbsPos();
                        float m = horiz ? world.x - base.x : world.y - base.y;
                        int idx = 0;
                        for (auto& c : target->children)
                        {
                            if (c.get() == n) continue;
                            float center = horiz ? c->x + c->w * 0.5f : c->y + c->h * 0.5f;
                            if (m > center) idx++;
                        }
                        auto u = doc.Detach(n);
                        doc.Insert(std::move(u), target, idx);
                    }
                    continue;
                }
                ImVec2 abs = n->AbsPos();
                auto u = doc.Detach(n);
                ImVec2 base = target ? target->AbsPos() : ImVec2(0, 0);
                u->x = Snap(abs.x - base.x);
                u->y = Snap(abs.y - base.y);
                doc.Insert(std::move(u), target);
            }
            MarkDirty();
        }
        if (m_drag == Drag::Marquee)
        {
            ImVec2 a(std::min(m_dragStartWorld.x, world.x), std::min(m_dragStartWorld.y, world.y));
            ImVec2 b(std::max(m_dragStartWorld.x, world.x), std::max(m_dragStartWorld.y, world.y));
            if (b.x - a.x > 1 || b.y - a.y > 1)
            {
                Node* scope = nullptr;
                for (int i = (int)doc.roots.size() - 1; i >= 0; i--)
                {
                    Node* r = doc.roots[i].get();
                    ImVec2 ra, rb;
                    NodeWorldRect(r, &vars, &ra, &rb);
                    if (r->type == NodeType::Frame && PointIn(m_dragStartWorld, ra, rb)) { scope = r; break; }
                }
                auto& list = scope ? scope->children : doc.roots;
                for (auto& c : list)
                {
                    if (!c->visible || c->locked || (!ignoreConditions && !NodeConditionVisible(c.get(), vars))) continue;
                    ImVec2 ca, cb;
                    NodeWorldRect(c.get(), &vars, &ca, &cb);
                    if (ca.x <= b.x && cb.x >= a.x && ca.y <= b.y && cb.y >= a.y && !IsSelected(c->id))
                        selection.push_back(c->id);
                }
            }
        }
        m_drag = Drag::None;
        m_guides.clear();
        m_dragOrig.clear();
    }
    (void)hovered;
}

// ---------------------------------------------------------------- overlay

void App::CanvasOverlay(ImDrawList* dl)
{
    ImFont* uiFont = fonts::UI(fonts::UI_Regular);
    ImFont* uiMed = fonts::UI(fonts::UI_Medium);
    float fs = 11.0f * dpi;

    // Frame labels
    for (auto& r : doc.roots)
    {
        ImVec2 la, lb;
        if (!r->visible || !LabelRect(this, r.get(), &la, &lb)) continue;
        bool sel = IsSelected(r->id), hov = m_hoverId == r->id;
        dl->PushClipRect(la, ImVec2(lb.x, lb.y + U(2)), true);
        dl->AddText(uiFont, fs, la, sel ? Accent : hov ? Text : TextDim, r->name.c_str());
        dl->PopClipRect();
    }

    // Hover outline
    if (Node* h = doc.Find(m_hoverId))
        if (!IsSelected(h->id) && m_drag == Drag::None)
        {
            ImVec2 a, b;
            NodeWorldRect(h, &vars, &a, &b);
            ImVec2 sa = WorldToScreen(a), sb = WorldToScreen(b);
            if (h->type == NodeType::Line) dl->AddLine(sa, ImVec2(sb.x, sa.y), Accent, 2.0f);
            else dl->AddRect(sa, sb, Accent, 0, 0, 2.0f);
        }

    // Auto layout hints: show children bounds of selected auto-layout frame
    auto sel = SelNodes();
    for (Node* n : sel)
    {
        ImVec2 a, b;
        NodeWorldRect(n, &vars, &a, &b);
        ImVec2 sa = WorldToScreen(a), sb = WorldToScreen(b);
        if (n->type == NodeType::Line) dl->AddLine(sa, ImVec2(sb.x, sa.y), Accent, 1.0f);
        else dl->AddRect(sa, sb, Accent, 0, 0, 1.0f);
        if (n->layout && sel.size() == 1)
            for (auto& c : n->children)
            {
                if (!c->visible) continue;
                ImVec2 ca, cb;
                NodeWorldRect(c.get(), &vars, &ca, &cb);
                dl->AddRect(WorldToScreen(ca), WorldToScreen(cb), IM_COL32(13, 153, 255, 70), 0, 0, 1.0f);
            }
    }

    ImVec2 wa, wb;
    if (SelectionBounds(this, &wa, &wb) && editingText.empty())
    {
        ImVec2 a = WorldToScreen(wa), b = WorldToScreen(wb);
        if (sel.size() > 1) dl->AddRect(a, b, Accent, 0, 0, 1.0f);
        bool locked = false;
        for (Node* n : sel) locked |= n->locked;
        bool line = sel.size() == 1 && sel[0]->type == NodeType::Line;
        if (!locked && m_drag != Drag::Move)
        {
            float hs = U(3.5f);
            auto handle = [&](ImVec2 p) {
                dl->AddRectFilled(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), IM_COL32_WHITE);
                dl->AddRect(ImVec2(p.x - hs, p.y - hs), ImVec2(p.x + hs, p.y + hs), Accent, 0, 0, 1.0f);
            };
            if (line) { handle(a); handle(ImVec2(b.x, a.y)); }
            else for (int h : { 0, 2, 4, 6 }) handle(HandlePos(h, a, b));
        }
        // Size pill
        char buf[64];
        snprintf(buf, sizeof(buf), "%g \xC3\x97 %g", std::round((wb.x - wa.x) * 100) / 100, std::round((wb.y - wa.y) * 100) / 100);
        ImVec2 ts = uiMed->CalcTextSizeA(fs, FLT_MAX, 0, buf);
        ImVec2 pc((a.x + b.x) * 0.5f, b.y + U(8));
        ImVec2 pa(pc.x - ts.x * 0.5f - U(4), pc.y), pb(pc.x + ts.x * 0.5f + U(4), pc.y + ts.y + U(4));
        dl->AddRectFilled(pa, pb, Accent, U(3));
        dl->AddText(uiMed, fs, ImVec2(pa.x + U(4), pa.y + U(2)), IM_COL32_WHITE, buf);
    }

    // Snap guides
    for (auto& g : m_guides)
    {
        if (g.vertical)
        {
            ImVec2 p0 = WorldToScreen(ImVec2(g.pos, g.from)), p1 = WorldToScreen(ImVec2(g.pos, g.to));
            dl->AddLine(p0, p1, theme::Guide, 1.0f);
        }
        else
        {
            ImVec2 p0 = WorldToScreen(ImVec2(g.from, g.pos)), p1 = WorldToScreen(ImVec2(g.to, g.pos));
            dl->AddLine(p0, p1, theme::Guide, 1.0f);
        }
    }

    // Marquee
    if (m_drag == Drag::Marquee)
    {
        ImVec2 s = WorldToScreen(m_dragStartWorld), e = ImGui::GetIO().MousePos;
        ImVec2 a(std::min(s.x, e.x), std::min(s.y, e.y)), b(std::max(s.x, e.x), std::max(s.y, e.y));
        dl->AddRectFilled(a, b, IM_COL32(13, 153, 255, 25));
        dl->AddRect(a, b, Accent, 0, 0, 1.0f);
    }
}

// ---------------------------------------------------------------- canvas

void App::DrawCanvas()
{
    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    canvasMin = ImGui::GetWindowPos();
    canvasMax = ImVec2(canvasMin.x + ImGui::GetWindowWidth(), canvasMin.y + ImGui::GetWindowHeight());
    ImVec2 size = canvasMax - canvasMin;

    dl->AddRectFilled(canvasMin, canvasMax, ImGui::ColorConvertFloat4ToU32(doc.canvasColor));

    ImGui::SetCursorScreenPos(canvasMin);
    ImGui::SetNextItemAllowOverlap();
    ImGui::InvisibleButton("##canvasbg", ImVec2(std::max(1.0f, size.x), std::max(1.0f, size.y)),
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    bool hovered = ImGui::IsItemHovered();

    // Wheel: pan / zoom (Ctrl)
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && (io.MouseWheel != 0 || io.MouseWheelH != 0) && !ImGui::IsAnyItemActive())
    {
        if (io.KeyCtrl) SetZoom(zoom * std::pow(1.18f, io.MouseWheel), io.MousePos);
        else if (io.KeyShift) pan.x += io.MouseWheel * U(80);
        else { pan.y += io.MouseWheel * U(80); pan.x += io.MouseWheelH * U(80); }
    }
    m_spaceHeld = ImGui::IsKeyDown(ImGuiKey_Space) && !io.WantTextInput;

    if (!preview && !MotionPathInput()) CanvasInput();
    else
    {
        // Preview still allows panning with middle mouse / space.
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (m_spaceHeld && ImGui::IsMouseClicked(0)))) m_drag = Drag::Pan;
        if (m_drag == Drag::Pan) { pan = pan + io.MouseDelta; if (!ImGui::IsMouseDown(0) && !ImGui::IsMouseDown(2)) m_drag = Drag::None; }
    }

    // Right click
    if (hovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !preview && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0)
    {
        std::string hit = HitTest(ScreenToWorld(io.MousePos), io.KeyCtrl);
        if (!hit.empty() && !IsSelected(hit)) Select(hit);
        openContextMenu = true;
    }

    dl->PushClipRect(canvasMin, canvasMax, true);
    RenderCtx ctx;
    ctx.dl = dl;
    ctx.origin = WorldToScreen(ImVec2(0, 0));
    ctx.zoom = zoom;
    ctx.interactive = preview;
    ctx.ignoreConditions = ignoreConditions && !preview;
    ctx.vars = &vars;
    ctx.doc = &doc;
    ctx.hideTextOf = editingText.empty() ? nullptr : &editingText;
    static bool lastPreview = false;
    if (preview != lastPreview) { animClocks.clear(); lastPreview = preview; }
    ctx.clocks = &animClocks;
    ctx.animate = preview;
    ctx.time = ImGui::GetTime();
    ctx.frame = ImGui::GetFrameCount();
    ctx.playRoot = (!preview && ctx.time < animPlayUntil) ? doc.Find(animPlayId) : nullptr;
    ctx.timeline = timelineOpen && !preview ? timelineTime : -1.0;
    // Pixel preview: show the page rendered at 1x (last frame), magnified without smoothing.
    bool usePixelPreview = pixelPreview && !preview && editingText.empty() && pixelPreviewTex && zoom > 1.0f;
    if (usePixelPreview)
    {
        dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
        dl->AddImage(pixelPreviewTex, WorldToScreen(pixelPreviewMin), WorldToScreen(pixelPreviewMax));
        dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
    }
    else
        RenderDocument(ctx);
    if (pixelPreview && !preview && zoom > 1.0f)
    {
        // Render the visible area at 1x after this frame for the next one.
        ImVec2 w0 = ScreenToWorld(canvasMin), w1 = ScreenToWorld(canvasMax);
        ImVec2 wa(std::floor(w0.x), std::floor(w0.y)), wb(std::ceil(w1.x), std::ceil(w1.y));
        if (wb.x - wa.x <= 4096 && wb.y - wa.y <= 4096)
            deferred.push_back([this, wa, wb] {
                RenderCtx rc;
                rc.zoom = 1.0f;
                rc.origin = ImVec2(-wa.x, -wa.y);
                rc.vars = &vars;
                rc.doc = &doc;
                ImTextureID t = gfx::RenderToTexture((int)(wb.x - wa.x), (int)(wb.y - wa.y), [&](ImDrawList* d) { rc.dl = d; RenderDocument(rc); }, ImVec4(doc.canvasColor.x, doc.canvasColor.y, doc.canvasColor.z, 1.0f));
                if (t) { pixelPreviewTex = t; pixelPreviewMin = wa; pixelPreviewMax = wb; }
            });
    }

    DrawMotionPath(dl);

    // Pixel grid (above the content, like design tools) from 400%
    if (pixelGrid && zoom >= kPixelGridZoom)
    {
        float fadeIn = std::clamp((zoom - kPixelGridZoom) / kPixelGridZoom + 0.35f, 0.0f, 1.0f);
        ImU32 gc = IM_COL32(150, 150, 150, (int)(55 * fadeIn));
        ImVec2 w0 = ScreenToWorld(canvasMin), w1 = ScreenToWorld(canvasMax);
        for (float x = std::floor(w0.x); x <= w1.x; x += 1)
        {
            float sx = std::floor(WorldToScreen(ImVec2(x, 0)).x) + 0.5f;
            dl->AddLine(ImVec2(sx, canvasMin.y), ImVec2(sx, canvasMax.y), gc);
        }
        for (float y = std::floor(w0.y); y <= w1.y; y += 1)
        {
            float sy = std::floor(WorldToScreen(ImVec2(0, y)).y) + 0.5f;
            dl->AddLine(ImVec2(canvasMin.x, sy), ImVec2(canvasMax.x, sy), gc);
        }
    }
    if (!preview) CanvasOverlay(dl);
    dl->PopClipRect();

    // Inline text editor
    if (Node* n = doc.Find(editingText))
    {
        ImVec2 a, b;
        NodeWorldRect(n, &vars, &a, &b);
        ImVec2 sa = WorldToScreen(a), sb = WorldToScreen(b);
        ImFont* font = fonts::Get(n->fontFamily, n->fontStyle);
        float fsz = n->fontSize * zoom;
        ImGui::PushFont(font, fsz / std::max(0.01f, ImGui::GetStyle().FontScaleDpi));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertFloat4ToU32(n->fill.c0));
        ImGui::SetCursorScreenPos(sa);
        if (textEditFocus) { ImGui::SetKeyboardFocusHere(); textEditFocus = false; }
        ImVec2 boxSize(std::max(sb.x - sa.x, U(40)) + fsz, std::max(sb.y - sa.y, fsz * 1.3f) + fsz * 1.3f);
        ImGui::InputTextMultiline("##textedit", textBuf, sizeof(textBuf), boxSize, ImGuiInputTextFlags_NoHorizontalScroll);
        bool deactivated = ImGui::IsItemDeactivated();
        n->text = textBuf;
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        dl->AddRect(sa, sb, Accent, 0, 0, 1.0f);
        if (deactivated || ImGui::IsKeyPressed(ImGuiKey_Escape)) CommitTextEdit();
    }

    // Empty-state hint
    if (doc.roots.empty())
    {
        const char* l1 = "Draw a frame (F) or ask AI to build a menu";
        const char* l2 = "Paste SVG / PNG / CSS with Ctrl+V, drop images, fonts or .fimgui files";
        ImFont* f = fonts::UI(fonts::UI_Medium);
        float s1 = 15 * dpi, s2 = 12 * dpi;
        ImVec2 c = (canvasMin + canvasMax);
        c = ImVec2(c.x * 0.5f, c.y * 0.5f);
        ImVec2 t1 = f->CalcTextSizeA(s1, FLT_MAX, 0, l1), t2 = f->CalcTextSizeA(s2, FLT_MAX, 0, l2);
        DrawIcon(dl, Icon::Sparkle, ImVec2(c.x, c.y - U(46)), U(28), Accent);
        dl->AddText(f, s1, ImVec2(c.x - t1.x * 0.5f, c.y - U(20)), Text, l1);
        dl->AddText(f, s2, ImVec2(c.x - t2.x * 0.5f, c.y + U(4)), TextDim, l2);
    }

    // Preview banner
    if (preview)
    {
        const char* t = "Preview - interact with your menu. Esc to exit";
        ImFont* f = fonts::UI(fonts::UI_Medium);
        float s = 12 * dpi;
        ImVec2 ts = f->CalcTextSizeA(s, FLT_MAX, 0, t);
        ImVec2 a(canvasMin.x + (size.x - ts.x) * 0.5f - U(12), canvasMin.y + U(12));
        dl->AddRectFilled(a, ImVec2(a.x + ts.x + U(24), a.y + ts.y + U(12)), IM_COL32(151, 71, 255, 230), U(8));
        dl->AddText(f, s, ImVec2(a.x + U(12), a.y + U(6)), IM_COL32_WHITE, t);
    }

    DrawContextMenu();
}

void App::DrawContextMenu()
{
    if (openContextMenu) { ImGui::OpenPopup("##canvasctx"); openContextMenu = false; }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(U(6), U(6)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(U(8), U(4)));
    if (ImGui::BeginPopup("##canvasctx"))
    {
        bool has = !selection.empty();
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, has)) CopySelection();
        if (ImGui::MenuItem("Cut", "Ctrl+X", false, has)) CutSelection();
        if (ImGui::MenuItem("Paste", "Ctrl+V")) Paste();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, has)) DuplicateSelection();
        if (ImGui::MenuItem("Delete", "Del", false, has)) DeleteSelection();
        ImGui::Separator();
        if (ImGui::BeginMenu("Copy as", has))
        {
            if (ImGui::MenuItem("Copy as CSS")) CopyAs(0);
            if (ImGui::MenuItem("Copy as SVG")) CopyAs(1);
            if (ImGui::MenuItem("Copy as PNG", "Ctrl+Shift+C")) CopyAs(2);
            if (ImGui::MenuItem("Copy as ImGui code")) CopyAs(3);
            if (ImGui::MenuItem("Copy as JSON")) CopyAs(4);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Bring to front", "Ctrl+Shift+]", false, has)) ReorderSelection(100);
        if (ImGui::MenuItem("Bring forward", "Ctrl+]", false, has)) ReorderSelection(1);
        if (ImGui::MenuItem("Send backward", "Ctrl+[", false, has)) ReorderSelection(-1);
        if (ImGui::MenuItem("Send to back", "Ctrl+Shift+[", false, has)) ReorderSelection(-100);
        ImGui::Separator();
        if (ImGui::MenuItem("Group selection", "Ctrl+G", false, has)) GroupSelection(false);
        if (ImGui::MenuItem("Frame selection", "Ctrl+Alt+G", false, has)) GroupSelection(true);
        if (ImGui::MenuItem("Ungroup", "Ctrl+Shift+G", false, has)) Ungroup();
        ImGui::Separator();
        if (ImGui::MenuItem("Show/Hide", "Ctrl+Shift+H", false, has)) { for (Node* n : SelNodes()) n->visible = !n->visible; MarkDirty(); }
        if (ImGui::MenuItem("Lock/Unlock", "Ctrl+Shift+L", false, has)) { for (Node* n : SelNodes()) n->locked = !n->locked; MarkDirty(); }
        ImGui::Separator();
        if (ImGui::MenuItem("Export PNG @2x...", nullptr, false, has)) ExportSelectionFile(0, 2.0f);
        if (ImGui::MenuItem("Export SVG...", nullptr, false, has)) ExportSelectionFile(1, 1.0f);
        ImGui::Separator();
        if (ImGui::MenuItem("Ask AI about selection...", nullptr, false, has))
        {
            rightTab = Tab_AI;
            Node* n = Sel();
            snprintf(aiInput, sizeof(aiInput), "Improve \"%s\": ", n ? n->name.c_str() : "selection");
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}
