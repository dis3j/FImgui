#include "fonts.h"
#include "document.h"
#include "../runtime/fimgui_runtime.h"
#include "renderer.h"
#include <windows.h>
#include <shlobj.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <set>

namespace fs = std::filesystem;

namespace fonts
{
    static std::vector<FontFace> g_faces;
    static std::vector<std::string> g_families;
    static std::map<std::string, ImFont*> g_loaded;      // key: path
    static std::set<std::string> g_pending;
    static ImFont* g_ui[UI_Count] = {};

    static unsigned U16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
    static unsigned U32(const unsigned char* p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

    static std::string Utf16BeToUtf8(const unsigned char* p, unsigned len)
    {
        std::wstring w;
        for (unsigned i = 0; i + 1 < len; i += 2) w += (wchar_t)U16(p + i);
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    // Reads family/subfamily (+ weight from OS/2) from a TTF/OTF file without loading the whole file.
    static bool ReadFaceInfo(const std::wstring& path, FontFace* out)
    {
        FILE* f = _wfopen(path.c_str(), L"rb");
        if (!f) return false;
        unsigned char hdr[12];
        bool ok = false;
        if (fread(hdr, 1, 12, f) == 12)
        {
            unsigned tag = U32(hdr);
            if (tag == 0x00010000 || tag == 0x4F54544F /*OTTO*/ || tag == 0x74727565 /*true*/)
            {
                unsigned numTables = U16(hdr + 4);
                std::vector<unsigned char> dir(numTables * 16);
                if (fread(dir.data(), 1, dir.size(), f) == dir.size())
                {
                    unsigned nameOff = 0, nameLen = 0, os2Off = 0;
                    for (unsigned i = 0; i < numTables; i++)
                    {
                        const unsigned char* e = &dir[i * 16];
                        if (!memcmp(e, "name", 4)) { nameOff = U32(e + 8); nameLen = U32(e + 12); }
                        if (!memcmp(e, "OS/2", 4)) os2Off = U32(e + 8);
                    }
                    if (os2Off)
                    {
                        unsigned char os2[6];
                        fseek(f, os2Off, SEEK_SET);
                        if (fread(os2, 1, 6, f) == 6) out->weight = U16(os2 + 4);
                    }
                    if (nameOff && nameLen && nameLen < 1 << 20)
                    {
                        std::vector<unsigned char> nt(nameLen);
                        fseek(f, nameOff, SEEK_SET);
                        if (fread(nt.data(), 1, nameLen, f) == nameLen && nameLen >= 6)
                        {
                            unsigned count = U16(&nt[2]), strOff = U16(&nt[4]);
                            std::string fam[2], sub[2];   // [0] = id 1/2, [1] = typographic id 16/17
                            for (unsigned i = 0; i < count && 6 + i * 12 + 12 <= nameLen; i++)
                            {
                                const unsigned char* r = &nt[6 + i * 12];
                                unsigned plat = U16(r), enc = U16(r + 2), lang = U16(r + 4), id = U16(r + 6), len = U16(r + 8), off = U16(r + 10);
                                if (plat != 3 || (enc != 1 && enc != 0) || lang != 0x409) continue;
                                if (strOff + off + len > nameLen) continue;
                                std::string s = Utf16BeToUtf8(&nt[strOff + off], len);
                                if (id == 1) fam[0] = s; else if (id == 2) sub[0] = s;
                                else if (id == 16) fam[1] = s; else if (id == 17) sub[1] = s;
                            }
                            out->family = !fam[1].empty() ? fam[1] : fam[0];
                            out->style = !sub[1].empty() ? sub[1] : sub[0];
                            if (out->style.empty()) out->style = "Regular";
                            out->italic = out->style.find("Italic") != std::string::npos || out->style.find("Oblique") != std::string::npos;
                            ok = !out->family.empty();
                        }
                    }
                }
            }
        }
        fclose(f);
        return ok;
    }

    static void AddFace(const FontFace& face)
    {
        for (auto& f : g_faces)
            if (f.family == face.family && f.style == face.style) return;
        g_faces.push_back(face);
    }

    static void ScanDir(const fs::path& dir)
    {
        std::error_code ec;
        if (!fs::exists(dir, ec)) return;
        for (auto& e : fs::directory_iterator(dir, ec))
        {
            std::wstring ext = e.path().extension().wstring();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::towlower);
            if (ext != L".ttf" && ext != L".otf") continue;
            FontFace face;
            if (ReadFaceInfo(e.path().wstring(), &face))
            {
                face.path = e.path().u8string();
                AddFace(face);
            }
        }
    }

    static void RebuildFamilies()
    {
        std::set<std::string> s;
        for (auto& f : g_faces) s.insert(f.family);
        g_families.assign(s.begin(), s.end());
    }

    static ImFont* LoadPath(const std::string& path)
    {
        auto it = g_loaded.find(path);
        if (it != g_loaded.end()) return it->second;
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), 14.0f, &cfg);
        g_loaded[path] = f;
        return f;
    }

    void Init(const std::string& resourceDir)
    {
        ScanDir(fs::u8path(resourceDir) / "fonts");
        wchar_t* p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Fonts, 0, nullptr, &p))) { ScanDir(p); CoTaskMemFree(p); }
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        {
            ScanDir(fs::path(p) / "Microsoft" / "Windows" / "Fonts");
            CoTaskMemFree(p);
        }
        RebuildFamilies();

        // UI fonts: Inter if bundled, else Segoe UI.
        const char* styles[UI_Count] = { "Regular", "Medium", "SemiBold" };
        for (int i = 0; i < UI_Count; i++)
        {
            const FontFace* f = Find("Inter", styles[i]);
            if (!f || f->family != "Inter") f = Find("Segoe UI", i == 0 ? "Regular" : "Semibold");
            if (f) g_ui[i] = LoadPath(f->path);
        }
        if (!g_ui[0]) g_ui[0] = ImGui::GetIO().Fonts->AddFontDefault();
        for (int i = 1; i < UI_Count; i++) if (!g_ui[i]) g_ui[i] = g_ui[0];
        ImGui::GetIO().FontDefault = g_ui[0];
    }

    void ProcessPending()
    {
        for (auto& p : g_pending) LoadPath(p);
        g_pending.clear();
    }

    const std::vector<std::string>& Families() { return g_families; }

    std::vector<std::string> Styles(const std::string& family)
    {
        std::vector<const FontFace*> v;
        for (auto& f : g_faces) if (f.family == family) v.push_back(&f);
        std::sort(v.begin(), v.end(), [](const FontFace* a, const FontFace* b) {
            if (a->italic != b->italic) return !a->italic;
            return a->weight < b->weight;
        });
        std::vector<std::string> out;
        for (auto* f : v) out.push_back(f->style);
        return out;
    }

    static int StyleWeight(const std::string& s)
    {
        std::string l = s;
        std::transform(l.begin(), l.end(), l.begin(), ::tolower);
        l.erase(std::remove(l.begin(), l.end(), ' '), l.end());
        if (l.find("thin") != std::string::npos) return 100;
        if (l.find("extralight") != std::string::npos) return 200;
        if (l.find("light") != std::string::npos) return 300;
        if (l.find("medium") != std::string::npos) return 500;
        if (l.find("semibold") != std::string::npos || l.find("demibold") != std::string::npos) return 600;
        if (l.find("extrabold") != std::string::npos) return 800;
        if (l.find("black") != std::string::npos || l.find("heavy") != std::string::npos) return 900;
        if (l.find("bold") != std::string::npos) return 700;
        return 400;
    }

    const FontFace* Find(const std::string& family, const std::string& style)
    {
        const FontFace* best = nullptr;
        int bestScore = INT_MAX;
        bool wantItalic = style.find("Italic") != std::string::npos;
        int wantW = StyleWeight(style);
        for (auto& f : g_faces)
        {
            if (_stricmp(f.family.c_str(), family.c_str()) != 0) continue;
            if (_stricmp(f.style.c_str(), style.c_str()) == 0) return &f;
            int score = abs(f.weight - wantW) + (f.italic != wantItalic ? 1000 : 0);
            if (score < bestScore) { bestScore = score; best = &f; }
        }
        if (best) return best;
        if (_stricmp(family.c_str(), "Inter") != 0) return Find("Inter", style);
        for (auto& f : g_faces) if (f.family == "Segoe UI" && f.style == "Regular") return &f;
        return g_faces.empty() ? nullptr : &g_faces[0];
    }

    ImFont* Get(const std::string& family, const std::string& style)
    {
        const FontFace* f = Find(family, style);
        if (!f) return g_ui[0];
        auto it = g_loaded.find(f->path);
        if (it != g_loaded.end() && it->second) return it->second;
        g_pending.insert(f->path);
        return g_ui[0];
    }

    bool AddFontFile(const std::string& path, std::string* familyOut)
    {
        FontFace face;
        if (!ReadFaceInfo(fs::u8path(path).wstring(), &face)) return false;
        face.path = path;
        AddFace(face);
        RebuildFamilies();
        if (familyOut) *familyOut = face.family;
        return true;
    }

    void AddFontDir(const std::string& dir)
    {
        ScanDir(fs::u8path(dir));
        RebuildFamilies();
    }

    ImFont* UI(UiFont f) { return g_ui[f]; }
}

const VarStore* g_textVars = nullptr;

ImVec2 MeasureNodeText(const Node* n, float wrapWidth)
{
    ImFont* font = fonts::Get(n->fontFamily, n->fontStyle);
    std::string text = g_textVars ? ExpandText(n->text, *g_textVars) : n->text;
    return fim::MeasureText(font, n->fontSize, text.empty() ? " " : text.c_str(), wrapWidth, n->lineHeight);
}
