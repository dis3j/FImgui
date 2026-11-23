// Code tab: generated code with IDE-style highlighting, tied to the canvas. The selected layer's lines are marked and
// scrolled into view; hovering a line outlines its layer on the canvas, clicking selects it, double-clicking zooms to it.
#include "app.h"
#include "theme.h"
#include "fonts.h"
#include "widgets.h"
#include "exporters.h"
#include "codegen.h"
#include "codegen_web.h"
#include "codemap.h"
#include "platform.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <functional>
#include <unordered_set>

using namespace theme;

namespace
{
    enum Lang { Lang_CSS, Lang_SVG, Lang_Cpp, Lang_Json, Lang_Html };
    enum Tok : unsigned char { T_Text, T_Comment, T_Keyword, T_Control, T_Type, T_Func, T_String, T_Number, T_Var, T_Macro, T_Punct, T_Tag, T_Selector, T_Preproc, T_Count };

    // Dark+ colours, the scheme most editors ship with
    const ImU32 kTokCol[T_Count] = {
        IM_COL32(212, 212, 212, 255),   // text, operators
        IM_COL32(106, 153, 85, 255),    // comment
        IM_COL32(86, 156, 214, 255),    // keyword
        IM_COL32(197, 134, 192, 255),   // control flow, at-rules
        IM_COL32(78, 201, 176, 255),    // type, namespace
        IM_COL32(220, 220, 170, 255),   // function
        IM_COL32(206, 145, 120, 255),   // string, CSS value
        IM_COL32(181, 206, 168, 255),   // number
        IM_COL32(156, 220, 254, 255),   // variable, property, attribute, JSON key
        IM_COL32(79, 193, 255, 255),    // macro
        IM_COL32(128, 128, 128, 255),   // markup brackets
        IM_COL32(86, 156, 214, 255),    // tag
        IM_COL32(215, 186, 125, 255),   // CSS selector
        IM_COL32(197, 134, 192, 255),   // preprocessor
    };
    const ImU32 kCodeBg = IM_COL32(30, 30, 30, 255);
    const ImU32 kLayerBg = IM_COL32(13, 153, 255, 26);       // lines of the selected layer
    const ImU32 kHoverBg = IM_COL32(255, 255, 255, 9);       // lines of the layer under the mouse
    const ImU32 kSelectBg = IM_COL32(38, 79, 120, 255);      // text selection
    const ImU32 kFindBg = IM_COL32(234, 179, 8, 64), kFindCurBg = IM_COL32(234, 179, 8, 150);
    const int kMaxCols = 800;                                 // longer lines (inline images) are cut on screen, not in copies

    struct Token { int a, b; Tok t; };
    enum : unsigned char { St_Comment = 1, St_Style = 2, St_Tag = 4, St_StyleNext = 8 };

    void Push(std::vector<Token>& o, int a, int b, Tok t)
    {
        if (b <= a) return;
        if (!o.empty() && o.back().t == t && o.back().b == a) { o.back().b = b; return; }
        o.push_back({ a, b, t });
    }

