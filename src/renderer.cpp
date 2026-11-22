#include "renderer.h"
#include "fonts.h"
#include "gfx.h"
#include "../runtime/fimgui_runtime.h"
#include "../runtime/fimgui_fx.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>
#include <set>

static ImU32 U32(ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); }

static fim::Paint ToPaint(const Paint& p)
{
    fim::Paint o;
    o.type = p.type;
    o.c0 = U32(p.c0);
    o.c1 = U32(p.c1);
    o.angle = p.angle;
    return o;
}

std::vector<VarInfo> CollectVars(Document& doc)
{
    std::vector<VarInfo> out;
    std::set<std::string> seen;
    auto add = [&](const std::string& name, VarKind k, float def, bool strong) {
        if (name.empty()) return;
        for (auto& v : out)
            if (v.name == name)
            {
                if (strong && (v.kind == VarKind::Float || (v.kind == VarKind::Int && k == VarKind::Mask)) && k != VarKind::Float) { v.kind = k; v.def = def; }
                return;
            }
        out.push_back({ name, k, def });
    };
    doc.ForEach([&](Node* n) {
        switch (n->role)
        {
        case Role::Checkbox: add(n->var, VarKind::Bool, n->value != 0 ? 1.0f : 0.0f, true); break;
        case Role::Slider:   add(n->var, VarKind::Float, std::clamp(n->value, std::min(n->vmin, n->vmax), std::max(n->vmin, n->vmax)), true); break;
        case Role::Tab:
        case Role::Button:   add(n->var, VarKind::Int, 0.0f, true); break;
        case Role::Input:    add(n->var, VarKind::String, 0.0f, true); break;
        case Role::Combo:    add(n->var, VarKind::Int, std::clamp(std::round(n->value), 0.0f, std::max(0.0f, (float)n->options.size() - 1)), true); break;
        case Role::MultiCombo: add(n->var, VarKind::Mask, (float)((unsigned)n->value & (n->options.size() >= 32 ? 0xFFFFFFFFu : ((1u << n->options.size()) - 1u))), true); break;
        default: break;
        }
    });
    doc.ForEach([&](Node* n) {
        if (!n->showVar.empty()) add(n->showVar, VarKind::Int, 0.0f, false);
        if (n->type == NodeType::Text)
        {
            size_t p = 0;
            while ((p = n->text.find('{', p)) != std::string::npos)
            {
                size_t e = n->text.find('}', p);
                if (e == std::string::npos) break;
                std::string inner = n->text.substr(p + 1, e - p - 1);
                std::string name = inner.substr(0, inner.find(':'));
                bool ident = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return isalnum((unsigned char)c) || c == '_'; });
                if (ident) add(name, VarKind::Float, 0.0f, false);
                p = e + 1;
            }
        }
    });
    return out;
}

void EnsureVars(Document& doc, VarStore& vars)
{
    vars.combos.clear();
    doc.ForEach([&](Node* n) {
        if ((n->role == Role::Combo || n->role == Role::MultiCombo) && !n->var.empty() && !vars.combos.count(n->var))
            vars.combos[n->var] = { n->options, n->role == Role::MultiCombo, n->noneLabel, n->allLabel };
    });
    for (auto& v : CollectVars(doc))
    {
        if (v.kind == VarKind::String) { if (!vars.str.count(v.name)) vars.str[v.name] = ""; continue; }
        auto it = vars.num.find(v.name);
        auto d = vars.defaults.find(v.name);
        // Follow default changes (e.g. a control created after a text that references it) unless the value was edited.
        if (it == vars.num.end() || (d != vars.defaults.end() && d->second != v.def && std::fabs(it->second - d->second) < 1e-6f))
            vars.num[v.name] = v.def;
        vars.defaults[v.name] = v.def;
    }
}

bool NodeConditionVisible(const Node* n, const VarStore& vars)
{
    if (n->showVar.empty()) return true;
    bool eq = std::fabs(vars.Get(n->showVar) - n->showValue) < 1e-4f;
    return n->showNot ? !eq : eq;
}

