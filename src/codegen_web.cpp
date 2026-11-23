// Web export (HTML Living Standard + modern CSS): pages, shared stylesheet, runtime, media and fonts.
#include "codegen_web.h"
#include "codemap.h"
#include "exporters.h"
#include "renderer.h"
#include "fonts.h"
#include "platform.h"
#include "version.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <set>
#include <sstream>
#include <tuple>

namespace fs = std::filesystem;

namespace
{
    std::string N(float v) { return CssNum(v); }
    std::string Px(float v) { return v == 0 ? "0" : CssNum(v) + "px"; }
    std::string Sec(float v) { return CssNum(std::max(0.0f, v)) + "s"; }

    // HTML text (attr = false) or attribute value.
    std::string Esc(const std::string& s, bool attr)
    {
        std::string o;
        for (char c : s)
        {
            if (c == '&') o += "&amp;";
            else if (c == '<') o += "&lt;";
            else if (c == '>') o += "&gt;";
            else if (c == '"' && attr) o += "&quot;";
            else if (c == '\r') continue;
            else o += c;
        }
        return o;
    }

    std::string Slug(const std::string& s, const char* fallback)
    {
        std::string o;
        for (unsigned char c : s)
        {
            if (c < 128 && std::isalnum(c)) o += (char)std::tolower(c);
            else if (!o.empty() && o.back() != '-') o += '-';
        }
        while (!o.empty() && o.back() == '-') o.pop_back();
        return o.empty() ? fallback : o;
    }

    std::string ClassOf(const Node* n) { return "n-" + Slug(n->id, "x"); }

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return s;
    }

    // Pages in order: the first visible top-level frame is index.html, the others are named after the frame.
    std::vector<std::pair<Node*, std::string>> Pages(Document& doc)
    {
        std::vector<std::pair<Node*, std::string>> out;
        std::set<std::string> used;
        for (auto& r : doc.roots)
        {
            if (r->type != NodeType::Frame || !r->visible) continue;
            std::string base = out.empty() ? "index" : Slug(r->name, "page");
            std::string name = base;
            for (int i = 2; used.count(name); i++) name = base + "-" + std::to_string(i);
            used.insert(name);
            out.push_back({ r.get(), name + ".html" });
        }
        return out;
    }

    // Live preview rebuilds the site often: media and font bytes are shared between builds.
    WebSite::Bytes CachedAsset(const Asset& a)
    {
        static std::map<std::string, std::tuple<const void*, size_t, WebSite::Bytes>> cache;
        auto& e = cache[a.id];
        if (std::get<0>(e) != a.data.data() || std::get<1>(e) != a.data.size() || !std::get<2>(e))
            e = { a.data.data(), a.data.size(), std::make_shared<const std::string>(a.data.begin(), a.data.end()) };
        return std::get<2>(e);
    }

    WebSite::Bytes CachedFile(const std::string& path)
    {
        static std::map<std::string, WebSite::Bytes> cache;
        auto it = cache.find(path);
        if (it != cache.end()) return it->second;
        std::vector<unsigned char> bytes;
        WebSite::Bytes b = platform::ReadFile(path, bytes) ? std::make_shared<const std::string>(bytes.begin(), bytes.end()) : nullptr;
        cache[path] = b;
        return b;
    }

    WebSite::Bytes Text(const std::string& s) { return std::make_shared<const std::string>(s); }

    const char* kBaseCss = R"(*, *::before, *::after { box-sizing: border-box; }