    bool IdStart(char c) { return std::isalpha((unsigned char)c) || c == '_'; }
    bool IdChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }
    bool Digit(char c) { return c >= '0' && c <= '9'; }

    int FindStr(const char* s, int n, int from, const char* what)
    {
        int k = (int)strlen(what);
        for (int i = from; i + k <= n; i++) if (!memcmp(s + i, what, k)) return i;
        return -1;
    }

    int SkipQuoted(const char* s, int n, int i)   // i at the opening quote; returns the index after the closing one
    {
        char q = s[i];
        int e = i + 1;
        while (e < n && s[e] != q) { if (s[e] == '\\') e++; e++; }
        return std::min(e + 1, n);
    }

    // ---- C++
    Tok CppWord(const std::string& w, const char* s, int n, int a, int e)
    {
        static const std::unordered_set<std::string> control = { "if", "else", "for", "while", "do", "return", "switch", "case", "break", "continue", "default", "goto" };
        static const std::unordered_set<std::string> keyword = { "const", "static", "void", "bool", "int", "float", "double", "char", "unsigned", "signed", "long", "short",
            "auto", "struct", "class", "namespace", "using", "extern", "inline", "constexpr", "true", "false", "nullptr", "sizeof", "enum", "typedef", "template",
            "typename", "this", "new", "delete", "operator", "public", "private", "protected", "virtual", "override", "static_cast", "size_t" };
        if (control.count(w)) return T_Control;
        if (keyword.count(w)) return T_Keyword;
        int k = e;
        while (k < n && s[k] == ' ') k++;
        if (k < n && s[k] == '(') return T_Func;
        if (e + 1 < n && s[e] == ':' && s[e + 1] == ':') return T_Type;
        if ((a > 0 && s[a - 1] == '.') || (a > 1 && s[a - 1] == '>' && s[a - 2] == '-')) return T_Var;
        bool caps = w.size() > 1, upper = false;
        for (char c : w) { if (std::islower((unsigned char)c)) caps = false; if (std::isupper((unsigned char)c)) upper = true; }
        if (caps && upper) return T_Macro;
        if (std::isupper((unsigned char)w[0])) return T_Type;
        return T_Var;
    }

    void TokCpp(const char* s, int n, unsigned char& st, std::vector<Token>& o)
    {
        int i = 0;
        if (st & St_Comment)
        {
            int e = FindStr(s, n, 0, "*/");
            if (e < 0) { Push(o, 0, n, T_Comment); return; }
            Push(o, 0, e + 2, T_Comment);
            st &= ~St_Comment;
            i = e + 2;
        }
        int f = i;
        while (f < n && s[f] == ' ') f++;
        if (f < n && s[f] == '#')
        {
            int e = f + 1;
            while (e < n && IdChar(s[e])) e++;
            Push(o, i, f, T_Text);
            Push(o, f, e, T_Preproc);
            if (std::string(s + f, e - f) == "#include") { Push(o, e, n, T_String); return; }
            i = e;
        }
        while (i < n)
        {
            char c = s[i];
            if (c == '/' && i + 1 < n && s[i + 1] == '/') { Push(o, i, n, T_Comment); return; }
            if (c == '/' && i + 1 < n && s[i + 1] == '*')
            {
                int e = FindStr(s, n, i + 2, "*/");
                if (e < 0) { Push(o, i, n, T_Comment); st |= St_Comment; return; }
                Push(o, i, e + 2, T_Comment);
                i = e + 2;
                continue;
            }
            if (c == '"' || c == '\'') { int e = SkipQuoted(s, n, i); Push(o, i, e, T_String); i = e; continue; }
            if (Digit(c) || (c == '.' && i + 1 < n && Digit(s[i + 1])))
            {
                bool hex = c == '0' && i + 1 < n && (s[i + 1] == 'x' || s[i + 1] == 'X');
                int e = i + 1;
                while (e < n && (IdChar(s[e]) || s[e] == '.' || (!hex && (s[e] == '+' || s[e] == '-') && (s[e - 1] == 'e' || s[e - 1] == 'E')))) e++;
                Push(o, i, e, T_Number);
                i = e;
                continue;
            }
            if (IdStart(c))
            {
                int e = i;
                while (e < n && IdChar(s[e])) e++;
                Push(o, i, e, CppWord(std::string(s + i, e - i), s, n, i, e));
                i = e;
                continue;
            }
            Push(o, i, i + 1, T_Text);
            i++;
        }
    }

    // ---- CSS: rules ("sel { prop: value; }") or bare declarations (the CSS tab)
    int CssValue(const char* s, int i, int to, std::vector<Token>& o)   // until ';' or '}' outside parentheses
    {
        int depth = 0;
        while (i < to)
        {
            char c = s[i];
            if ((c == ';' || c == '}') && depth == 0) return i;
            if (c == '(') depth++;
            if (c == ')') depth = std::max(0, depth - 1);
            if (c == '/' && i + 1 < to && s[i + 1] == '*') { int e = FindStr(s, to, i + 2, "*/"); e = e < 0 ? to : e + 2; Push(o, i, e, T_Comment); i = e; continue; }
            if (c == '"' || c == '\'') { int e = std::min(SkipQuoted(s, to, i), to); Push(o, i, e, T_String); i = e; continue; }
            if (c == '#') { int e = i + 1; while (e < to && std::isalnum((unsigned char)s[e])) e++; Push(o, i, e, T_String); i = e; continue; }
            if (Digit(c) || ((c == '-' || c == '.') && i + 1 < to && (Digit(s[i + 1]) || s[i + 1] == '.')))
            {
                int e = i + 1;
                while (e < to && (std::isalnum((unsigned char)s[e]) || s[e] == '.' || s[e] == '%')) e++;
                Push(o, i, e, T_Number);
                i = e;
                continue;
            }
            if (c == '!' && i + 1 < to && IdStart(s[i + 1])) { int e = i + 1; while (e < to && IdChar(s[e])) e++; Push(o, i, e, T_Keyword); i = e; continue; }
            if (IdStart(c) || c == '-')
            {
                int e = i + 1;
                while (e < to && (IdChar(s[e]) || s[e] == '-')) e++;
                Tok t = s[e < to ? e : to - 1] == '(' && e < to ? T_Func : (c == '-' && i + 1 < to && s[i + 1] == '-') ? T_Var : T_String;
                Push(o, i, e, t);
                i = e;
                continue;
            }
            Push(o, i, i + 1, T_Text);
            i++;
        }
        return to;
    }

    void CssDecls(const char* s, int i, int to, std::vector<Token>& o)
    {
        while (i < to)
        {
            char c = s[i];
            if (c == ' ' || c == ';' || c == '}' || c == '{') { Push(o, i, i + 1, T_Text); i++; continue; }
            if (c == '/' && i + 1 < to && s[i + 1] == '*') { int e = FindStr(s, to, i + 2, "*/"); e = e < 0 ? to : e + 2; Push(o, i, e, T_Comment); i = e; continue; }
            int e = i;
            while (e < to && (IdChar(s[e]) || s[e] == '-')) e++;
            int k = e;
            while (k < to && s[k] == ' ') k++;
            if (e > i && k < to && s[k] == ':')
            {
                Push(o, i, e, T_Var);
                Push(o, e, k + 1, T_Text);
                i = CssValue(s, k + 1, to, o);
                continue;
            }
            Push(o, i, to, T_Selector);   // not a declaration
            return;
        }
    }

    void TokCss(const char* s, int from, int to, unsigned char& st, std::vector<Token>& o)
    {
        int i = from;
        if (st & St_Comment)
        {
            int e = FindStr(s, to, i, "*/");
            if (e < 0) { Push(o, i, to, T_Comment); return; }
            Push(o, i, e + 2, T_Comment);
            st &= ~St_Comment;
            i = e + 2;
        }
        // a comment that stays open swallows the rest of the line
        int open = FindStr(s, to, i, "/*");
        if (open >= 0 && FindStr(s, to, open + 2, "*/") < 0) { st |= St_Comment; TokCss(s, i, open, st, o); Push(o, open, to, T_Comment); return; }
        int brace = -1;
        for (int k = i; k < to; k++)
        {
            if (s[k] == '"' || s[k] == '\'') { k = SkipQuoted(s, to, k) - 1; continue; }
            if (s[k] == '/' && k + 1 < to && s[k + 1] == '*') break;
            if (s[k] == ':' || s[k] == ';') { if (s[k] == ';') break; }
            if (s[k] == '{') { brace = k; break; }
        }
        if (brace < 0) { CssDecls(s, i, to, o); return; }
        int k = i;
        while (k < brace && s[k] == ' ') k++;
        Push(o, i, k, T_Text);
        if (k < brace && s[k] == '@') { int e = k + 1; while (e < brace && (IdChar(s[e]) || s[e] == '-')) e++; Push(o, k, e, T_Control); k = e; }
        Push(o, k, brace, T_Selector);
        Push(o, brace, brace + 1, T_Text);
        CssDecls(s, brace + 1, to, o);
    }

    // ---- HTML / SVG (with CSS inside <style>)
    void TokMarkup(const char* s, int n, unsigned char& st, std::vector<Token>& o)
    {
        int i = 0;
        while (i < n)
        {
            if (st & St_Style)
            {
                int e = FindStr(s, n, i, "</style");
                if (e < 0) { TokCss(s, i, n, st, o); return; }
                TokCss(s, i, e, st, o);
                st &= ~(St_Style | St_Comment);
                i = e;
            }
            if (st & St_Comment)
            {
                int e = FindStr(s, n, i, "-->");
                if (e < 0) { Push(o, i, n, T_Comment); return; }
                Push(o, i, e + 3, T_Comment);
                st &= ~St_Comment;
                i = e + 3;
                continue;
            }
            if (st & St_Tag)
            {
                char c = s[i];
                if (c == '>' || (c == '/' && i + 1 < n && s[i + 1] == '>'))
                {
                    int e = c == '>' ? i + 1 : i + 2;
                    Push(o, i, e, T_Punct);
                    st &= ~St_Tag;
                    if (st & St_StyleNext) { st &= ~St_StyleNext; st |= St_Style; }
                    i = e;
                    continue;
                }
                if (c == '"' || c == '\'') { int e = SkipQuoted(s, n, i); Push(o, i, e, T_String); i = e; continue; }
                if (c == ' ' || c == '=') { Push(o, i, i + 1, T_Text); i++; continue; }
                int e = i;
                while (e < n && s[e] != ' ' && s[e] != '=' && s[e] != '>' && !(s[e] == '/' && e + 1 < n && s[e + 1] == '>')) e++;
                Push(o, i, std::max(e, i + 1), T_Var);
                i = std::max(e, i + 1);
                continue;
            }
            if (s[i] == '<')
            {
                if (FindStr(s, n, i, "<!--") == i) { st |= St_Comment; Push(o, i, i + 4, T_Comment); i += 4; continue; }
                int e = i + 1;
                if (e < n && (s[e] == '/' || s[e] == '!' || s[e] == '?')) e++;
                Push(o, i, e, T_Punct);
                int t = e;
                while (t < n && (std::isalnum((unsigned char)s[t]) || s[t] == '-' || s[t] == ':')) t++;
                Push(o, e, t, T_Tag);
                if (s[i + 1] != '/' && std::string(s + e, t - e) == "style") st |= St_StyleNext;
                st |= St_Tag;
                i = t;
                continue;
            }
            int e = i;
            while (e < n && s[e] != '<') e++;
            Push(o, i, e, T_Text);
            i = e;
        }
    }

    // ---- JSON
    void TokJson(const char* s, int n, std::vector<Token>& o)
    {
        int i = 0;
        while (i < n)
        {
            char c = s[i];
            if (c == '"')
            {
                int e = SkipQuoted(s, n, i), k = e;
                while (k < n && s[k] == ' ') k++;
                Push(o, i, e, k < n && s[k] == ':' ? T_Var : T_String);
                i = e;
                continue;
            }
            if (Digit(c) || c == '-') { int e = i + 1; while (e < n && (std::isalnum((unsigned char)s[e]) || s[e] == '.' || s[e] == '+' || s[e] == '-')) e++; Push(o, i, e, T_Number); i = e; continue; }
            if (IdStart(c)) { int e = i; while (e < n && IdChar(s[e])) e++; Push(o, i, e, T_Keyword); i = e; continue; }
            Push(o, i, i + 1, T_Text);
            i++;
        }
    }

    // ---- view state
    struct Line { int start = 0, len = 0, tok0 = 0, tokN = 0, cols = 0; };
    struct Hit { int line, a, b; };
    struct Pos { int line = 0, byte = 0; };

    struct CodeView
    {
        std::string key, target;        // what the code was generated for
        size_t snapHash = 0;
        int lang = 0;
        std::string text;
        std::vector<Line> lines;
        std::vector<Token> toks;
        std::vector<codemap::Span> spans;
        std::vector<int> owner;         // innermost span of each line, -1 none
        int maxCols = 0;

        bool hasSel = false, dragging = false, moved = false, fromCode = false;
        Pos anchor, caret;
        int clickLine = -1;
        std::string scrolledFor;        // selection + key the view last scrolled to

        std::string query, hitsFor;
        std::vector<Hit> hits;
        int cur = -1;
        bool refocus = false, jumpToHit = false;
    };
    CodeView g_cv;

    int Cols(const char* a, const char* b)
    {
        int c = 0;
        for (const char* p = a; p < b; p++) if (((unsigned char)*p & 0xC0) != 0x80) c++;
        return c;
    }

    const char* AdvanceCols(const char* a, const char* b, int cols)
    {
        const char* p = a;
        while (p < b && cols > 0) { p++; while (p < b && ((unsigned char)*p & 0xC0) == 0x80) p++; cols--; }
        return p;
    }

    // Each node object of dump(2) holds an "id": its object runs from the "{" above to the "}" below at the outer indent.
    void JsonSpans(CodeView& v)
    {
        auto indent = [&](int l) { const Line& L = v.lines[l]; int k = 0; while (k < L.len && v.text[L.start + k] == ' ') k++; return k; };
        for (int l = 0; l < (int)v.lines.size(); l++)
        {
            const Line& L = v.lines[l];
            int k = indent(l);
            static const char kId[] = "\"id\": \"";
            if (L.len - k < 8 || v.text.compare(L.start + k, 7, kId) != 0) continue;
            size_t q = v.text.find('"', L.start + k + 7);
            if (q == std::string::npos || (int)q > L.start + L.len) continue;
            codemap::Span s;
            s.id = v.text.substr(L.start + k + 7, q - (L.start + k + 7));
            int a = l, b = l;
            while (a > 0 && indent(a - 1) >= k) a--;
            while (b + 1 < (int)v.lines.size() && indent(b + 1) >= k) b++;
            s.first = std::max(0, a - 1);
            s.last = std::min((int)v.lines.size() - 1, b + 1);
            v.spans.push_back(s);
        }
    }

    void Build(CodeView& v, const std::string& raw)
    {
        v.spans.clear();
        v.text = codemap::Strip(raw, &v.spans);
        if (v.text.empty() || v.text.back() != '\n') v.text += '\n';
        v.lines.clear();
        v.toks.clear();
        v.maxCols = 0;
        unsigned char st = 0;
        size_t p = 0;
        while (p < v.text.size())
        {
            size_t e = v.text.find('\n', p);
            if (e == std::string::npos) e = v.text.size();
            Line L;
            L.start = (int)p;
            L.len = (int)(e - p);
            if (L.len && v.text[e - 1] == '\r') L.len--;
            L.tok0 = (int)v.toks.size();
            std::vector<Token> t;
            const char* s = v.text.data() + p;
            switch (v.lang)
            {
            case Lang_Cpp: TokCpp(s, L.len, st, t); break;
            case Lang_CSS: TokCss(s, 0, L.len, st, t); break;
            case Lang_Json: TokJson(s, L.len, t); break;
            default: TokMarkup(s, L.len, st, t); break;
            }
            v.toks.insert(v.toks.end(), t.begin(), t.end());
            L.tokN = (int)t.size();
            L.cols = Cols(s, s + L.len);
            v.maxCols = std::max(v.maxCols, std::min(L.cols, kMaxCols + 24));
            v.lines.push_back(L);
            p = e + 1;
        }
        if (v.lang == Lang_Json) JsonSpans(v);
        // innermost layer per line: bigger spans first, smaller ones overwrite
        v.owner.assign(v.lines.size(), -1);
        std::vector<int> order(v.spans.size());
        for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
        std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return v.spans[a].last - v.spans[a].first > v.spans[b].last - v.spans[b].first; });
        for (int i : order)
            for (int l = std::max(0, v.spans[i].first); l <= std::min((int)v.lines.size() - 1, v.spans[i].last); l++) v.owner[l] = i;
        v.hasSel = false;
        v.hitsFor.clear();
    }

    std::string Lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }

    void FindHits(CodeView& v)
    {
        v.hits.clear();
        v.cur = -1;
        if (v.query.empty()) return;
        std::string q = Lower(v.query), t = Lower(v.text);
        for (int l = 0; l < (int)v.lines.size(); l++)
        {
            const Line& L = v.lines[l];
            size_t p = L.start;
            while ((p = t.find(q, p)) != std::string::npos && (int)(p + q.size()) <= L.start + L.len)
            {
                v.hits.push_back({ l, (int)p - L.start, (int)(p + q.size()) - L.start });
                p += q.size();
                if (v.hits.size() > 5000) return;
            }
        }
    }

    bool Before(const Pos& a, const Pos& b) { return a.line < b.line || (a.line == b.line && a.byte < b.byte); }

    std::string Selected(const CodeView& v)
    {
        if (!v.hasSel) return "";
        Pos a = v.anchor, b = v.caret;
        if (Before(b, a)) std::swap(a, b);
        size_t s = v.lines[a.line].start + a.byte, e = v.lines[b.line].start + b.byte;
        return e > s ? v.text.substr(s, e - s) : "";
    }

    struct Swatch { int a, b; ImU32 col; };

    // #RRGGBB / #RRGGBBAA and IM_COL32(r, g, b, a) in a line
    std::vector<Swatch> Swatches(const char* s, int n)
    {
        std::vector<Swatch> out;
        auto hex = [](char c) { return std::isxdigit((unsigned char)c) != 0; };
        for (int i = 0; i < n; i++)
        {
            if (s[i] == '#' && (i == 0 || !IdChar(s[i - 1])))
            {
                int e = i + 1;
                while (e < n && hex(s[e])) e++;
                int len = e - i - 1;
                if ((len == 6 || len == 8) && (e >= n || !IdChar(s[e])))
                {
                    unsigned v = (unsigned)strtoul(std::string(s + i + 1, 6).c_str(), nullptr, 16);
                    out.push_back({ i, e, IM_COL32((v >> 16) & 255, (v >> 8) & 255, v & 255, 255) });
                }
                i = e - 1;
            }
            else if (s[i] == 'I' && i + 9 <= n && !memcmp(s + i, "IM_COL32(", 9))
            {
                int r, g, b, a;
                int e = i + 9;
                while (e < n && s[e] != ')') e++;
                if (e < n && sscanf(std::string(s + i + 9, e - i - 9).c_str(), "%d , %d , %d , %d", &r, &g, &b, &a) == 4)
                    out.push_back({ i, e + 1, IM_COL32(r, g, b, 255) });
                i = e;
            }
        }
        return out;
    }

    Node* RootOf(Node* n) { while (n && n->parent) n = n->parent; return n; }
}

