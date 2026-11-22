#include "exporters.h"
#include "renderer.h"
#include "gfx.h"
#include "fonts.h"
#include "../runtime/fimgui_fx.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <regex>
#include <set>
#include "../runtime/fimgui_runtime.h"

static std::string Fmt(float v)
{
    char b[32];
    if (std::fabs(v - std::round(v)) < 0.005f) snprintf(b, sizeof(b), "%d", (int)std::round(v));
    else snprintf(b, sizeof(b), "%.2f", v);
    std::string s = b;
    if (s.find('.') != std::string::npos) { while (s.back() == '0') s.pop_back(); if (s.back() == '.') s.pop_back(); }
    return s;
}

static std::string Px(float v) { return v == 0 ? "0" : Fmt(v) + "px"; }

static std::string CssColor(ImVec4 c)
{
    if (c.w >= 0.999f) return ColorToHex(c);
    char b[64];
    snprintf(b, sizeof(b), "rgba(%d, %d, %d, %s)", (int)std::lround(c.x * 255), (int)std::lround(c.y * 255), (int)std::lround(c.z * 255), Fmt(c.w).c_str());
    return b;
}

static std::string CssPaint(const Paint& p)
{
    if (p.type == 2)
        return "linear-gradient(" + Fmt(p.angle + 90.0f) + "deg, " + CssColor(p.c0) + " 0%, " + CssColor(p.c1) + " 100%)";
    return CssColor(p.c0);
}

static int StyleToWeight(const std::string& s)
{
    std::string l = s;
    std::transform(l.begin(), l.end(), l.begin(), ::tolower);
    if (l.find("thin") != std::string::npos) return 100;
    if (l.find("extralight") != std::string::npos) return 200;
    if (l.find("light") != std::string::npos) return 300;
    if (l.find("medium") != std::string::npos) return 500;
    if (l.find("semibold") != std::string::npos) return 600;
    if (l.find("extrabold") != std::string::npos) return 800;
    if (l.find("black") != std::string::npos) return 900;
    if (l.find("bold") != std::string::npos) return 700;
    return 400;
}