std::string ExpandText(const std::string& text, const VarStore& vars)
{
    if (text.find('{') == std::string::npos) return text;
    std::string out;
    size_t i = 0;
    while (i < text.size())
    {
        size_t p = text.find('{', i);
        if (p == std::string::npos) { out += text.substr(i); break; }
        size_t e = text.find('}', p);
        if (e == std::string::npos) { out += text.substr(i); break; }
        out += text.substr(i, p - i);
        std::string inner = text.substr(p + 1, e - p - 1);
        size_t colon = inner.find(':');
        std::string name = inner.substr(0, colon);
        std::string fmt = colon == std::string::npos ? "" : inner.substr(colon + 1);
        auto sit = vars.str.find(name);
        auto cit = vars.combos.find(name);
        if (fmt == "label" && cit != vars.combos.end())
        {
            const auto& c = cit->second;
            unsigned v = (unsigned)vars.Get(name);
            if (!c.multi) { if (v < c.options.size()) out += c.options[v]; }
            else
            {
                int count = 0, total = (int)std::min<size_t>(c.options.size(), 32);
                for (int k = 0; k < total; k++) if (v & (1u << k)) count++;
                if (count == 0) out += c.none;
                else if (count == total && total > 1 && !c.all.empty()) out += c.all;
                else
                {
                    bool first = true;
                    for (int k = 0; k < total; k++)
                        if (v & (1u << k)) { if (!first) out += ", "; out += c.options[k]; first = false; }
                }
            }
        }
        else if (sit != vars.str.end()) out += sit->second;
        else if (vars.num.count(name))
        {
            char buf[64];
            float v = vars.Get(name);
            if (fmt.empty()) snprintf(buf, sizeof(buf), v == std::floor(v) ? "%.0f" : "%.2f", v);
            else if (fmt.find('d') != std::string::npos || fmt.find('i') != std::string::npos) snprintf(buf, sizeof(buf), fmt.c_str(), (int)v);
            else snprintf(buf, sizeof(buf), fmt.c_str(), v);
            out += buf;
        }
        else out += text.substr(p, e - p + 1);
        i = e + 1;
    }
    return out;
}

Node* SliderOwner(Node* n)
{
    for (Node* q = n->parent; q; q = q->parent)
        if (q->role == Role::Slider) return q;
    return nullptr;
}

static float SliderT(Node* slider, const VarStore* vars)
{
    if (!vars || slider->vmax == slider->vmin) return 0.5f;
    float v = vars->Get(slider->var, slider->value);
    return std::clamp((v - slider->vmin) / (slider->vmax - slider->vmin), 0.0f, 1.0f);
}

// Local rect adjusted for slider parts (relative to parent).
static void LocalRect(Node* n, const VarStore* vars, float* x, float* y, float* w, float* h)
{
    *x = n->x; *y = n->y; *w = n->w; *h = n->h;
    if (!n->sliderPart) return;
    Node* s = SliderOwner(n);
    if (!s) return;
    float t = SliderT(s, vars);
    // Offset of the slider relative to n's parent.
    float ox = 0;
    for (Node* q = n->parent; q && q != s; q = q->parent) ox += q->x;
    float sw = s->w;
    if (n->sliderPart == 1) { *x = -ox; *w = std::round(sw * t); }
    else *x = std::round(-ox + sw * t - n->w * 0.5f);
}

void NodeWorldRect(Node* n, const VarStore* vars, ImVec2* a, ImVec2* b)
{
    float x, y, w, h;
    LocalRect(n, vars, &x, &y, &w, &h);
    ImVec2 p = n->parent ? n->parent->AbsPos() : ImVec2(0, 0);
    *a = ImVec2(p.x + x, p.y + y);
    *b = ImVec2(a->x + w, a->y + h);
}

fim::ComboStyle ComboStyleFor(const Node* n, float z)
{
    fim::ComboStyle cs;
    const Node* label = nullptr;
    for (auto& c : n->children) if (c->type == NodeType::Text) { label = c.get(); break; }
    cs.font = label ? fonts::Get(label->fontFamily, label->fontStyle) : fonts::Get("Inter", "Regular");
    cs.fontSize = (label ? label->fontSize : 13.0f) * z;
    cs.text = ImGui::ColorConvertFloat4ToU32(label ? label->fill.c0 : ImVec4(0.9f, 0.9f, 0.93f, 1));
    cs.bg = ImGui::ColorConvertFloat4ToU32(n->listFill);
    cs.hover = n->hasHover ? ImGui::ColorConvertFloat4ToU32(ImVec4(n->hoverColor.x, n->hoverColor.y, n->hoverColor.z, std::max(0.07f, std::min(n->hoverColor.w, 0.25f))))
                           : IM_COL32(255, 255, 255, 18);
    cs.accent = ImGui::ColorConvertFloat4ToU32(n->listAccent);
    cs.border = n->stroke.type ? ImGui::ColorConvertFloat4ToU32(n->stroke.c0) : IM_COL32(255, 255, 255, 24);
    cs.rounding = n->radius[0] * z;
    cs.itemHeight = (n->itemHeight > 0 ? n->itemHeight : n->h) * z;
    cs.padX = (label ? label->x : 12.0f) * z;
    cs.gap = 4.0f * z;
    cs.allLabel = n->role == Role::MultiCombo ? n->allLabel.c_str() : nullptr;
    return cs;
}

