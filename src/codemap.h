#pragma once
#include <ostream>
#include <string>
#include <vector>

// Layer -> line ranges of generated code (Code tab). While `enabled` is set, the generators wrap each layer's output
// in markers (\x01 id \x02 ... \x03, always at line starts); Strip removes them and returns the ranges.
namespace codemap
{
    inline thread_local bool enabled = false;

    inline void Begin(std::ostream& o, const std::string& id) { if (enabled) o << '\x01' << id << '\x02'; }
    inline void End(std::ostream& o) { if (enabled) o << '\x03'; }

    struct Span { std::string id; int first = 0, last = 0; };   // lines, 0-based, inclusive

    // Turns marking on for one scope.
    struct Scope
    {
        bool prev;
        Scope() : prev(enabled) { enabled = true; }
        ~Scope() { enabled = prev; }
    };

    inline std::string Strip(const std::string& in, std::vector<Span>* spans)
    {
        std::string out;
        out.reserve(in.size());
        std::vector<Span> open;
        int line = 0;
        bool lineStart = true;
        for (size_t i = 0; i < in.size(); i++)
        {
            char c = in[i];
            if (c == '\x01')
            {
                size_t e = in.find('\x02', i + 1);
                if (e == std::string::npos) break;
                Span s;
                s.id = in.substr(i + 1, e - i - 1);
                s.first = line;
                open.push_back(s);
                i = e;
                continue;
            }
            if (c == '\x03')
            {
                if (open.empty()) continue;
                Span s = open.back();
                open.pop_back();
                s.last = lineStart ? line - 1 : line;
                if (s.last >= s.first && spans) spans->push_back(s);
                continue;
            }
            out += c;
            if (c == '\n') { line++; lineStart = true; }
            else lineStart = false;
        }
        return out;
    }
}