static void CssFor(Document& doc, Node* n, std::ostringstream& o, bool withChildren, int depth)
{
    std::string ind(depth * 2, ' ');
    o << ind << "/* " << (n->name.empty() ? NodeTypeName(n->type) : n->name) << " */\n";
    o << ind << "position: absolute;\n";
    o << ind << "width: " << Px(n->w) << ";\n";
    o << ind << "height: " << Px(n->h) << ";\n";
    o << ind << "left: " << Px(n->x) << ";\n";
    o << ind << "top: " << Px(n->y) << ";\n";
    if (n->layout)
    {
        static const char* jm[] = { "flex-start", "center", "flex-end", "space-between" };
        static const char* ac[] = { "flex-start", "center", "flex-end", "stretch" };
        o << ind << "display: flex;\n" << ind << "flex-direction: " << (n->layout == 1 ? "row" : "column") << ";\n";
        o << ind << "justify-content: " << jm[n->alignMain] << ";\n" << ind << "align-items: " << ac[n->alignCross] << ";\n";
        o << ind << "padding: " << Px(n->pad[1]) << " " << Px(n->pad[2]) << " " << Px(n->pad[3]) << " " << Px(n->pad[0]) << ";\n";
        o << ind << "gap: " << Px(n->gap) << ";\n";
    }
    if (n->type == NodeType::Text)
    {
        o << ind << "font-family: '" << n->fontFamily << "';\n";
        o << ind << "font-style: " << (n->fontStyle.find("Italic") != std::string::npos ? "italic" : "normal") << ";\n";
        o << ind << "font-weight: " << StyleToWeight(n->fontStyle) << ";\n";
        o << ind << "font-size: " << Px(n->fontSize) << ";\n";
        o << ind << "line-height: " << (n->lineHeight > 0 ? Px(n->lineHeight) : std::string("120%")) << ";\n";
        if (n->alignH) o << ind << "text-align: " << (n->alignH == 1 ? "center" : "right") << ";\n";
        if (n->fill.type) o << ind << "color: " << CssColor(n->fill.c0) << ";\n";
    }
    else if (n->type == NodeType::Line)
    {
        o << ind << "border-top: " << Px(n->strokeWidth) << " solid " << CssColor(n->stroke.c0) << ";\n";
    }
    else
    {
        if (n->fill.type && n->type != NodeType::Image && n->type != NodeType::Vector) o << ind << "background: " << CssPaint(n->fill) << ";\n";
        if (n->type == NodeType::Image) o << ind << "background: url(" << n->asset << ".png);\n" << ind << "background-size: " << (n->imageFit == 1 ? "contain" : n->imageFit == 2 ? "100% 100%" : "cover") << ";\n";
        if (n->stroke.type)
        {
            if (n->strokeAlign == 0) o << ind << "border: " << Px(n->strokeWidth) << " solid " << CssColor(n->stroke.c0) << ";\n" << ind << "box-sizing: border-box;\n";
            else o << ind << "outline: " << Px(n->strokeWidth) << " solid " << CssColor(n->stroke.c0) << ";\n" << ind << "outline-offset: " << Px(n->strokeAlign == 1 ? -n->strokeWidth / 2 : 0) << ";\n";
        }
        if (n->type == NodeType::Ellipse) o << ind << "border-radius: 50%;\n";
        else if (n->radius[0] || n->radius[1] || n->radius[2] || n->radius[3])
        {
            if (n->radius[0] == n->radius[1] && n->radius[1] == n->radius[2] && n->radius[2] == n->radius[3]) o << ind << "border-radius: " << Px(n->radius[0]) << ";\n";
            else o << ind << "border-radius: " << Px(n->radius[0]) << " " << Px(n->radius[1]) << " " << Px(n->radius[2]) << " " << Px(n->radius[3]) << ";\n";
        }
        if (n->type == NodeType::Frame && n->clip && !n->children.empty()) o << ind << "overflow: hidden;\n";
    }
    if (n->effect.type)
        o << ind << "box-shadow: " << (n->effect.type == 2 ? "inset " : "") << Px(n->effect.offset.x) << " " << Px(n->effect.offset.y) << " "
          << Px(n->effect.blur) << " " << Px(n->effect.spread) << " " << CssColor(n->effect.color) << ";\n";
    if (n->opacity < 1.0f) o << ind << "opacity: " << Fmt(n->opacity) << ";\n";
    if (n->glass.on)
    {
        o << ind << "backdrop-filter: blur(" << Px(n->glass.blur * 0.5f) << ") saturate(" << Fmt(n->glass.saturation * 100) << "%);\n";
        o << ind << "/* liquid glass: refraction " << Fmt(n->glass.refraction) << ", rim " << Px(n->glass.thickness) << ", highlight " << Fmt(n->glass.highlight) << " (rendered by fimgui_fx) */\n";
    }
    else if (n->blur.on) o << ind << "backdrop-filter: blur(" << Px(n->blur.radius * 0.5f) << ");\n";
    if (n->texture.on)
    {
        auto it = doc.assets.find(n->texture.asset);
        static const char* blends[] = { "normal", "plus-lighter", "multiply", "screen" };
        if (it != doc.assets.end())
            o << ind << "/* texture overlay: url(" << it->second.name << ") " << (n->texture.fit == 2 ? "repeat" : "no-repeat") << ", opacity " << Fmt(n->texture.opacity)
              << ", mix-blend-mode: " << blends[n->texture.blend & 3] << " */\n";
    }
    if (n->anim.preset)
    {
        const AnimFx& a = n->anim;
        int cat = fim::AnimCategory(a.preset);
        std::string name = a.preset == fim::Anim_Keyframes ? "fim-kf-" + n->id : std::string("fim-") + AnimPresetKey(a.preset);
        if (a.preset == fim::Anim_Keyframes) cat = fim::AnimCategoryOf(ToAnimSpec(a));
        if (cat == 2 && a.preset == fim::Anim_Keyframes)
            o << ind << "/* keyframes play on :" << (a.trigger == 2 ? "active" : a.trigger == 3 ? "checked" : "hover") << " - animation: " << name << " " << Fmt(a.duration) << "s both; */\n";
        else if (cat == 2)
            o << ind << "transition: transform " << Fmt(a.duration) << "s, opacity " << Fmt(a.duration) << "s; /* on :" << (a.trigger == 2 ? "active" : a.trigger == 3 ? "checked" : "hover") << " */\n";
        else
            o << ind << "animation: " << name << " " << Fmt(cat == 1 ? a.duration + a.delay : a.duration) << "s linear " << (cat == 0 ? Fmt(a.delay) + "s both" : "infinite") << ";\n";
    }
    if (n->type == NodeType::Text) o << ind << "/* content: \"" << n->text << "\" */\n";
    if (withChildren)
        for (auto& c : n->children) { o << "\n"; CssFor(doc, c.get(), o, true, depth + 1); }
}

// Keyframes sampled from the shared animation engine, so CSS matches the editor and the ImGui export.
static void CssKeyframes(Node* n, std::ostringstream& o, std::set<int>& done)
{
    if (n->anim.preset == fim::Anim_Keyframes)
    {
        fim::AnimSpec s = ToAnimSpec(n->anim);
        o << "\n@keyframes fim-kf-" << n->id << " {\n";
        for (int k = 0; k <= 20; k++)
        {
            fim::AnimValue v = fim::AnimKeyframesAt(s, n->anim.duration * k / 20.0f);
            o << "  " << k * 5 << "% { transform: translate(" << Px(v.dx) << ", " << Px(v.dy) << ") scale(" << Fmt(v.scale) << "); opacity: " << Fmt(v.alpha) << "; }\n";
        }
        o << "}\n";
    }
    else if (n->anim.preset && !done.count(n->anim.preset) && fim::AnimCategory(n->anim.preset) != 2)
    {
        done.insert(n->anim.preset);
        AnimFx a = n->anim;
        fim::AnimSpec s;
        s.preset = a.preset; s.easing = a.easing; s.duration = a.duration; s.delay = 0; s.distance = a.distance; s.scale = a.scale; s.intensity = a.intensity;
        for (int i = 0; i < 4; i++) s.bezier[i] = a.bezier[i];
        bool loop = fim::AnimCategory(a.preset) == 1;
        float total = loop ? a.duration + a.delay : a.duration;
        o << "\n@keyframes fim-" << AnimPresetKey(a.preset) << " {\n";
        for (int k = 0; k <= 10; k++)
        {
            float tt = total * k / 10.0f;
            if (!loop) s.delay = 0;
            fim::AnimValue v = fim::AnimSample(s, tt, 0);
            o << "  " << k * 10 << "% { transform: translate(" << Px(v.dx) << ", " << Px(v.dy) << ") scale(" << Fmt(v.scale) << "); opacity: " << Fmt(v.alpha) << "; }\n";
        }
        o << "}\n";
    }
    for (auto& c : n->children) CssKeyframes(c.get(), o, done);
}

