// Minecraft Java GUI export (Fabric / NeoForge 1.21.1).
#include "codegen_java.h"
#include "exporters.h"
#include "renderer.h"
#include "fonts.h"
#include "platform.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace
{
    std::string JF(float v)
    {
        char b[32];
        if (std::fabs(v - std::round(v)) < 1e-4f) snprintf(b, sizeof(b), "%df", (int)std::round(v));
        else
        {
            snprintf(b, sizeof(b), "%.3f", v);
            std::string s = b;
            while (s.back() == '0') s.pop_back();
            return s + "f";
        }
        return b;
    }

    std::string JStr(const std::string& s)
    {
        std::string o = "\"";
        for (char c : s)
        {
            if (c == '"') o += "\\\"";
            else if (c == '\\') o += "\\\\";
            else if (c == '\n') o += "\\n";
            else if (c == '\r') continue;
            else if (c == '\t') o += "\\t";
            else o += c;
        }
        return o + "\"";
    }

    std::string Argb(ImVec4 c)
    {
        auto b = [](float f) { return (unsigned)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
        char buf[24];
        snprintf(buf, sizeof(buf), "0x%02X%02X%02X%02X", b(c.w), b(c.x), b(c.y), b(c.z));
        return buf;
    }

    // Resource paths: [a-z0-9_.-]
    std::string ResName(const std::string& s, const char* fallback)
    {
        std::string o;
        for (unsigned char c : s)
        {
            if (std::isalnum(c)) o += (char)std::tolower(c);
            else if (c == '_' || c == '-' || c == '.') o += (char)c;
            else if (!o.empty() && o.back() != '_') o += '_';
        }
        while (!o.empty() && o.back() == '_') o.pop_back();
        return o.empty() ? fallback : o;
    }

    std::string ClassName(const std::string& s, const char* fallback)
    {
        std::string o;
        bool up = true;
        for (unsigned char c : s)
        {
            if (std::isalnum(c)) { o += up ? (char)std::toupper(c) : (char)c; up = false; }
            else up = true;
        }
        if (o.empty() || std::isdigit((unsigned char)o[0])) o = fallback + o;
        return o;
    }

    bool IsSimpleFill(const Node* n)
    {
        return (n->type == NodeType::Rect || n->type == NodeType::Frame) && n->fill.type == 1 && !n->radius[0] && !n->radius[1] && !n->radius[2] && !n->radius[3] &&
               !n->stroke.type && !n->effect.type && !n->blur.on && !n->glass.on && !n->texture.on && !n->blend;
    }

    bool HasVisual(const Node* n)
    {
        switch (n->type)
        {
        case NodeType::Frame: case NodeType::Rect: case NodeType::Ellipse:
            return n->fill.type || n->stroke.type || n->effect.type || n->blur.on || n->glass.on || n->texture.on || n->hasHover || n->hasActive;
        case NodeType::Image: case NodeType::Vector: return true;
        case NodeType::Line: return n->stroke.type != 0;
        case NodeType::Path: return n->fill.type || n->stroke.type;
        default: return false;
        }
    }

    struct Tex { std::string path; int w = 0, h = 0; ImVec2 off; ImVec2 size; };

    struct Gen
    {
        Document& doc;
        const JavaOptions& opt;
        const BakeLayerFn& bake;
        std::string menuRes;                 // texture folder name
        fs::path assets;                     // .../assets/<modid>
        std::vector<std::string> files;
        std::vector<std::string> warnings;
        std::map<std::string, VarInfo> vars;
        std::map<std::string, std::string> varIdent;
        std::map<std::string, Node*> combos;
        std::map<std::string, std::string> fontIds;   // family|style -> font id ("" = vanilla)
        std::map<const Node*, std::string> nodeSym;
        std::vector<std::string> pressed;
        std::ostringstream specs;            // static animation specs
        std::ostringstream body;
        int ind = 2, counter = 0;

        VarStore restState;                  // defaults, for var-dependent geometry
        Gen(Document& d, const JavaOptions& o, const BakeLayerFn& b) : doc(d), opt(o), bake(b) { EnsureVars(doc, restState); }

        void L(const std::string& s) { body << std::string(ind * 4, ' ') << s << "\n"; }

        // ---- state
        void CollectState()
        {
            doc.ForEach([&](Node* n) {
                if ((n->role == Role::Checkbox || n->role == Role::Slider || n->role == Role::Input || n->role == Role::Combo || n->role == Role::MultiCombo) && n->var.empty())
                    n->var = SanitizeIdent(n->name.empty() ? n->id : n->name, "value");
                if ((n->role == Role::Combo || n->role == Role::MultiCombo) && !n->var.empty() && !combos.count(n->var)) combos[n->var] = n;
            });
            std::set<std::string> taken = { "anim", "render", "WIDTH", "HEIGHT" };
            for (auto& v : CollectVars(doc))
            {
                vars[v.name] = v;
                std::string id = SanitizeIdent(v.name, "value");
                while (taken.count(id)) id += "_";
                taken.insert(id);
                varIdent[v.name] = id;
            }
        }
        bool HasVar(const std::string& v) const { return !v.empty() && vars.count(v); }
        std::string V(const std::string& v) { return varIdent[v]; }
        // numeric view of a variable
        std::string Num(const std::string& v)
        {
            switch (vars[v].kind)
            {
            case VarKind::Bool: return "(" + V(v) + " ? 1f : 0f)";
            case VarKind::Float: return V(v);
            case VarKind::String: return "0f";
            default: return "(float) " + V(v);
            }
        }
        std::string Assign(const std::string& v, float value)
        {
            switch (vars[v].kind)
            {
            case VarKind::Bool: return V(v) + " = " + (value != 0 ? "true" : "false") + ";";
            case VarKind::Float: return V(v) + " = " + JF(value) + ";";
            case VarKind::String: return "";
            default: return V(v) + " = " + std::to_string((int)std::lround(value)) + ";";
            }
        }
        std::string Cond(const Node* n)
        {
            if (!HasVar(n->showVar) || vars[n->showVar].kind == VarKind::String) return "";
            std::string eq = "Math.abs(" + Num(n->showVar) + " - " + JF(n->showValue) + ") < 1e-4f";
            return n->showNot ? "!(" + eq + ")" : eq;
        }

        // ---- fonts
        std::string FontId(const std::string& family, const std::string& style)
        {
            std::string key = family + "|" + style;
            auto it = fontIds.find(key);
            if (it != fontIds.end()) return it->second;
            std::string id;
            const FontFace* face = opt.ttfFonts ? fonts::Find(family, style) : nullptr;
            if (face)
            {
                std::string ext = fs::u8path(face->path).extension().u8string();
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if (ext == ".ttf")
                {
                    id = ResName(face->family + "_" + face->style, "font");
                    std::vector<unsigned char> bytes;
                    fs::path dir = assets / "font";
                    fs::create_directories(dir);
                    if (platform::ReadFile(face->path, bytes) && platform::WriteFile(((dir / (id + ".ttf")).u8string()), bytes.data(), bytes.size()))
                    {
                        std::string json = "{\n  \"providers\": [\n    { \"type\": \"ttf\", \"file\": \"" + opt.modId + ":" + id + ".ttf\", \"shift\": [0, 0], \"size\": 16.0, \"oversample\": 4.0 }\n  ]\n}\n";
                        platform::WriteText((dir / (id + ".json")).u8string(), json);
                        files.push_back("assets/" + opt.modId + "/font/" + id + ".ttf");
                        files.push_back("assets/" + opt.modId + "/font/" + id + ".json");
                    }
                    else id.clear();
                }
                else warnings.push_back(face->family + " " + face->style + ": only TrueType (.ttf) fonts work in Minecraft, using the vanilla font");
            }
            fontIds[key] = id;
            return id;
        }

        // ---- textures
        bool BakeTex(Node* n, int variant, Tex* t)
        {
            std::vector<unsigned char> png;
            int w = 0, h = 0;
            ImVec2 off;
            if (!bake(n, variant, opt.textureScale, png, &w, &h, &off)) return false;
            std::string name = ResName(n->id, "layer") + (variant == 1 ? "_hover" : variant == 2 ? "_active" : variant == 3 ? "_full" : "");
            fs::path dir = assets / "textures" / "gui" / menuRes;
            fs::create_directories(dir);
            if (!platform::WriteFile((dir / (name + ".png")).u8string(), png.data(), png.size())) return false;
            t->path = "textures/gui/" + menuRes + "/" + name + ".png";
            t->w = w; t->h = h;
            t->off = off;
            t->size = ImVec2(w / opt.textureScale, h / opt.textureScale);
            files.push_back("assets/" + opt.modId + "/" + t->path);
            return true;
        }
        std::string DrawTex(const Tex& t, const std::string& x, const std::string& y, const std::string& alpha)
        {
            std::string x0 = x + " + " + JF(t.off.x), y0 = y + " + " + JF(t.off.y);
            return "g.texture(" + JStr(t.path) + ", " + std::to_string(t.w) + ", " + std::to_string(t.h) + ", " + x0 + ", " + y0 + ", " + x0 + " + " + JF(t.size.x) + ", " + y0 + " + " + JF(t.size.y) + ", " + alpha + ");";
        }

        // ---- text
        std::string TextExpr(const std::string& text)
        {
            if (text.find('{') == std::string::npos) return JStr(text);
            std::string out;
            auto add = [&](const std::string& e) { out += (out.empty() ? "" : " + ") + e; };
            size_t i = 0;
            while (i < text.size())
            {
                size_t p = text.find('{', i);
                size_t e = p == std::string::npos ? std::string::npos : text.find('}', p);
                if (p == std::string::npos || e == std::string::npos) { add(JStr(text.substr(i))); break; }
                if (p > i) add(JStr(text.substr(i, p - i)));
                std::string inner = text.substr(p + 1, e - p - 1);
                size_t colon = inner.find(':');
                std::string name = inner.substr(0, colon), fmt = colon == std::string::npos ? "" : inner.substr(colon + 1);
                if (!HasVar(name)) add(JStr(text.substr(p, e - p + 1)));
                else if (fmt == "label" && combos.count(name))
                {
                    Node* c = combos[name];
                    if (c->role == Role::MultiCombo) add("FimUi.options(O_" + V(name) + ", " + V(name) + ", " + JStr(c->noneLabel) + ", " + JStr(c->allLabel) + ")");
                    else add("FimUi.option(O_" + V(name) + ", " + V(name) + ")");
                }
                else if (vars[name].kind == VarKind::String) add(V(name));
                else if (vars[name].kind == VarKind::Bool) add("(" + V(name) + " ? \"on\" : \"off\")");
                else add("FimUi.fmt(" + JStr(fmt) + ", " + Num(name) + ")");
                i = e + 1;
            }
            return out.empty() ? "\"\"" : out;
        }

        // ---- auto layout at runtime (frames whose children appear / disappear with state)
        std::map<const Node*, std::string> layoutSym;   // frame -> "lyN": arrays lyNx / lyNy / lyNw / lyNh, size lyNs[0..1]
        std::map<const Node*, int> layoutIndex;
        int rotDepth = 0;

        bool DynLayout(const Node* n)
        {
            if (n->type != NodeType::Frame || !n->layout) return false;
            for (auto& c : n->children)
                if (c->visible && (!Cond(c.get()).empty() || DynSize(c.get()))) return true;
            return false;
        }
        bool DynSize(const Node* n) { return (n->hugW || n->hugH) && DynLayout(n); }

        void EmitLayout(Node* n)
        {
            if (layoutSym.count(n)) return;
            for (auto& c : n->children) if (c->visible && DynLayout(c.get())) EmitLayout(c.get());
            std::string ly = "ly" + std::to_string(++counter);
            bool horiz = n->layout == 1;
            std::string ws, hs, vis, fm, fc;
            int i = 0;
            for (auto& cp : n->children)
            {
                Node* c = cp.get();
                if (!c->visible) continue;
                std::string cond = Cond(c);
                bool ds = DynSize(c);
                auto add = [](std::string& list, const std::string& v) { list += (list.empty() ? "" : ", ") + v; };
                add(ws, ds ? layoutSym[c] + "s[0]" : JF(c->w));
                add(hs, ds ? layoutSym[c] + "s[1]" : JF(c->h));
                add(vis, cond.empty() ? "true" : cond);
                add(fm, (horiz ? c->fillW : c->fillH) ? "true" : "false");
                add(fc, (horiz ? c->fillH : c->fillW) ? "true" : "false");
                layoutIndex[c] = i++;
            }
            L("// auto layout of " + (n->name.empty() ? std::string("frame") : n->name) + " (layers hidden by state take no space)");
            L("float[] " + ly + "x = new float[" + std::to_string(i) + "], " + ly + "y = new float[" + std::to_string(i) + "], " + ly + "w = { " + ws + " }, " + ly + "h = { " + hs + " };");
            L("float[] " + ly + "s = { " + JF(n->w) + ", " + JF(n->h) + " };");
            L("FimUi.autoLayout(" + ly + "x, " + ly + "y, " + ly + "w, " + ly + "h, new boolean[] { " + vis + " }, new boolean[] { " + fm + " }, new boolean[] { " + fc + " }, " +
              (horiz ? "true" : "false") + ", " + JF(n->gap) + ", " + JF(n->pad[0]) + ", " + JF(n->pad[1]) + ", " + JF(n->pad[2]) + ", " + JF(n->pad[3]) + ", " +
              std::to_string(n->alignMain) + ", " + std::to_string(n->alignCross) + ", " + ((horiz ? n->hugW : n->hugH) ? "true" : "false") + ", " +
              ((horiz ? n->hugH : n->hugW) ? "true" : "false") + ", " + ly + "s);");
            layoutSym[n] = ly;
        }

        // ---- nodes
        void Emit(Node* n, const std::string& px, const std::string& py, const std::string& pa, bool isRoot, const AnimFx* inherited, float extraDelay)
        {
            if (!n->visible) return;
            std::string s = "n" + std::to_string(++counter);
            nodeSym[n] = s;
            L("// " + (n->name.empty() ? std::string(NodeTypeName(n->type)) : n->name) + " [" + NodeTypeName(n->type) + "]");
            std::string cond = Cond(n);
            L(cond.empty() ? "{" : "if (" + cond + ") {");
            ind++;

            // geometry (design px, relative to the parent)
            ImVec2 wa, wb, pa0(0, 0), pb0;
            NodeWorldRect(n, &restState, &wa, &wb);
            if (n->parent && !isRoot) NodeWorldRect(n->parent, &restState, &pa0, &pb0);
            float X = isRoot ? 0 : wa.x - pa0.x, Y = isRoot ? 0 : wa.y - pa0.y, W = wb.x - wa.x, H = wb.y - wa.y;
            if (DynLayout(n)) EmitLayout(n);
            std::string XS = JF(X), YS = JF(Y), WS = JF(W), HS = JF(H);
            if (!isRoot && n->parent && layoutSym.count(n->parent) && layoutIndex.count(n))
            {
                std::string ly = layoutSym[n->parent], ix = "[" + std::to_string(layoutIndex[n]) + "]";
                XS = ly + "x" + ix; YS = ly + "y" + ix; WS = ly + "w" + ix; HS = ly + "h" + ix;
            }
            else if (layoutSym.count(n)) { WS = layoutSym[n] + "s[0]"; HS = layoutSym[n] + "s[1]"; }
            Node* owner = n->sliderPart ? SliderOwner(n) : nullptr;
            bool part = owner && nodeSym.count(owner) && HasVar(owner->var);
            std::string x = s + "x", y = s + "y", w = s + "w", h = s + "h";
            if (part)
            {
                std::string o = nodeSym[owner];
                float span = owner->vmax - owner->vmin;
                L("float " + s + "t = Math.max(0f, Math.min(1f, (" + Num(owner->var) + " - " + JF(owner->vmin) + ") / " + JF(span == 0 ? 1.0f : span) + "));");
                if (n->sliderPart == 1) L("float " + x + " = " + o + "x, " + y + " = " + py + " + " + YS + ", " + w + " = " + o + "w * " + s + "t, " + h + " = " + HS + ";");
                else L("float " + x + " = " + o + "x + " + o + "w * " + s + "t - " + JF(W * 0.5f) + ", " + y + " = " + py + " + " + YS + ", " + w + " = " + JF(W) + ", " + h + " = " + HS + ";");
            }
            else L("float " + x + " = " + px + " + " + XS + ", " + y + " = " + py + " + " + YS + ", " + w + " = " + WS + ", " + h + " = " + HS + ";");

            // interaction (on the resting rect)
            std::string hov = "false", act = "false", held = "false";
            std::string id = JStr(n->id);
            bool hasVar = HasVar(n->var);
            std::string rect = x + ", " + y + ", " + x + " + " + w + ", " + y + " + " + h;
            switch (n->role)
            {
            case Role::Button:
            case Role::Tab:
                L("FimUi.Item " + s + "i = ui.item(" + id + ", " + rect + ");");
                hov = s + "i.hovered"; held = s + "i.held";
                if (hasVar) L("if (" + s + "i.clicked) { " + Assign(n->var, n->value) + " }");
                if (n->role == Role::Button)
                {
                    std::string flag = SanitizeIdent(n->name.empty() ? n->id : n->name, "button") + "_pressed";
                    if (std::find(pressed.begin(), pressed.end(), flag) == pressed.end()) pressed.push_back(flag);
                    L("if (" + s + "i.clicked) " + flag + " = true;");
                    act = s + "i.held";
                }
                else if (hasVar) act = "(Math.abs(" + Num(n->var) + " - " + JF(n->value) + ") < 1e-4f)";
                break;
            case Role::Checkbox:
                L("FimUi.Item " + s + "i = ui.item(" + id + ", " + rect + ");");
                hov = s + "i.hovered"; held = s + "i.held";
                if (hasVar)
                {
                    if (vars[n->var].kind == VarKind::Bool) { L("if (" + s + "i.clicked) " + V(n->var) + " = !" + V(n->var) + ";"); act = V(n->var); }
                    else { L("if (" + s + "i.clicked) " + V(n->var) + " = " + V(n->var) + " != 0 ? 0 : 1;"); act = "(" + V(n->var) + " != 0)"; }
                }
                break;
            case Role::Slider:
                L("FimUi.Item " + s + "i = new FimUi.Item();");
                if (hasVar && vars[n->var].kind == VarKind::Float)
                    L(V(n->var) + " = ui.slider(" + id + ", " + rect + ", " + V(n->var) + ", " + JF(n->vmin) + ", " + JF(n->vmax) + ", " + s + "i);");
                else if (hasVar && (vars[n->var].kind == VarKind::Int))
                    L(V(n->var) + " = Math.round(ui.slider(" + id + ", " + rect + ", " + V(n->var) + ", " + JF(n->vmin) + ", " + JF(n->vmax) + ", " + s + "i));");
                hov = "(" + s + "i.hovered || " + s + "i.held)"; act = held = s + "i.held";
                break;
            case Role::Combo:
            case Role::MultiCombo:
            {
                if (!hasVar || !combos.count(n->var)) break;
                const Node* label = nullptr;
                for (auto& c : n->children) if (c->type == NodeType::Text) { label = c.get(); break; }
                std::string st = s + "cs";
                fim::ComboStyle ref = ComboStyleFor(n, 1.0f);
                L("FimUi.ComboStyle " + st + " = new FimUi.ComboStyle();");
                L(st + ".bg = " + Argb(n->listFill) + "; " + st + ".accent = " + Argb(n->listAccent) + "; " + st + ".hover = " + Argb(ImGui::ColorConvertU32ToFloat4(ref.hover)) +
                  "; " + st + ".border = " + Argb(ImGui::ColorConvertU32ToFloat4(ref.border)) + ";");
                L(st + ".text = " + Argb(label ? label->fill.c0 : ImVec4(0.9f, 0.9f, 0.93f, 1)) + "; " + st + ".fontSize = " + JF(label ? label->fontSize : 13.0f) + "; " +
                  st + ".itemHeight = " + JF(n->itemHeight > 0 ? n->itemHeight : H) + "; " + st + ".padX = " + JF(label ? label->x : 12.0f) + ";");
                std::string fid = label ? FontId(label->fontFamily, label->fontStyle) : "";
                L(st + ".font = " + (fid.empty() ? "null" : JStr(fid)) + ";" + (n->role == Role::MultiCombo ? " " + st + ".allLabel = " + JStr(n->allLabel) + ";" : ""));
                L("FimUi.Item " + s + "i = new FimUi.Item();");
                L(V(n->var) + " = ui." + (n->role == Role::Combo ? "combo" : "multiCombo") + "(g, " + id + ", " + rect + ", " + V(n->var) + ", O_" + V(n->var) + ", " + st + ", " + s + "i);");
                hov = s + "i.hovered"; act = held = s + "i.held";
                break;
            }
            case Role::Input:
                L("FimUi.Item " + s + "i = new FimUi.Item();");
                if (hasVar && vars[n->var].kind == VarKind::String) L(V(n->var) + " = ui.input(" + id + ", " + rect + ", " + V(n->var) + ", " + s + "i);");
                hov = s + "i.hovered"; act = held = s + "i.held";
                break;
            default: break;
            }

            // animation
            const AnimFx* spec = (n->anim.preset && n->anim.stagger <= 0.0f) ? &n->anim : (n->anim.preset ? nullptr : inherited);
            float delay = spec == inherited ? extraDelay : 0.0f;
            std::string alpha = s + "a";
            std::string anim;
            if (spec)
            {
                std::string spSym = "A_" + s;
                std::ostringstream sp;
                sp << "    private static final FimAnim.Spec " << spSym << " = new FimAnim.Spec(" << spec->preset << ", " << spec->trigger << ", " << spec->easing << ", "
                   << JF(spec->duration) << ", " << JF(spec->delay + delay) << ", " << JF(spec->distance) << ", " << JF(spec->scale) << ", " << JF(spec->intensity) << ", new float[] { "
                   << JF(spec->bezier[0]) << ", " << JF(spec->bezier[1]) << ", " << JF(spec->bezier[2]) << ", " << JF(spec->bezier[3]) << " }";
                if (spec->preset == fim::Anim_Keyframes)
                    for (auto& t : spec->tracks)
                    {
                        std::string ts, vs, es;
                        for (auto& k : t.keys) { ts += (ts.empty() ? "" : ", ") + JF(k.time); vs += (vs.empty() ? "" : ", ") + JF(k.value); es += (es.empty() ? "" : ", ") + std::to_string(k.easing); }
                        sp << ",\n        new FimAnim.Track(" << t.prop << ", new float[] { " << ts << " }, new float[] { " << vs << " }, new int[] { " << es << " })";
                    }
                sp << ");\n";
                specs << sp.str();
                std::string hovE = n->role != Role::None ? hov : "ui.inside(" + rect + ")";
                std::string heldE = n->role != Role::None ? held : "(" + hovE + " && ui.down)";
                anim = s + "v";
                L("FimAnim.Value " + anim + " = anim.update(" + JStr(n->id + "#m") + ", " + spSym + ", " + hovE + ", " + heldE + ", " + act + ", ui);");
                // the static rotation goes into the same transform
                std::string rot = n->rotation != 0 ? " g.rotate(" + JF(n->rotation) + " + " + anim + ".rot);" : " if (" + anim + ".rot != 0) g.rotate(" + anim + ".rot);";
                L("boolean " + s + "T = " + (n->rotation != 0 ? std::string("true") : "!" + anim + ".isIdentity()") + ";");
                L("if (" + s + "T) { float cx = " + x + " + " + w + " / 2, cy = " + y + " + " + h + " / 2; g.push(); g.translate(cx + " + anim + ".dx, cy + " + anim + ".dy); g.scale(" + anim + ".scale);" + rot + " g.translate(-cx, -cy); }");
                L("float " + alpha + " = " + pa + " * " + JF(n->opacity) + " * " + anim + ".alpha;");
            }
            else
            {
                if (n->rotation != 0)
                {
                    anim = "rot";   // only the pop below uses it
                    L("boolean " + s + "T = true;");
                    L("{ float cx = " + x + " + " + w + " / 2, cy = " + y + " + " + h + " / 2; g.push(); g.translate(cx, cy); g.rotate(" + JF(n->rotation) + "); g.translate(-cx, -cy); }");
                }
                L("float " + alpha + " = " + pa + " * " + JF(n->opacity) + ";");
            }
            bool rotated = n->rotation != 0 || (spec && spec->preset == fim::Anim_Keyframes && spec->Track(fim::Prop_Rotation));
            if (rotated) rotDepth++;

            // hover / active transitions
            std::string hv, av;
            if (n->role != Role::None && n->hasHover) { hv = s + "hv"; L("float " + hv + " = ui.animate(" + JStr(n->id + "#h") + ", " + hov + " ? 1f : 0f, 14f);"); }
            if (n->role != Role::None && n->hasActive) { av = s + "av"; L("float " + av + " = ui.animate(" + JStr(n->id + "#a") + ", " + act + " ? 1f : 0f, 14f);"); }

            // visual
            if (n->type == NodeType::Text) EmitText(n, s, x, y, w, h, alpha);
            else if (HasVisual(n))
            {
                if (IsSimpleFill(n))
                {
                    std::string c = Argb(n->fill.c0);
                    if (!hv.empty()) c = "FimUi.lerpColor(" + c + ", " + Argb(n->hoverColor) + ", " + hv + ")";
                    if (!av.empty()) c = "FimUi.lerpColor(" + c + ", " + Argb(n->activeColor) + ", " + av + ")";
                    L("g.fill(" + rect + ", FimUi.fade(" + c + ", " + alpha + "));");
                }
                else
                {
                    Tex base;
                    bool fullWidth = part && n->sliderPart == 1;
                    if (BakeTex(n, fullWidth ? 3 : 0, &base))
                    {
                        if (fullWidth)
                        {
                            // the fill is baked at the full slider width and revealed up to the value
                            L("g.scissor(" + rect + ");");
                            L(DrawTex(base, x, y, alpha));
                            L("g.endScissor();");
                        }
                        else L(DrawTex(base, x, y, alpha));
                        Tex tv;
                        if (!hv.empty() && BakeTex(n, 1, &tv)) L("if (" + hv + " > 0) " + DrawTex(tv, x, y, alpha + " * " + hv));
                        if (!av.empty() && BakeTex(n, 2, &tv)) L("if (" + av + " > 0) " + DrawTex(tv, x, y, alpha + " * " + av));
                    }
                    else warnings.push_back("Could not bake layer " + n->name);
                    if (n->glass.on || n->blur.on) warnings.push_back(n->name + ": background blur / glass is baked without the content behind it");
                }
            }

            // children
            bool clip = n->type == NodeType::Frame && n->clip && !n->children.empty() && !rotDepth;   // scissor cannot turn
            if (clip) L("g.scissor(" + rect + ");");
            bool stagger = n->anim.preset && n->anim.stagger > 0;
            for (size_t i = 0; i < n->children.size(); i++)
                Emit(n->children[i].get(), x, y, alpha, false, stagger ? &n->anim : nullptr, stagger ? n->anim.stagger * (float)i : 0.0f);
            if (clip) L("g.endScissor();");
            if (!anim.empty()) L("if (" + s + "T) g.pop();");
            if (rotated) rotDepth--;
            ind--;
            L("}");
        }

        void EmitText(Node* n, const std::string& s, const std::string& x, const std::string& y, const std::string& w, const std::string& h, const std::string& alpha)
        {
            std::string fid = FontId(n->fontFamily, n->fontStyle);
            std::string str = TextExpr(n->text);
            ImVec4 col = n->fill.type ? n->fill.c0 : ImVec4(0, 0, 0, 1);
            std::string color = "FimUi.fade(" + Argb(col) + ", " + alpha + ")";
            // text inside an input field shows the typed value (the design text is the placeholder)
            Node* field = n->parent && n->parent->role == Role::Input && HasVar(n->parent->var) && vars[n->parent->var].kind == VarKind::String ? n->parent : nullptr;
            if (field)
            {
                std::string v = V(field->var);
                str = v + ".isEmpty() && !ui.focused(" + JStr(field->id) + ") ? " + str + " : " + v + " + (ui.focused(" + JStr(field->id) + ") && (ui.frame / 30) % 2 == 0 ? \"_\" : \"\")";
                color = v + ".isEmpty() && !ui.focused(" + JStr(field->id) + ") ? FimUi.fade(" + Argb(col) + ", " + alpha + " * 0.55f) : " + color;
            }
            float lh = n->lineHeight > 0 ? n->lineHeight : n->fontSize * 1.2f;
            // An auto-width text box fits the editor's text exactly; Minecraft measures a little differently,
            // so keep the text anchored to the side of the parent it sits against.
            int alignH = n->alignH;
            if (n->textSizing == 1 && n->parent)
            {
                ImVec2 a, b, pa, pb;
                NodeWorldRect(n, &restState, &a, &b);
                NodeWorldRect(n->parent, &restState, &pa, &pb);
                float left = a.x - pa.x, right = pb.x - b.x;
                alignH = std::fabs(left - right) < 2.0f ? 1 : right < left ? 2 : 0;
            }
            L("text(g, " + str + ", " + x + ", " + y + ", " + w + ", " + h + ", " + JF(n->fontSize) + ", " + JF(lh) + ", " + std::to_string(alignH) + ", " + std::to_string(n->alignV) + ", " +
              color + ", " + (fid.empty() ? "null" : JStr(fid)) + ");");
            (void)s;
        }
    };

    std::string ReadRuntime(const std::string& dir, const char* name, const std::string& pkg)
    {
        std::string text;
        if (!platform::ReadText((fs::u8path(dir) / "java" / name).u8string(), text)) return "";
        size_t p;
        while ((p = text.find("__PACKAGE__")) != std::string::npos) text.replace(p, 11, pkg);
        return text;
    }

    std::string ScreenSource(const JavaOptions& opt, const std::string& cls, const std::string& menu, const std::string& title)
    {
        std::ostringstream o;
        bool fabric = opt.loader == 0;
        o << "package " << opt.package << ";\n\n";
        o << "import " << opt.package << ".fimgui.FimGfx;\nimport " << opt.package << ".fimgui.FimUi;\n";
        o << "import com.mojang.blaze3d.systems.RenderSystem;\n";
        if (fabric)
            o << "import net.minecraft.client.gui.DrawContext;\nimport net.minecraft.client.gui.screen.Screen;\nimport net.minecraft.text.Text;\nimport net.minecraft.util.Identifier;\n"
                 "import net.minecraft.util.math.RotationAxis;\n";
        else
            o << "import com.mojang.math.Axis;\nimport net.minecraft.client.gui.GuiGraphics;\nimport net.minecraft.client.gui.screens.Screen;\nimport net.minecraft.network.chat.Component;\n"
                 "import net.minecraft.resources.ResourceLocation;\n";
        o << "import org.lwjgl.glfw.GLFW;\n\nimport java.util.ArrayDeque;\nimport java.util.HashMap;\nimport java.util.Map;\n\n";
        std::string G = fabric ? "DrawContext" : "GuiGraphics", T = fabric ? "Text" : "Component", RL = fabric ? "Identifier" : "ResourceLocation";
        std::string rl = fabric ? "Identifier.of(MOD_ID, path)" : "ResourceLocation.fromNamespaceAndPath(MOD_ID, path)";
        std::string fontField = fabric ? "textRenderer" : "font";
        o << "/**\n * " << title << " - generated by Fimgui for Minecraft 1.21.1 (" << (fabric ? "Fabric, Yarn mappings" : "NeoForge, Mojang mappings") << ").\n"
          << " * Open it with: " << (fabric ? "MinecraftClient.getInstance()" : "Minecraft.getInstance()") << ".setScreen(new " << cls << "());\n"
          << " * The design is drawn by " << menu << " in design pixels and fitted into the window.\n */\n";
        o << "public class " << cls << " extends Screen {\n";
        o << "    public static final String MOD_ID = \"" << opt.modId << "\";\n";
        o << "    public final " << menu << " menu = new " << menu << "();\n";
        o << "    private final FimUi ui = new FimUi();\n";
        o << "    private float fit = 1, ox, oy;\n\n";
        o << "    public " << cls << "() {\n        super(" << T << ".literal(" << JStr(title) << "));\n    }\n\n";
        o << "    @Override\n    protected void init() {\n        menu.anim.restart();\n    }\n\n";
        o << "    private void layout() {\n"
          << "        fit = Math.min(1f, Math.min((width - 16) / " << menu << ".WIDTH, (height - 16) / " << menu << ".HEIGHT));\n"
          << "        ox = (width - " << menu << ".WIDTH * fit) / 2;\n        oy = (height - " << menu << ".HEIGHT * fit) / 2;\n    }\n\n";
        o << "    private double dx(double x) { return (x - ox) / fit; }\n    private double dy(double y) { return (y - oy) / fit; }\n\n";
        o << "    @Override\n    public void render(" << G << " ctx, int mouseX, int mouseY, float delta) {\n"
          << "        renderBackground(ctx, mouseX, mouseY, delta);\n"
          << "        layout();\n"
          << "        Gfx g = new Gfx(ctx);\n"
          << "        ui.beginFrame(dx(mouseX), dy(mouseY), System.nanoTime() / 1e9);\n"
          << "        g.push();\n        g.translate(ox, oy);\n        g.scale(fit);\n"
          << "        menu.render(g, ui);\n"
          << "        ui.endFrame();\n"
          << "        g.pop();\n    }\n\n";
        o << "    @Override\n    public boolean mouseClicked(double mouseX, double mouseY, int button) {\n"
          << "        if (button == 0) { ui.mx = dx(mouseX); ui.my = dy(mouseY); ui.down = true; ui.pressed = true; }\n        return true;\n    }\n\n";
        o << "    @Override\n    public boolean mouseReleased(double mouseX, double mouseY, int button) {\n"
          << "        if (button == 0) { ui.down = false; ui.released = true; }\n        return true;\n    }\n\n";
        o << "    @Override\n    public boolean mouseDragged(double mouseX, double mouseY, int button, double deltaX, double deltaY) {\n        return true;\n    }\n\n";
        o << "    @Override\n    public boolean mouseScrolled(double mouseX, double mouseY, double horizontalAmount, double verticalAmount) {\n"
          << "        ui.wheel += (float) verticalAmount;\n        return true;\n    }\n\n";
        o << "    @Override\n    public boolean charTyped(char chr, int modifiers) {\n"
          << "        if (ui.wantsKeyboard()) { ui.charTyped(chr); return true; }\n        return super.charTyped(chr, modifiers);\n    }\n\n";
        o << "    @Override\n    public boolean keyPressed(int keyCode, int scanCode, int modifiers) {\n"
          << "        if (ui.wantsKeyboard()) {\n"
          << "            if (keyCode == GLFW.GLFW_KEY_BACKSPACE) ui.keyBackspace();\n"
          << "            else if (keyCode == GLFW.GLFW_KEY_ENTER || keyCode == GLFW.GLFW_KEY_KP_ENTER) ui.keyEnter();\n"
          << "            else if (keyCode == GLFW.GLFW_KEY_ESCAPE) ui.keyEscape();\n"
          << "            return true;\n        }\n"
          << "        return super.keyPressed(keyCode, scanCode, modifiers);\n    }\n\n";
        o << "    @Override\n    public boolean " << (fabric ? "shouldPause" : "isPauseScreen") << "() {\n        return false;\n    }\n\n";

        // Gfx adapter
        o << "    // Draws in design pixels: the matrix stack does the transform, a mirror of it is kept for scissor rectangles.\n";
        o << "    private final class Gfx implements FimGfx {\n";
        o << "        private static final float TTF_SIZE = 16f, VANILLA_SIZE = 9f;\n";
        o << "        private final " << G << " c;\n        private float tx, ty, s = 1;\n        private final ArrayDeque<float[]> stack = new ArrayDeque<>();\n\n";
        o << "        Gfx(" << G << " c) { this.c = c; }\n\n";
        std::string push = fabric ? "c.getMatrices().push()" : "c.pose().pushPose()", pop = fabric ? "c.getMatrices().pop()" : "c.pose().popPose()";
        std::string mat = fabric ? "c.getMatrices()" : "c.pose()";
        o << "        @Override public void push() { " << push << "; stack.push(new float[] { tx, ty, s }); }\n";
        o << "        @Override public void pop() { " << pop << "; float[] t = stack.pop(); tx = t[0]; ty = t[1]; s = t[2]; }\n";
        o << "        @Override public void translate(float x, float y) { " << mat << ".translate(x, y, 0f); tx += x * s; ty += y * s; }\n";
        o << "        @Override public void scale(float k) { " << mat << ".scale(k, k, 1f); s *= k; }\n";
        o << "        // scissor rectangles stay axis-aligned: the generated menu does not clip inside rotated layers\n";
        o << "        @Override public void rotate(float degrees) { " << mat << (fabric ? ".multiply(RotationAxis.POSITIVE_Z.rotationDegrees(degrees))" : ".mulPose(Axis.ZP.rotationDegrees(degrees))") << "; }\n\n";
        o << "        @Override public void fill(float x0, float y0, float x1, float y1, int argb) {\n"
          << "            if ((argb >>> 24) == 0 || x1 <= x0 || y1 <= y0) return;\n"
          << "            " << push << ";\n            " << mat << ".translate(x0, y0, 0f);\n            " << mat << ".scale(x1 - x0, y1 - y0, 1f);\n"
          << "            c.fill(0, 0, 1, 1, argb);\n            " << pop << ";\n        }\n\n";
        o << "        private final Map<String, " << RL << "> ids = new HashMap<>();\n";
        o << "        private " << RL << " id(String path) { return ids.computeIfAbsent(path, p -> " << (fabric ? "Identifier.of(MOD_ID, p)" : "ResourceLocation.fromNamespaceAndPath(MOD_ID, p)") << "); }\n\n";
        (void)rl;
        o << "        @Override public void texture(String path, int texW, int texH, float x0, float y0, float x1, float y1, float alpha) {\n"
          << "            if (alpha <= 0.004f) return;\n"
          << "            RenderSystem.enableBlend();\n"
          << "            c." << (fabric ? "setShaderColor" : "setColor") << "(1f, 1f, 1f, Math.min(1f, alpha));\n"
          << "            " << push << ";\n            " << mat << ".translate(x0, y0, 0f);\n            " << mat << ".scale((x1 - x0) / texW, (y1 - y0) / texH, 1f);\n"
          << "            c." << (fabric ? "drawTexture" : "blit") << "(id(path), 0, 0, texW, texH, 0f, 0f, texW, texH, texW, texH);\n"
          << "            " << pop << ";\n"
          << "            c." << (fabric ? "setShaderColor" : "setColor") << "(1f, 1f, 1f, 1f);\n"
          << "            RenderSystem.disableBlend();\n        }\n\n";
        o << "        private " << T << " styled(String str, String fontId) {\n";
        if (fabric) o << "            return fontId == null ? Text.literal(str) : Text.literal(str).styled(st -> st.withFont(id(fontId)));\n";
        else o << "            return fontId == null ? Component.literal(str) : Component.literal(str).withStyle(st -> st.withFont(id(fontId)));\n";
        o << "        }\n\n";
        o << "        @Override public void text(String str, float x, float y, float size, int argb, String fontId) {\n"
          << "            if ((argb >>> 24) < 4 || str.isEmpty()) return;\n"
          << "            float k = size / (fontId == null ? VANILLA_SIZE : TTF_SIZE);\n"
          << "            " << push << ";\n            " << mat << ".translate(x, y + size * 0.12f, 0f);\n            " << mat << ".scale(k, k, 1f);\n"
          << "            c." << (fabric ? "drawText" : "drawString") << "(" << fontField << ", styled(str, fontId), 0, 0, argb, false);\n"
          << "            " << pop << ";\n        }\n\n";
        o << "        @Override public float textWidth(String str, float size, String fontId) {\n"
          << "            return " << fontField << "." << (fabric ? "getWidth" : "width") << "(styled(str, fontId)) * size / (fontId == null ? VANILLA_SIZE : TTF_SIZE);\n        }\n\n";
        o << "        @Override public void scissor(float x0, float y0, float x1, float y1) {\n"
          << "            c.enableScissor(Math.round(tx + x0 * s), Math.round(ty + y0 * s), Math.round(tx + x1 * s), Math.round(ty + y1 * s));\n        }\n\n";
        o << "        @Override public void endScissor() { c.disableScissor(); }\n    }\n}\n";
        return o.str();
    }
}

ExportResult ExportJava(Document& doc, const JavaOptions& opt, const std::string& runtimeDir, const BakeLayerFn& bake)
{
    ExportResult res;
    if (opt.outDir.empty()) { res.message = "Choose an output folder"; return res; }
    Node* root = nullptr;
    for (auto& r : doc.roots) if (r->type == NodeType::Frame && r->visible) { root = r.get(); break; }
    if (!root) { res.message = "Nothing to export: add a frame first"; return res; }

    std::string pkg = opt.package.empty() ? "com.example.gui" : opt.package;
    std::string base = opt.className.empty() ? ClassName(root->name, "Fimgui") : ClassName(opt.className, "Fimgui");
    if (base.size() > 4 && base.compare(base.size() - 4, 4, "Menu") == 0) base.resize(base.size() - 4);
    std::string menu = base + "Menu", screen = base + "Screen";
    fs::path out = fs::u8path(opt.outDir);
    fs::path java = out / "src" / "main" / "java";
    std::string pkgPath = pkg;
    std::replace(pkgPath.begin(), pkgPath.end(), '.', '/');
    fs::path pkgDir = java / fs::u8path(pkgPath);
    std::error_code ec;
    fs::create_directories(pkgDir / "fimgui", ec);
    if (ec) { res.message = "Cannot create " + pkgDir.u8string(); return res; }

    // Static positions are those of the starting state; frames with state-dependent children lay out at runtime.
    VarStore start;
    EnsureVars(doc, start);
    const VarStore* layoutVars = g_layoutVars;
    g_layoutVars = &start;
    doc.ApplyLayout();
    g_layoutVars = layoutVars;

    Gen g(doc, opt, bake);
    g.menuRes = ResName(base, "menu");
    g.assets = out / "src" / "main" / "resources" / "assets" / fs::u8path(opt.modId);
    g.CollectState();

    // body
    g.Emit(root, "0f", "0f", "1f", true, nullptr, 0.0f);

    std::ostringstream m;
    m << "package " << pkg << ";\n\n";
    m << "import " << pkg << ".fimgui.FimAnim;\nimport " << pkg << ".fimgui.FimGfx;\nimport " << pkg << ".fimgui.FimUi;\n\n";
    m << "/**\n * \"" << root->name << "\" - generated by Fimgui. Do not edit by hand: change the design and export again.\n"
      << " * State fields are public: read them from your mod (settings, toggles), set them to change the menu.\n */\n";
    m << "public class " << menu << " {\n";
    m << "    public static final float WIDTH = " << JF(root->w) << ", HEIGHT = " << JF(root->h) << ";\n\n";
    m << "    // ---- state\n";
    for (auto& [name, v] : g.vars)
    {
        std::string id = g.varIdent[name];
        switch (v.kind)
        {
        case VarKind::Bool: m << "    public boolean " << id << " = " << (v.def != 0 ? "true" : "false") << ";\n"; break;
        case VarKind::Float: m << "    public float " << id << " = " << JF(v.def) << ";\n"; break;
        case VarKind::String: m << "    public String " << id << " = \"\";\n"; break;
        default: m << "    public int " << id << " = " << (int)std::lround(v.def) << ";\n"; break;
        }
    }
    for (auto& p : g.pressed) m << "    /** Set to true when the button is clicked; reset it after handling. */\n    public boolean " << p << ";\n";
    for (auto& [var, n] : g.combos)
    {
        if (!g.varIdent.count(var)) continue;
        m << "    static final String[] O_" << g.varIdent[var] << " = { ";
        for (size_t i = 0; i < n->options.size(); i++) m << (i ? ", " : "") << JStr(n->options[i]);
        m << " };\n";
    }
    m << "\n    public final FimAnim anim = new FimAnim();\n\n";
    m << "    // ---- animation specs\n" << g.specs.str() << "\n";
    m << "    public void render(FimGfx g, FimUi ui) {\n" << g.body.str() << "    }\n\n";
    m << "    // Multi-line text with the alignment of the design.\n"
      << "    private static void text(FimGfx g, String s, float x, float y, float w, float h, float size, float lineH, int alignH, int alignV, int argb, String font) {\n"
      << "        String[] lines = s.split(\"\\n\", -1);\n"
      << "        float total = lines.length * lineH;\n"
      << "        float ty = alignV == 1 ? y + (h - total) / 2 : alignV == 2 ? y + h - total : y;\n"
      << "        for (String line : lines) {\n"
      << "            float tw = alignH == 0 ? 0 : g.textWidth(line, size, font);\n"
      << "            float tx = alignH == 1 ? x + (w - tw) / 2 : alignH == 2 ? x + w - tw : x;\n"
      << "            g.text(line, tx, ty + (lineH - size) / 2, size, argb, font);\n"
      << "            ty += lineH;\n"
      << "        }\n    }\n}\n";

    auto write = [&](const fs::path& p, const std::string& text, const std::string& rel) {
        if (platform::WriteText(p.u8string(), text)) res.files.push_back(rel);
        else res.message = "Cannot write " + p.u8string();
    };
    std::string javaRel = "src/main/java/" + pkgPath + "/";
    write(pkgDir / (menu + ".java"), m.str(), javaRel + menu + ".java");
    write(pkgDir / (screen + ".java"), ScreenSource(opt, screen, menu, root->name), javaRel + screen + ".java");
    for (const char* f : { "FimAnim.java", "FimUi.java", "FimGfx.java" })
    {
        std::string src = ReadRuntime(runtimeDir, f, pkg);
        if (src.empty()) { res.message = std::string("Java runtime not found: ") + f; return res; }
        write(pkgDir / "fimgui" / f, src, javaRel + "fimgui/" + f);
    }
    for (auto& f : g.files) res.files.push_back("src/main/resources/" + f);

    // license + readme
    std::vector<unsigned char> lic;
    if (platform::ReadFile((fs::u8path(runtimeDir) / "FIMGUI_LICENSE.txt").u8string(), lic) && platform::WriteFile((out / "LICENSE-FIMGUI.txt").u8string(), lic.data(), lic.size()))
        res.files.push_back("LICENSE-FIMGUI.txt");
    bool fabric = opt.loader == 0;
    std::ostringstream rd;
    rd << "# " << root->name << " - Minecraft GUI\n\nGenerated by [Fimgui](https://github.com/dis3j/FImgui) for **Minecraft 1.21.1, "
       << (fabric ? "Fabric (Yarn mappings)" : "NeoForge (Mojang mappings)") << "**.\n\n"
       << "## Add to your mod\n\nCopy `src/` into your mod project (it merges with your `src/main`). The mod id is `" << opt.modId << "` and the package `" << pkg << "`.\n"
       << "The screen is client-only code: keep it in your client source set or behind a client check.\n\n"
       << "Open the menu, for example from a key binding:\n\n```java\n"
       << (fabric ? "MinecraftClient.getInstance().setScreen(new " : "Minecraft.getInstance().setScreen(new ") << screen << "());\n```\n\n"
       << "Read or change the state from your code:\n\n```java\n" << screen << " s = new " << screen << "();\n";
    for (auto& [name, v] : g.vars)
    {
        const char* type = v.kind == VarKind::Bool ? "boolean" : v.kind == VarKind::Float ? "float" : v.kind == VarKind::String ? "String" : "int";
        rd << type << " " << g.varIdent[name] << " = s.menu." << g.varIdent[name] << ";\n";
    }
    rd << "```\n\n## Files\n\n- `" << menu << ".java` - the design: state fields, layout, interaction, animations (loader independent)\n"
       << "- `" << screen << ".java` - the Screen and its drawing adapter for " << (fabric ? "Fabric" : "NeoForge") << "\n"
       << "- `fimgui/` - Fimgui Java runtime (animations, input, widgets)\n"
       << "- `assets/" << opt.modId << "/textures/gui/" << g.menuRes << "/` - baked layers (fills, gradients, strokes, shadows, images, icons) at " << (int)opt.textureScale << "x\n"
       << (g.fontIds.empty() ? "" : "- `assets/" + opt.modId + "/font/` - TTF font providers for the design fonts\n")
       << "\n## How it maps\n\n- Shapes with effects are baked into textures, plain rectangles use `fill`, texts use the Minecraft text renderer with your TTF fonts.\n"
       << "- Buttons, tabs, checkboxes, sliders, dropdowns and text fields work like in the Fimgui preview; hover and active colors fade in.\n"
       << "- Entrance, loop, hover / press animations and keyframe timelines run through `FimAnim`, the same math as the editor.\n"
       << "- The menu is fitted into the window (never scaled above 1 design px per GUI px).\n\n"
       << "## Limitations\n\n- Background blur and glass are baked without the world behind them. GIFs and videos show their first frame.\n"
       << "- Only TrueType (.ttf) fonts can be shipped; others fall back to the vanilla font.\n\n"
       << "## License\n\nThe generated code and the Fimgui runtime may be used, modified and sold as part of your own mod (see `LICENSE-FIMGUI.txt`); "
       << "keep the copyright line at the top of the runtime files. Fimgui itself may not be sold.\n";
    if (!g.warnings.empty())
    {
        rd << "\n## Export notes\n\n";
        std::set<std::string> seen;
        for (auto& w : g.warnings) if (seen.insert(w).second) rd << "- " << w << "\n";
    }
    write(out / "README.md", rd.str(), "README.md");

    if (res.message.empty())
    {
        res.ok = true;
        res.message = "Exported " + std::to_string(res.files.size()) + " files for " + (fabric ? "Fabric" : "NeoForge") + " 1.21.1";
        if (!g.warnings.empty()) res.message += " (" + std::to_string(g.warnings.size()) + " notes in README.md)";
    }
    return res;
}
