// Crash handler and the crash report window. See crash.h.
#include "crash.h"
#include "platform.h"
#include "version.h"
#include <windows.h>
#include <dbghelp.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <exception>

#pragma comment(lib, "dbghelp.lib")
#pragma comment(lib, "uxtheme.lib")
// Common Controls 6: themed buttons and scroll bars in the report window.
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace crash
{
    // Filled by Install(): the handler avoids work that can fail in a broken process.
    static wchar_t s_exe[MAX_PATH * 2];
    static wchar_t s_dir[MAX_PATH * 2];
    static char s_context[2048];
    static volatile LONG s_entered = 0;

    static void AppendF(std::string& s, const char* fmt, ...)
    {
        char buf[2048];
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        s += buf;
    }

    static const char* CodeName(DWORD code)
    {
        switch (code)
        {
        case EXCEPTION_ACCESS_VIOLATION: return "Access violation";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "Array bounds exceeded";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "Datatype misalignment";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "Float divide by zero";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "Integer divide by zero";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "Illegal instruction";
        case EXCEPTION_IN_PAGE_ERROR: return "In-page error";
        case EXCEPTION_PRIV_INSTRUCTION: return "Privileged instruction";
        case EXCEPTION_STACK_OVERFLOW: return "Stack overflow";
        case 0xC0000374: return "Heap corruption";
        case 0xE06D7363: return "Unhandled C++ exception";
        default: return "Exception";
        }
    }

    // ".?AVtype_error@detail@nlohmann@@" -> "nlohmann::detail::type_error"
    static std::string TypeName(const char* raw)
    {
        std::string r = raw;
        if (r.rfind(".?AV", 0) != 0 && r.rfind(".?AU", 0) != 0) return r;
        r = r.substr(4);
        if (r.find('?') != std::string::npos) return raw;   // templates: keep the decorated name
        if (r.size() >= 2 && r.compare(r.size() - 2, 2, "@@") == 0) r.resize(r.size() - 2);
        std::string out;
        size_t end = r.size();
        while (true)
        {
            size_t at = r.rfind('@', end - 1);
            std::string part = r.substr(at == std::string::npos ? 0 : at + 1, end - (at == std::string::npos ? 0 : at + 1));
            out += (out.empty() ? "" : "::") + part;
            if (at == std::string::npos || at == 0) break;
            end = at;
        }
        return out;
    }

    static const char* SafeWhat(const std::exception* e)
    {
        __try { return e->what(); }
        __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    }

    // MSVC C++ exception (0xE06D7363): the thrown type and, for std::exception, its message.
    static std::string DescribeCppException(const EXCEPTION_RECORD* rec)
    {
        if (rec->NumberParameters < 4 || rec->ExceptionInformation[0] != 0x19930520) return {};
        struct ThrowInfo { DWORD attributes; int unwind, forwardCompat, catchableTypes; };
        struct CatchableTypeArray { int count; int types[1]; };
        struct CatchableType { DWORD properties; int type; int mdisp, pdisp, vdisp; int size; int copy; };
        struct TypeDescriptor { const void* vtable; void* spare; char name[1]; };
        ULONG_PTR obj = rec->ExceptionInformation[1], base = rec->ExceptionInformation[3];
        auto* ti = (const ThrowInfo*)rec->ExceptionInformation[2];
        if (!ti || !ti->catchableTypes) return {};
        auto* cta = (const CatchableTypeArray*)(base + ti->catchableTypes);
        std::string type, what;
        for (int i = 0; i < cta->count && i < 32; i++)
        {
            auto* ct = (const CatchableType*)(base + cta->types[i]);
            auto* td = (const TypeDescriptor*)(base + ct->type);
            if (i == 0) type = TypeName(td->name);
            if (!strcmp(td->name, ".?AVexception@std@@") && obj)
                if (const char* w = SafeWhat((const std::exception*)(obj + ct->mdisp))) what = w;
        }
        return what.empty() ? type : type + ": " + what;
    }

    static std::string ModuleOf(DWORD64 addr)
    {
        HMODULE mod = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR)addr, &mod)) return "?";
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(mod, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        char buf[MAX_PATH + 32];
        snprintf(buf, sizeof(buf), "%s+0x%llx", platform::Narrow(name ? name + 1 : path).c_str(), (unsigned long long)(addr - (DWORD64)mod));
        return buf;
    }

    static std::string Frame(HANDLE proc, DWORD64 pc)
    {
        std::string s = ModuleOf(pc);
        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 512];
        auto* sym = (SYMBOL_INFO*)symBuf;
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 511;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, pc, &disp, sym)) AppendF(s, "  %s + 0x%llx", sym->Name, (unsigned long long)disp);
        IMAGEHLP_LINE64 line = { sizeof(line) };
        DWORD ldisp = 0;
        if (SymGetLineFromAddr64(proc, pc, &ldisp, &line)) AppendF(s, "  (%s:%lu)", line.FileName, line.LineNumber);
        return s;
    }

    static std::string OsVersion()
    {
        typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
        OSVERSIONINFOW v = { sizeof(v) };
        auto fn = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
        if (!fn || fn(&v) != 0) return "?";
        char buf[64];
        snprintf(buf, sizeof(buf), "%lu.%lu.%lu", v.dwMajorVersion, v.dwMinorVersion, v.dwBuildNumber);
        return buf;
    }

    static std::string BuildReport(const char* reason, EXCEPTION_POINTERS* ep, const std::string& extra)
    {
        std::string s;
        SYSTEMTIME t;
        GetLocalTime(&t);
        AppendF(s, "Fimgui %s crashed\r\n", FIMGUI_VERSION);
        AppendF(s, "Time:     %04d-%02d-%02d %02d:%02d:%02d\r\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        AppendF(s, "Windows:  %s\r\n", OsVersion().c_str());
        const EXCEPTION_RECORD* rec = ep ? ep->ExceptionRecord : nullptr;
        if (rec && reason == nullptr)
        {
            AppendF(s, "Error:    %s (0x%08lX)", CodeName(rec->ExceptionCode), rec->ExceptionCode);
            if ((rec->ExceptionCode == EXCEPTION_ACCESS_VIOLATION || rec->ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && rec->NumberParameters >= 2)
            {
                ULONG_PTR op = rec->ExceptionInformation[0];
                AppendF(s, " %s address 0x%llx", op == 0 ? "reading" : op == 1 ? "writing" : "executing", (unsigned long long)rec->ExceptionInformation[1]);
            }
            s += "\r\n";
            if (rec->ExceptionCode == 0xE06D7363)
            {
                std::string d = DescribeCppException(rec);
                if (!d.empty()) s += "          " + d + "\r\n";
            }
        }
        else AppendF(s, "Error:    %s\r\n", reason ? reason : "?");
        if (!extra.empty()) s += "          " + extra + "\r\n";
        if (s_context[0]) AppendF(s, "Doing:    %s\r\n", s_context);
        s += "Command:  " + platform::Narrow(GetCommandLineW()) + "\r\n";

        s += "\r\nStack:\r\n";
#if defined(_M_X64)
        HANDLE proc = GetCurrentProcess();
        SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES | SYMOPT_FAIL_CRITICAL_ERRORS | SYMOPT_NO_PROMPTS);
        std::wstring exeDir = s_exe;
        exeDir.resize(exeDir.find_last_of(L"\\/") == std::wstring::npos ? 0 : exeDir.find_last_of(L"\\/"));
        SymInitializeW(proc, exeDir.empty() ? nullptr : exeDir.c_str(), TRUE);
        if (ep && ep->ContextRecord)
        {
            CONTEXT c = *ep->ContextRecord;
            STACKFRAME64 f = {};
            f.AddrPC.Offset = c.Rip; f.AddrPC.Mode = AddrModeFlat;
            f.AddrFrame.Offset = c.Rbp; f.AddrFrame.Mode = AddrModeFlat;
            f.AddrStack.Offset = c.Rsp; f.AddrStack.Mode = AddrModeFlat;
            HANDLE thread = GetCurrentThread();
            for (int i = 0; i < 64; i++)
            {
                if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, thread, &f, &c, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
                if (!f.AddrPC.Offset) break;
                AppendF(s, "  #%-2d ", i);
                s += Frame(proc, f.AddrPC.Offset) + "\r\n";
            }
        }
        SymCleanup(proc);
#else
        s += "  (not available on this architecture)\r\n";
#endif
        return s;
    }

    static bool WriteAll(const std::wstring& path, const std::string& text)
    {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        DWORD wr = 0;
        BOOL ok = WriteFile(h, text.data(), (DWORD)text.size(), &wr, nullptr);
        CloseHandle(h);
        return ok && wr == text.size();
    }

    struct Job { const char* reason; EXCEPTION_POINTERS* ep; DWORD threadId; std::string extra; };

    static void WriteReportAndShow(const Job& job)
    {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[64];
        swprintf(stamp, 64, L"crash_%04d-%02d-%02d_%02d-%02d-%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        std::wstring dir = s_dir;
        CreateDirectoryW(dir.c_str(), nullptr);
        std::wstring base = dir + L"\\" + stamp;
        std::string text = BuildReport(job.reason, job.ep, job.extra);

        HANDLE dump = CreateFileW((base + L".dmp").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION mei = { job.threadId, job.ep, FALSE };
            if (MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump, (MINIDUMP_TYPE)(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo),
                                  job.ep ? &mei : nullptr, nullptr, nullptr))
                text += "\r\nMinidump: " + platform::Narrow(base) + ".dmp\r\n";
            CloseHandle(dump);
        }
        std::wstring report = base + L".txt";
        if (!WriteAll(report, text))
        {
            // The exe folder may be read-only: fall back to %TEMP%.
            wchar_t tmp[MAX_PATH];
            GetTempPathW(MAX_PATH, tmp);
            report = std::wstring(tmp) + stamp + L".txt";
            WriteAll(report, text);
        }

        std::wstring cmd = L"\"" + std::wstring(s_exe) + L"\" --crash-report \"" + report + L"\"";
        STARTUPINFOW si = { sizeof(si) };
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(s_exe, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        {
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
        }
    }

    // The report is built on a fresh thread: the crashed one may have no stack left (stack overflow).
    static DWORD WINAPI ReportThread(LPVOID p)
    {
        WriteReportAndShow(*(const Job*)p);
        return 0;
    }

    [[noreturn]] static void ReportAndExit(const char* reason, EXCEPTION_POINTERS* ep, const std::string& extra, UINT exitCode)
    {
        if (InterlockedExchange(&s_entered, 1) == 0)
        {
            Job job{ reason, ep, GetCurrentThreadId(), extra };
            HANDLE th = CreateThread(nullptr, 1 << 20, ReportThread, &job, 0, nullptr);
            if (th) { WaitForSingleObject(th, 60000); CloseHandle(th); }
        }
        TerminateProcess(GetCurrentProcess(), exitCode);
        for (;;) Sleep(INFINITE);
    }

    static LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* ep)
    {
        ReportAndExit(nullptr, ep, {}, ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 1);
    }

    // Crashes reported by the CRT instead of SEH: build exception pointers for the current place.
    [[noreturn]] static void ReportHere(const char* reason, const std::string& extra)
    {
        CONTEXT ctx = {};
        RtlCaptureContext(&ctx);
        EXCEPTION_RECORD rec = {};
        rec.ExceptionCode = 0xE0000001;
#if defined(_M_X64)
        rec.ExceptionAddress = (PVOID)ctx.Rip;
#endif
        EXCEPTION_POINTERS ep = { &rec, &ctx };
        ReportAndExit(reason, &ep, extra, 3);
    }

    static void OnTerminate()
    {
        std::string what;
        if (std::exception_ptr e = std::current_exception())
        {
            try { std::rethrow_exception(e); }
            catch (const std::exception& x) { what = x.what(); }
            catch (...) { what = "unknown exception type"; }
        }
        ReportHere("std::terminate (unhandled C++ exception)", what);
    }

    static void OnAbort(int) { ReportHere("abort()", {}); }
    static void OnPurecall() { ReportHere("Pure virtual function call", {}); }
    static void OnInvalidParameter(const wchar_t* expr, const wchar_t* func, const wchar_t* file, unsigned line, uintptr_t)
    {
        std::string extra;
        if (func) extra = platform::Narrow(func) + (file ? " in " + platform::Narrow(file) + ":" + std::to_string(line) : std::string());
        if (expr) extra += (extra.empty() ? "" : ": ") + platform::Narrow(expr);
        ReportHere("Invalid parameter passed to a C runtime function", extra);
    }

    void Install()
    {
        GetModuleFileNameW(nullptr, s_exe, MAX_PATH * 2);
        std::wstring dir = platform::Widen(LogDir());
        for (wchar_t& c : dir) if (c == L'/') c = L'\\';
        wcsncpy_s(s_dir, dir.c_str(), _TRUNCATE);
        // No Windows Error Reporting dialog (and its sound): the report window replaces it.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        SetUnhandledExceptionFilter(OnUnhandledException);
        std::set_terminate(OnTerminate);
        signal(SIGABRT, OnAbort);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_purecall_handler(OnPurecall);
        _set_invalid_parameter_handler(OnInvalidParameter);
    }

    void SetContext(const std::string& what)
    {
        strncpy_s(s_context, what.c_str(), _TRUNCATE);
    }

    std::string LogDir()
    {
        return platform::ExeDir() + "/crashlogs";
    }

    // ---------------------------------------------------------------- report window

    enum { ID_COPY = 101, ID_FOLDER, ID_RESTART, ID_CLOSE };
    static std::wstring s_reportPath, s_reportText;
    static HWND s_edit, s_title, s_info, s_buttons[4];
    static HFONT s_fontUi, s_fontTitle, s_fontMono;
    static HBRUSH s_bg, s_bgEdit;
    static const COLORREF kBg = RGB(30, 30, 30), kBgEdit = RGB(22, 22, 22), kText = RGB(230, 230, 230), kTextDim = RGB(160, 160, 160);

    static void Layout(HWND hwnd)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        float s = GetDpiForWindow(hwnd) / 96.0f;
        auto S = [s](int v) { return (int)(v * s); };
        int pad = S(18), bh = S(30), bw = S(130), gap = S(8);
        int W = rc.right - pad * 2;
        MoveWindow(s_title, pad, pad, W, S(28), TRUE);
        MoveWindow(s_info, pad, pad + S(32), W, S(40), TRUE);
        int editTop = pad + S(78), by = rc.bottom - pad - bh;
        MoveWindow(s_edit, pad, editTop, W, by - gap * 2 - editTop, TRUE);
        MoveWindow(s_buttons[0], pad, by, bw, bh, TRUE);
        MoveWindow(s_buttons[1], pad + bw + gap, by, bw, bh, TRUE);
        MoveWindow(s_buttons[3], rc.right - pad - bw, by, bw, bh, TRUE);
        MoveWindow(s_buttons[2], rc.right - pad - bw * 2 - gap, by, bw, bh, TRUE);
    }

    static void CreateFonts(HWND hwnd)
    {
        for (HFONT f : { s_fontUi, s_fontTitle, s_fontMono }) if (f) DeleteObject(f);
        int dpi = GetDpiForWindow(hwnd);
        s_fontUi = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        s_fontTitle = CreateFontW(-MulDiv(14, dpi, 72), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
        s_fontMono = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Consolas");
        SendMessageW(s_title, WM_SETFONT, (WPARAM)s_fontTitle, TRUE);
        SendMessageW(s_info, WM_SETFONT, (WPARAM)s_fontUi, TRUE);
        SendMessageW(s_edit, WM_SETFONT, (WPARAM)s_fontMono, TRUE);
        for (HWND b : s_buttons) SendMessageW(b, WM_SETFONT, (WPARAM)s_fontUi, TRUE);
    }

    static void CopyReport(HWND hwnd)
    {
        if (!OpenClipboard(hwnd)) return;
        EmptyClipboard();
        size_t bytes = (s_reportText.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes))
        {
            memcpy(GlobalLock(mem), s_reportText.c_str(), bytes);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem)) GlobalFree(mem);
        }
        CloseClipboard();
        SetWindowTextW(s_buttons[0], L"Copied");
    }

    static LRESULT CALLBACK ReportProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        switch (msg)
        {
        case WM_CREATE:
        {
            HINSTANCE inst = ((CREATESTRUCTW*)lp)->hInstance;
            s_title = CreateWindowW(L"STATIC", L"Fimgui stopped because of an error", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, inst, nullptr);
            std::wstring info = L"Files saved before the crash are not affected. The report was saved to:\n" + s_reportPath;
            s_info = CreateWindowW(L"STATIC", info.c_str(), WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, inst, nullptr);
            s_edit = CreateWindowExW(0, L"EDIT", s_reportText.c_str(), WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL,
                                     0, 0, 0, 0, hwnd, nullptr, inst, nullptr);
            const wchar_t* labels[] = { L"Copy report", L"Open folder", L"Restart Fimgui", L"Close" };
            for (int i = 0; i < 4; i++)
            {
                s_buttons[i] = CreateWindowW(L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)(ID_COPY + i), inst, nullptr);
                SetWindowTheme(s_buttons[i], L"DarkMode_Explorer", nullptr);
            }
            SetWindowTheme(s_edit, L"DarkMode_Explorer", nullptr);
            CreateFonts(hwnd);
            Layout(hwnd);
            SetFocus(s_edit);
            return 0;
        }
        case WM_SIZE: Layout(hwnd); return 0;
        case WM_DPICHANGED:
        {
            RECT* r = (RECT*)lp;
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            CreateFonts(hwnd);
            Layout(hwnd);
            return 0;
        }
        case WM_GETMINMAXINFO:
        {
            float s = GetDpiForWindow(hwnd) / 96.0f;
            ((MINMAXINFO*)lp)->ptMinTrackSize = { (LONG)(600 * s), (LONG)(360 * s) };
            return 0;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT:
        {
            HDC dc = (HDC)wp;
            bool edit = (HWND)lp == s_edit;
            SetTextColor(dc, (HWND)lp == s_info ? kTextDim : kText);
            SetBkColor(dc, edit ? kBgEdit : kBg);
            return (LRESULT)(edit ? s_bgEdit : s_bg);
        }
        case WM_ERASEBKGND:
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            FillRect((HDC)wp, &rc, s_bg);
            return 1;
        }
        case WM_COMMAND:
            switch (LOWORD(wp))
            {
            case ID_COPY: CopyReport(hwnd); return 0;
            case ID_FOLDER:
            {
                std::wstring args = L"/select,\"" + s_reportPath + L"\"";
                ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
                return 0;
            }
            case ID_RESTART:
            {
                wchar_t exe[MAX_PATH * 2];
                GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
                ShellExecuteW(nullptr, L"open", exe, nullptr, nullptr, SW_SHOWNORMAL);
                DestroyWindow(hwnd);
                return 0;
            }
            case ID_CLOSE: DestroyWindow(hwnd); return 0;
            }
            break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    int ShowReport(const std::string& reportPath)
    {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        s_reportPath = platform::Widen(reportPath);
        for (wchar_t& c : s_reportPath) if (c == L'/') c = L'\\';
        std::string text;
        if (!platform::ReadText(reportPath, text)) text = "The report file could not be read: " + reportPath;
        // The edit control needs CRLF line breaks.
        std::wstring wt = platform::Widen(text);
        for (size_t i = 0; i < wt.size(); i++)
            if (wt[i] == L'\n' && (i == 0 || wt[i - 1] != L'\r')) { wt.insert(i, 1, L'\r'); i++; }
        s_reportText = wt;
        s_bg = CreateSolidBrush(kBg);
        s_bgEdit = CreateSolidBrush(kBgEdit);

        HINSTANCE inst = GetModuleHandleW(nullptr);
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.lpfnWndProc = ReportProc;
        wc.hInstance = inst;
        wc.hIcon = wc.hIconSm = LoadIconW(inst, MAKEINTRESOURCEW(1));
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"FimguiCrashReport";
        RegisterClassExW(&wc);

        HMONITOR mon = MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi = { sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        UINT dpi = GetDpiForSystem();
        int w = MulDiv(780, dpi, 96), h = MulDiv(540, dpi, 96);
        int x = (mi.rcWork.left + mi.rcWork.right - w) / 2, y = (mi.rcWork.top + mi.rcWork.bottom - h) / 2;
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Fimgui - crash report", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, inst, nullptr);
        if (!hwnd) return 1;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        ShowWindow(hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        {
            if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { DestroyWindow(hwnd); continue; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        return 0;
    }
}