std::string NodeToCSS(Document& doc, Node* n, bool withChildren)
{
    std::ostringstream o;
    CssFor(doc, n, o, withChildren, 0);
    std::set<int> done;
    if (withChildren) CssKeyframes(n, o, done);
    else if (n->anim.preset) { Node tmp; tmp.id = n->id; tmp.anim = n->anim; CssKeyframes(&tmp, o, done); }
    return o.str();
}

// ---------------------------------------------------------------- SVG export

static std::string XmlEsc(const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        if (c == '<') o += "&lt;"; else if (c == '>') o += "&gt;"; else if (c == '&') o += "&amp;"; else if (c == '"') o += "&quot;"; else o += c;
    }
    return o;
}

struct SvgCtx { std::ostringstream defs; int counter = 0; };

static std::string SvgPaint(SvgCtx& s, const Paint& p, float x, float y, float w, float h, float* opacityOut)
{
    *opacityOut = 1.0f;
    if (p.type == 0) return "none";
    if (p.type == 1) { *opacityOut = p.c0.w; return ColorToHex(ImVec4(p.c0.x, p.c0.y, p.c0.z, 1)); }
    std::string id = "g" + std::to_string(++s.counter);
    float a = p.angle * 3.14159265f / 180.0f;
    float cx = x + w / 2, cy = y + h / 2;
    float half = (std::fabs(std::cos(a)) * w + std::fabs(std::sin(a)) * h) / 2;
    s.defs << "<linearGradient id=\"" << id << "\" gradientUnits=\"userSpaceOnUse\" x1=\"" << Fmt(cx - std::cos(a) * half) << "\" y1=\"" << Fmt(cy - std::sin(a) * half)
           << "\" x2=\"" << Fmt(cx + std::cos(a) * half) << "\" y2=\"" << Fmt(cy + std::sin(a) * half) << "\">"
           << "<stop offset=\"0\" stop-color=\"" << ColorToHex(ImVec4(p.c0.x, p.c0.y, p.c0.z, 1)) << "\" stop-opacity=\"" << Fmt(p.c0.w) << "\"/>"
           << "<stop offset=\"1\" stop-color=\"" << ColorToHex(ImVec4(p.c1.x, p.c1.y, p.c1.z, 1)) << "\" stop-opacity=\"" << Fmt(p.c1.w) << "\"/></linearGradient>\n";
    return "url(#" + id + ")";
}

static std::string RoundRectPath(float x, float y, float w, float h, const float rin[4])
{
    float m = std::min(w, h) / 2;
    float r[4];
    for (int i = 0; i < 4; i++) r[i] = std::clamp(rin[i], 0.0f, m);
    std::ostringstream p;
    p << "M" << Fmt(x + r[0]) << " " << Fmt(y)
      << "H" << Fmt(x + w - r[1]) << (r[1] > 0 ? "A" + Fmt(r[1]) + " " + Fmt(r[1]) + " 0 0 1 " + Fmt(x + w) + " " + Fmt(y + r[1]) : "")
      << "V" << Fmt(y + h - r[2]) << (r[2] > 0 ? "A" + Fmt(r[2]) + " " + Fmt(r[2]) + " 0 0 1 " + Fmt(x + w - r[2]) + " " + Fmt(y + h) : "")
      << "H" << Fmt(x + r[3]) << (r[3] > 0 ? "A" + Fmt(r[3]) + " " + Fmt(r[3]) + " 0 0 1 " + Fmt(x) + " " + Fmt(y + h - r[3]) : "")
      << "V" << Fmt(y + r[0]) << (r[0] > 0 ? "A" + Fmt(r[0]) + " " + Fmt(r[0]) + " 0 0 1 " + Fmt(x + r[0]) + " " + Fmt(y) : "") << "Z";
    return p.str();
}

