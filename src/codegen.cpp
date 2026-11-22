#include "codegen.h"
#include "renderer.h"
#include "exporters.h"
#include "fonts.h"
#include "gfx.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace fs = std::filesystem;

static std::string F(float v)
{
    char b[32];
    if (std::fabs(v - std::round(v)) < 1e-4f) snprintf(b, sizeof(b), "%d.0f", (int)std::round(v));
    else
    {
        snprintf(b, sizeof(b), "%.3f", v);
        std::string s = b;
        while (s.back() == '0') s.pop_back();
        return s + "f";
    }
    return b;
}

static std::string Col(ImVec4 c)
{
    auto b = [](float f) { return (int)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
    char buf[64];
    snprintf(buf, sizeof(buf), "IM_COL32(%d, %d, %d, %d)", b(c.x), b(c.y), b(c.z), b(c.w));
    return buf;
}

// C string literal; non-ASCII bytes as octal escapes (safe against greedy hex escapes).
static std::string CStr(const std::string& s)
{
    std::string o = "\"";
    char buf[8];
    for (unsigned char c : s)
    {
        if (c == '"') o += "\\\"";
        else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n";
        else if (c == '\r') continue;
        else if (c == '\t') o += "\\t";
        else if (c == '?') o += "\\?";
        else if (c < 0x20 || c >= 0x7F) { snprintf(buf, sizeof(buf), "\\%03o", c); o += buf; }
        else o += (char)c;
    }
    return o + "\"";
}

static std::string VideoExt(const std::vector<unsigned char>& d)
{
    if (d.size() > 12 && !memcmp(d.data() + 4, "ftyp", 4)) return (d.size() > 11 && !memcmp(d.data() + 8, "qt", 2)) ? ".mov" : ".mp4";
    if (d.size() > 4 && d[0] == 0x30 && d[1] == 0x26) return ".wmv";
    if (d.size() > 4 && !memcmp(d.data(), "RIFF", 4)) return ".avi";
    return ".mp4";
}

struct FontRef { std::string family, style, path, symbol, file; };
struct TexRef { std::string asset, symbol; bool recolor = false; float maxW = 0, maxH = 0; int kind = 0; };

// Alpha as "variable * constant" so constant opacities fold at export time.
struct AlphaE
{
    std::string var;
    float k = 1.0f;
    std::string str() const { return var.empty() ? F(k) : (k == 1.0f ? var : var + " * " + F(k)); }
};

struct Gen
{
    Document& doc;
    bool snippet = false;
    bool usesFx = false;
    std::map<std::string, VarInfo> vars;
    std::map<std::string, std::string> varIdent;
    std::vector<FontRef> fontRefs;
    std::vector<TexRef> texRefs;
    std::vector<std::string> pressed;            // button pressed flags
    std::unordered_map<const Node*, std::string> nodeSym;
    std::map<std::string, const Node*> comboOptions;   // var -> combo node that defines its labels
    std::set<std::string> usedSyms;
    std::ostringstream o;
    int ind = 1;
    int counter = 0;

    explicit Gen(Document& d) : doc(d) {}

    void L(const std::string& s) { o << std::string(ind * 4, ' ') << s << "\n"; }

    std::string Unique(const std::string& base)
    {
        std::string s = base;
        int i = 2;
        while (usedSyms.count(s)) s = base + "_" + std::to_string(i++);
        usedSyms.insert(s);
        return s;
    }

    std::string FontSym(const std::string& family, const std::string& style)
    {
        const FontFace* face = fonts::Find(family, style);
        std::string path = face ? face->path : "";
        for (auto& f : fontRefs) if (f.path == path) return f.symbol;
        FontRef r;
        r.family = face ? face->family : family;
        r.style = face ? face->style : style;
        r.path = path;
        r.symbol = Unique("F_" + SanitizeIdent(r.family + "_" + r.style, "font"));
        r.file = path.empty() ? "" : fs::u8path(path).filename().u8string();
        fontRefs.push_back(r);
        return r.symbol;
    }

    // Expression yielding the ImTextureID of an asset at draw time (static texture, GIF frame or video frame).
    std::string TexExpr(const std::string& asset, bool recolor, float w, float h)
    {
        auto it = doc.assets.find(asset);
        int kind = it != doc.assets.end() ? it->second.kind : 0;
        if (kind >= 2) recolor = false;
        TexRef* ref = nullptr;
        for (auto& t : texRefs)
            if (t.asset == asset && t.recolor == recolor) { ref = &t; t.maxW = std::max(t.maxW, w); t.maxH = std::max(t.maxH, h); }
        if (!ref)
        {
            TexRef t;
            t.asset = asset;
            t.recolor = recolor;
            t.maxW = w; t.maxH = h;
            t.kind = kind;
            std::string nm = it != doc.assets.end() ? it->second.name : asset;
            const char* prefix = kind == 2 ? "G_" : kind == 3 ? "V_" : "T_";
            t.symbol = Unique(prefix + SanitizeIdent(nm, "tex") + (recolor ? "_mask" : ""));
            texRefs.push_back(t);
            ref = &texRefs.back();
        }
        if (ref->kind == 2) return "fim::GifFrame(" + ref->symbol + ")";
        if (ref->kind == 3) { usesFx = true; return "fim::fx::VideoFrame(" + ref->symbol + ")"; }
        return ref->symbol;
    }

    std::string V(const std::string& var) { return "s." + varIdent[var]; }

    void CollectCombos()
    {
        doc.ForEach([&](Node* n) {
            if ((n->role == Role::Combo || n->role == Role::MultiCombo) && !n->var.empty() && !comboOptions.count(n->var)) comboOptions[n->var] = n;
        });
    }
    std::string OptSym(const std::string& var) { return "O_" + varIdent[var]; }
    // Static label arrays for combos ("O_<var>").
    std::string OptionArrays()
    {
        std::ostringstream s;
        for (auto& [var, n] : comboOptions)
        {
            if (!varIdent.count(var)) continue;
            s << "static const char* const " << OptSym(var) << "[] = { ";
            if (n->options.empty()) s << "\"\"";
            for (size_t i = 0; i < n->options.size(); i++) s << (i ? ", " : "") << CStr(n->options[i]);
            s << " };\n";
        }
        return s.str();
    }

    void CollectState()
    {
        // Interactive nodes without a variable get a synthesized one so they keep state.
        doc.ForEach([&](Node* n) {
            if ((n->role == Role::Checkbox || n->role == Role::Slider || n->role == Role::Input || n->role == Role::Combo || n->role == Role::MultiCombo) && n->var.empty())
                n->var = SanitizeIdent(n->name.empty() ? n->id : n->name, "value");
        });
        CollectCombos();
        std::set<std::string> taken;
        for (auto& v : CollectVars(doc))
        {
            vars[v.name] = v;
            std::string id = SanitizeIdent(v.name, "var");
            while (taken.count(id)) id += "_";
            taken.insert(id);
            varIdent[v.name] = id;
        }
    }

    // Emits the expression for a text, handling {var} placeholders.
    std::string TextExpr(const std::string& text, const std::string& sym)
    {
        if (text.find('{') == std::string::npos) return CStr(text);
        std::string buf = sym + "_txt";
        L("fim::TextBuf " + buf + ";");
        size_t i = 0;
        while (i < text.size())
        {
            size_t p = text.find('{', i);
            size_t e = p == std::string::npos ? std::string::npos : text.find('}', p);
            if (p == std::string::npos || e == std::string::npos) { L(buf + ".Add(" + CStr(text.substr(i)) + ");"); break; }
            if (p > i) L(buf + ".Add(" + CStr(text.substr(i, p - i)) + ");");
            std::string inner = text.substr(p + 1, e - p - 1);
            size_t colon = inner.find(':');
            std::string name = inner.substr(0, colon), fmt = colon == std::string::npos ? "" : inner.substr(colon + 1);
            auto it = vars.find(name);
            auto co = comboOptions.find(name);
            if (it != vars.end() && fmt == "label" && co != comboOptions.end())
            {
                std::string cnt = std::to_string(std::max<size_t>(1, co->second->options.size()));
                if (co->second->role == Role::MultiCombo)
                    L(buf + ".Options(" + V(name) + ", " + OptSym(name) + ", " + cnt + ", " + CStr(co->second->noneLabel) + ", " + CStr(co->second->allLabel) + ");");
                else
                    L(buf + ".Option((int)" + V(name) + ", " + OptSym(name) + ", " + cnt + ");");
            }
            else if (it == vars.end()) L(buf + ".Add(" + CStr(text.substr(p, e - p + 1)) + ");");
            else if (it->second.kind == VarKind::String) L(buf + ".Add(" + V(name) + ");");
            else if (it->second.kind == VarKind::Float && fmt.find('d') == std::string::npos)
                L(buf + ".Num(" + V(name) + (fmt.empty() ? "" : ", " + CStr(fmt)) + ");");
            else
                L(buf + ".Int((int)" + V(name) + (fmt.empty() ? "" : ", " + CStr(fmt)) + ");");
            i = e + 1;
        }
        return buf + ".c_str()";
    }

    std::string PaintLit(const Paint& p)
    {
        if (p.type == 0) return "fim::Paint{}";
        if (p.type == 1) return "fim::Paint{fim::Paint_Solid, " + Col(p.c0) + "}";
        return "fim::Paint{fim::Paint_Linear, " + Col(p.c0) + ", " + Col(p.c1) + ", " + F(p.angle) + "}";
    }

    std::string SpecLit(const AnimFx& a, float extraDelay)
    {
        return "fim::AnimSpec{ " + std::to_string(a.preset) + ", " + std::to_string(a.trigger) + ", " + std::to_string(a.easing) + ", " + F(a.duration) + ", " +
               F(a.delay + extraDelay) + ", " + F(a.distance) + ", " + F(a.scale) + ", " + F(a.intensity) + ", { " + F(a.bezier[0]) + ", " + F(a.bezier[1]) + ", " +
               F(a.bezier[2]) + ", " + F(a.bezier[3]) + " } }";
    }

    std::string CondExpr(const Node* n)
    {
        auto it = vars.find(n->showVar);
        if (it == vars.end() || it->second.kind == VarKind::String) return "";
        std::string eq = "std::fabs((float)" + V(n->showVar) + " - " + F(n->showValue) + ") < 1e-4f";
        return n->showNot ? "!(" + eq + ")" : eq;
    }

    void Emit(Node* n, const std::string& pa, const AlphaE& parentAlpha, const std::string& sv, bool isRoot, const AnimFx* inherited = nullptr, float extraDelay = 0.0f)
    {
        if (!n->visible) return;
        std::string sym = "n" + std::to_string(++counter);
        nodeSym[n] = sym;
        L("// " + (n->name.empty() ? std::string(NodeTypeName(n->type)) : n->name) + " [" + NodeTypeName(n->type) + "]");
        std::string cond = n->showVar.empty() ? "" : CondExpr(n);
        L(cond.empty() ? "{" : "if (" + cond + ")\n" + std::string(ind * 4, ' ') + "{");
        ind++;

        const AnimFx* spec = (n->anim.preset && n->anim.stagger <= 0.0f) ? &n->anim : (n->anim.preset ? nullptr : inherited);
        float delay = spec == inherited ? extraDelay : 0.0f;
        bool animated = spec != nullptr;
        std::string a = sym + "_a", b = sym + "_b";
        std::string ba = animated ? sym + "_a0" : a, bb = animated ? sym + "_b0" : b;   // un-animated rect

        // Base rect (relative to the parent rect)
        float X = isRoot ? 0.0f : n->x, Y = isRoot ? 0.0f : n->y;
        Node* owner = n->sliderPart ? SliderOwner(n) : nullptr;
        if (owner && nodeSym.count(owner))
        {
            std::string oa = nodeSym[owner] + "_a";
            float span = owner->vmax - owner->vmin;
            L("const float " + sym + "_t = ImClamp(((float)" + V(owner->var) + " - " + F(owner->vmin) + ") / " + F(span == 0 ? 1.0f : span) + ", 0.0f, 1.0f);");
            if (n->sliderPart == 1)
                L("const ImVec2 " + ba + "(" + oa + ".x, " + pa + ".y + " + F(Y) + " * " + sv + "), " + bb + "(" + oa + ".x + IM_ROUND(" + F(owner->w) + " * " + sv + " * " + sym + "_t), " + ba + ".y + " + F(n->h) + " * " + sv + ");");
            else
                L("const ImVec2 " + ba + "(IM_ROUND(" + oa + ".x + " + F(owner->w) + " * " + sv + " * " + sym + "_t - " + F(n->w * 0.5f) + " * " + sv + "), " + pa + ".y + " + F(Y) + " * " + sv + "), " + bb + "(" + ba + ".x + " + F(n->w) + " * " + sv + ", " + ba + ".y + " + F(n->h) + " * " + sv + ");");
        }
        else
            L("const ImVec2 " + ba + "(" + pa + ".x + " + F(X) + " * " + sv + ", " + pa + ".y + " + F(Y) + " * " + sv + "), " + bb + "(" + ba + ".x + " + F(n->w) + " * " + sv + ", " + ba + ".y + " + F(n->h) + " * " + sv + ");");

        // Interaction (on the un-animated rect)
        std::string hov = "false", act = "false", held;
        std::string id = CStr("##" + n->id);
        bool hasVar = !n->var.empty() && vars.count(n->var);
        switch (n->role)
        {
        case Role::Button:
        case Role::Tab:
        {
            L("const fim::ItemState " + sym + "_st = fim::Item(" + id + ", " + ba + ", " + bb + ");");
            hov = sym + "_st.hovered";
            held = sym + "_st.held";
            if (hasVar) L("if (" + sym + "_st.clicked) " + V(n->var) + " = " + (vars[n->var].kind == VarKind::Float ? F(n->value) : std::to_string((int)n->value)) + ";");
            if (n->role == Role::Button)
            {
                std::string flag = SanitizeIdent(n->name.empty() ? n->id : n->name, "button") + "_pressed";
                if (std::find(pressed.begin(), pressed.end(), flag) == pressed.end()) pressed.push_back(flag);
                if (!snippet) L("if (" + sym + "_st.clicked) s." + flag + " = true;");
                act = sym + "_st.held";
            }
            else if (hasVar) act = "(std::fabs((float)" + V(n->var) + " - " + F(n->value) + ") < 1e-4f)";
            break;
        }
        case Role::Checkbox:
            L("const fim::ItemState " + sym + "_st = fim::Item(" + id + ", " + ba + ", " + bb + ");");
            if (hasVar) { L("if (" + sym + "_st.clicked) " + V(n->var) + " = !" + V(n->var) + ";"); act = "(" + V(n->var) + " != 0)"; }
            hov = sym + "_st.hovered";
            held = sym + "_st.held";
            break;
        case Role::Slider:
            L("fim::ItemState " + sym + "_st;");
            if (hasVar && vars[n->var].kind == VarKind::Float)
                L("fim::SliderBehavior(" + id + ", " + ba + ", " + bb + ", &" + V(n->var) + ", " + F(n->vmin) + ", " + F(n->vmax) + ", &" + sym + "_st);");
            hov = "(" + sym + "_st.hovered || " + sym + "_st.held)";
            act = held = sym + "_st.held";
            break;
        case Role::Combo:
        case Role::MultiCombo:
        {
            if (!hasVar || !comboOptions.count(n->var)) break;
            const Node* label = nullptr;
            for (auto& c : n->children) if (c->type == NodeType::Text) { label = c.get(); break; }
            std::string cs = sym + "_cs";
            auto Ms = [&](float v) { return F(v) + " * " + sv; };
            L("fim::ComboStyle " + cs + ";");
            L(cs + ".font = " + (label ? FontSym(label->fontFamily, label->fontStyle) : FontSym("Inter", "Regular")) + ";");
            L(cs + ".fontSize = " + Ms(label ? label->fontSize : 13.0f) + ";");
            L(cs + ".text = " + Col(label ? label->fill.c0 : ImVec4(0.9f, 0.9f, 0.93f, 1)) + ";");
            L(cs + ".bg = " + Col(n->listFill) + ";");
            fim::ComboStyle ref = ComboStyleFor(n, 1.0f);
            L(cs + ".hover = " + Col(ImGui::ColorConvertU32ToFloat4(ref.hover)) + ";");
            L(cs + ".accent = " + Col(n->listAccent) + ";");
            L(cs + ".border = " + Col(ImGui::ColorConvertU32ToFloat4(ref.border)) + ";");
            L(cs + ".rounding = " + Ms(n->radius[0]) + ";");
            L(cs + ".itemHeight = " + Ms(n->itemHeight > 0 ? n->itemHeight : n->h) + ";");
            L(cs + ".padX = " + Ms(label ? label->x : 12.0f) + ";");
            L(cs + ".gap = " + Ms(4.0f) + ";");
            if (n->role == Role::MultiCombo) L(cs + ".allLabel = " + CStr(n->allLabel) + ";");
            L("fim::ItemState " + sym + "_st;");
            std::string cnt = std::to_string(std::max<size_t>(1, n->options.size()));
            if (n->role == Role::Combo)
                L("fim::Combo(" + id + ", " + ba + ", " + bb + ", &" + V(n->var) + ", " + OptSym(n->var) + ", " + cnt + ", " + cs + ", &" + sym + "_st);");
            else
                L("fim::MultiCombo(" + id + ", " + ba + ", " + bb + ", &" + V(n->var) + ", " + OptSym(n->var) + ", " + cnt + ", " + cs + ", &" + sym + "_st);");
            hov = sym + "_st.hovered";
            act = held = sym + "_st.held";
            break;
        }
        case Role::Input:
            L("const bool " + sym + "_act = ImGui::GetActiveID() == ImGui::GetID(" + id + ");");
            hov = "ImGui::IsMouseHoveringRect(" + ba + ", " + bb + ")";
            act = sym + "_act";
            break;
        default: break;
        }

        // Animation: offset/scale the subtree around the node center.
        std::string nsv = sv;
        AlphaE al = parentAlpha;
        al.k *= n->opacity;
        if (animated)
        {
            std::string hovE = n->role != Role::None ? hov : "(ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(" + ba + ", " + bb + "))";
            std::string heldE = !held.empty() ? held : "(" + hovE + " && ImGui::IsMouseDown(0))";
            L("const fim::AnimValue " + sym + "_an = fim::AnimUpdate(" + CStr(n->id + "#m") + ", " + SpecLit(*spec, delay) + ", " + hovE + ", " + heldE + ", " + act + ");");
            L("const float " + sym + "_S = " + sv + " * " + sym + "_an.scale;");
            L("const ImVec2 " + sym + "_c((" + ba + ".x + " + bb + ".x) * 0.5f + " + sym + "_an.dx * " + sv + ", (" + ba + ".y + " + bb + ".y) * 0.5f + " + sym + "_an.dy * " + sv + ");");
            L("const ImVec2 " + sym + "_h0((" + bb + ".x - " + ba + ".x) * 0.5f * " + sym + "_an.scale, (" + bb + ".y - " + ba + ".y) * 0.5f * " + sym + "_an.scale);");
            L("const ImVec2 " + a + "(" + sym + "_c.x - " + sym + "_h0.x, " + sym + "_c.y - " + sym + "_h0.y), " + b + "(" + sym + "_c.x + " + sym + "_h0.x, " + sym + "_c.y + " + sym + "_h0.y);");
            L("const float " + sym + "_al = " + al.str() + " * " + sym + "_an.alpha;");
            nsv = sym + "_S";
            al = AlphaE{ sym + "_al", 1.0f };
        }
        auto M = [&](float v) { return F(v) + " * " + nsv; };

        bool ellipse = n->type == NodeType::Ellipse;
        bool anyR = n->radius[0] || n->radius[1] || n->radius[2] || n->radius[3];
        std::string r = "nullptr";
        if (anyR && n->type != NodeType::Text && n->type != NodeType::Line && !ellipse)
        {
            r = sym + "_r";
            L("const float " + r + "[4] = { " + M(n->radius[0]) + ", " + M(n->radius[1]) + ", " + M(n->radius[2]) + ", " + M(n->radius[3]) + " };");
        }

        // Fill paint (with animated hover / active states)
        std::string paint = PaintLit(n->fill);
        if (n->role != Role::None && (n->hasHover || n->hasActive))
        {
            ImVec4 base = n->fill.type ? n->fill.c0 : ImVec4(n->fill.c0.x, n->fill.c0.y, n->fill.c0.z, 0);
            std::string c = Col(base);
            if (n->hasHover)
            {
                L("const float " + sym + "_hv = fim::Animate(" + CStr(n->id + "#h") + ", " + hov + " ? 1.0f : 0.0f);");
                c = "fim::LerpColor(" + c + ", " + Col(n->hoverColor) + ", " + sym + "_hv)";
            }
            if (n->hasActive)
            {
                L("const float " + sym + "_k = fim::Animate(" + CStr(n->id + "#a") + ", " + act + " ? 1.0f : 0.0f);");
                c = "fim::LerpColor(" + c + ", " + Col(n->activeColor) + ", " + sym + "_k)";
            }
            if (n->fill.type == 2)
                paint = "(" + (n->hasHover ? sym + "_hv" : std::string("0.0f")) + " + " + (n->hasActive ? sym + "_k" : std::string("0.0f")) + " > 0.0f ? fim::Paint{fim::Paint_Solid, " + c + "} : " + PaintLit(n->fill) + ")";
            else
                paint = "fim::Paint{fim::Paint_Solid, " + c + "}";
        }

        std::string shadow;
        if (n->effect.type)
        {
            shadow = sym + "_sh";
            L("const fim::Shadow " + shadow + "{ " + std::string(n->effect.type == 1 ? "fim::Shadow_Drop" : "fim::Shadow_Inner") + ", ImVec2(" + F(n->effect.offset.x) + ", " + F(n->effect.offset.y) + "), "
              + F(n->effect.blur) + ", " + F(n->effect.spread) + ", " + Col(n->effect.color) + " };");
        }
        std::string als = al.str();

        auto emitBackdrop = [&]() {
            if (!(n->type == NodeType::Frame || n->type == NodeType::Rect || ellipse) || (!n->glass.on && !n->blur.on)) return;
            usesFx = true;
            std::string bd = sym + "_bd";
            L("fim::fx::Backdrop " + bd + ";");
            if (n->glass.on)
            {
                const GlassFx& g = n->glass;
                L(bd + ".mode = fim::fx::Backdrop_Glass;");
                L(bd + ".blur = " + M(g.blur) + ";");
                L(bd + ".refraction = " + F(g.refraction) + ";");
                L(bd + ".thickness = " + M(g.thickness) + ";");
                L(bd + ".highlight = " + F(g.highlight) + ";");
                L(bd + ".dispersion = " + F(g.dispersion) + ";");
                L(bd + ".lightAngle = " + F(g.lightAngle) + ";");
                L(bd + ".saturation = " + F(g.saturation) + ";");
                L(bd + ".tint = " + Col(g.tint) + ";");
            }
            else
                L(bd + ".blur = " + M(n->blur.radius) + ";");
            L("fim::fx::DrawBackdrop(dl, " + a + ", " + b + ", " + r + ", " + bd + ", " + als + (ellipse ? ", true" : "") + ");");
        };

        auto emitTexture = [&]() {
            if (!n->texture.on) return;
            auto it = doc.assets.find(n->texture.asset);
            if (it == doc.assets.end()) return;
            usesFx = true;
            const TextureFx& tx = n->texture;
            const Asset& as = it->second;
            std::string tex = TexExpr(tx.asset, false, n->w, n->h);
            ImVec2 isz((float)std::max(1, as.width), (float)std::max(1, as.height));
            std::string ta = a, tb = b, u0 = "ImVec2(0.0f, 0.0f)", u1 = "ImVec2(1.0f, 1.0f)";
            if (tx.fit == 0)
            {
                ImVec2 uv0, uv1;
                fim::CoverUV(isz, ImVec2(n->w, n->h), &uv0, &uv1);
                u0 = "ImVec2(" + F(uv0.x) + ", " + F(uv0.y) + ")";
                u1 = "ImVec2(" + F(uv1.x) + ", " + F(uv1.y) + ")";
            }
            else if (tx.fit == 1)
            {
                ImVec2 ia(0, 0), ib(n->w, n->h);
                fim::ContainRect(isz, &ia, &ib);
                ta = "ImVec2(" + a + ".x + " + M(ia.x) + ", " + a + ".y + " + M(ia.y) + ")";
                tb = "ImVec2(" + a + ".x + " + M(ib.x) + ", " + a + ".y + " + M(ib.y) + ")";
            }
            else
            {
                float tw = isz.x * std::max(0.01f, tx.scale), th = isz.y * std::max(0.01f, tx.scale);
                ImVec2 span(n->w / tw, n->h / th);
                if (tx.speed.x != 0 || tx.speed.y != 0)
                {
                    L("const float " + sym + "_tt = (float)ImGui::GetTime();");
                    std::string ox = "fmodf(" + sym + "_tt * " + F(tx.speed.x / tw) + ", 1.0f)", oy = "fmodf(" + sym + "_tt * " + F(tx.speed.y / th) + ", 1.0f)";
                    u0 = "ImVec2(" + ox + ", " + oy + ")";
                    u1 = "ImVec2(" + ox + " + " + F(span.x) + ", " + oy + " + " + F(span.y) + ")";
                }
                else u1 = "ImVec2(" + F(span.x) + ", " + F(span.y) + ")";
            }
            std::string rr = r;
            if (ellipse && tx.fit != 1)
            {
                rr = sym + "_er";
                std::string h = M(std::min(n->w, n->h) * 0.5f);
                L("const float " + rr + "[4] = { " + h + ", " + h + ", " + h + ", " + h + " };");
            }
            if (tx.fit == 1) rr = "nullptr";
            char op[64];
            snprintf(op, sizeof(op), "IM_COL32(255, 255, 255, %d)", (int)std::lround(tx.opacity * 255));
            L("fim::fx::DrawTexture(dl, " + tex + ", " + ta + ", " + tb + ", " + rr + ", " + u0 + ", " + u1 + ", " + op + ", " + std::to_string(tx.blend) + ", " +
              (tx.fit == 2 ? "true" : "false") + ", " + als + ");");
        };

        switch (n->type)
        {
        case NodeType::Frame:
        case NodeType::Rect:
            if (n->effect.type == 1) L("fim::DrawShadow(dl, " + a + ", " + b + ", " + r + ", " + shadow + ", " + nsv + ", " + als + ");");
            emitBackdrop();
            if (n->fill.type || paint.find("Paint_Solid") != std::string::npos) L("fim::FillRect(dl, " + a + ", " + b + ", " + r + ", " + paint + ", " + als + ");");
            emitTexture();
            if (n->effect.type == 2) L("fim::DrawShadow(dl, " + a + ", " + b + ", " + r + ", " + shadow + ", " + nsv + ", " + als + ");");
            break;
        case NodeType::Group:
            break;
        case NodeType::Ellipse:
            if (n->effect.type == 1) L("fim::DrawShadow(dl, " + a + ", " + b + ", nullptr, " + shadow + ", " + nsv + ", " + als + ", true);");
            emitBackdrop();
            L("fim::FillEllipse(dl, " + a + ", " + b + ", " + paint + ", " + als + ");");
            emitTexture();
            if (n->effect.type == 2) L("fim::DrawShadow(dl, " + a + ", " + b + ", nullptr, " + shadow + ", " + nsv + ", " + als + ", true);");
            break;
        case NodeType::Line:
            L("fim::DrawLine(dl, " + a + ", ImVec2(" + b + ".x, " + a + ".y), " + Col(n->stroke.c0) + ", " + M(std::max(0.5f, n->strokeWidth)) + ", " + als + ");");
            break;
        case NodeType::Text:
        {
            std::string font = FontSym(n->fontFamily, n->fontStyle);
            std::string txt = TextExpr(n->text, sym);
            L("fim::DrawText(dl, " + font + ", " + M(n->fontSize) + ", " + a + ", " + b + ", " + txt + ", " + Col(n->fill.c0) + ", " + std::to_string(n->alignH) + ", "
              + std::to_string(n->alignV) + ", " + M(n->lineHeight) + ", " + (n->textSizing != 1 ? "true" : "false") + ", " + als + ");");
            break;
        }
        case NodeType::Image:
        case NodeType::Vector:
        {
            auto it = doc.assets.find(n->asset);
            if (it == doc.assets.end()) { L("// missing asset " + n->asset); break; }
            bool recolor = n->type == NodeType::Vector && n->recolor;
            std::string tex = TexExpr(n->asset, recolor, n->w, n->h);
            if (n->effect.type == 1) L("fim::DrawShadow(dl, " + a + ", " + b + ", " + r + ", " + shadow + ", " + nsv + ", " + als + ");");
            ImVec2 isz((float)std::max(1, it->second.width), (float)std::max(1, it->second.height));
            ImVec2 uv0(0, 0), uv1(1, 1), ia(0, 0), ib(n->w, n->h);
            if (n->type == NodeType::Vector || n->imageFit == 1) fim::ContainRect(isz, &ia, &ib);
            else if (n->imageFit == 0) fim::CoverUV(isz, ImVec2(n->w, n->h), &uv0, &uv1);
            ImVec4 tint = recolor ? (n->fill.type ? n->fill.c0 : ImVec4(1, 1, 1, 1)) : ImVec4(1, 1, 1, 1);
            L("fim::DrawImage(dl, " + tex + ", ImVec2(" + a + ".x + " + M(ia.x) + ", " + a + ".y + " + M(ia.y) + "), ImVec2(" + a + ".x + " + M(ib.x) + ", " + a + ".y + " + M(ib.y) + "), "
              + (n->type == NodeType::Image ? r : std::string("nullptr")) + ", ImVec2(" + F(uv0.x) + ", " + F(uv0.y) + "), ImVec2(" + F(uv1.x) + ", " + F(uv1.y) + "), " + Col(tint) + ", " + als + ");");
            if (n->type == NodeType::Image) emitTexture();
            break;
        }
        }

        if (!n->children.empty())
        {
            bool clip = n->type == NodeType::Frame && n->clip;
            if (clip) L("dl->PushClipRect(" + a + ", " + b + ", true);");
            bool placeholder = n->role == Role::Input && n->var.size() && vars.count(n->var);
            bool stagger = n->anim.preset && n->anim.stagger > 0.0f;
            for (size_t i = 0; i < n->children.size(); i++)
            {
                Node* c = n->children[i].get();
                const AnimFx* inh = stagger ? &n->anim : nullptr;
                float d = stagger ? n->anim.stagger * (float)i : 0.0f;
                if (placeholder && c->type == NodeType::Text)
                {
                    L("if (!" + sym + "_act && " + V(n->var) + "[0] == 0)");
                    L("{");
                    ind++;
                    Emit(c, a, al, nsv, false, inh, d);
                    ind--;
                    L("}");
                }
                else Emit(c, a, al, nsv, false, inh, d);
            }
            if (clip) L("dl->PopClipRect();");
        }

        if (n->stroke.type && n->type != NodeType::Line)
        {
            if (ellipse) L("fim::StrokeEllipse(dl, " + a + ", " + b + ", " + Col(n->stroke.c0) + ", " + M(n->strokeWidth) + ", " + als + ");");
            else L("fim::StrokeRect(dl, " + a + ", " + b + ", " + r + ", " + Col(n->stroke.c0) + ", " + M(n->strokeWidth) + ", " + std::to_string(n->strokeAlign) + ", " + als + ");");
        }

        if (n->role == Role::Input && n->var.size() && vars.count(n->var) && vars[n->var].kind == VarKind::String)
        {
            Node* label = nullptr;
            for (auto& c : n->children) if (c->type == NodeType::Text) { label = c.get(); break; }
            std::string font = label ? FontSym(label->fontFamily, label->fontStyle) : FontSym("Inter", "Regular");
            L("fim::InputBehavior(" + id + ", " + a + ", " + b + ", " + V(n->var) + ", (int)sizeof(" + V(n->var) + "), " + font + ", " + M(label ? label->fontSize : 13.0f) + ", "
              + Col(label ? label->fill.c0 : ImVec4(1, 1, 1, 1)) + ", " + M(label ? label->x : 8.0f) + ");");
        }

        ind--;
        L("}");
    }

    std::string StateStruct(const std::string& name)
    {
        std::ostringstream s;
        s << "struct " << name << "\n{\n";
        for (auto& [k, v] : vars)
        {
            std::string id = varIdent[k];
            switch (v.kind)
            {
            case VarKind::Bool:   s << "    bool  " << id << " = " << (v.def != 0 ? "true" : "false") << ";\n"; break;
            case VarKind::Int:    s << "    int   " << id << " = " << (int)v.def << ";\n"; break;
            case VarKind::Float:  s << "    float " << id << " = " << F(v.def) << ";\n"; break;
            case VarKind::String: s << "    char  " << id << "[256] = \"\";\n"; break;
            case VarKind::Mask:
            {
                s << "    unsigned int " << id << " = " << (unsigned)v.def << "u;";
                auto co = comboOptions.find(k);
                if (co != comboOptions.end())
                {
                    s << "   // bits:";
                    for (size_t i = 0; i < co->second->options.size(); i++) s << " " << i << "=" << co->second->options[i];
                }
                s << "\n";
                break;
            }
            }
        }
        if (!pressed.empty()) s << "\n    // Set to true for the frame in which the button was clicked.\n";
        for (auto& p : pressed) s << "    bool  " << p << " = false;\n";
        s << "};\n";
        return s.str();
    }
};

// ---------------------------------------------------------------- snippet

std::string GenerateSnippet(Document& doc, Node* n)
{
    Gen g(doc);
    g.snippet = true;
    std::vector<VarInfo> vlist = CollectVars(doc);
    for (auto& v : vlist) { g.vars[v.name] = v; g.varIdent[v.name] = SanitizeIdent(v.name, "var"); }
    g.CollectCombos();
    g.ind = 1;
    g.Emit(n, "o", AlphaE(), "S", true);
    std::string body = g.o.str();
    std::ostringstream out;
    out << "// Generated by Fimgui - requires fimgui_runtime.h/.cpp" << (g.usesFx ? " and fimgui_fx" : "") << " (Dear ImGui 1.92+)\n";
    out << "#include \"imgui.h\"\n#include \"imgui_internal.h\"\n#include \"fimgui_runtime.h\"\n" << (g.usesFx ? "#include \"fimgui_fx.h\"\n" : "") << "#include <cmath>\n\n";
    for (auto& f : g.fontRefs) out << "extern ImFont* " << f.symbol << ";      // " << f.family << " " << f.style << (f.file.empty() ? "" : " (" + f.file + ")") << "\n";
    for (auto& t : g.texRefs)
    {
        const char* type = t.kind == 2 ? "fim::GifAnim" : t.kind == 3 ? "int" : "ImTextureID";
        out << "extern " << type << " " << t.symbol << ";  // asset '" << t.asset << "'" << (t.recolor ? " (white mask, tinted)" : "") << (t.kind == 3 ? " (fim::fx::VideoOpenFile handle)" : "") << "\n";
    }
    if (!g.vars.empty() || !g.pressed.empty()) out << "\n" << g.StateStruct("State") << "\n";
    std::string opts = g.OptionArrays();
    if (!opts.empty()) out << opts << "\n";
    std::string fn = SanitizeIdent(n->name.empty() ? n->id : n->name, "Element");
    out << "// 'o' = top-left corner on screen, 'S' = scale. Call inside an ImGui window.\n";
    out << "void Draw_" << fn << "(ImDrawList* dl, ImVec2 o, float S" << (g.vars.empty() ? "" : ", State& s") << ")\n{\n" << body << "}\n";
    return out.str();
}

// ---------------------------------------------------------------- project export

static void HexArray(std::ostringstream& o, const std::string& name, const std::vector<unsigned char>& d)
{
    o << "static const unsigned char " << name << "[" << std::max<size_t>(1, d.size()) << "] = {\n";
    char buf[8];
    for (size_t i = 0; i < d.size(); i++)
    {
        if (i % 20 == 0) o << "    ";
        snprintf(buf, sizeof(buf), "%u,", d[i]);
        o << buf;
        if (i % 20 == 19 || i + 1 == d.size()) o << "\n";
    }
    if (d.empty()) o << "    0\n";
    o << "};\n";
}

static std::string ExampleMain(const std::string& ns, const std::string& base)
{
    std::string s = R"(// Example host: Win32 + DirectX 11. Build with the CMakeLists.txt next to this file.
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "../menu/@BASE@.h"
#include "../menu/fimgui_fx.h"
#include <d3d11.h>
#include <tchar.h>

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static void CreateRTV()
{
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg)
    {
    case WM_SIZE: if (wParam != SIZE_MINIMIZED) { g_resizeW = LOWORD(lParam); g_resizeH = HIWORD(lParam); } return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Texture factory handed to the generated menu (RGBA8).
static ImTextureID CreateTexture(const unsigned char* rgba, int w, int h)
{
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA sub = { rgba, (UINT)w * 4, 0 };
    ID3D11Texture2D* tex = nullptr;
    if (FAILED(g_device->CreateTexture2D(&desc, &sub, &tex))) return 0;
    ID3D11ShaderResourceView* srv = nullptr;
    g_device->CreateShaderResourceView(tex, nullptr, &srv);
    tex->Release();
    return (ImTextureID)srv;
}

int main(int, char**)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr, L"FimguiExample", nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"@NS@ - Fimgui export", WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, nullptr, nullptr, wc.hInstance, nullptr);

    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    if (D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context) != S_OK)
        return 1;
    CreateRTV();
    ShowWindow(hwnd, SW_SHOWDEFAULT);

    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);

    // GPU effects (background blur, glass, blend modes, video).
    fim::fx::InitDX11(g_device, g_context);
    // Fonts are loaded from ../fonts and videos from ../videos relative to the working directory.
    @NS@::Init(CreateTexture, "../fonts", "..");
    @NS@::State state;

    bool done = false;
    while (!done)
    {
        MSG msg;
        while (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;
        if (g_resizeW && g_resizeH)
        {
            g_rtv->Release();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRTV();
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        @NS@::Render(state);

        ImGui::Render();
        const float clear[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    fim::fx::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    return 0;
}
)";
    auto rep = [&](const std::string& k, const std::string& v) {
        size_t p;
        while ((p = s.find(k)) != std::string::npos) s.replace(p, k.size(), v);
    };
    rep("@NS@", ns);
    rep("@BASE@", base);
    return s;
}

static std::string ExampleCMake(const std::string& base)
{
    return "cmake_minimum_required(VERSION 3.20)\nproject(fimgui_example CXX)\nset(CMAKE_CXX_STANDARD 17)\n\n"
           "# Point this to your Dear ImGui (1.92+) checkout.\nset(IMGUI_DIR \"${CMAKE_CURRENT_LIST_DIR}/../../imgui\" CACHE PATH \"Dear ImGui source directory\")\n\n"
           "add_executable(fimgui_example\n    main.cpp\n    ../menu/" + base + ".cpp\n    ../menu/" + base + "_assets.cpp\n    ../menu/fimgui_runtime.cpp\n    ../menu/fimgui_fx_dx11.cpp\n"
           "    ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_tables.cpp ${IMGUI_DIR}/imgui_widgets.cpp\n"
           "    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp)\n"
           "target_include_directories(fimgui_example PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ../menu)\n"
           "target_link_libraries(fimgui_example PRIVATE d3d11 dxgi d3dcompiler mfplat mfreadwrite mfuuid shlwapi)\n"
           "if(MSVC)\n    target_compile_options(fimgui_example PRIVATE /utf-8 /bigobj)\nendif()\n";
}

ExportResult ExportProject(Document& doc, const ExportOptions& opt, const std::string& runtimeDir)
{
    ExportResult res;
    if (opt.outDir.empty()) { res.message = "No output folder"; return res; }
    fs::path out = fs::u8path(opt.outDir);
    std::error_code ec;
    fs::create_directories(out / "menu", ec);
    if (ec) { res.message = "Cannot create folder: " + ec.message(); return res; }

    std::string ns = SanitizeIdent(opt.ns, "menu");
    std::string base = SanitizeIdent(opt.fileBase, "menu");

    auto write = [&](const fs::path& p, const std::string& text) {
        if (platform::WriteText(p.u8string(), text)) res.files.push_back(fs::relative(p, out, ec).u8string());
    };
    auto writeBin = [&](const fs::path& p, const std::vector<unsigned char>& d) {
        if (platform::WriteFile(p.u8string(), d.data(), d.size())) res.files.push_back(fs::relative(p, out, ec).u8string());
    };

    doc.ApplyLayout();
    Gen g(doc);
    g.CollectState();

    // Frames -> windows
    std::vector<Node*> frames;
    for (auto& n : doc.roots) if (n->type == NodeType::Frame && n->visible) frames.push_back(n.get());
    if (frames.empty()) { res.message = "Nothing to export: add at least one top-level Frame."; return res; }
    float minX = 1e9f, minY = 1e9f;
    for (Node* f : frames) { minX = std::min(minX, f->x); minY = std::min(minY, f->y); }

    std::ostringstream fnDecls, fnDefs, renderAll;
    for (Node* f : frames)
    {
        std::string fname = g.Unique("Render_" + SanitizeIdent(f->name.empty() ? f->id : f->name, "Frame"));
        float m = 0;
        if (f->effect.type == 1) m = std::ceil(f->effect.blur + std::fabs(f->effect.spread) + std::max(std::fabs(f->effect.offset.x), std::fabs(f->effect.offset.y)));
        if (f->anim.preset) m = std::max(m, std::fabs(f->anim.distance) + 8.0f);
        g.o.str("");
        g.ind = 1;
        g.Emit(f, "o", AlphaE(), "S", true);
        fnDecls << "    void " << fname << "(State& state, float scale = 1.0f);\n";
        fnDefs << "void " << fname << "(State& s, float S)\n{\n"
               << "    const float M = " << F(m) << " * S;   // margin for the outer shadow / entrance motion\n"
               << "    ImGui::SetNextWindowPos(ImVec2(" << F(60 + f->x - minX) << ", " << F(60 + f->y - minY) << "), ImGuiCond_FirstUseEver);\n"
               << "    ImGui::SetNextWindowSize(ImVec2(" << F(f->w) << " * S + M * 2, " << F(f->h) << " * S + M * 2), ImGuiCond_Always);\n"
               << "    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));\n"
               << "    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);\n"
               << "    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));\n"
               << "    const bool open = ImGui::Begin(" << CStr((f->name.empty() ? f->id : f->name) + "##fimgui_" + f->id) << ", nullptr,\n"
               << "        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);\n"
               << "    ImGui::PopStyleVar(3);\n"
               << "    if (open)\n    {\n"
               << "        ImDrawList* dl = ImGui::GetWindowDrawList();\n"
               << "        const ImVec2 o(ImGui::GetWindowPos().x + M, ImGui::GetWindowPos().y + M);\n";
        std::string body = g.o.str();
        // Re-indent the emitted body one level deeper (it sits inside 'if (open)').
        std::istringstream lines(body);
        std::string line;
        while (std::getline(lines, line)) fnDefs << (line.empty() ? "" : "    " + line) << "\n";
        fnDefs << "    }\n    ImGui::End();\n}\n\n";
        renderAll << "    " << fname << "(state, scale);\n";
    }

    // Assets: static textures, GIFs and videos
    std::ostringstream assetsCpp;
    assetsCpp << "// Generated by Fimgui - embedded assets.\n#include \"" << base << ".h\"\n#include \"fimgui_runtime.h\"\n\n"
              << "#define STB_IMAGE_STATIC\n#define STB_IMAGE_IMPLEMENTATION\n#define STBI_ONLY_PNG\n#define STBI_ONLY_JPEG\n#define STBI_ONLY_GIF\n#include \"stb_image.h\"\n\n"
              << "namespace " << ns << "\n{\n";
    std::ostringstream texTable, gifTable, assetDecl, assetInit;
    int texCount = 0, gifCount = 0;
    std::set<std::string> writtenFiles;
    for (auto& t : g.texRefs)
    {
        const Asset& as = doc.assets[t.asset];
        if (t.kind == 3)
        {
            std::string file = as.name + VideoExt(as.data);
            if (!writtenFiles.count(file)) { writtenFiles.insert(file); writeBin(out / "videos" / file, as.data); }
            assetDecl << "int " << t.symbol << " = -1;\n";
            assetInit << "    " << t.symbol << " = fim::fx::VideoOpenFile((std::string(media_dir) + \"/videos/" << file << "\").c_str());\n";
            continue;
        }
        if (t.kind == 2)
        {
            if (!writtenFiles.count(as.name + ".gif")) { writtenFiles.insert(as.name + ".gif"); writeBin(out / "images" / (as.name + ".gif"), as.data); }
            HexArray(assetsCpp, "gif_" + std::to_string(gifCount) + "_data", as.data);
            gifTable << "        { gif_" << gifCount << "_data, (int)sizeof(gif_" << gifCount << "_data) },   // " << t.symbol << "\n";
            assetDecl << "fim::GifAnim " << t.symbol << ";\n";
            assetInit << "    if (create_texture) detail_LoadGif(" << gifCount << ", create_texture, &" << t.symbol << ");\n";
            gifCount++;
            continue;
        }
        std::vector<unsigned char> bytes;
        if (as.kind == 1)
        {
            std::string svg(as.data.begin(), as.data.end());
            float sw = (float)std::max(1, as.width), sh = (float)std::max(1, as.height);
            float k = std::max(t.maxW / sw, t.maxH / sh) * opt.iconScale;
            int w = std::max(1, (int)std::lround(sw * k)), h = std::max(1, (int)std::lround(sh * k));
            std::vector<unsigned char> rgba;
            if (gfx::RasterizeSVG(svg, w, h, t.recolor, rgba)) bytes = gfx::EncodePNG(rgba.data(), w, h);
            if (!writtenFiles.count(as.name))
            {
                writtenFiles.insert(as.name);
                write(out / "icons" / (as.name + ".svg"), svg);
                writeBin(out / "icons" / (as.name + ".png"), bytes);
            }
        }
        else
        {
            bytes = as.data;
            bool jpg = bytes.size() > 2 && bytes[0] == 0xFF && bytes[1] == 0xD8;
            if (!writtenFiles.count(as.name)) { writtenFiles.insert(as.name); writeBin(out / "images" / (as.name + (jpg ? ".jpg" : ".png")), bytes); }
        }
        HexArray(assetsCpp, "tex_" + std::to_string(texCount) + "_data", bytes);
        texTable << "        { tex_" << texCount << "_data, (int)sizeof(tex_" << texCount << "_data) },   // " << t.symbol << "\n";
        assetDecl << "ImTextureID " << t.symbol << " = 0;\n";
        assetInit << "    " << t.symbol << " = create_texture ? detail_LoadTexture(" << texCount << ", create_texture) : 0;\n";
        texCount++;
    }

    // Fonts
    std::ostringstream fontTable, fontDecl;
    int fontIdx = 0;
    for (auto& f : g.fontRefs)
    {
        if (!f.path.empty())
        {
            std::vector<unsigned char> data;
            if (platform::ReadFile(f.path, data))
            {
                writeBin(out / "fonts" / f.file, data);
                if (opt.embedFonts) HexArray(assetsCpp, "font_" + std::to_string(fontIdx) + "_data", data);
                if (f.family == "Inter")
                {
                    fs::path lic = fs::u8path(f.path).parent_path() / "Inter-LICENSE.txt";
                    if (fs::exists(lic, ec)) fs::copy_file(lic, out / "fonts" / "Inter-LICENSE.txt", fs::copy_options::overwrite_existing, ec);
                }
            }
        }
        fontTable << "        { " << (opt.embedFonts && !f.path.empty() ? "font_" + std::to_string(fontIdx) + "_data, (int)sizeof(font_" + std::to_string(fontIdx) + "_data)" : std::string("nullptr, 0"))
                  << ", " << CStr(f.file) << " },   // " << f.symbol << ": " << f.family << " " << f.style << "\n";
        fontDecl << "ImFont* " << f.symbol << " = nullptr;\n";
        fontIdx++;
    }

    assetsCpp << "\nstruct Blob { const unsigned char* data; int size; };\nstruct FontBlob { const unsigned char* data; int size; const char* file; };\n\n";
    assetsCpp << "static const Blob g_textures[] = {\n" << (texCount ? texTable.str() : "        { nullptr, 0 },\n") << "};\n";
    assetsCpp << "static const Blob g_gifs[] = {\n" << (gifCount ? gifTable.str() : "        { nullptr, 0 },\n") << "};\n";
    assetsCpp << "static const FontBlob g_fontFiles[] = {\n" << (fontIdx ? fontTable.str() : "        { nullptr, 0, \"\" },\n") << "};\n\n";
    assetsCpp << "ImTextureID detail_LoadTexture(int index, CreateTextureFn create)\n{\n"
              << "    if (index < 0 || index >= " << texCount << ") return 0;\n"
              << "    int w = 0, h = 0, n = 0;\n"
              << "    unsigned char* px = stbi_load_from_memory(g_textures[index].data, g_textures[index].size, &w, &h, &n, 4);\n"
              << "    if (!px) return 0;\n"
              << "    ImTextureID id = create(px, w, h);\n"
              << "    stbi_image_free(px);\n"
              << "    return id;\n}\n\n"
              << "bool detail_LoadGif(int index, CreateTextureFn create, fim::GifAnim* out)\n{\n"
              << "    if (index < 0 || index >= " << gifCount << ") return false;\n"
              << "    int* delays = nullptr;\n"
              << "    int w = 0, h = 0, frames = 0, comp = 0;\n"
              << "    unsigned char* px = stbi_load_gif_from_memory(g_gifs[index].data, g_gifs[index].size, &delays, &w, &h, &frames, &comp, 4);\n"
              << "    if (!px) return false;\n"
              << "    for (int f = 0; f < frames; f++)\n    {\n"
              << "        out->frames.push_back(create(px + (size_t)f * w * h * 4, w, h));\n"
              << "        int d = delays && delays[f] > 0 ? delays[f] : 100;\n"
              << "        out->delays.push_back(d);\n"
              << "        out->totalMs += d;\n    }\n"
              << "    out->width = w;\n    out->height = h;\n"
              << "    stbi_image_free(px);\n"
              << "    if (delays) stbi_image_free(delays);\n"
              << "    return true;\n}\n\n"
              << "ImFont* detail_LoadFont(int index, const char* fonts_dir)\n{\n"
              << "    if (index < 0 || index >= " << fontIdx << ") return nullptr;\n"
              << "    const FontBlob& f = g_fontFiles[index];\n"
              << "    ImFontConfig cfg;\n"
              << "    if (f.data)\n    {\n"
              << "        cfg.FontDataOwnedByAtlas = false;\n"
              << "        return ImGui::GetIO().Fonts->AddFontFromMemoryTTF((void*)f.data, f.size, 16.0f, &cfg);\n    }\n"
              << "    char path[1024];\n"
              << "    snprintf(path, sizeof(path), \"%s/%s\", fonts_dir ? fonts_dir : \"fonts\", f.file);\n"
              << "    return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 16.0f, &cfg);\n}\n"
              << "} // namespace " << ns << "\n";

    // Header
    std::ostringstream h;
    h << "// Generated by Fimgui from \"" << doc.name << "\". Do not edit by hand - re-export instead.\n"
      << "// Generated code and runtime: non-commercial use only, see LICENSE-FIMGUI.txt.\n"
      << "// Requires Dear ImGui 1.92+, fimgui_runtime.h/.cpp and fimgui_fx (fimgui_fx_dx11.cpp, or fimgui_fx_null.cpp on other renderers).\n"
      << "#pragma once\n#include \"imgui.h\"\n\nnamespace fim { struct GifAnim; }\n\nnamespace " << ns << "\n{\n"
      << g.StateStruct("State") << "\n"
      << "    // Creates an RGBA8 texture for the active renderer (see example/main.cpp for DirectX 11).\n"
      << "    using CreateTextureFn = ImTextureID (*)(const unsigned char* rgba, int width, int height);\n\n"
      << "    // Call once after ImGui::CreateContext(), backend init and fim::fx::InitDX11(), before the first NewFrame().\n"
      << "    // fonts_dir is used when fonts were not embedded at export; videos are read from <media_dir>/videos.\n"
      << "    bool Init(CreateTextureFn create_texture, const char* fonts_dir = \"fonts\", const char* media_dir = \".\");\n\n"
      << "    // Draws every top-level frame as its own ImGui window.\n"
      << "    void Render(State& state, float scale = 1.0f);\n\n"
      << fnDecls.str()
      << "\n    // Internal (implemented in " << base << "_assets.cpp)\n"
      << "    ImTextureID detail_LoadTexture(int index, CreateTextureFn create);\n"
      << "    bool detail_LoadGif(int index, CreateTextureFn create, fim::GifAnim* out);\n"
      << "    ImFont* detail_LoadFont(int index, const char* fonts_dir);\n"
      << "}\n";
    std::string header = h.str();

    std::ostringstream cpp;
    cpp << "// Generated by Fimgui from \"" << doc.name << "\".\n#include \"" << base << ".h\"\n#include \"fimgui_runtime.h\"\n#include \"fimgui_fx.h\"\n#include \"imgui_internal.h\"\n#include <cmath>\n#include <string>\n\n"
        << "namespace " << ns << "\n{\n"
        << fontDecl.str() << assetDecl.str() << "\n"
        << g.OptionArrays() << "\n"
        << "bool Init(CreateTextureFn create_texture, const char* fonts_dir, const char* media_dir)\n{\n    bool ok = true;\n    (void)media_dir;\n";
    for (size_t i = 0; i < g.fontRefs.size(); i++)
        cpp << "    " << g.fontRefs[i].symbol << " = detail_LoadFont(" << i << ", fonts_dir);\n    if (!" << g.fontRefs[i].symbol << ") { " << g.fontRefs[i].symbol << " = ImGui::GetIO().Fonts->AddFontDefault(); ok = false; }\n";
    cpp << assetInit.str()
        << "    return ok;\n}\n\n"
        << fnDefs.str()
        << "void Render(State& state, float scale)\n{\n";
    for (auto& p : g.pressed) cpp << "    state." << p << " = false;\n";
    cpp << renderAll.str() << "}\n\n} // namespace " << ns << "\n";

    write(out / "menu" / (base + ".h"), header);
    write(out / "menu" / (base + ".cpp"), cpp.str());
    write(out / "menu" / (base + "_assets.cpp"), assetsCpp.str());

    // Runtime + stb_image
    for (const char* f : { "fimgui_runtime.h", "fimgui_runtime.cpp", "fimgui_fx.h", "fimgui_fx_dx11.cpp", "fimgui_fx_null.cpp", "stb_image.h" })
    {
        fs::path src = fs::u8path(runtimeDir) / f;
        std::vector<unsigned char> d;
        if (platform::ReadFile(src.u8string(), d)) writeBin(out / "menu" / f, d);
        else { res.message = std::string("Runtime file missing: ") + src.u8string(); return res; }
    }

    if (opt.writeExample)
    {
        write(out / "example" / "main.cpp", ExampleMain(ns, base));
        write(out / "example" / "CMakeLists.txt", ExampleCMake(base));
    }

    // License that covers the generated code and the copied runtime
    {
        std::vector<unsigned char> lic;
        if (platform::ReadFile((fs::u8path(runtimeDir) / "FIMGUI_LICENSE.txt").u8string(), lic)) writeBin(out / "LICENSE-FIMGUI.txt", lic);
    }

    // Design source for round-tripping
    write(out / (base + ".fimgui"), doc.ToJson(true).dump(1));

    std::ostringstream readme;
    readme << "# " << doc.name << " - exported by Fimgui\n\n"
           << "## Layout\n\n"
           << "- `menu/" << base << ".h/.cpp` - generated ImGui code (`" << ns << "::State`, `" << ns << "::Init`, `" << ns << "::Render`)\n"
           << "- `menu/" << base << "_assets.cpp` - embedded textures and GIFs" << (opt.embedFonts ? " and fonts" : "") << "\n"
           << "- `menu/fimgui_runtime.h/.cpp` - drawing, animation and GIF helpers used by the generated code\n"
           << "- `menu/fimgui_fx.h` + `fimgui_fx_dx11.cpp` - background blur, glass, blend modes, video (DirectX 11). Use `fimgui_fx_null.cpp` instead on other renderers.\n"
           << "- `icons/` - SVG + PNG icons, `images/` - raster images and GIFs, `videos/` - video files, `fonts/` - font files\n"
           << "- `example/` - Win32 + DirectX 11 host (set `IMGUI_DIR` in CMake)\n"
           << "- `" << base << ".fimgui` - design source, open it in Fimgui to keep editing\n\n"
           << "## Usage\n\n```cpp\n#include \"" << base << ".h\"\n#include \"fimgui_fx.h\"\n\n" << ns << "::State state;\n"
           << "fim::fx::InitDX11(device, context);          // once, after ImGui_ImplDX11_Init()\n"
           << ns << "::Init(MyCreateTexture, \"fonts\", \".\");   // fonts dir, folder that contains videos/\n\n"
           << "// every frame, between ImGui::NewFrame() and ImGui::Render():\n" << ns << "::Render(state);\n```\n\n"
           << "Add `menu/*.cpp` (one of the two fimgui_fx_*.cpp) to your build. Dear ImGui 1.92 or newer is required (dynamic font sizes).\n"
           << "The DirectX 11 effects link with d3dcompiler, mfplat, mfreadwrite, mfuuid and shlwapi.\n\n"
           << "## License\n\nThe generated code and the Fimgui runtime are for non-commercial use only (see `LICENSE-FIMGUI.txt`). "
           << "Your own content (texts, images, layouts) remains yours.\n\n## State\n\n";
    for (auto& [k, v] : g.vars)
        readme << "- `" << g.varIdent[k] << "` (" << (v.kind == VarKind::Bool ? "bool" : v.kind == VarKind::Int ? "int" : v.kind == VarKind::Float ? "float" : v.kind == VarKind::Mask ? "unsigned int, bit mask" : "char[256]") << ")\n";
    for (auto& p : g.pressed) readme << "- `" << p << "` (bool, true for one frame after a click)\n";
    write(out / "README.md", readme.str());

    res.ok = true;
    res.message = "Exported " + std::to_string(res.files.size()) + " files";
    return res;
}