void App::DrawCodeTab()
{
    CodeView& v = g_cv;
    float W = ui::Avail();
    const char* langs[] = { "CSS", "SVG", "ImGui", "JSON", "HTML" };
    ImGui::Dummy(ImVec2(0, U(4)));
    ui::Segmented("##lang", &codeLang, langs, 5, W);
    codeLang = std::clamp(codeLang, 0, 4);

    // scope + find
    ImGui::Dummy(ImVec2(0, U(2)));
    const char* scopes[] = { IsWeb() ? "Whole page" : "Whole frame", "Layer only" };
    float bh = ImGui::GetFrameHeight(), x0 = ImGui::GetCursorScreenPos().x, y0 = ImGui::GetCursorScreenPos().y;
    ui::Segmented("##scope", &codeScope, scopes, 2, W - bh - U(6));
    // widen the panel for reading code, and back
    {
        static float narrowW = 0;
        bool wide = narrowW > 0;
        ImVec2 p(x0 + W - bh, y0);
        ImGui::SetCursorScreenPos(p);
        if (ImGui::InvisibleButton("##wide", ImVec2(bh, bh)))
        {
            if (wide) { rightW = narrowW; narrowW = 0; }
            else { narrowW = rightW; rightW = U(100000); }   // clamped to the widest panel
        }
        bool hov = ImGui::IsItemHovered();
        if (hov) ImGui::SetTooltip(wide ? "Restore the panel width" : "Widen the panel to read code\n(or drag the panel edge)");
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->AddRectFilled(p, ImVec2(p.x + bh, p.y + bh), hov ? IM_COL32(255, 255, 255, 40) : Field, ImGui::GetStyle().FrameRounding);
        ImU32 c = hov || wide ? Text : TextDim;
        float cx = p.x + bh * 0.5f, cy = p.y + bh * 0.5f, s = U(3.5f), d = wide ? 1.0f : -1.0f;
        wdl->AddLine(ImVec2(cx - d * s * 0.5f + d * U(1), cy - s), ImVec2(cx + d * s * 0.5f + d * U(1), cy), c, U(1.4f));
        wdl->AddLine(ImVec2(cx + d * s * 0.5f + d * U(1), cy), ImVec2(cx - d * s * 0.5f + d * U(1), cy + s), c, U(1.4f));
        wdl->AddLine(ImVec2(cx - d * U(4.5f), cy - U(5)), ImVec2(cx - d * U(4.5f), cy + U(5)), c, U(1.4f));
        ImGui::SetCursorScreenPos(ImVec2(x0, p.y + bh));
        ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::Dummy(ImVec2(0, U(2)));
    if (v.refocus) { ImGui::SetKeyboardFocusHere(); v.refocus = false; }
    ui::TextField("##find", &v.query, W, "Find (Enter: next match)");
    ImVec2 findMin = ImGui::GetItemRectMin(), findMax = ImGui::GetItemRectMax();
    bool findActive = ImGui::IsItemActive();
    if (ImGui::IsItemDeactivated() && ImGui::IsKeyPressed(ImGuiKey_Enter) && !v.hits.empty())
    {
        int k = (int)v.hits.size();
        v.cur = ((v.cur < 0 ? -1 : v.cur) + (ImGui::GetIO().KeyShift ? -1 : 1) + k) % k;
        v.jumpToHit = true;
        v.refocus = true;
    }
    if (findActive && ImGui::IsKeyPressed(ImGuiKey_Escape)) v.query.clear();

    Node* n = Sel();
    Node* target = n ? (codeScope == 0 ? RootOf(n) : n) : nullptr;

    // regenerate when the target, language or document changes
    if (ImGui::GetFrameCount() % 15 == 0 || v.key.empty()) v.snapHash = std::hash<std::string>{}(m_snapshot);
    std::string key = (target ? target->id : std::string("-")) + "|" + std::to_string(codeLang) + "|" + std::to_string(v.snapHash);
    if (key != v.key)
    {
        v.key = key;
        v.target = target ? target->id : "";
        v.lang = codeLang;
        std::string raw;
        if (target)
        {
            codemap::Scope mark;
            switch (codeLang)
            {
            case Lang_CSS: raw = NodeToCSS(doc, target, true); break;
            case Lang_SVG: raw = NodeToSVG(doc, target); break;
            case Lang_Cpp: raw = GenerateSnippet(doc, target); break;
            case Lang_Json: raw = NodeToJson(target).dump(2); break;
            case Lang_Html: raw = NodeToHTML(doc, target); break;
            }
        }
        Build(v, raw);
    }
    if (v.hitsFor != v.query + "\x1f" + v.key) { v.hitsFor = v.query + "\x1f" + v.key; FindHits(v); }

    // match count inside the find field
    if (!v.query.empty())
    {
        char buf[32];
        if (v.hits.empty()) snprintf(buf, sizeof(buf), "none");
        else if (v.cur >= 0) snprintf(buf, sizeof(buf), "%d of %d", v.cur + 1, (int)v.hits.size());
        else snprintf(buf, sizeof(buf), "%d", (int)v.hits.size());
        ImVec2 ts = ImGui::CalcTextSize(buf);
        ImGui::GetWindowDrawList()->AddText(ImVec2(findMax.x - ts.x - U(8), (findMin.y + findMax.y - ts.y) * 0.5f), v.hits.empty() ? Danger : TextFaint, buf);
    }

    // selected layer: its line ranges (not when it is the whole code)
    std::vector<std::pair<int, int>> mine;
    const codemap::Span* firstMine = nullptr;
    if (n && n->id != v.target)
        for (auto& s : v.spans)
            if (s.id == n->id) { mine.push_back({ s.first, s.last }); if (!firstMine || s.first < firstMine->first) firstMine = &s; }

    const ImGuiStyle& style = ImGui::GetStyle();
    float footerH = ImGui::GetFrameHeight() + ImGui::GetTextLineHeight() + U(6) + style.ItemSpacing.y * 4 + style.WindowPadding.y;
    ImVec2 size(W, std::max(U(80), ImGui::GetContentRegionAvail().y - footerH));
    ImGui::Dummy(ImVec2(0, U(2)));

    std::string status;
    if (!target)
    {
        // empty state
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImDrawList* wdl = ImGui::GetWindowDrawList();
        wdl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), kCodeBg, U(4));
        ImGui::PushTextWrapPos(p.x - ImGui::GetWindowPos().x + size.x - U(16));
        ImGui::SetCursorScreenPos(ImVec2(p.x + U(14), p.y + U(14)));
        ImGui::PushStyleColor(ImGuiCol_Text, Text);
        ImGui::TextUnformatted("No layer selected");
        ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(p.x + U(14), ImGui::GetCursorScreenPos().y + U(2)));
        ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
        ImGui::TextUnformatted(codeScope == 0
            ? "Click a layer on the canvas. The code of its whole frame appears here with the layer's lines marked; hover a line to find its layer, click to select it."
            : "Click a layer on the canvas to see the code of that layer alone.");
        ImGui::PopStyleColor();
        ImGui::PopTextWrapPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y));
        ImGui::Dummy(ImVec2(0, 0));
    }
    else
    {
        ImFont* mono = nullptr;
        for (const char* fam : { "Cascadia Mono", "Consolas" })
        {
            const FontFace* f = fonts::Find(fam, "Regular");
            if (f && f->family == fam) { mono = fonts::Get(fam, "Regular"); break; }
        }
        ImGui::PushStyleColor(ImGuiCol_ChildBg, kCodeBg);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, U(4));
        ImGui::BeginChild("##codeview", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoNav);
        ImGui::PushFont(mono, 12.0f);
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImFont* font = ImGui::GetFont();
        float fs = ImGui::GetFontSize();
        float cw = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "M").x;
        float lh = std::floor(fs * 1.45f);
        float pad = U(6);
        int count = (int)v.lines.size();
        int digits = 1;
        for (int k = count; k >= 10; k /= 10) digits++;
        float gutterW = cw * (digits + 1) + U(10);
        ImRect inner = win->InnerRect;
        float scrollX = ImGui::GetScrollX(), scrollY = ImGui::GetScrollY();
        float top = win->Pos.y + pad - scrollY;
        float textX0 = win->Pos.x + gutterW + U(8) - scrollX;
        float viewH = inner.GetHeight();
        int first = std::max(0, (int)((scrollY - pad) / lh)), last = std::min(count - 1, (int)((scrollY - pad + viewH) / lh) + 1);
        ImGuiIO& io = ImGui::GetIO();

        auto lineText = [&](int l) { return v.text.data() + v.lines[l].start; };
        auto xOf = [&](int l, int byte) { return textX0 + Cols(lineText(l), lineText(l) + std::min(byte, v.lines[l].len)) * cw; };
        auto posAt = [&](ImVec2 m) {
            Pos p;
            p.line = std::clamp((int)std::floor((m.y - top) / lh), 0, count - 1);
            int col = std::max(0, (int)std::lround((m.x - textX0) / cw));
            const char* s = lineText(p.line);
            p.byte = (int)(AdvanceCols(s, s + v.lines[p.line].len, col) - s);
            return p;
        };

        // scroll to the selected layer when the selection changes (not when it was picked here)
        std::string scrollKey = (n ? n->id : std::string()) + "|" + v.key + "|" + std::to_string(codeScope);
        if (scrollKey != v.scrolledFor)
        {
            v.scrolledFor = scrollKey;
            if (firstMine && !v.fromCode)
            {
                int vis0 = first + 1, vis1 = std::max(first + 1, (int)((scrollY - pad + viewH) / lh) - 2);
                if (firstMine->first < vis0 || firstMine->first > vis1) ImGui::SetScrollY(std::max(0.0f, (firstMine->first - 2) * lh));
                ImGui::SetScrollX(0.0f);
            }
            v.fromCode = false;
        }
        if (v.jumpToHit && v.cur >= 0 && v.cur < (int)v.hits.size())
        {
            const Hit& h = v.hits[v.cur];
            ImGui::SetScrollY(std::max(0.0f, (h.line + 0.5f) * lh + pad - viewH * 0.4f));
            float hx = Cols(lineText(h.line), lineText(h.line) + h.a) * cw;
            if (hx < scrollX || hx > scrollX + inner.GetWidth() - gutterW - U(40)) ImGui::SetScrollX(std::max(0.0f, hx - U(40)));
            v.jumpToHit = false;
        }

        // mouse
        bool hovered = ImGui::IsWindowHovered() && inner.Contains(io.MousePos);
        int hoverLine = hovered ? std::clamp((int)std::floor((io.MousePos.y - top) / lh), -1, count - 1) : -1;
        if (hoverLine >= 0 && io.MousePos.y - top >= count * lh) hoverLine = -1;
        int hoverSpan = hoverLine >= 0 && !v.dragging ? v.owner[hoverLine] : -1;
        if (hoverSpan >= 0 && v.spans[hoverSpan].id != (n ? n->id : ""))
        {
            codeHoverId = v.spans[hoverSpan].id;
            codeHoverFrame = ImGui::GetFrameCount();
        }
        if (hovered && hoverLine >= 0)
        {
            if (ImGui::IsMouseDoubleClicked(0))
            {
                int sp = v.owner[hoverLine];
                if (sp >= 0 && doc.Find(v.spans[sp].id)) { v.fromCode = true; Select(v.spans[sp].id); ZoomToFit(true); }
                v.dragging = false;
            }
            else if (ImGui::IsMouseClicked(0))
            {
                v.dragging = true;
                v.moved = false;
                v.clickLine = hoverLine;
                if (io.KeyShift && v.hasSel) v.caret = posAt(io.MousePos);
                else { v.anchor = v.caret = posAt(io.MousePos); v.hasSel = false; }
            }
        }
        if (v.dragging)
        {
            if (ImGui::IsMouseDown(0))
            {
                Pos p = posAt(io.MousePos);
                if (p.line != v.caret.line || p.byte != v.caret.byte) { v.caret = p; }
                if (v.caret.line != v.anchor.line || v.caret.byte != v.anchor.byte) { v.moved = true; v.hasSel = true; }
                if (io.MousePos.y < inner.Min.y) ImGui::SetScrollY(std::max(0.0f, scrollY - lh));
                if (io.MousePos.y > inner.Max.y) ImGui::SetScrollY(scrollY + lh);
                if (io.MousePos.x > inner.Max.x) ImGui::SetScrollX(scrollX + cw * 2);
                if (io.MousePos.x < inner.Min.x + gutterW) ImGui::SetScrollX(std::max(0.0f, scrollX - cw * 2));
            }
            else
            {
                v.dragging = false;
                if (!v.moved)
                {
                    v.hasSel = false;
                    int sp = v.clickLine >= 0 && v.clickLine < count ? v.owner[v.clickLine] : -1;
                    if (sp >= 0 && doc.Find(v.spans[sp].id) && !(n && n->id == v.spans[sp].id)) { v.fromCode = true; Select(v.spans[sp].id); }
                }
            }
        }

        // keys while the view has focus
        if (ImGui::IsWindowFocused())
        {
            codeFocusFrame = ImGui::GetFrameCount();
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
            {
                v.anchor = Pos();
                v.caret.line = count - 1;
                v.caret.byte = v.lines[count - 1].len;
                v.hasSel = true;
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            {
                if (v.hasSel) { platform::SetClipboardText(Selected(v)); Toast("Copied selection"); }
                else if (firstMine)
                {
                    size_t s = v.lines[firstMine->first].start, e = v.lines[firstMine->last].start + v.lines[firstMine->last].len;
                    platform::SetClipboardText(v.text.substr(s, e - s));
                    Toast("Copied lines " + std::to_string(firstMine->first + 1) + "-" + std::to_string(firstMine->last + 1) + " of " + (n->name.empty() ? NodeTypeName(n->type) : n->name));
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) v.hasSel = false;
            if (ImGui::IsKeyPressed(ImGuiKey_F3) && !v.hits.empty())
            {
                int k = (int)v.hits.size();
                v.cur = ((v.cur < 0 ? -1 : v.cur) + (io.KeyShift ? -1 : 1) + k) % k;
                v.jumpToHit = true;
            }
        }

        // ---- draw
        Pos sa = v.anchor, sb = v.caret;
        if (Before(sb, sa)) std::swap(sa, sb);
        auto inMine = [&](int l) { for (auto& r : mine) if (l >= r.first && l <= r.second) return true; return false; };
        const codemap::Span* hs = hoverSpan >= 0 ? &v.spans[hoverSpan] : nullptr;
        float rowL = inner.Min.x, rowR = inner.Max.x;
        dl->PushClipRect(ImVec2(inner.Min.x + gutterW, inner.Min.y), inner.Max, true);
        size_t hitIdx = 0;
        while (hitIdx < v.hits.size() && v.hits[hitIdx].line < first) hitIdx++;
        for (int l = first; l <= last; l++)
        {
            const Line& L = v.lines[l];
            const char* s = lineText(l);
            float y = top + l * lh, ty = y + (lh - fs) * 0.5f;
            if (inMine(l)) dl->AddRectFilled(ImVec2(rowL, y), ImVec2(rowR, y + lh), kLayerBg);
            else if (hs && l >= hs->first && l <= hs->last) dl->AddRectFilled(ImVec2(rowL, y), ImVec2(rowR, y + lh), kHoverBg);
            for (size_t h = hitIdx; h < v.hits.size() && v.hits[h].line == l; h++)
                dl->AddRectFilled(ImVec2(xOf(l, v.hits[h].a), y + 1), ImVec2(xOf(l, v.hits[h].b), y + lh - 1), (int)h == v.cur ? kFindCurBg : kFindBg, U(2));
            while (hitIdx < v.hits.size() && v.hits[hitIdx].line <= l) hitIdx++;
            if (v.hasSel && l >= sa.line && l <= sb.line)
            {
                float x0 = l == sa.line ? xOf(l, sa.byte) : textX0;
                float x1 = l == sb.line ? xOf(l, sb.byte) : xOf(l, L.len) + cw * 0.5f;
                if (x1 > x0) dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + lh), kSelectBg);
            }
            float x = textX0;
            int col = 0;
            bool cut = false;
            for (int k = 0; k < L.tokN && !cut; k++)
            {
                const Token& t = v.toks[L.tok0 + k];
                const char* a = s + t.a;
                const char* b = s + t.b;
                int tc = Cols(a, b);
                if (col + tc > kMaxCols) { b = AdvanceCols(a, b, kMaxCols - col); tc = kMaxCols - col; cut = true; }
                if (x > rowR) break;
                if (x + tc * cw >= inner.Min.x + gutterW) dl->AddText(font, fs, ImVec2(x, ty), kTokCol[t.t], a, b);
                x += tc * cw;
                col += tc;
            }
            if (cut)
            {
                char more[48];
                snprintf(more, sizeof(more), "  ... %d more characters", L.cols - kMaxCols);
                dl->AddText(font, fs, ImVec2(x, ty), TextFaint, more);
            }
            // colour literals get an underline in their colour
            for (const Swatch& sw : Swatches(s, std::min(L.len, (int)(AdvanceCols(s, s + L.len, kMaxCols) - s))))
                dl->AddRectFilled(ImVec2(xOf(l, sw.a), ty + fs + U(1)), ImVec2(xOf(l, sw.b), ty + fs + U(3)), sw.col | IM_COL32_A_MASK);

            // footnote: which layer starts here (C++ and CSS already name it in a comment)
            bool notes = v.lang == Lang_Html || v.lang == Lang_Json || v.lang == Lang_SVG;
            bool selStart = notes && firstMine && l == firstMine->first;
            bool hovStart = notes && hs && l == hs->first && !inMine(l);
            if (selStart || hovStart)
            {
                Node* ln = doc.Find(selStart ? n->id : hs->id);
                if (ln)
                {
                    std::string label = (ln->name.empty() ? std::string(NodeTypeName(ln->type)) : ln->name) + "  " + NodeTypeName(ln->type);
                    float lx = std::max(x, xOf(l, L.len)) + cw * 3;
                    dl->AddText(font, fs, ImVec2(lx, ty), selStart ? IM_COL32(13, 153, 255, 210) : TextFaint, label.c_str());
                }
            }
        }
        dl->PopClipRect();

        // gutter: line numbers, a bar beside the selected layer's lines
        dl->PushClipRect(inner.Min, inner.Max, true);
        dl->AddRectFilled(ImVec2(inner.Min.x, inner.Min.y), ImVec2(inner.Min.x + gutterW, inner.Max.y), kCodeBg);
        for (int l = first; l <= last; l++)
        {
            float y = top + l * lh, ty = y + (lh - fs) * 0.5f;
            bool m = inMine(l);
            char num[16];
            snprintf(num, sizeof(num), "%d", l + 1);
            float nw = Cols(num, num + strlen(num)) * cw;
            dl->AddText(font, fs, ImVec2(inner.Min.x + gutterW - U(8) - nw, ty), m ? Text : l == hoverLine ? TextDim : IM_COL32(255, 255, 255, 70), num);
            if (m) dl->AddRectFilled(ImVec2(inner.Min.x + gutterW - U(3), y), ImVec2(inner.Min.x + gutterW - U(1), y + lh), Accent);
            else if (hs && l >= hs->first && l <= hs->last) dl->AddRectFilled(ImVec2(inner.Min.x + gutterW - U(3), y), ImVec2(inner.Min.x + gutterW - U(1), y + lh), TextFaint);
        }
        dl->PopClipRect();

        // scroll extents
        ImGui::SetCursorPos(ImVec2(0, 0));
        ImGui::Dummy(ImVec2(gutterW + U(8) + (v.maxCols + 30) * cw, pad * 2 + count * lh));
        if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        // status line
        if (hoverLine >= 0 && hs)
        {
            Node* hn = doc.Find(hs->id);
            status = "Ln " + std::to_string(hoverLine + 1) + "   " + (hn ? (hn->name.empty() ? std::string(NodeTypeName(hn->type)) : hn->name) : std::string()) +
                     (hn && !(n && hn == n) ? "   click to select" : "");
        }
        else if (v.hasSel)
        {
            std::string t = Selected(v);
            int ls = (int)std::count(t.begin(), t.end(), '\n') + 1;
            status = std::to_string(Cols(t.data(), t.data() + t.size())) + " characters on " + std::to_string(ls) + (ls == 1 ? " line" : " lines") + " selected";
        }
        else if (firstMine)
            status = (n->name.empty() ? std::string(NodeTypeName(n->type)) : n->name) + "   " +
                     (firstMine->first == firstMine->last ? "line " + std::to_string(firstMine->first + 1) : "lines " + std::to_string(firstMine->first + 1) + "-" + std::to_string(firstMine->last + 1)) +
                     (mine.size() > 1 ? "  (+" + std::to_string(mine.size() - 1) + " more)" : "");
        else
            status = std::to_string(count) + " lines";
    }

    ImGui::Dummy(ImVec2(0, U(2)));
    ImGui::PushStyleColor(ImGuiCol_Text, TextDim);
    ImGui::PushClipRect(ImGui::GetCursorScreenPos(), ImVec2(ImGui::GetCursorScreenPos().x + W, ImGui::GetCursorScreenPos().y + ImGui::GetTextLineHeight() + U(2)), true);
    ImGui::TextUnformatted(status.empty() ? " " : status.c_str());
    ImGui::PopClipRect();
    ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, U(2)));

    bool selText = v.hasSel && !Selected(v).empty();
    ImGui::BeginDisabled(!target);
    if (ui::PrimaryButton(selText ? "Copy selection" : "Copy", ImVec2(W * 0.5f - U(3), 0)) && target)
    {
        if (selText) { platform::SetClipboardText(Selected(v)); Toast("Copied selection"); }
        else if (codeLang == Lang_Json && codeScope == 1) CopyAs(4);
        else { platform::SetClipboardText(v.text); Toast("Copied " + std::string(langs[codeLang])); }
    }
    ImGui::EndDisabled();
    ImGui::SameLine(0, U(6));
    if (ImGui::Button("Copy PNG @2x", ImVec2(W * 0.5f - U(3), 0))) CopyAs(2);
}