static void SvgNode(Document& doc, SvgCtx& s, Node* n, float ox, float oy, std::ostringstream& o)
{
    if (!n->visible) return;
    float x = ox + n->x, y = oy + n->y, w = n->w, h = n->h;
    std::string filter;
    if (n->effect.type == 1)
    {
        std::string id = "f" + std::to_string(++s.counter);
        s.defs << "<filter id=\"" << id << "\" x=\"-50%\" y=\"-50%\" width=\"200%\" height=\"200%\"><feDropShadow dx=\"" << Fmt(n->effect.offset.x) << "\" dy=\"" << Fmt(n->effect.offset.y)
               << "\" stdDeviation=\"" << Fmt(n->effect.blur / 2) << "\" flood-color=\"" << ColorToHex(ImVec4(n->effect.color.x, n->effect.color.y, n->effect.color.z, 1))
               << "\" flood-opacity=\"" << Fmt(n->effect.color.w) << "\"/></filter>\n";
        filter = " filter=\"url(#" + id + ")\"";
    }
    o << "<g" << (n->opacity < 1 ? " opacity=\"" + Fmt(n->opacity) + "\"" : "") << ">\n";
    float fo = 1, so = 1;
    std::string fill = SvgPaint(s, n->fill, x, y, w, h, &fo);
    std::string stroke = n->stroke.type ? SvgPaint(s, n->stroke, x, y, w, h, &so) : "none";
    std::string fillAttr = " fill=\"" + fill + "\"" + (fo < 1 ? " fill-opacity=\"" + Fmt(fo) + "\"" : "");
    float sw = n->strokeWidth, inset = n->strokeAlign == 0 ? sw / 2 : n->strokeAlign == 2 ? -sw / 2 : 0;
    std::string strokeAttr = n->stroke.type ? " fill=\"none\" stroke=\"" + stroke + "\" stroke-width=\"" + Fmt(sw) + "\"" + (so < 1 ? " stroke-opacity=\"" + Fmt(so) + "\"" : "") : "";
    bool uniform = n->radius[0] == n->radius[1] && n->radius[1] == n->radius[2] && n->radius[2] == n->radius[3];
    auto shape = [&](float sx, float sy, float sw2, float sh2, const float* rr, const std::string& attrs) {
        if (uniform)
            o << "<rect x=\"" << Fmt(sx) << "\" y=\"" << Fmt(sy) << "\" width=\"" << Fmt(sw2) << "\" height=\"" << Fmt(sh2) << "\""
              << (rr[0] > 0 ? " rx=\"" + Fmt(rr[0]) + "\"" : "") << attrs << "/>\n";
        else
            o << "<path d=\"" << RoundRectPath(sx, sy, sw2, sh2, rr) << "\"" << attrs << "/>\n";
    };
    std::string clipId;
    switch (n->type)
    {
    case NodeType::Frame:
    case NodeType::Rect:
        if (n->fill.type) shape(x, y, w, h, n->radius, fillAttr + filter);
        if (n->type == NodeType::Frame && n->clip && !n->children.empty())
        {
            clipId = "c" + std::to_string(++s.counter);
            s.defs << "<clipPath id=\"" << clipId << "\">";
            std::ostringstream tmp;
            std::swap(tmp, o);
            shape(x, y, w, h, n->radius, "");
            std::swap(tmp, o);
            s.defs << tmp.str() << "</clipPath>\n";
        }
        break;
    case NodeType::Ellipse:
        o << "<ellipse cx=\"" << Fmt(x + w / 2) << "\" cy=\"" << Fmt(y + h / 2) << "\" rx=\"" << Fmt(w / 2) << "\" ry=\"" << Fmt(h / 2) << "\"" << fillAttr << filter << "/>\n";
        break;
    case NodeType::Line:
        o << "<line x1=\"" << Fmt(x) << "\" y1=\"" << Fmt(y) << "\" x2=\"" << Fmt(x + w) << "\" y2=\"" << Fmt(y) << "\" stroke=\"" << ColorToHex(ImVec4(n->stroke.c0.x, n->stroke.c0.y, n->stroke.c0.z, 1))
          << "\" stroke-opacity=\"" << Fmt(n->stroke.c0.w) << "\" stroke-width=\"" << Fmt(n->strokeWidth) << "\"/>\n";
        break;
    case NodeType::Text:
    {
        float lh = n->lineHeight > 0 ? n->lineHeight : n->fontSize * 1.2f;
        const char* anchor = n->alignH == 1 ? "middle" : n->alignH == 2 ? "end" : "start";
        float tx = n->alignH == 1 ? x + w / 2 : n->alignH == 2 ? x + w : x;
        std::istringstream lines(n->text);
        std::string line;
        int i = 0;
        o << "<text font-family=\"" << XmlEsc(n->fontFamily) << "\" font-size=\"" << Fmt(n->fontSize) << "\" font-weight=\"" << StyleToWeight(n->fontStyle)
          << "\" text-anchor=\"" << anchor << "\"" << fillAttr << ">";
        while (std::getline(lines, line))
        {
            float baseline = y + i * lh + (lh - n->fontSize) / 2 + n->fontSize * 0.8f;
            o << "<tspan x=\"" << Fmt(tx) << "\" y=\"" << Fmt(baseline) << "\">" << XmlEsc(line) << "</tspan>";
            i++;
        }
        o << "</text>\n";
        break;
    }
    case NodeType::Image:
    {
        auto it = doc.assets.find(n->asset);
        if (it != doc.assets.end())
        {
            const char* mime = (it->second.data.size() > 3 && it->second.data[0] == 0xFF && it->second.data[1] == 0xD8) ? "image/jpeg" : "image/png";
            std::string par = n->imageFit == 0 ? "xMidYMid slice" : n->imageFit == 1 ? "xMidYMid meet" : "none";
            o << "<image x=\"" << Fmt(x) << "\" y=\"" << Fmt(y) << "\" width=\"" << Fmt(w) << "\" height=\"" << Fmt(h) << "\" preserveAspectRatio=\"" << par
              << "\" href=\"data:" << mime << ";base64," << Base64Encode(it->second.data.data(), it->second.data.size()) << "\"/>\n";
        }
        break;
    }
    case NodeType::Vector:
    {
        auto it = doc.assets.find(n->asset);
        if (it != doc.assets.end())
        {
            std::string src(it->second.data.begin(), it->second.data.end());
            size_t p = src.find("<svg");
            if (p != std::string::npos)
            {
                src = src.substr(p);
                // Position the nested svg and strip its own size/position attributes.
                std::regex attr(R"((\s)(x|y|width|height)\s*=\s*"[^"]*")");
                size_t tagEnd = src.find('>');
                std::string tag = std::regex_replace(src.substr(0, tagEnd), attr, "$1");
                std::string extra = " x=\"" + Fmt(x) + "\" y=\"" + Fmt(y) + "\" width=\"" + Fmt(w) + "\" height=\"" + Fmt(h) + "\"";
                if (n->recolor && n->fill.type)
                    extra += " color=\"" + ColorToHex(ImVec4(n->fill.c0.x, n->fill.c0.y, n->fill.c0.z, 1)) + "\"";
                o << tag.insert(4, extra) << src.substr(tagEnd) << "\n";
            }
        }
        break;
    }
    case NodeType::Group:
        break;
    }
    if (!clipId.empty()) o << "<g clip-path=\"url(#" << clipId << ")\">\n";
    for (auto& c : n->children) SvgNode(doc, s, c.get(), x, y, o);
    if (!clipId.empty()) o << "</g>\n";
    if (n->stroke.type && n->type != NodeType::Line)
    {
        if (n->type == NodeType::Ellipse)
            o << "<ellipse cx=\"" << Fmt(x + w / 2) << "\" cy=\"" << Fmt(y + h / 2) << "\" rx=\"" << Fmt(w / 2 - inset) << "\" ry=\"" << Fmt(h / 2 - inset) << "\"" << strokeAttr << "/>\n";
        else
        {
            float rr[4];
            for (int i = 0; i < 4; i++) rr[i] = std::max(0.0f, n->radius[i] - inset);
            shape(x + inset, y + inset, w - inset * 2, h - inset * 2, rr, strokeAttr);
        }
    }
    o << "</g>\n";
}

std::string NodeToSVG(Document& doc, Node* n)
{
    SvgCtx s;
    std::ostringstream body;
    SvgNode(doc, s, n, -n->x, -n->y, body);
    std::ostringstream o;
    o << "<svg width=\"" << Fmt(n->w) << "\" height=\"" << Fmt(std::max(n->h, n->type == NodeType::Line ? n->strokeWidth : 0.0f)) << "\" viewBox=\"0 0 " << Fmt(n->w) << " " << Fmt(std::max(n->h, 1.0f))
      << "\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">\n";
    std::string defs = s.defs.str();
    if (!defs.empty()) o << "<defs>\n" << defs << "</defs>\n";
    o << body.str() << "</svg>\n";
    return o.str();
}

// ---------------------------------------------------------------- CSS import

static std::string Trim(const std::string& s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

bool LooksLikeCSS(const std::string& s)
{
    static std::regex decl(R"((^|[;\n{])\s*[a-z-]+\s*:\s*[^;]+;)", std::regex::icase);
    return s.find('<') == std::string::npos && std::regex_search(s, decl);
}

static float PxVal(const std::string& v, float rel = 0)
{
    try
    {
        std::string t = Trim(v);
        if (!t.empty() && t.back() == '%') return std::stof(t) / 100.0f * rel;
        return std::stof(t);
    }
    catch (...) { return 0; }
}

// Splits by spaces/commas at top level (outside parentheses).
static std::vector<std::string> SplitTop(const std::string& s, char sep)
{
    std::vector<std::string> out;
    int depth = 0;
    std::string cur;
    for (char c : s)
    {
        if (c == '(') depth++;
        if (c == ')') depth--;
        if (depth == 0 && (c == sep || (sep == ' ' && (c == '\t' || c == '\n'))))
        {
            if (!Trim(cur).empty()) out.push_back(Trim(cur));
            cur.clear();
        }
        else cur += c;
    }
    if (!Trim(cur).empty()) out.push_back(Trim(cur));
    return out;
}

static bool IsColorToken(const std::string& t)
{
    return t[0] == '#' || t.rfind("rgb", 0) == 0 || t == "white" || t == "black" || t == "transparent";
}

static bool CssPaintParse(const std::string& v, Paint* p)
{
    std::string t = Trim(v);
    size_t g = t.find("linear-gradient(");
    if (g != std::string::npos)
    {
        std::string inner = t.substr(g + 16, t.rfind(')') - g - 16);
        auto parts = SplitTop(inner, ',');
        float angle = 180.0f;
        std::vector<ImVec4> cols;
        for (auto& part : parts)
        {
            if (part.find("deg") != std::string::npos) angle = PxVal(part);
            else if (part == "to right") angle = 90; else if (part == "to left") angle = 270;
            else if (part == "to bottom") angle = 180; else if (part == "to top") angle = 0;
            else
            {
                auto tok = SplitTop(part, ' ');
                if (!tok.empty()) cols.push_back(ParseColor(json(tok[0]), ImVec4(0, 0, 0, 1)));
            }
        }
        if (cols.size() >= 2) { p->type = 2; p->c0 = cols.front(); p->c1 = cols.back(); p->angle = angle - 90.0f; return true; }
        if (cols.size() == 1) { p->type = 1; p->c0 = cols[0]; return true; }
        return false;
    }
    for (auto& tok : SplitTop(t, ' '))
        if (IsColorToken(tok)) { p->type = 1; p->c0 = ParseColor(json(tok), p->c0); return true; }
    return false;
}

int ApplyCSS(Node* n, const std::string& cssIn)
{
    std::string css = std::regex_replace(cssIn, std::regex(R"(/\*[\s\S]*?\*/)"), "");
    size_t brace = css.find('{');
    if (brace != std::string::npos) css = css.substr(brace + 1, css.rfind('}') == std::string::npos ? std::string::npos : css.rfind('}') - brace - 1);
    int count = 0;
    for (auto& d : SplitTop(css, ';'))
    {
        size_t c = d.find(':');
        if (c == std::string::npos) continue;
        std::string k = Trim(d.substr(0, c)), v = Trim(d.substr(c + 1));
        std::transform(k.begin(), k.end(), k.begin(), ::tolower);
        auto vals = SplitTop(v, ' ');
        bool ok = true;
        if (k == "width") n->w = PxVal(v);
        else if (k == "height") n->h = PxVal(v);
        else if (k == "left") n->x = PxVal(v);
        else if (k == "top") n->y = PxVal(v);
        else if (k == "background" || k == "background-color")
        {
            if (n->type == NodeType::Text) { Paint p = n->fill; if (CssPaintParse(v, &p)) n->fill = p; }
            else ok = CssPaintParse(v, &n->fill);
        }
        else if (k == "color" && n->type == NodeType::Text) { n->fill.type = 1; n->fill.c0 = ParseColor(json(v), n->fill.c0); }
        else if (k == "opacity") n->opacity = std::clamp(PxVal(v), 0.0f, 1.0f);
        else if (k == "border-radius")
        {
            float r[4];
            std::vector<float> f;
            for (auto& t : vals) f.push_back(PxVal(t, std::min(n->w, n->h)));
            if (f.size() == 1) { r[0] = r[1] = r[2] = r[3] = f[0]; }
            else if (f.size() == 2) { r[0] = r[2] = f[0]; r[1] = r[3] = f[1]; }
            else if (f.size() == 3) { r[0] = f[0]; r[1] = r[3] = f[1]; r[2] = f[2]; }
            else if (f.size() >= 4) { r[0] = f[0]; r[1] = f[1]; r[2] = f[2]; r[3] = f[3]; }
            else ok = false;
            if (ok) for (int i = 0; i < 4; i++) n->radius[i] = r[i];
        }
        else if (k == "border" || k == "outline" || k == "border-top")
        {
            for (auto& t : vals)
            {
                if (IsColorToken(t)) { n->stroke.type = 1; n->stroke.c0 = ParseColor(json(t), n->stroke.c0); }
                else if (t.find("px") != std::string::npos) n->strokeWidth = PxVal(t);
            }
            if (n->stroke.type == 0) n->stroke.type = 1;
            n->strokeAlign = k == "border" ? 0 : 1;
        }
        else if (k == "box-shadow")
        {
            if (v == "none") { n->effect.type = 0; continue; }
            auto first = SplitTop(v, ',');
            auto tok = SplitTop(first.empty() ? v : first[0], ' ');
            std::vector<float> nums;
            n->effect.type = 1;
            for (auto& t : tok)
            {
                if (t == "inset") n->effect.type = 2;
                else if (IsColorToken(t)) n->effect.color = ParseColor(json(t), n->effect.color);
                else nums.push_back(PxVal(t));
            }
            if (nums.size() > 0) n->effect.offset.x = nums[0];
            if (nums.size() > 1) n->effect.offset.y = nums[1];
            n->effect.blur = nums.size() > 2 ? nums[2] : 0;
            n->effect.spread = nums.size() > 3 ? nums[3] : 0;
        }
        else if (k == "font-size") n->fontSize = PxVal(v);
        else if (k == "font-family")
        {
            std::string f = SplitTop(v, ',')[0];
            f.erase(std::remove_if(f.begin(), f.end(), [](char ch) { return ch == '\'' || ch == '"'; }), f.end());
            n->fontFamily = Trim(f);
        }
        else if (k == "font-weight")
        {
            int w = v == "bold" ? 700 : v == "normal" ? 400 : (int)PxVal(v);
            n->fontStyle = w >= 700 ? "Bold" : w >= 600 ? "SemiBold" : w >= 500 ? "Medium" : w <= 300 ? "Light" : "Regular";
        }
        else if (k == "line-height") n->lineHeight = v == "normal" ? 0 : v.back() == '%' ? PxVal(v, n->fontSize) : v.find("px") != std::string::npos ? PxVal(v) : PxVal(v) * n->fontSize;
        else if (k == "backdrop-filter" || k == "-webkit-backdrop-filter")
        {
            size_t bp = v.find("blur(");
            if (bp != std::string::npos) { n->blur.on = true; n->blur.radius = PxVal(v.substr(bp + 5)) * 2.0f; }
            else ok = false;
        }
        else if (k == "text-align") n->alignH = v == "center" ? 1 : v == "right" ? 2 : 0;
        else if (k == "display" && v == "flex") { if (!n->layout) n->layout = 1; }
        else if (k == "flex-direction") n->layout = v.rfind("column", 0) == 0 ? 2 : 1;
        else if (k == "gap") n->gap = PxVal(v);
        else if (k == "padding")
        {
            std::vector<float> f;
            for (auto& t : vals) f.push_back(PxVal(t));
            if (f.size() == 1) for (float& p : n->pad) p = f[0];
            else if (f.size() == 2) { n->pad[1] = n->pad[3] = f[0]; n->pad[0] = n->pad[2] = f[1]; }
            else if (f.size() == 3) { n->pad[1] = f[0]; n->pad[0] = n->pad[2] = f[1]; n->pad[3] = f[2]; }
            else if (f.size() >= 4) { n->pad[1] = f[0]; n->pad[2] = f[1]; n->pad[3] = f[2]; n->pad[0] = f[3]; }
        }
        else if (k == "justify-content") n->alignMain = v == "center" ? 1 : v == "flex-end" || v == "end" ? 2 : v == "space-between" ? 3 : 0;
        else if (k == "align-items") n->alignCross = v == "center" ? 1 : v == "flex-end" || v == "end" ? 2 : v == "stretch" ? 3 : 0;
        else ok = false;
        if (ok) count++;
    }
    if (n->type == NodeType::Text && count) n->textSizing = 0;
    return count;
}

// ---------------------------------------------------------------- import

std::string SanitizeIdent(const std::string& s, const char* fallback)
{
    std::string o;
    for (char c : s)
    {
        if (isalnum((unsigned char)c) || c == '_') o += c;
        else if (c == ' ' || c == '-' || c == '.') o += '_';
    }
    while (o.find("__") != std::string::npos) o.replace(o.find("__"), 2, "_");
    if (o.empty()) o = fallback;
    if (isdigit((unsigned char)o[0])) o = "_" + o;
    return o;
}

std::string AddAsset(Document& doc, int kind, const std::string& name, std::vector<unsigned char> data)
{
    // De-duplicate identical data.
    for (auto& [id, a] : doc.assets)
        if (a.kind == kind && a.data == data) return id;
    Asset a;
    a.kind = kind;
    a.name = SanitizeIdent(name, kind == 1 ? "icon" : kind == 3 ? "video" : "image");
    std::string base = a.name;
    int suffix = 1;
    auto taken = [&](const std::string& nm) { for (auto& [i, e] : doc.assets) if (e.name == nm) return true; return false; };
    while (taken(a.name)) a.name = base + "_" + std::to_string(++suffix);
    a.id = "a_" + a.name;
    if (kind == 1)
    {
        float w = 24, h = 24;
        gfx::SvgSize(std::string(data.begin(), data.end()), &w, &h);
        a.width = (int)std::lround(w > 0 ? w : 24);
        a.height = (int)std::lround(h > 0 ? h : 24);
    }
    else if (kind == 3)
    {
        fim::fx::VideoProbe(data.data(), data.size(), &a.width, &a.height);
    }
    else
    {
        std::vector<unsigned char> rgba;
        gfx::DecodeImage(data.data(), data.size(), rgba, &a.width, &a.height);
    }
    a.data = std::move(data);
    std::string id = a.id;
    doc.assets[id] = std::move(a);
    return id;
}

Node* ImportSVG(Document& doc, const std::string& svg, const std::string& name, Node* parent, ImVec2 pos)
{
    float w = 0, h = 0;
    if (!gfx::SvgSize(svg, &w, &h)) return nullptr;
    std::string id = AddAsset(doc, 1, name, std::vector<unsigned char>(svg.begin(), svg.end()));
    auto n = std::make_unique<Node>();
    n->type = NodeType::Vector;
    n->id = doc.NewId();
    n->name = name;
    n->asset = id;
    n->fill.type = 0;
    n->w = w > 0 ? w : 24;
    n->h = h > 0 ? h : 24;
    n->x = std::round(pos.x);
    n->y = std::round(pos.y);
    Node* raw = n.get();
    doc.Insert(std::move(n), parent);
    return raw;
}

bool IsGif(const std::vector<unsigned char>& d) { return d.size() > 6 && !memcmp(d.data(), "GIF8", 4); }

Node* ImportImage(Document& doc, const std::vector<unsigned char>& bytes, const std::string& name, Node* parent, ImVec2 pos)
{
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!gfx::DecodeImage(bytes.data(), bytes.size(), rgba, &w, &h)) return nullptr;
    std::string id = AddAsset(doc, IsGif(bytes) ? 2 : 0, name, bytes);
    auto n = std::make_unique<Node>();
    n->type = NodeType::Image;
    n->id = doc.NewId();
    n->name = name;
    n->asset = id;
    n->fill.type = 0;
    n->w = (float)w;
    n->h = (float)h;
    n->x = std::round(pos.x);
    n->y = std::round(pos.y);
    Node* raw = n.get();
    doc.Insert(std::move(n), parent);
    return raw;
}

Node* ImportVideo(Document& doc, const std::vector<unsigned char>& bytes, const std::string& name, Node* parent, ImVec2 pos)
{
    int w = 0, h = 0;
    if (!fim::fx::VideoProbe(bytes.data(), bytes.size(), &w, &h)) return nullptr;
    std::string id = AddAsset(doc, 3, name, bytes);
    auto n = std::make_unique<Node>();
    n->type = NodeType::Image;
    n->id = doc.NewId();
    n->name = name;
    n->asset = id;
    n->fill.type = 0;
    // Keep imported videos at a reasonable size on the canvas.
    float k = std::min(1.0f, 480.0f / std::max(1, std::max(w, h)));
    n->w = std::round(w * k);
    n->h = std::round(h * k);
    n->x = std::round(pos.x);
    n->y = std::round(pos.y);
    Node* raw = n.get();
    doc.Insert(std::move(n), parent);
    return raw;
}

std::string MakeTextureAsset(Document& doc, int pattern)
{
    pattern = std::clamp(pattern, 0, 5);
    const int S = pattern == 0 ? 128 : 64;
    std::vector<unsigned char> rgba(S * S * 4);
    unsigned seed = 1234567u;
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
        {
            unsigned char v = 255, a = 255;
            if (pattern == 0) { seed = seed * 1664525u + 1013904223u; v = (unsigned char)(seed >> 24); }          // noise
            else if (pattern == 1) { v = 0; a = (y % 4) < 2 ? 140 : 0; }                                            // scanlines
            else if (pattern == 2) { a = (x % 16 == 0 || y % 16 == 0) ? 90 : 0; }                                   // grid
            else if (pattern == 3) { float d = std::fabs(((x + y) % 16) - 8.0f) / 8.0f; a = (unsigned char)(d < 0.25f ? 110 : 0); }   // stripes
            else if (pattern == 4) { float dx = (x % 16) - 7.5f, dy = (y % 16) - 7.5f; a = dx * dx + dy * dy < 9.0f ? 110 : 0; }   // dots
            else { a = (((x / 16) + (y / 16)) & 1) ? 60 : 0; }                                                       // checker
            unsigned char* p = &rgba[(y * S + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = a;
        }
    static const char* names[] = { "noise", "scanlines", "grid", "stripes", "dots", "checker" };
    return AddAsset(doc, 0, names[pattern], gfx::EncodePNG(rgba.data(), S, S));
}

const char* const kShaderStyleNames[5] = { "Aurora", "Mesh", "Waves", "Radial glow", "Plasma" };

std::string MakeShaderAsset(Document& doc, int style, ImVec4 c0, ImVec4 c1)
{
    style = std::clamp(style, 0, 4);
    const int S = 256;
    std::vector<unsigned char> rgba(S * S * 4);
    auto mix = [](float a, float b, float t) { return a + (b - a) * t; };
    for (int y = 0; y < S; y++)
        for (int x = 0; x < S; x++)
        {
            float u = x / (float)(S - 1), v = y / (float)(S - 1), t = 0, shade = 1;
            switch (style)
            {
            case 0:   // aurora: soft curtains
                t = 0.5f + 0.5f * std::sin(u * 6.0f + std::sin(v * 4.0f + u * 2.0f) * 1.8f);
                shade = 0.55f + 0.45f * std::exp(-std::pow((v - 0.45f - 0.15f * std::sin(u * 5.0f)) * 3.0f, 2.0f));
                break;
            case 1:   // mesh: four soft corners
                t = std::clamp(0.5f * (u + v) + 0.25f * std::sin(u * 3.1f) * std::cos(v * 2.7f), 0.0f, 1.0f);
                break;
            case 2:   // waves
                t = 0.5f + 0.5f * std::sin((v + 0.08f * std::sin(u * 12.0f)) * 18.0f);
                break;
            case 3:   // radial glow
            {
                float d = std::sqrt((u - 0.5f) * (u - 0.5f) + (v - 0.5f) * (v - 0.5f)) * 2.0f;
                t = std::clamp(d, 0.0f, 1.0f);
                t = t * t * (3 - 2 * t);
                break;
            }
            default:  // plasma
                t = 0.5f + 0.25f * (std::sin(u * 10.0f) + std::sin((u + v) * 7.0f + std::sin(v * 9.0f)));
                t = std::clamp(t, 0.0f, 1.0f);
                break;
            }
            unsigned char* p = &rgba[(y * S + x) * 4];
            p[0] = (unsigned char)std::clamp(mix(c0.x, c1.x, t) * shade * 255.0f, 0.0f, 255.0f);
            p[1] = (unsigned char)std::clamp(mix(c0.y, c1.y, t) * shade * 255.0f, 0.0f, 255.0f);
            p[2] = (unsigned char)std::clamp(mix(c0.z, c1.z, t) * shade * 255.0f, 0.0f, 255.0f);
            p[3] = (unsigned char)std::clamp(mix(c0.w, c1.w, t) * 255.0f, 0.0f, 255.0f);
        }
    std::string name = std::string("shader_") + kShaderStyleNames[style];
    for (auto& ch : name) if (ch == ' ') ch = '_';
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    return AddAsset(doc, 0, name, gfx::EncodePNG(rgba.data(), S, S));
}