static bool StateActive(Node* n, const VarStore* vars)
{
    if (!vars || n->var.empty()) return false;
    float v = vars->Get(n->var);
    switch (n->role)
    {
    case Role::Checkbox: return v != 0.0f;
    case Role::Tab:
    case Role::Button:   return std::fabs(v - n->value) < 1e-4f && n->role == Role::Tab;
    default: return false;
    }
}

fim::AnimSpec ToAnimSpec(const AnimFx& a, float extraDelay)
{
    fim::AnimSpec s;
    s.preset = a.preset;
    s.trigger = a.trigger;
    s.easing = a.easing;
    s.duration = a.duration;
    s.delay = a.delay + extraDelay;
    s.distance = a.distance;
    s.scale = a.scale;
    s.intensity = a.intensity;
    for (int i = 0; i < 4; i++) s.bezier[i] = a.bezier[i];
    return s;
}

static bool CanHaveBackdrop(const Node* n) { return n->type == NodeType::Frame || n->type == NodeType::Rect || n->type == NodeType::Ellipse; }

static void RenderBody(RenderCtx& ctx, Node* n, ImVec2 world, float lw, float lh, float parentAlpha, ImVec2 ia, ImVec2 ib);

void RenderNode(RenderCtx& ctx, Node* n, ImVec2 parentWorld, float parentAlpha, const AnimFx* inherited, float extraDelay)
{
    if (!n->visible) return;
    if (!ctx.ignoreConditions && ctx.vars && !NodeConditionVisible(n, *ctx.vars)) return;

    float lx, ly, lw, lh;
    LocalRect(n, ctx.vars, &lx, &ly, &lw, &lh);
    ImVec2 world(parentWorld.x + lx, parentWorld.y + ly);
    float z = ctx.zoom;
    ImVec2 a(ctx.origin.x + world.x * z, ctx.origin.y + world.y * z);
    ImVec2 b(a.x + lw * z, a.y + lh * z);

    // A container with "stagger" animates its children instead of itself.
    const AnimFx* spec = (n->anim.preset && n->anim.stagger <= 0.0f) ? &n->anim : (n->anim.preset ? nullptr : inherited);
    float delay = spec == inherited ? extraDelay : 0.0f;
    bool run = false;
    if (spec && ctx.clocks)
    {
        run = ctx.animate;
        for (const Node* q = n; q && !run; q = q->parent) run = q == ctx.playRoot;
    }
    if (!run)
    {
        RenderBody(ctx, n, world, lw, lh, parentAlpha, a, b);
        return;
    }

    fim::AnimSpec s = ToAnimSpec(*spec, delay);
    AnimClock& c = (*ctx.clocks)[n->id];
    if (c.lastFrame < ctx.frame - 1) { c.start = ctx.time; c.progress = 0; }
    c.lastFrame = ctx.frame;
    fim::AnimValue v;
    if (fim::AnimCategory(s.preset) == 2)
    {
        bool hovered = ctx.interactive && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(a, b);
        bool on = s.trigger == fim::Trigger_Press ? (hovered && ImGui::IsMouseDown(0)) : s.trigger == fim::Trigger_Active ? StateActive(n, ctx.vars) : hovered;
        if (!ctx.interactive) on = std::fmod(ctx.time - c.start, 1.6) < 0.8;   // editor "Play": toggle the state
        float step = ImGui::GetIO().DeltaTime / std::max(0.01f, s.duration);
        c.progress = on ? std::min(1.0f, c.progress + step) : std::max(0.0f, c.progress - step);
        v = fim::AnimSample(s, 0, c.progress);
    }
    else v = fim::AnimSample(s, (float)(ctx.time - c.start), 0);

    if (v.dx == 0 && v.dy == 0 && v.scale == 1 && v.alpha == 1)
    {
        RenderBody(ctx, n, world, lw, lh, parentAlpha, a, b);
        return;
    }
    // Scale the whole subtree around the node center and offset it.
    RenderCtx sub = ctx;
    ImVec2 wc(world.x + lw * 0.5f, world.y + lh * 0.5f);
    ImVec2 sc(ctx.origin.x + wc.x * z + v.dx * z, ctx.origin.y + wc.y * z + v.dy * z);
    sub.zoom = z * v.scale;
    sub.origin = ImVec2(sc.x - wc.x * sub.zoom, sc.y - wc.y * sub.zoom);
    RenderBody(sub, n, world, lw, lh, parentAlpha * v.alpha, a, b);
}

