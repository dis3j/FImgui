#include "platform.h"
#include "gfx.h"
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <filesystem>
#include <cstdio>

namespace fs = std::filesystem;
extern HWND g_hwnd;

namespace platform
{
    std::wstring Widen(const std::string& s)
    {
        if (s.empty()) return {};
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
        return w;
    }

    std::string Narrow(const std::wstring& w)
    {
        if (w.empty()) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        std::string s(n, 0);
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    std::string ExeDir()
    {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return fs::path(buf).parent_path().u8string();
    }

    std::string AppDataDir()
    {
        wchar_t* p = nullptr;
        std::string out;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &p)))
        {
            fs::path d = fs::path(p) / "Fimgui";
            std::error_code ec;
            fs::create_directories(d, ec);
            out = d.u8string();
            CoTaskMemFree(p);
        }
        return out;
    }

    std::string GetEnv(const char* name)
    {
        wchar_t buf[4096];
        DWORD n = GetEnvironmentVariableW(Widen(name).c_str(), buf, 4096);
        return n && n < 4096 ? Narrow(std::wstring(buf, n)) : std::string();
    }

    bool ReadFile(const std::string& path, std::vector<unsigned char>& out)
    {
        FILE* f = _wfopen(Widen(path).c_str(), L"rb");
        if (!f) return false;
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        out.resize(sz > 0 ? sz : 0);
        size_t rd = sz > 0 ? fread(out.data(), 1, sz, f) : 0;
        fclose(f);
        return rd == out.size();
    }

    bool ReadText(const std::string& path, std::string& out)
    {
        std::vector<unsigned char> d;
        if (!ReadFile(path, d)) return false;
        out.assign(d.begin(), d.end());
        if (out.size() >= 3 && (unsigned char)out[0] == 0xEF && (unsigned char)out[1] == 0xBB && (unsigned char)out[2] == 0xBF) out.erase(0, 3);
        return true;
    }

    bool WriteFile(const std::string& path, const void* data, size_t size)
    {
        std::error_code ec;
        fs::path p = fs::u8path(path);
        if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
        FILE* f = _wfopen(p.wstring().c_str(), L"wb");
        if (!f) return false;
        size_t wr = size ? fwrite(data, 1, size, f) : 0;
        fclose(f);
        return wr == size;
    }

    bool WriteText(const std::string& path, const std::string& text) { return WriteFile(path, text.data(), text.size()); }

    static std::vector<COMDLG_FILTERSPEC> ParseFilters(const char* filters, std::vector<std::wstring>& storage)
    {
        std::vector<COMDLG_FILTERSPEC> specs;
        std::string f = filters ? filters : "";
        size_t i = 0;
        while (i < f.size())
        {
            size_t a = f.find('|', i);
            if (a == std::string::npos) break;
            size_t b = f.find('|', a + 1);
            storage.push_back(Widen(f.substr(i, a - i)));
            storage.push_back(Widen(f.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1)));
            i = b == std::string::npos ? f.size() : b + 1;
        }
        for (size_t k = 0; k + 1 < storage.size(); k += 2) specs.push_back({ storage[k].c_str(), storage[k + 1].c_str() });
        return specs;
    }

    static std::vector<std::string> RunOpen(const char* title, const char* filters, bool multi, bool folder)
    {
        std::vector<std::string> out;
        IFileOpenDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) return out;
        std::vector<std::wstring> storage;
        auto specs = ParseFilters(filters, storage);
        if (!specs.empty()) dlg->SetFileTypes((UINT)specs.size(), specs.data());
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        if (multi) opts |= FOS_ALLOWMULTISELECT;
        if (folder) opts |= FOS_PICKFOLDERS;
        dlg->SetOptions(opts | FOS_FORCEFILESYSTEM);
        dlg->SetTitle(Widen(title).c_str());
        if (SUCCEEDED(dlg->Show(g_hwnd)))
        {
            IShellItemArray* items = nullptr;
            if (SUCCEEDED(dlg->GetResults(&items)))
            {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; i++)
                {
                    IShellItem* it = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &it)))
                    {
                        PWSTR p = nullptr;
                        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out.push_back(Narrow(p)); CoTaskMemFree(p); }
                        it->Release();
                    }
                }
                items->Release();
            }
        }
        dlg->Release();
        return out;
    }

    std::string OpenFileDialog(const char* title, const char* filters)
    {
        auto v = RunOpen(title, filters, false, false);
        return v.empty() ? std::string() : v[0];
    }

    std::vector<std::string> OpenFilesDialog(const char* title, const char* filters) { return RunOpen(title, filters, true, false); }

    std::string PickFolderDialog(const char* title)
    {
        auto v = RunOpen(title, nullptr, false, true);
        return v.empty() ? std::string() : v[0];
    }

    std::string SaveFileDialog(const char* title, const char* filters, const char* defaultName, const char* defExt)
    {
        std::string out;
        IFileSaveDialog* dlg = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&dlg)))) return out;
        std::vector<std::wstring> storage;
        auto specs = ParseFilters(filters, storage);
        if (!specs.empty()) dlg->SetFileTypes((UINT)specs.size(), specs.data());
        if (defaultName) dlg->SetFileName(Widen(defaultName).c_str());
        if (defExt) dlg->SetDefaultExtension(Widen(defExt).c_str());
        dlg->SetTitle(Widen(title).c_str());
        if (SUCCEEDED(dlg->Show(g_hwnd)))
        {
            IShellItem* it = nullptr;
            if (SUCCEEDED(dlg->GetResult(&it)))
            {
                PWSTR p = nullptr;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { out = Narrow(p); CoTaskMemFree(p); }
                it->Release();
            }
        }
        dlg->Release();
        return out;
    }

    void OpenInExplorer(const std::string& path) { ShellExecuteW(nullptr, L"open", Widen(path).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    void OpenUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", Widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
    void SetWindowTitle(const std::string& t) { SetWindowTextW(g_hwnd, Widen(t).c_str()); }
    void MinimizeWindow() { ShowWindow(g_hwnd, SW_MINIMIZE); }
    void ToggleMaximizeWindow() { ShowWindow(g_hwnd, IsZoomed(g_hwnd) ? SW_RESTORE : SW_MAXIMIZE); }
    bool IsWindowMaximized() { return IsZoomed(g_hwnd) != 0; }
    void CloseMainWindow() { PostMessageW(g_hwnd, WM_CLOSE, 0, 0); }
    bool ScreenPixel(float x, float y, float rgb[3])
    {
        POINT pt = { (LONG)x, (LONG)y };
        ClientToScreen(g_hwnd, &pt);
        HDC dc = GetDC(nullptr);
        COLORREF c = GetPixel(dc, pt.x, pt.y);
        ReleaseDC(nullptr, dc);
        if (c == CLR_INVALID) return false;
        rgb[0] = GetRValue(c) / 255.0f; rgb[1] = GetGValue(c) / 255.0f; rgb[2] = GetBValue(c) / 255.0f;
        return true;
    }
    void StartWindowDrag()
    {
        // Hand the drag to Windows (Aero snap, moving between monitors).
        ReleaseCapture();
        POINT pt;
        GetCursorPos(&pt);
        PostMessageW(g_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
    }

    // ------------------------------------------------------------ clipboard

    struct ClipboardScope
    {
        bool ok;
        ClipboardScope() { ok = false; for (int i = 0; i < 10 && !ok; i++) { ok = OpenClipboard(g_hwnd) != 0; if (!ok) Sleep(5); } }
        ~ClipboardScope() { if (ok) CloseClipboard(); }
    };

    static HGLOBAL MakeGlobal(const void* data, size_t size)
    {
        HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, size);
        if (!h) return nullptr;
        memcpy(GlobalLock(h), data, size);
        GlobalUnlock(h);
        return h;
    }

    bool SetClipboardText(const std::string& utf8)
    {
        ClipboardScope cs;
        if (!cs.ok) return false;
        EmptyClipboard();
        std::wstring w = Widen(utf8);
        HGLOBAL h = MakeGlobal(w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        return h && SetClipboardData(CF_UNICODETEXT, h);
    }

    std::string GetClipboardText()
    {
        ClipboardScope cs;
        if (!cs.ok) return {};
        HANDLE h = GetClipboardData(CF_UNICODETEXT);
        if (!h) return {};
        const wchar_t* p = (const wchar_t*)GlobalLock(h);
        std::string s = p ? Narrow(p) : std::string();
        GlobalUnlock(h);
        return s;
    }

    bool SetClipboardImage(const unsigned char* rgba, int w, int h)
    {
        ClipboardScope cs;
        if (!cs.ok) return false;
        EmptyClipboard();
        std::vector<unsigned char> png = gfx::EncodePNG(rgba, w, h);
        UINT pngFmt = RegisterClipboardFormatW(L"PNG");
        if (HGLOBAL hp = MakeGlobal(png.data(), png.size())) SetClipboardData(pngFmt, hp);

        // DIBV5 with alpha for apps that don't read PNG.
        BITMAPV5HEADER bh = {};
        bh.bV5Size = sizeof(bh);
        bh.bV5Width = w; bh.bV5Height = h; bh.bV5Planes = 1; bh.bV5BitCount = 32;
        bh.bV5Compression = BI_BITFIELDS;
        bh.bV5RedMask = 0x00FF0000; bh.bV5GreenMask = 0x0000FF00; bh.bV5BlueMask = 0x000000FF; bh.bV5AlphaMask = 0xFF000000;
        bh.bV5CSType = LCS_sRGB;
        bh.bV5SizeImage = w * h * 4;
        std::vector<unsigned char> dib(sizeof(bh) + (size_t)w * h * 4);
        memcpy(dib.data(), &bh, sizeof(bh));
        unsigned char* px = dib.data() + sizeof(bh);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
            {
                const unsigned char* s = rgba + ((size_t)(h - 1 - y) * w + x) * 4;
                unsigned char* d = px + ((size_t)y * w + x) * 4;
                d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3];
            }
        if (HGLOBAL hd = MakeGlobal(dib.data(), dib.size())) SetClipboardData(CF_DIBV5, hd);
        return true;
    }

    bool ClipboardHasImage()
    {
        return IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")) || IsClipboardFormatAvailable(CF_DIBV5) || IsClipboardFormatAvailable(CF_DIB);
    }

    bool GetClipboardImage(std::vector<unsigned char>& encoded)
    {
        ClipboardScope cs;
        if (!cs.ok) return false;
        UINT pngFmt = RegisterClipboardFormatW(L"PNG");
        if (HANDLE h = GetClipboardData(pngFmt))
        {
            size_t sz = GlobalSize(h);
            const unsigned char* p = (const unsigned char*)GlobalLock(h);
            if (p) encoded.assign(p, p + sz);
            GlobalUnlock(h);
            if (!encoded.empty()) return true;
        }
        HANDLE h = GetClipboardData(CF_DIBV5);
        if (!h) h = GetClipboardData(CF_DIB);
        if (!h) return false;
        const unsigned char* p = (const unsigned char*)GlobalLock(h);
        if (!p) return false;
        const BITMAPINFOHEADER* bi = (const BITMAPINFOHEADER*)p;
        int w = bi->biWidth, hgt = abs(bi->biHeight);
        bool bottomUp = bi->biHeight > 0;
        int bpp = bi->biBitCount;
        bool ok = false;
        if ((bpp == 32 || bpp == 24) && w > 0 && hgt > 0)
        {
            size_t off = bi->biSize;
            if (bi->biCompression == BI_BITFIELDS && bi->biSize == sizeof(BITMAPINFOHEADER)) off += 12;
            off += bi->biClrUsed * 4;
            const unsigned char* src = p + off;
            int stride = ((w * bpp + 31) / 32) * 4;
            std::vector<unsigned char> rgba((size_t)w * hgt * 4);
            bool anyAlpha = false;
            for (int y = 0; y < hgt; y++)
            {
                const unsigned char* row = src + (size_t)(bottomUp ? hgt - 1 - y : y) * stride;
                for (int x = 0; x < w; x++)
                {
                    unsigned char* d = &rgba[((size_t)y * w + x) * 4];
                    d[0] = row[x * (bpp / 8) + 2]; d[1] = row[x * (bpp / 8) + 1]; d[2] = row[x * (bpp / 8)];
                    d[3] = bpp == 32 ? row[x * 4 + 3] : 255;
                    anyAlpha |= d[3] != 0;
                }
            }
            if (!anyAlpha) for (size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
            encoded = gfx::EncodePNG(rgba.data(), w, hgt);
            ok = !encoded.empty();
        }
        GlobalUnlock(h);
        return ok;
    }

    std::vector<std::string> GetClipboardFiles()
    {
        std::vector<std::string> out;
        ClipboardScope cs;
        if (!cs.ok) return out;
        HANDLE h = GetClipboardData(CF_HDROP);
        if (!h) return out;
        HDROP drop = (HDROP)h;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++)
        {
            wchar_t buf[MAX_PATH];
            if (DragQueryFileW(drop, i, buf, MAX_PATH)) out.push_back(Narrow(buf));
        }
        return out;
    }
}