html { -webkit-text-size-adjust: 100%; text-size-adjust: 100%; }
body { margin: 0; min-height: 100vh; }
body[data-fit] { overflow-x: hidden; }
img, video, svg { display: block; }
button, input { font: inherit; color: inherit; background: none; border: 0; padding: 0; margin: 0; text-align: inherit; }
button, a, [data-combo], [role="slider"] { cursor: pointer; }
a { color: inherit; text-decoration: none; }
[hidden] { display: none !important; }
[role="slider"] { touch-action: none; }
.fim-input { position: absolute; top: 0; height: 100%; background: transparent; outline: none; min-width: 0; }
.fim-list { position: fixed; z-index: 1000; display: grid; gap: 2px; padding: 4px; background: var(--fim-list-bg, #1c1c22);
  border: 1px solid var(--fim-list-border, rgba(255, 255, 255, .1)); border-radius: var(--fim-radius, 8px);
  box-shadow: 0 12px 32px rgba(0, 0, 0, .35); color: var(--fim-text, #e6e6ec); max-height: calc(var(--fim-item-h, 32px) * 8 + 8px); overflow-y: auto; }
.fim-item { display: flex; align-items: center; gap: 8px; min-height: var(--fim-item-h, 32px); padding: 0 var(--fim-pad-x, 12px);
  border-radius: max(0px, calc(var(--fim-radius, 8px) - 3px)); white-space: nowrap; cursor: pointer; }
.fim-item:hover { background: var(--fim-hover, rgba(255, 255, 255, .07)); }
.fim-item.is-on:not(.fim-check) { background: var(--fim-accent, #4f7dff); color: #fff; }
.fim-check::before { content: ""; flex: none; width: 14px; height: 14px; border-radius: 4px; border: 1.5px solid currentColor; opacity: .45; }
.fim-check.is-on::before { opacity: 1; border-color: var(--fim-accent, #4f7dff); background: var(--fim-accent, #4f7dff)
  url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 10 10'%3E%3Cpath d='M2 5.2l2 2L8 3' fill='none' stroke='white' stroke-width='1.6' stroke-linecap='round' stroke-linejoin='round'/%3E%3C/svg%3E") center / 10px no-repeat; }
:focus-visible { outline: 2px solid #0d99ff; outline-offset: 2px; }
@media (prefers-reduced-motion: reduce) {
  *, *::before, *::after { animation-duration: .01ms !important; animation-iteration-count: 1 !important; transition-duration: .01ms !important; }
}
)";

    const char* kLiveScript = R"(<script>
/* Fimgui live preview: reloads when the design changes and follows the page you edit. Not part of the export. */
(() => {
  let v = null, page = null;
  try { const y = sessionStorage.getItem('fimgui:scroll'); if (y) { sessionStorage.removeItem('fimgui:scroll'); addEventListener('load', () => scrollTo(0, +y)); } } catch {}
  setInterval(async () => {
    try {
      const s = await (await fetch('/__fimgui/live', { cache: 'no-store' })).json();
      if (page !== null && s.page && s.page !== page && !location.pathname.endsWith('/' + s.page)) { location.href = '/' + s.page; return; }
      if (v !== null && s.v !== v) { try { sessionStorage.setItem('fimgui:scroll', scrollY); } catch {} location.reload(); return; }
      v = s.v; page = s.page;
    } catch { /* the editor is closed */ }
  }, 350);
})();
</script>
)";

    struct WebGen
    {
        Document& doc;
        const WebOptions& opt;
        WebSite& site;
        bool snippet = false;
        VarStore start;                                 // the starting state (defaults)
        std::map<std::string, std::string> pageFile;    // frame id -> html file
        std::ostringstream css, states, fontCss;        // states: hover / active rules (after the base rules)
        std::set<std::string> fontsDone;
        std::map<std::string, std::string> assetPath;   // asset id -> site path
        std::set<std::string> usedPaths;
        const Node* heading = nullptr;

        WebGen(Document& d, const WebOptions& o, WebSite& s) : doc(d), opt(o), site(s) {}

        void Warn(const std::string& w)
        {
            if (std::find(site.warnings.begin(), site.warnings.end(), w) == site.warnings.end()) site.warnings.push_back(w);
        }

        void Rule(std::ostringstream& o, const std::string& sel, const std::string& decls)
        {
            if (!decls.empty()) o << sel << " {\n" << decls << "}\n";
        }

        // ---- media
        std::string AssetUrl(const std::string& id)
        {
            auto known = assetPath.find(id);
            if (known != assetPath.end()) return known->second;
            auto it = doc.assets.find(id);
            if (it == doc.assets.end() || it->second.data.empty()) return assetPath[id] = "";
            const Asset& a = it->second;
            const auto& d = a.data;
            std::string ext = ".png";
            if (a.kind == 1) ext = ".svg";
            else if (a.kind == 2) ext = ".gif";
            else if (a.kind == 3)
            {
                ext = Lower(fs::u8path(a.name).extension().u8string());
                if (ext != ".mp4" && ext != ".webm" && ext != ".mov" && ext != ".m4v") ext = ".mp4";
                if (ext == ".mov") Warn(a.name + ": .mov plays in Safari only, convert it to .mp4 for other browsers");
            }
            else if (d.size() > 3 && d[0] == 0xFF && d[1] == 0xD8) ext = ".jpg";
            else if (d.size() > 12 && !memcmp(d.data(), "RIFF", 4) && !memcmp(d.data() + 8, "WEBP", 4)) ext = ".webp";
            std::string stem = Slug(fs::u8path(a.name).stem().u8string(), "image");
            std::string path = "assets/" + stem + ext;
            for (int i = 2; usedPaths.count(path); i++) path = "assets/" + stem + "-" + std::to_string(i) + ext;
            usedPaths.insert(path);
            site.files[path] = CachedAsset(a);
            return assetPath[id] = path;
        }

        // ---- fonts: the face Fimgui draws the text with, under the family name of the design
        std::string FontStack(const std::string& family, const std::string& style)
        {
            std::string key = family + "|" + style;
            if (opt.fonts && fontsDone.insert(key).second)
            {
                const FontFace* face = fonts::Find(family, style);
                std::string ext = face ? Lower(fs::u8path(face->path).extension().u8string()) : "";
                const char* format = ext == ".ttf" ? "truetype" : ext == ".otf" ? "opentype" : ext == ".woff2" ? "woff2" : ext == ".woff" ? "woff" : nullptr;
                if (!format) Warn(family + " " + style + ": only TTF / OTF / WOFF fonts can be shipped, the browser picks a fallback");
                else
                {
                    std::string path = "assets/fonts/" + fs::u8path(face->path).filename().u8string();
                    if (!site.files.count(path))
                    {
                        if (WebSite::Bytes b = CachedFile(face->path)) site.files[path] = b;
                        else Warn("Cannot read the font " + face->path);
                    }
                    bool italic = Lower(style).find("italic") != std::string::npos;
                    fontCss << "@font-face {\n  font-family: \"" << family << "\";\n  src: url(\"../" << path << "\") format(\"" << format << "\");\n"
                            << "  font-weight: " << CssFontWeight(style) << ";\n  font-style: " << (italic ? "italic" : "normal") << ";\n  font-display: swap;\n}\n";
                }
            }
            return "\"" + family + "\", system-ui, sans-serif";
        }

        // ---- animation: keyframes sampled from the shared engine (same motion as the editor and the C++ / Java exports)
        void Anim(Node* n, const AnimFx& a, float extraDelay, const std::string& sel, std::string& d, std::vector<std::string>& transitions)
        {
            fim::AnimSpec s = ToAnimSpec(a, 0.0f);
            int cat = fim::AnimCategoryOf(s);
            std::string name = "fim-" + Slug(n->id, "x");
            auto Frame = [&](float pct, const fim::AnimValue& v) {
                css << "  " << N(pct) << "% { translate: " << Px(v.dx) << " " << Px(v.dy) << "; scale: " << N(v.scale)
                    << "; rotate: " << N(n->rotation + v.rot) << "deg; opacity: " << N(v.alpha * n->opacity) << "; }\n";
            };
            const char* trig = a.trigger == fim::Trigger_Press ? ":active" : a.trigger == fim::Trigger_Active ? ".is-active" : ":hover";
            if (a.preset == fim::Anim_Keyframes)
            {
                css << "@keyframes " << name << " {\n";
                for (int k = 0; k <= 30; k++) Frame(k * 100.0f / 30, fim::AnimKeyframesAt(s, a.duration * k / 30));
                css << "}\n";
                if (cat == 2) Rule(states, sel + trig, "  animation: " + name + " " + Sec(a.duration) + " linear both;\n");
                else d += "  animation: " + name + " " + Sec(a.duration) + " linear " + Sec(a.delay + extraDelay) + (cat == 1 ? " infinite" : " both") + ";\n";
            }
            else if (cat == 0 || cat == 1)
            {
                // Easing is baked into the samples, so the CSS timing is linear.
                s.delay = 0;
                float total = cat == 1 ? a.duration + a.delay : a.duration;
                css << "@keyframes " << name << " {\n";
                for (int k = 0; k <= 20; k++) Frame(k * 5.0f, fim::AnimSample(s, total * k / 20, 0));
                css << "}\n";
                d += "  animation: " + name + " " + Sec(total) + " linear " + Sec(cat == 1 ? extraDelay : a.delay + extraDelay) + (cat == 1 ? " infinite" : " both") + ";\n";
            }
            else
            {
                fim::AnimValue v = fim::AnimSample(s, 0, 1);
                Rule(states, sel + trig, "  translate: " + Px(v.dx) + " " + Px(v.dy) + ";\n  scale: " + N(v.scale) + ";\n  opacity: " + N(v.alpha * n->opacity) + ";\n");
                std::string t = Sec(a.duration) + " cubic-bezier(.2, .8, .2, 1)";
                transitions.push_back("translate " + t);
                transitions.push_back("scale " + t);
                transitions.push_back("opacity " + t);
            }
        }

        // ---- one layer
        void Emit(Node* n, std::ostringstream& h, int depth, bool phrasing, const AnimFx* inh, float inhDelay)
        {
            if (!n->visible) return;
            codemap::Begin(h, n->id); codemap::Begin(css, n->id); codemap::Begin(states, n->id);
            EmitLayer(n, h, depth, phrasing, inh, inhDelay);
            codemap::End(h); codemap::End(css); codemap::End(states);
        }

        void EmitLayer(Node* n, std::ostringstream& h, int depth, bool phrasing, const AnimFx* inh, float inhDelay)
        {
            Node* parent = n->parent;
            bool root = !parent && !snippet;
            bool text = n->type == NodeType::Text;
            std::string cls = ClassOf(n), sel = "." + cls;
            std::string ind(depth * 2, ' ');
            std::string d;
            auto P = [&](const char* k, const std::string& v) { d += std::string("  ") + k + ": " + v + ";\n"; };
            std::vector<std::string> shadows, transitions;

            // -- link target
            std::string href;
            bool external = false;
            if (!n->link.empty())
            {
                if (n->link.rfind("page:", 0) == 0)
                {
                    auto it = pageFile.find(n->link.substr(5));
                    href = it != pageFile.end() ? it->second : "#";
                }
                else { href = n->link; external = href.rfind("http", 0) == 0; }
            }

            // -- box
            bool autoW = (text && n->textSizing == 1) || (n->IsContainer() && n->layout && n->hugW);
            bool autoH = (text && n->textSizing >= 1) || (n->IsContainer() && n->layout && n->hugH);
            float x = n->x, w = n->w;
            if (n->sliderPart)
                if (Node* s = SliderOwner(n))
                {
                    // the starting value; the runtime moves the part with the value
                    float range = s->vmax - s->vmin, t = range == 0 ? 0.5f : std::clamp((start.Get(s->var, s->value) - s->vmin) / range, 0.0f, 1.0f);
                    float ox = 0;
                    for (Node* q = n->parent; q && q != s; q = q->parent) ox += q->x;
                    if (n->sliderPart == 1) { x = -ox; w = std::round(s->w * t); }
                    else x = std::round(-ox + s->w * t - n->w * 0.5f);
                }
            if (root) { P("position", "relative"); P("width", Px(n->w)); P("height", Px(n->h)); P("margin", "0 auto"); }
            else if (parent && parent->layout)
            {
                bool row = parent->layout == 1;
                bool fillMain = row ? n->fillW : n->fillH, fillCross = row ? n->fillH : n->fillW;
                P("position", "relative");
                if (fillMain) { P("flex", "1 1 0"); P(row ? "min-width" : "min-height", "0"); }
                else P("flex", "none");
                if (fillCross) P("align-self", "stretch");
                if (!(row ? fillMain || autoW : fillCross || autoW)) P("width", Px(w));
                if (!(row ? fillCross || autoH : fillMain || autoH)) P("height", Px(n->h));
            }
            else
            {
                float y = n->type == NodeType::Line ? n->y - n->strokeWidth * 0.5f : n->y;
                P("position", "absolute");
                P("left", Px(x));
                P("top", Px(y));
                if (!autoW) P("width", Px(w));
                if (n->type == NodeType::Line) P("height", Px(std::max(1.0f, n->strokeWidth)));
                else if (!autoH) P("height", Px(n->h));
            }
            if (n->IsContainer() && n->layout)
            {
                static const char* jm[] = { "flex-start", "center", "flex-end", "space-between" };
                static const char* ac[] = { "flex-start", "center", "flex-end", "stretch" };
                P("display", "flex");
                P("flex-direction", n->layout == 1 ? "row" : "column");
                P("justify-content", jm[std::clamp(n->alignMain, 0, 3)]);
                P("align-items", ac[std::clamp(n->alignCross, 0, 3)]);
                if (n->gap) P("gap", Px(n->gap));
                if (n->pad[0] || n->pad[1] || n->pad[2] || n->pad[3]) P("padding", Px(n->pad[1]) + " " + Px(n->pad[2]) + " " + Px(n->pad[3]) + " " + Px(n->pad[0]));
            }

            // -- paint
            if (text)
            {
                P("margin", "0");
                P("font-family", FontStack(n->fontFamily, n->fontStyle));
                P("font-weight", std::to_string(CssFontWeight(n->fontStyle)));
                if (Lower(n->fontStyle).find("italic") != std::string::npos) P("font-style", "italic");
                P("font-size", Px(n->fontSize));
                P("line-height", n->lineHeight > 0 ? Px(n->lineHeight) : "1.2");
                P("white-space", n->textSizing == 1 ? "pre" : "pre-wrap");
                if (n->alignH) P("text-align", n->alignH == 1 ? "center" : "right");
                if (n->alignV && !autoH) { P("display", "flex"); P("flex-direction", "column"); P("justify-content", n->alignV == 1 ? "center" : "flex-end"); }
                if (n->fill.type == 1) P("color", CssColor(n->fill.c0));
                else if (n->fill.type > 1) { P("background", CssPaint(n->fill)); P("-webkit-background-clip", "text"); P("background-clip", "text"); P("color", "transparent"); }
            }
            else if (n->type == NodeType::Line)
            {
                if (n->stroke.type) P("background", CssPaint(n->stroke));
            }
            else if (n->type != NodeType::Image && n->type != NodeType::Vector && n->type != NodeType::Path && !root)
            {
                if (n->fill.type) P("background", CssPaint(n->fill));
                else if (n->glass.on) P("background", CssColor(n->glass.tint));
            }
            if (n->type == NodeType::Ellipse) P("border-radius", "50%");
            else if (!text && n->type != NodeType::Path && (n->radius[0] || n->radius[1] || n->radius[2] || n->radius[3]))
            {
                bool same = n->radius[0] == n->radius[1] && n->radius[1] == n->radius[2] && n->radius[2] == n->radius[3];
                P("border-radius", same ? Px(n->radius[0]) : Px(n->radius[0]) + " " + Px(n->radius[1]) + " " + Px(n->radius[2]) + " " + Px(n->radius[3]));
            }
            // Strokes are outlines: they follow the corner radius and do not move the content like a border would.
            if (n->stroke.type && !text && n->type != NodeType::Line && n->type != NodeType::Path && n->strokeWidth > 0)
            {
                P("outline", Px(n->strokeWidth) + " solid " + CssColor(n->stroke.c0));
                P("outline-offset", Px(n->strokeAlign == 0 ? -n->strokeWidth : n->strokeAlign == 1 ? -n->strokeWidth * 0.5f : 0.0f));
                if (n->stroke.type > 1) Warn(n->name + ": gradient strokes are exported in their first color");
            }
            if (n->IsContainer() && n->clip && !n->children.empty()) P("overflow", "hidden");

            // -- effects
            if (n->effect.type)
            {
                const Effect& e = n->effect;
                std::string off = Px(e.offset.x) + " " + Px(e.offset.y) + " " + Px(e.blur);
                if (e.type == 1 && text) P("text-shadow", off + " " + CssColor(e.color));
                else if (e.type == 1 && (n->type == NodeType::Vector || n->type == NodeType::Path)) P("filter", "drop-shadow(" + off + " " + CssColor(e.color) + ")");
                else shadows.push_back(std::string(e.type == 2 ? "inset " : "") + off + " " + Px(e.spread) + " " + CssColor(e.color));
            }
            if (n->glass.on)
            {
                const GlassFx& g = n->glass;
                std::string f = "blur(" + Px(g.blur * 0.5f) + ") saturate(" + N(g.saturation * 100) + "%)";
                P("-webkit-backdrop-filter", f);
                P("backdrop-filter", f);
                shadows.push_back("inset 0 1px 0 " + CssColor(ImVec4(1, 1, 1, 0.55f * g.highlight)));
                shadows.push_back("inset 0 0 " + Px(g.thickness * 0.6f) + " " + CssColor(ImVec4(1, 1, 1, 0.14f * g.highlight)));
            }
            else if (n->blur.on)
            {
                std::string f = "blur(" + Px(n->blur.radius * 0.5f) + ")";
                P("-webkit-backdrop-filter", f);
                P("backdrop-filter", f);
            }
            if (!shadows.empty())
            {
                std::string s;
                for (auto& x2 : shadows) s += (s.empty() ? "" : ", ") + x2;
                P("box-shadow", s);
            }
            if (n->opacity < 1.0f) P("opacity", N(n->opacity));
            if (n->blend) P("mix-blend-mode", BlendModeKey(n->blend));
            if (n->rotation != 0.0f && !root) P("rotate", N(n->rotation) + "deg");

            // texture overlay: above the fill, below the content
            std::string textureVideo;
            if (n->texture.on && !n->texture.asset.empty())
            {
                const TextureFx& t = n->texture;
                std::string url = AssetUrl(t.asset);
                auto it = doc.assets.find(t.asset);
                static const char* blends[] = { "normal", "plus-lighter", "multiply", "screen" };
                const char* blend = blends[std::clamp(t.blend, 0, 3)];
                if (!url.empty() && it != doc.assets.end() && it->second.kind == 3)
                    textureVideo = "<video class=\"fim-texture\" src=\"" + url + "\" autoplay muted loop playsinline aria-hidden=\"true\"></video>";
                else if (!url.empty())
                {
                    float tw = std::max(1.0f, (float)(it != doc.assets.end() ? it->second.width : 64) * t.scale);
                    std::string after = "  content: \"\";\n  position: absolute;\n  inset: 0;\n  pointer-events: none;\n  border-radius: inherit;\n";
                    after += "  background: url(\"../" + url + "\") " + (t.fit == 2 ? "0 0 / " + Px(tw) + " auto repeat" : t.fit == 1 ? "center / contain no-repeat" : "center / cover no-repeat") + ";\n";
                    after += "  opacity: " + N(t.opacity) + ";\n";
                    if (t.blend) after += std::string("  mix-blend-mode: ") + blend + ";\n";
                    if (t.fit == 2 && (t.speed.x != 0 || t.speed.y != 0))
                    {
                        css << "@keyframes " << "fim-tex-" << Slug(n->id, "x") << " { to { background-position: " << Px(t.speed.x * 20) << " " << Px(t.speed.y * 20) << "; } }\n";
                        after += "  animation: fim-tex-" + Slug(n->id, "x") + " 20s linear infinite;\n";
                    }
                    Rule(states, sel + "::before", after);
                }
                if (!textureVideo.empty())
                    Rule(states, sel + " > .fim-texture", "  position: absolute;\n  inset: 0;\n  width: 100%;\n  height: 100%;\n  object-fit: " + std::string(t.fit == 1 ? "contain" : "cover")
                         + ";\n  opacity: " + N(t.opacity) + ";\n  pointer-events: none;\n  border-radius: inherit;\n" + (t.blend ? std::string("  mix-blend-mode: ") + blend + ";\n" : ""));
            }

            // -- interaction states: the hover / active colors replace the fill
            if (n->role != Role::None && (n->hasHover || n->hasActive))
            {
                const char* prop = text ? "color" : "background";
                if (n->hasHover) Rule(states, sel + ":hover", std::string("  ") + prop + ": " + CssColor(n->hoverColor) + ";\n");
                if (n->hasActive)
                {
                    bool latched = n->role == Role::Tab || n->role == Role::Checkbox;
                    Rule(states, sel + (latched ? ".is-active" : ":active"), std::string("  ") + prop + ": " + CssColor(n->activeColor) + ";\n");
                    if (latched && n->hasHover) Rule(states, sel + ".is-active:hover", std::string("  ") + prop + ": " + CssColor(n->activeColor) + ";\n");
                }
                transitions.push_back(text ? "color .15s" : "background-color .15s");
            }

            // -- motion (a staggered parent animates its children one after another)
            const AnimFx* anim = n->anim.preset && n->anim.stagger <= 0.0f ? &n->anim : inh;
            if (anim) Anim(n, *anim, anim == inh ? inhDelay : 0.0f, sel, d, transitions);
            if (!transitions.empty())
            {
                std::string t;
                for (auto& x2 : transitions) t += (t.empty() ? "" : ", ") + x2;
                P("transition", t);
            }

            // -- element
            std::string tag = "div", attrs, classes = root ? "page " + cls : cls;
            auto A = [&](const std::string& k, const std::string& v) { attrs += " " + k + "=\"" + Esc(v, true) + "\""; };
            bool hasVar = !n->var.empty();
            bool interactive = true;
            switch (n->role)
            {
            case Role::Button:
                tag = href.empty() ? "button" : "a";
                if (hasVar) { A("data-set", n->var); A("data-value", N(n->value)); }
                A("data-press", SanitizeIdent(n->name.empty() ? n->id : n->name, "button"));
                break;
            case Role::Tab:
                tag = "button"; A("role", "tab");
                if (hasVar) { A("data-set", n->var); A("data-value", N(n->value)); }
                break;
            case Role::Checkbox:
                tag = "button"; A("role", "switch");
                if (hasVar) A("data-toggle", n->var);
                A("aria-checked", start.Get(n->var) != 0 ? "true" : "false");
                break;
            case Role::Slider:
                A("role", "slider"); A("tabindex", "0");
                if (hasVar) { A("data-slider", n->var); A("data-min", N(n->vmin)); A("data-max", N(n->vmax)); A("data-w", N(n->w)); }
                A("aria-valuemin", N(std::min(n->vmin, n->vmax))); A("aria-valuemax", N(std::max(n->vmin, n->vmax))); A("aria-valuenow", N(start.Get(n->var, n->value)));
                A("aria-label", n->name);
                break;
            case Role::Combo:
            case Role::MultiCombo:
            {
                A("role", "combobox"); A("tabindex", "0"); A("aria-haspopup", "listbox"); A("aria-expanded", "false");
                if (hasVar) A("data-combo", n->var);
                A("aria-label", n->name);
                fim::ComboStyle cs = ComboStyleFor(n, 1.0f);
                const Node* label = nullptr;
                for (auto& c : n->children) if (c->type == NodeType::Text) { label = c.get(); break; }
                P("--fim-list-bg", CssColor(n->listFill));
                P("--fim-accent", CssColor(n->listAccent));
                P("--fim-hover", CssColor(ImGui::ColorConvertU32ToFloat4(cs.hover)));
                P("--fim-list-border", CssColor(ImGui::ColorConvertU32ToFloat4(cs.border)));
                P("--fim-text", CssColor(ImGui::ColorConvertU32ToFloat4(cs.text)));
                P("--fim-radius", Px(n->radius[0]));
                P("--fim-item-h", Px(n->itemHeight > 0 ? n->itemHeight : n->h));
                P("--fim-pad-x", Px(label ? label->x : 12.0f));
                if (label) { P("font-family", FontStack(label->fontFamily, label->fontStyle)); P("font-size", Px(label->fontSize)); P("font-weight", std::to_string(CssFontWeight(label->fontStyle))); }
                break;
            }
            default:
                interactive = !href.empty();
                if (!href.empty()) tag = "a";
                break;
            }
            if (!href.empty() && tag == "a")
            {
                A("href", href);
                if (external) { A("target", "_blank"); A("rel", "noopener"); }
            }
            if (tag == "button") attrs = " type=\"button\"" + attrs;
            if (root) { tag = "main"; A("data-w", N(n->w)); }

            // media and text
            bool selfClose = false;
            std::string content;
            if (text)
            {
                if (tag != "a") tag = heading == n ? "h1" : "p";
                content = Esc(ExpandText(n->text, start), false);
                if (n->text.find('{') != std::string::npos) A("data-text", n->text);
            }
            else if (n->type == NodeType::Image)
            {
                std::string url = AssetUrl(n->asset);
                auto it = doc.assets.find(n->asset);
                P("object-fit", n->imageFit == 1 ? "contain" : n->imageFit == 2 ? "fill" : "cover");
                if (it != doc.assets.end() && it->second.kind == 3) { tag = "video"; attrs += " src=\"" + Esc(url, true) + "\" autoplay muted loop playsinline"; }
                else { tag = "img"; selfClose = true; A("src", url); A("alt", n->name); attrs += " decoding=\"async\""; }
            }
            else if (n->type == NodeType::Vector)
            {
                std::string url = AssetUrl(n->asset);
                auto it = doc.assets.find(n->asset);
                if (n->recolor && it != doc.assets.end())
                {
                    // a data URI: masks are CORS requests, which fail for pages opened from disk
                    tag = "span";
                    std::string m = "url(\"data:image/svg+xml;base64," + Base64Encode(it->second.data.data(), it->second.data.size()) + "\") center / contain no-repeat";
                    P("background", CssColor(n->fill.c0));
                    P("-webkit-mask", m);
                    P("mask", m);
                    A("role", "img"); A("aria-label", n->name);
                }
                else { tag = "img"; selfClose = true; A("src", url); A("alt", n->name); attrs += " decoding=\"async\""; }
            }
            if (phrasing && (tag == "div" || tag == "p" || tag == "h1" || tag == "button" || tag == "a")) tag = "span";   // no blocks or nested controls inside a button / link

            // Show when: the runtime toggles "hidden"; the starting state is written right away
            if (!n->showVar.empty())
            {
                A("data-show", n->showVar);
                A("data-eq", N(n->showValue));
                if (n->showNot) attrs += " data-not";
                if (!NodeConditionVisible(n, start)) attrs += " hidden";
            }

            if (root) Rule(css, "main" + sel, d);
            else Rule(css, sel, d);

            if (n->type == NodeType::Path)
            {
                // the curve itself as inline SVG; rotation, opacity and shadow come from the CSS above
                std::unique_ptr<Node> flat = doc.CloneNode(n, false);
                flat->rotation = 0; flat->opacity = 1; flat->effect.type = 0;
                std::string svg = NodeToSVG(doc, flat.get());
                if (svg.rfind("<svg ", 0) == 0) svg = "<svg class=\"" + classes + "\" aria-hidden=\"true\"" + attrs + " " + svg.substr(5);
                while (!svg.empty() && svg.back() == '\n') svg.pop_back();
                h << ind << svg << "\n";
                return;
            }
            h << ind << "<" << tag << " class=\"" << classes << "\"" << attrs << ">";
            if (selfClose) { h << "\n"; return; }
            if (text) { h << content << "</" << tag << ">\n"; return; }
            bool empty = textureVideo.empty() && n->role != Role::Input && std::none_of(n->children.begin(), n->children.end(), [](const std::unique_ptr<Node>& c) { return c->visible; });
            if (empty) { h << "</" << tag << ">\n"; return; }
            h << "\n";
            if (!textureVideo.empty()) h << ind << "  " << textureVideo << "\n";

            // text field: an <input> where the label is; the label text is its placeholder
            const Node* placeholder = nullptr;
            if (n->role == Role::Input && hasVar)
            {
                for (auto& c : n->children) if (c->type == NodeType::Text) { placeholder = c.get(); break; }
                const Node* l = placeholder;
                std::string in = "  left: " + Px(l ? l->x : 8.0f) + ";\n  right: " + Px(l ? l->x : 8.0f) + ";\n";
                if (l)
                {
                    in += "  font-family: " + FontStack(l->fontFamily, l->fontStyle) + ";\n  font-weight: " + std::to_string(CssFontWeight(l->fontStyle)) + ";\n  font-size: " + Px(l->fontSize) + ";\n";
                    if (l->fill.type) in += "  color: " + CssColor(ImVec4(l->fill.c0.x, l->fill.c0.y, l->fill.c0.z, 1)) + ";\n";
                    if (l->alignH) in += std::string("  text-align: ") + (l->alignH == 1 ? "center" : "right") + ";\n";
                }
                Rule(states, sel + " > .fim-input", in);
                if (l && l->fill.type) Rule(states, sel + " > .fim-input::placeholder", "  color: " + CssColor(l->fill.c0) + ";\n  opacity: 1;\n");
                h << ind << "  <input class=\"fim-input\" type=\"text\" data-input=\"" << Esc(n->var, true) << "\" placeholder=\"" << Esc(l ? ExpandText(l->text, start) : std::string(), true)
                  << "\" aria-label=\"" << Esc(n->name, true) << "\" value=\"" << Esc(start.str.count(n->var) ? start.str.at(n->var) : std::string(), true) << "\">\n";
            }

            bool childPhrasing = phrasing || tag == "button" || tag == "a" || (interactive && tag == "span");
            const AnimFx* childInh = n->anim.preset && n->anim.stagger > 0.0f ? &n->anim : nullptr;
            for (size_t i = 0; i < n->children.size(); i++)
            {
                Node* c = n->children[i].get();
                if (c == placeholder) continue;
                if (c->sliderPart)
                {
                    // the runtime needs the part's offset inside its slider
                    float ox = 0;
                    for (Node* q = c->parent; q && q != SliderOwner(c); q = q->parent) ox += q->x;
                    std::ostringstream sub;
                    Emit(c, sub, depth + 1, childPhrasing, childInh, childInh ? childInh->delay + childInh->stagger * i : 0.0f);
                    std::string s = sub.str();
                    size_t at = s.find(" class=\"");
                    if (at != std::string::npos) s.insert(at, std::string(" data-part=\"") + (c->sliderPart == 1 ? "fill" : "knob") + "\" data-ox=\"" + N(ox) + "\"");
                    h << s;
                    continue;
                }
                Emit(c, h, depth + 1, childPhrasing, childInh, childInh ? childInh->delay + childInh->stagger * i : 0.0f);
            }
            h << ind << "</" << tag << ">\n";
        }

        // ---- the state description read by fimgui.js
        std::string StateJson(const std::string& siteName)
        {
            json vj = json::object(), cj = json::object();
            for (auto& v : CollectVars(doc))
            {
                const char* kind = v.kind == VarKind::Bool ? "bool" : v.kind == VarKind::Int ? "int" : v.kind == VarKind::Float ? "float" : v.kind == VarKind::String ? "string" : "mask";
                json def = v.kind == VarKind::String ? json(start.str.count(v.name) ? start.str.at(v.name) : std::string()) : json(start.Get(v.name, v.def));
                vj[v.name] = { { "kind", kind }, { "def", def } };
            }
            doc.ForEach([&](Node* n) {
                if ((n->role != Role::Combo && n->role != Role::MultiCombo) || n->var.empty() || cj.contains(n->var)) return;
                cj[n->var] = { { "options", n->options }, { "multi", n->role == Role::MultiCombo }, { "none", n->noneLabel }, { "all", n->allLabel } };
            });
            std::string s = json{ { "site", siteName }, { "vars", vj }, { "combos", cj } }.dump();
            for (size_t p = 0; (p = s.find("</", p)) != std::string::npos; p += 3) s.replace(p, 2, "<\\/");
            return s;
        }
    };

    const Node* HeadingOf(Node* page)
    {
        const Node* best = nullptr;
        std::function<void(Node*)> walk = [&](Node* n) {
            if (!n->visible) return;
            if (n->type == NodeType::Text && n->fontSize >= 24 && (!best || n->fontSize > best->fontSize)) best = n;
            for (auto& c : n->children) walk(c.get());
        };
        walk(page);
        return best;
    }
}

std::string WebPageFile(Document& doc, const Node* frame)
{
    for (auto& [n, file] : Pages(doc)) if (n == frame) return file;
    return "";
}

WebSite BuildWebSite(Document& doc, const WebOptions& opt, const std::string& runtimeDir, bool liveReload)
{
    WebSite site;
    WebGen g(doc, opt, site);
    EnsureVars(doc, g.start);
    // Static layout is the one of the starting state; the browser re-flows auto layout when "Show when" hides layers.
    const VarStore* layoutVars = g_layoutVars;
    g_layoutVars = &g.start;
    doc.ApplyLayout();
    g_layoutVars = layoutVars;

    auto pages = Pages(doc);
    for (auto& [n, file] : pages) { g.pageFile[n->id] = file; site.pages.push_back({ n->id, file }); }
    std::string siteName = opt.title.empty() ? doc.name : opt.title;
    std::string state = g.StateJson(siteName);

    for (auto& [n, file] : pages)
    {
        std::ostringstream body;
        g.heading = HeadingOf(n);
        g.Emit(n, body, 1, false, nullptr, 0.0f);
        std::string bodyCls = "p-" + Slug(n->id, "x");
        if (n->fill.type) g.Rule(g.css, "body." + bodyCls, "  background: " + CssPaint(n->fill) + ";\n");

        std::string title = file == "index.html" ? siteName : n->name + " \xC2\xB7 " + siteName;
        std::ostringstream h;
        h << "<!doctype html>\n<html lang=\"" << Esc(opt.lang.empty() ? "en" : opt.lang, true) << "\">\n<head>\n"
          << "<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
          << "<title>" << Esc(title, false) << "</title>\n"
          << "<meta name=\"generator\" content=\"Fimgui " FIMGUI_VERSION "\">\n";
        if (n->fill.type == 1) h << "<meta name=\"theme-color\" content=\"" << ColorToHex(ImVec4(n->fill.c0.x, n->fill.c0.y, n->fill.c0.z, 1)) << "\">\n";
        h << "<link rel=\"stylesheet\" href=\"css/site.css\">\n<script src=\"js/fimgui.js\" defer></script>\n</head>\n"
          << "<body class=\"" << bodyCls << "\"" << (opt.fitWidth ? " data-fit" : "") << ">\n"
          << body.str()
          << "<script type=\"application/json\" id=\"fim-state\">" << state << "</script>\n";
        if (liveReload) h << kLiveScript;
        h << "</body>\n</html>\n";
        site.files[file] = Text(h.str());
    }

    std::ostringstream css;
    css << "/* " << siteName << " - generated by Fimgui " FIMGUI_VERSION ". Change the design and export again instead of editing this file. */\n\n"
        << g.fontCss.str() << (g.fontCss.tellp() > 0 ? "\n" : "") << kBaseCss << "\n" << g.css.str() << "\n/* states */\n" << g.states.str();
    site.files["css/site.css"] = Text(css.str());

    std::vector<unsigned char> js;
    if (platform::ReadFile((fs::u8path(runtimeDir) / "web" / "fimgui.js").u8string(), js)) site.files["js/fimgui.js"] = std::make_shared<const std::string>(js.begin(), js.end());
    else g.Warn("runtime/web/fimgui.js is missing next to Fimgui.exe: buttons, tabs and other controls will not work");
    if (pages.empty()) g.Warn("Nothing to export: add a frame, every top-level frame becomes a page");
    return site;
}

ExportResult ExportWeb(Document& doc, const WebOptions& opt, const std::string& runtimeDir)
{
    ExportResult res;
    if (opt.outDir.empty()) { res.message = "Choose an output folder"; return res; }
    WebSite site = BuildWebSite(doc, opt, runtimeDir, false);
    if (site.pages.empty()) { res.message = "Nothing to export: add a frame first"; return res; }
    fs::path out = fs::u8path(opt.outDir);

    std::ostringstream rd;
    std::string siteName = opt.title.empty() ? doc.name : opt.title;
    rd << "# " << siteName << "\n\nGenerated by Fimgui " FIMGUI_VERSION ". Open `index.html` in a browser or upload the folder to any static hosting.\n\n## Pages\n\n";
    for (auto& [id, file] : site.pages) if (Node* n = doc.Find(id)) rd << "- `" << file << "` - " << n->name << "\n";
    rd << "\n## Files\n\n- `css/site.css` - layout and styles of every page, fonts, animations\n- `js/fimgui.js` - state, buttons, tabs, toggles, sliders, text fields, dropdowns, \"Show when\", fitting into narrow windows\n"
       << "- `assets/` - images, videos, vectors and fonts\n\n"
       << "## Your code\n\n```js\nfimgui.get('tab');\nfimgui.set('tab', 1);\ndocument.addEventListener('fim:change', e => console.log(e.detail.name, e.detail.value));\n"
       << "document.addEventListener('fim:press', e => console.log('pressed', e.detail.name));\n```\n\n"
       << "## License\n\nThe generated pages and the Fimgui web runtime may be used, modified and sold as part of your own site (see `LICENSE-FIMGUI.txt`). Fimgui itself may not be sold.\n";
    if (!site.warnings.empty())
    {
        rd << "\n## Export notes\n\n";
        for (auto& w : site.warnings) rd << "- " << w << "\n";
    }
    site.files["README.md"] = std::make_shared<const std::string>(rd.str());
    std::vector<unsigned char> lic;
    if (platform::ReadFile((fs::u8path(runtimeDir) / "FIMGUI_LICENSE.txt").u8string(), lic)) site.files["LICENSE-FIMGUI.txt"] = std::make_shared<const std::string>(lic.begin(), lic.end());

    for (auto& [path, bytes] : site.files)
    {
        fs::path p = out / fs::u8path(path);
        std::error_code ec;
        fs::create_directories(p.parent_path(), ec);
        if (!bytes || !platform::WriteFile(p.u8string(), bytes->data(), bytes->size())) { res.message = "Cannot write " + p.u8string(); return res; }
        res.files.push_back(path);
    }
    res.ok = true;
    res.message = "Exported " + std::to_string(site.pages.size()) + (site.pages.size() == 1 ? " page" : " pages") + ", " + std::to_string(res.files.size()) + " files";
    if (!site.warnings.empty()) res.message += " (" + std::to_string(site.warnings.size()) + " notes in README.md)";
    return res;
}

std::string NodeToHTML(Document& doc, Node* n)
{
    WebSite site;
    WebOptions opt;
    opt.fonts = false;
    WebGen g(doc, opt, site);
    g.snippet = n->parent != nullptr || n->type != NodeType::Frame;
    EnsureVars(doc, g.start);
    for (auto& [p, file] : Pages(doc)) g.pageFile[p->id] = file;
    std::ostringstream h;
    if (!g.snippet) g.heading = HeadingOf(n);
    g.Emit(n, h, 0, false, nullptr, 0.0f);
    return h.str() + "\n<style>\n" + g.css.str() + g.states.str() + "</style>\n";
}