static void RenderBody(RenderCtx& ctx, Node* n, ImVec2 world, float lw, float lh, float parentAlpha, ImVec2 ia, ImVec2 ib)
{
    float z = ctx.zoom;
    ImVec2 a(ctx.origin.x + world.x * z, ctx.origin.y + world.y * z);
    ImVec2 b(a.x + lw * z, a.y + lh * z);
    float alpha = parentAlpha * n->opacity;
    ImDrawList* dl = ctx.dl;
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = n->radius[i] * z;

    // Interaction (preview)
    bool hovered = false, active = StateActive(n, ctx.vars);
    if (ctx.interactive && ctx.vars && n->role != Role::None)
    {
        ImGui::PushID(n->id.c_str());
        VarStore& vs = *ctx.vars;
        if (n->role == Role::Slider)
        {
            float v = vs.Get(n->var, n->value);
            fim::ItemState st;
            fim::SliderBehavior("##s", ia, ib, &v, n->vmin, n->vmax, &st);
            if (!n->var.empty()) vs.num[n->var] = v;
            hovered = st.hovered || st.held;
            active = st.held;
        }
        else if (n->role == Role::Combo || n->role == Role::MultiCombo)
        {
            fim::ComboStyle cs = ComboStyleFor(n, z);
            std::vector<const char*> items;
            for (auto& o : n->options) items.push_back(o.c_str());
            fim::ItemState st;
            if (n->role == Role::Combo)
            {
                int idx = (int)vs.Get(n->var, n->value);
                if (fim::Combo("##c", ia, ib, &idx, items.data(), (int)items.size(), cs, &st)) vs.num[n->var] = (float)idx;
            }
            else
            {
                unsigned m = (unsigned)vs.Get(n->var, n->value);
                if (fim::MultiCombo("##c", ia, ib, &m, items.data(), (int)items.size(), cs, &st)) vs.num[n->var] = (float)m;
            }
            hovered = st.hovered;
            active = st.held;
        }
        else if (n->role == Role::Input)
        {
            // The text field itself is submitted after the node visuals (see below).
            active = ImGui::GetActiveID() == ImGui::GetID("##i");
            hovered = ImGui::IsMouseHoveringRect(ia, ib);
        }
        else
        {
            fim::ItemState st = fim::Item("##b", ia, ib);
            hovered = st.hovered;
            if (st.clicked && !n->var.empty())
            {
                if (n->role == Role::Checkbox) vs.num[n->var] = vs.Get(n->var) != 0.0f ? 0.0f : 1.0f;
                else vs.num[n->var] = n->value;
            }
            active = StateActive(n, ctx.vars) || (st.held && n->role == Role::Button);
        }
        ImGui::PopID();
    }

    Paint fill = n->fill;
    if (n->role != Role::None && (n->hasHover || n->hasActive))
    {
        // Smoothly blend toward hover/active colors (mirrors generated code).
        float th = 0.0f, ta = 0.0f;
        if (ctx.interactive)
        {
            ImGui::PushID(n->id.c_str());
            th = fim::Animate("h", hovered ? 1.0f : 0.0f);
            ta = fim::Animate("a", active ? 1.0f : 0.0f);
            ImGui::PopID();
        }
        else ta = active ? 1.0f : 0.0f;
        if (fill.type == 0) { fill.type = 1; fill.c0 = ImVec4(fill.c0.x, fill.c0.y, fill.c0.z, 0.0f); }
        ImVec4 base = fill.c0;
        if (n->hasHover) base = ImLerp(base, n->hoverColor, th);
        if (n->hasActive) base = ImLerp(base, n->activeColor, ta);
        if (fill.type == 2 && (th > 0 || ta > 0)) fill.type = 1;
        fill.c0 = base;
    }

    fim::Shadow sh;
    sh.type = n->effect.type;
    sh.offset = n->effect.offset;
    sh.blur = n->effect.blur;
    sh.spread = n->effect.spread;
    sh.color = U32(n->effect.color);

    bool ellipse = n->type == NodeType::Ellipse;
    float er[4];
    for (int i = 0; i < 4; i++) er[i] = ellipse ? std::min(b.x - a.x, b.y - a.y) * 0.5f : r[i];
    auto backdrop = [&]() {
        if (!CanHaveBackdrop(n) || (!n->glass.on && !n->blur.on)) return;
        fim::fx::Backdrop p;
        if (n->glass.on)
        {
            const GlassFx& g = n->glass;
            p.mode = fim::fx::Backdrop_Glass;
            p.blur = g.blur * z;
            p.refraction = g.refraction;
            p.thickness = g.thickness * z;
            p.highlight = g.highlight;
            p.dispersion = g.dispersion;
            p.lightAngle = g.lightAngle;
            p.saturation = g.saturation;
            p.tint = U32(g.tint);
        }
        else
            p.blur = n->blur.radius * z;
        fim::fx::DrawBackdrop(dl, a, b, r, p, alpha, ellipse);
    };
    auto textureOverlay = [&]() {
        if (!n->texture.on || !ctx.doc) return;
        auto it = ctx.doc->assets.find(n->texture.asset);
        if (it == ctx.doc->assets.end()) return;
        const Asset& as = it->second;
        gfx::TexInfo t = gfx::AssetTexture(as, lw * z, lh * z, false);
        if (!t.id) return;
        const TextureFx& tx = n->texture;
        ImVec2 isz((float)std::max(1, as.width ? as.width : t.w), (float)std::max(1, as.height ? as.height : t.h));
        ImVec2 ta = a, tb = b, uv0(0, 0), uv1(1, 1);
        bool tile = tx.fit == 2;
        if (tx.fit == 0) fim::CoverUV(isz, ImVec2(lw, lh), &uv0, &uv1);
        else if (tx.fit == 1) fim::ContainRect(isz, &ta, &tb);
        else
        {
            float tw = isz.x * std::max(0.01f, tx.scale), th = isz.y * std::max(0.01f, tx.scale);
            uv0 = ImVec2((float)std::fmod(ctx.time * tx.speed.x / tw, 1.0), (float)std::fmod(ctx.time * tx.speed.y / th, 1.0));
            uv1 = ImVec2(uv0.x + lw / tw, uv0.y + lh / th);
        }
        fim::fx::DrawTexture(dl, (ImTextureID)t.id, ta, tb, tx.fit == 1 ? nullptr : er, uv0, uv1, IM_COL32(255, 255, 255, (int)(255 * tx.opacity)), tx.blend, tile, alpha);
    };

    switch (n->type)
    {
    case NodeType::Frame:
    case NodeType::Rect:
    case NodeType::Group:
    {
        if (sh.type == fim::Shadow_Drop) fim::DrawShadow(dl, a, b, r, sh, z, alpha);
        backdrop();
        if (n->type != NodeType::Group) fim::FillRect(dl, a, b, r, ToPaint(fill), alpha);
        textureOverlay();
        if (sh.type == fim::Shadow_Inner) fim::DrawShadow(dl, a, b, r, sh, z, alpha);
        break;
    }
    case NodeType::Ellipse:
        if (sh.type == fim::Shadow_Drop) fim::DrawShadow(dl, a, b, r, sh, z, alpha, true);
        backdrop();
        fim::FillEllipse(dl, a, b, ToPaint(fill), alpha);
        textureOverlay();
        if (sh.type == fim::Shadow_Inner) fim::DrawShadow(dl, a, b, r, sh, z, alpha, true);
        break;
    case NodeType::Line:
        fim::DrawLine(dl, a, ImVec2(b.x, a.y), U32(n->stroke.c0), std::max(0.5f, n->strokeWidth * z), alpha);
        break;
    case NodeType::Text:
        if (!(ctx.hideTextOf && *ctx.hideTextOf == n->id))
        {
            ImFont* font = fonts::Get(n->fontFamily, n->fontStyle);
            std::string t = ctx.vars ? ExpandText(n->text, *ctx.vars) : n->text;
            fim::DrawText(dl, font, n->fontSize * z, a, b, t.c_str(), U32(fill.c0), n->alignH, n->alignV, n->lineHeight * z, n->textSizing != 1, alpha);
        }
        break;
    case NodeType::Image:
    case NodeType::Vector:
    {
        const Asset* as = nullptr;
        if (ctx.doc)
        {
            auto it = ctx.doc->assets.find(n->asset);
            if (it != ctx.doc->assets.end()) as = &it->second;
        }
        if (as)
        {
            if (sh.type == fim::Shadow_Drop) fim::DrawShadow(dl, a, b, r, sh, z, alpha);
            bool recolor = n->type == NodeType::Vector && n->recolor;
            gfx::TexInfo t = gfx::AssetTexture(*as, lw * z, lh * z, recolor);
            if (t.id)
            {
                ImVec2 uv0(0, 0), uv1(1, 1), qa = a, qb = b;
                ImVec2 isz((float)(as->width ? as->width : t.w), (float)(as->height ? as->height : t.h));
                if (n->type == NodeType::Vector || n->imageFit == 1) fim::ContainRect(isz, &qa, &qb);
                else if (n->imageFit == 0) fim::CoverUV(isz, ImVec2(lw, lh), &uv0, &uv1);
                ImU32 tint = recolor ? U32(fill.type ? fill.c0 : ImVec4(1, 1, 1, 1)) : IM_COL32_WHITE;
                fim::DrawImage(dl, (ImTextureID)t.id, qa, qb, n->type == NodeType::Image ? r : nullptr, uv0, uv1, tint, alpha);
            }
            if (n->type == NodeType::Image) textureOverlay();
        }
        else
        {
            dl->AddRectFilled(a, b, IM_COL32(128, 128, 128, 60));
            dl->AddLine(a, b, IM_COL32(128, 128, 128, 160));
            dl->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, a.y), IM_COL32(128, 128, 128, 160));
        }
        break;
    }
    }

    if (!n->children.empty())
    {
        bool clip = n->type == NodeType::Frame && n->clip;
        if (clip) dl->PushClipRect(a, b, true);
        // Input placeholder text hides once the field has content or focus.
        bool hidePlaceholder = n->role == Role::Input && ctx.interactive && ctx.vars && (active || !ctx.vars->str[n->var].empty());
        bool stagger = n->anim.preset && n->anim.stagger > 0.0f;
        for (size_t i = 0; i < n->children.size(); i++)
        {
            Node* c = n->children[i].get();
            if (hidePlaceholder && c->type == NodeType::Text) continue;
            if (stagger) RenderNode(ctx, c, world, alpha, &n->anim, n->anim.stagger * (float)i);
            else RenderNode(ctx, c, world, alpha);
        }
        if (clip) dl->PopClipRect();
    }

    if (n->stroke.type && n->type != NodeType::Line)
    {
        if (n->type == NodeType::Ellipse) fim::StrokeEllipse(dl, a, b, U32(n->stroke.c0), n->strokeWidth * z, alpha);
        else fim::StrokeRect(dl, a, b, r, U32(n->stroke.c0), n->strokeWidth * z, n->strokeAlign, alpha);
    }

    if (n->role == Role::Input && ctx.interactive && ctx.vars)
    {
        ImGui::PushID(n->id.c_str());
        std::string& s = ctx.vars->str[n->var];
        char buf[256];
        snprintf(buf, sizeof(buf), "%s", s.c_str());
        Node* label = nullptr;
        for (auto& c : n->children) if (c->type == NodeType::Text) { label = c.get(); break; }
        ImFont* font = label ? fonts::Get(label->fontFamily, label->fontStyle) : fonts::UI(fonts::UI_Regular);
        float fs = (label ? label->fontSize : 13.0f) * z;
        ImU32 col = label ? U32(label->fill.c0) : IM_COL32_WHITE;
        float padx = label ? label->x * z : 8 * z;
        if (fim::InputBehavior("##i", a, b, buf, sizeof(buf), font, fs, col, padx)) s = buf;
        ImGui::PopID();
    }
}

void RenderDocument(RenderCtx& ctx)
{
    for (auto& n : ctx.doc->roots) RenderNode(ctx, n.get(), ImVec2(0, 0));
}
