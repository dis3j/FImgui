#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include "app.h"
#include "crash.h"
#include "fonts.h"
#include "gfx.h"
#include "theme.h"
#include "platform.h"
#include "../runtime/fimgui_fx.h"
#include <d3d11.h>
#include <algorithm>
#include <sstream>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <objbase.h>

HWND g_hwnd = nullptr;
static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_rtv = nullptr;
static UINT g_resizeW = 0, g_resizeH = 0;
static bool g_occluded = false;
static App g_appInstance;
static std::vector<std::string> g_droppedFiles;

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static void CreateRenderTarget()
{
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_rtv);
    back->Release();
}

static void CleanupRenderTarget()
{
    if (g_rtv) { g_rtv->Release(); g_rtv = nullptr; }
}

static bool CreateDevice(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };
    D3D_FEATURE_LEVEL got;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &sd, &g_swapChain, &g_device, &got, &g_context);
    if (hr != S_OK) return false;
    IDXGIFactory* factory = nullptr;
    if (SUCCEEDED(g_swapChain->GetParent(IID_PPV_ARGS(&factory)))) { factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER); factory->Release(); }
    CreateRenderTarget();
    return true;
}

static void CleanupDevice()
{
    CleanupRenderTarget();
    if (g_swapChain) g_swapChain->Release();
    if (g_context) g_context->Release();
    if (g_device) g_device->Release();
}

// Saves the back buffer as PNG (used by --screenshot for automated checks).
static void CaptureBackbuffer(const std::string& path)
{
    ID3D11Texture2D* back = nullptr;
    if (FAILED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) return;
    D3D11_TEXTURE2D_DESC desc;
    back->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&desc, nullptr, &staging)))
    {
        g_context->CopyResource(staging, back);
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(g_context->Map(staging, 0, D3D11_MAP_READ, 0, &map)))
        {
            std::vector<unsigned char> rgba((size_t)desc.Width * desc.Height * 4);
            for (UINT y = 0; y < desc.Height; y++)
            {
                memcpy(&rgba[(size_t)y * desc.Width * 4], (unsigned char*)map.pData + (size_t)y * map.RowPitch, desc.Width * 4);
                for (UINT x = 0; x < desc.Width; x++) rgba[((size_t)y * desc.Width + x) * 4 + 3] = 255;
            }
            g_context->Unmap(staging, 0);
            auto png = gfx::EncodePNG(rgba.data(), desc.Width, desc.Height);
            platform::WriteFile(path, png.data(), png.size());
        }
        staging->Release();
    }
    back->Release();
}

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam)) return true;
    switch (msg)
    {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) { g_resizeW = LOWORD(lParam); g_resizeH = HIWORD(lParam); }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DROPFILES:
    {
        HDROP drop = (HDROP)wParam;
        UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
        for (UINT i = 0; i < n; i++)
        {
            wchar_t buf[MAX_PATH];
            if (DragQueryFileW(drop, i, buf, MAX_PATH)) g_droppedFiles.push_back(platform::Narrow(buf));
        }
        DragFinish(drop);
        return 0;
    }
    case WM_NCCALCSIZE:
        // No system caption or frame: the client area covers the whole window, Fimgui draws its own title bar.
        if (wParam == TRUE)
        {
            if (IsZoomed(hWnd))
            {
                // A maximized window is larger than the monitor by the frame size; keep the content on screen.
                NCCALCSIZE_PARAMS* p = (NCCALCSIZE_PARAMS*)lParam;
                UINT dpiW = GetDpiForWindow(hWnd);
                int fx = GetSystemMetricsForDpi(SM_CXFRAME, dpiW) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpiW);
                int fy = GetSystemMetricsForDpi(SM_CYFRAME, dpiW) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpiW);
                p->rgrc[0].left += fx; p->rgrc[0].right -= fx;
                p->rgrc[0].top += fy; p->rgrc[0].bottom -= fy;
            }
            return 0;
        }
        break;
    case WM_NCHITTEST:
    {
        // Resize borders inside the client area (the title bar itself is handled by the app).
        if (IsZoomed(hWnd)) return HTCLIENT;
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        RECT rc;
        GetWindowRect(hWnd, &rc);
        int b = MulDiv(6, GetDpiForWindow(hWnd), 96);
        bool l = pt.x < rc.left + b, r = pt.x >= rc.right - b, t = pt.y < rc.top + b, bt = pt.y >= rc.bottom - b;
        if (t && l) return HTTOPLEFT;
        if (t && r) return HTTOPRIGHT;
        if (bt && l) return HTBOTTOMLEFT;
        if (bt && r) return HTBOTTOMRIGHT;
        if (l) return HTLEFT;
        if (r) return HTRIGHT;
        if (t) return HTTOP;
        if (bt) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize = { 900, 560 };
        return 0;
    case WM_CLOSE:
        if (g_appInstance.RequestClose()) DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int)
{
    {
        // --crash-report <file>: started by the crash handler of a crashed instance; shows the report only.
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::string report = argc >= 3 && wcscmp(argv[1], L"--crash-report") == 0 ? platform::Narrow(argv[2]) : std::string();
        LocalFree(argv);
        if (!report.empty()) return crash::ShowReport(report);
    }
    crash::Install();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    ImGui_ImplWin32_EnableDpiAwareness();
    float dpi = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{ 0, 0 }, MONITOR_DEFAULTTOPRIMARY));

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, hInst, LoadIcon(hInst, MAKEINTRESOURCE(1)), LoadCursor(nullptr, IDC_ARROW), nullptr, nullptr, L"FimguiWindow", LoadIcon(hInst, MAKEINTRESOURCE(1)) };
    RegisterClassExW(&wc);
    g_hwnd = CreateWindowW(wc.lpszClassName, L"Fimgui", WS_OVERLAPPEDWINDOW, 80, 60, (int)(1480 * dpi), (int)(900 * dpi), nullptr, nullptr, hInst, nullptr);
    if (!CreateDevice(g_hwnd)) { CleanupDevice(); UnregisterClassW(wc.lpszClassName, hInst); return 1; }
    DragAcceptFiles(g_hwnd, TRUE);
    {
        // Keep the DWM shadow and rounded corners without the system caption.
        MARGINS m = { 0, 0, 1, 0 };
        DwmExtendFrameIntoClientArea(g_hwnd, &m);
        SetWindowPos(g_hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    }
    ShowWindow(g_hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(g_hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigInputTextCursorBlink = true;

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    gfx::Init(g_device, g_context);
    fim::fx::InitDX11(g_device, g_context);
    theme::Apply(dpi);
    fonts::Init(platform::ExeDir() + "/resources");

    g_appInstance.Init();

    // Command line: [file.fimgui] --ops file.jsonl --select id --tab n --var name=value --preview --export dir --screenshot out.png --frames n
    std::string screenshot, exportDir, aiPrompt;
    float zoomArg = 0.0f;
    bool noEmbedFonts = false;
    bool forceHome = false;
    bool exportJava = false;
    std::string exportWeb;
    bool webPreview = false;
    struct Click { std::string id; int frame; float dy; };
    std::vector<Click> clicks;   // synthetic clicks for automated checks
    // Timelapse: --ops-stream applies operations one by one (like a streamed AI answer), --record saves frames.
    std::vector<json> streamOps;
    int streamEvery = 4, streamStart = 20;
    size_t streamPos = 0;
    int streamEndFrame = -1;
    std::string recordDir;
    int recordEvery = 2, recordTail = 150, recordIndex = 0;
    int captureFrame = 45;
    int crashTest = -1;   // --crash-test frame: null dereference at that frame (checks the crash report)
    {
        int argc = 0;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        std::vector<std::string> args;
        for (int i = 1; i < argc; i++) args.push_back(platform::Narrow(argv[i]));
        LocalFree(argv);
        for (size_t i = 0; i < args.size(); i++)
        {
            const std::string& a = args[i];
            auto next = [&]() { return i + 1 < args.size() ? args[++i] : std::string(); };
            if (a == "--ops")
            {
                std::string text;
                if (platform::ReadText(next(), text))
                {
                    size_t p = 0;
                    while (p < text.size())
                    {
                        size_t nl = text.find('\n', p);
                        std::string line = text.substr(p, nl == std::string::npos ? std::string::npos : nl - p);
                        json op = json::parse(line, nullptr, false);
                        std::string err;
                        if (!op.is_discarded() && op.is_object() && !g_appInstance.ApplyAiOp(op, &err)) OutputDebugStringA(("op failed: " + err + "\n").c_str());
                        if (nl == std::string::npos) break;
                        p = nl + 1;
                    }
                    g_appInstance.MarkDirty();
                }
            }
            else if (a == "--import")
            {
                // Places a media file at the given canvas position: --import file x y
                std::string f = next();
                float x = (float)std::atof(next().c_str()), y = (float)std::atof(next().c_str());
                g_appInstance.ImportImageFile(f, nullptr, ImVec2(x, y));
            }
            else if (a == "--select") g_appInstance.SetSelection({ next() });
            else if (a == "--tab") g_appInstance.rightTab = std::atoi(next().c_str());
            else if (a == "--tool")
            {
                // --tool move|frame|rect|ellipse|line|text|hand|pen: the active tool (screenshots of tool panels)
                static const char* names[] = { "move", "frame", "rect", "ellipse", "line", "text", "hand", "pen" };
                std::string t = next();
                for (int i = 0; i < 8; i++) if (t == names[i]) g_appInstance.tool = (Tool)i;
            }
            else if (a == "--var")
            {
                std::string kv = next();
                size_t eq = kv.find('=');
                if (eq != std::string::npos) g_appInstance.vars.num[kv.substr(0, eq)] = (float)std::atof(kv.substr(eq + 1).c_str());
            }
            else if (a == "--preview") g_appInstance.preview = true;
            else if (a == "--zoom") zoomArg = (float)std::atof(next().c_str());
            else if (a == "--pixel-preview") g_appInstance.pixelPreview = true;
            else if (a == "--click")
            {
                // --click node_id frame [dy]: click the node center (+dy design px) at the given frame
                Click c;
                c.id = next();
                c.frame = std::atoi(next().c_str());
                auto isNumber = [](const std::string& v) { char* e = nullptr; std::strtod(v.c_str(), &e); return !v.empty() && e && *e == 0; };
                c.dy = (i + 1 < args.size() && isNumber(args[i + 1])) ? (float)std::atof(next().c_str()) : 0.0f;
                clicks.push_back(c);
            }
            else if (a == "--export") exportDir = next();
            else if (a == "--workspace") { g_appInstance.workspace = next(); g_appInstance.persistSettings = false; }
            else if (a == "--home") forceHome = true;
            else if (a == "--check-updates") g_appInstance.CheckForUpdates();
            else if (a == "--ops-stream")
            {
                std::string text;
                if (platform::ReadText(next(), text))
                {
                    std::stringstream ss(text);
                    std::string line;
                    while (std::getline(ss, line))
                    {
                        json op = json::parse(line, nullptr, false);
                        if (!op.is_discarded() && op.is_object()) streamOps.push_back(op);
                    }
                }
                auto isNumber = [](const std::string& v) { char* e = nullptr; std::strtod(v.c_str(), &e); return !v.empty() && e && *e == 0; };
                if (i + 1 < args.size() && isNumber(args[i + 1])) streamEvery = std::max(1, std::atoi(next().c_str()));
            }
            else if (a == "--record")
            {
                recordDir = next();
                auto isNumber = [](const std::string& v) { char* e = nullptr; std::strtod(v.c_str(), &e); return !v.empty() && e && *e == 0; };
                if (i + 1 < args.size() && isNumber(args[i + 1])) recordEvery = std::max(1, std::atoi(next().c_str()));
                if (i + 1 < args.size() && isNumber(args[i + 1])) recordTail = std::max(0, std::atoi(next().c_str()));
                CreateDirectoryW(platform::Widen(recordDir).c_str(), nullptr);
            }
            else if (a == "--export-web") { exportWeb = next(); g_appInstance.persistSettings = false; }
            else if (a == "--web-preview") webPreview = true;
            else if (a == "--export-java")
            {
                g_appInstance.javaOpt.outDir = next();
                if (i + 1 < args.size() && (args[i + 1] == "fabric" || args[i + 1] == "neoforge")) g_appInstance.javaOpt.loader = next() == "neoforge" ? 1 : 0;
                g_appInstance.persistSettings = false;
                exportJava = true;
            }
            else if (a == "--timeline") { g_appInstance.timelineOpen = true; g_appInstance.timelineTime = std::atof(next().c_str()); }
            else if (a == "--ui-scale") { g_appInstance.pendingUiScale = (float)std::atof(next().c_str()); g_appInstance.persistSettings = false; }
            else if (a == "--save") g_appInstance.deferred.push_back([] { g_appInstance.SaveDocument(false); });
            else if (a == "--project")
            {
                // Open <workspace>/<name>, creating the project when it doesn't exist yet: --project name [imgui|minecraft|web]
                std::string name = next(), err;
                int type = Project_ImGui;
                if (i + 1 < args.size() && (args[i + 1] == "imgui" || args[i + 1] == "minecraft" || args[i + 1] == "web")) type = ProjectTypeFromKey(next());
                std::string path = g_appInstance.WorkspaceDir() + "/" + name + "/" + ProjectFileName(name);
                if (platform::ReadText(path, err)) g_appInstance.OpenDocument(path);
                else if (!g_appInstance.CreateProject(name, false, &err, type)) OutputDebugStringA(err.c_str());
            }
            else if (a == "--no-embed-fonts") noEmbedFonts = true;
            else if (a == "--crash-test") crashTest = std::atoi(next().c_str());
            else if (a == "--screenshot") screenshot = next();
            else if (a == "--frames") captureFrame = std::atoi(next().c_str());
            else if (a == "--code") g_appInstance.codeLang = std::atoi(next().c_str());
            else if (a == "--ai") aiPrompt = next();
            else if (a == "--ai-provider")
            {
                std::string p = next();
                g_appInstance.ai.provider = (p == "openai" || p == "openai-compatible") ? "openai" : "anthropic";
                g_appInstance.persistSettings = false;
            }
            else if (a == "--ai-base")
            {
                std::string v = next();
                if (g_appInstance.ai.IsOpenAI()) g_appInstance.ai.openaiBaseUrl = v;
                else g_appInstance.ai.baseUrl = v;
                g_appInstance.persistSettings = false;
            }
            else if (a == "--ai-key")
            {
                std::string v = next();
                if (g_appInstance.ai.IsOpenAI()) g_appInstance.ai.openaiApiKey = v;
                else g_appInstance.ai.apiKey = v;
                g_appInstance.persistSettings = false;
            }
            else if (a == "--mcp-port")
            {
                g_appInstance.mcpPort = std::atoi(next().c_str());
                g_appInstance.mcpEnabled = true;
                g_appInstance.persistSettings = false;
                g_appInstance.StartMcp();
                // the default port may have been busy at startup; the override worked, so drop that error toast
                if (g_appInstance.mcpError.empty()) g_appInstance.toast.clear();
            }
            else if (!a.empty() && a[0] != '-') g_appInstance.OpenDocument(a);
        }
        if (!args.empty()) g_appInstance.showHome = forceHome;
        // Automation flags (--screenshot, --ops, ...) skip the update check; opening a file by double click does not.
        if (std::none_of(args.begin(), args.end(), [](const std::string& a) { return a.rfind("--", 0) == 0; })) g_appInstance.StartupChecks();
        if (!g_appInstance.doc.roots.empty()) g_appInstance.fitPending = true;
        if (zoomArg > 0)
            g_appInstance.deferred.push_back([zoomArg] {
                // Zoom around the selection (or the canvas center) - used for screenshots.
                if (!g_appInstance.selection.empty()) g_appInstance.ZoomToFit(true);
                g_appInstance.SetZoom(zoomArg, ImVec2((g_appInstance.canvasMin.x + g_appInstance.canvasMax.x) * 0.5f, (g_appInstance.canvasMin.y + g_appInstance.canvasMax.y) * 0.5f));
            });
    }

    bool done = false;
    int frame = 0;
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
        if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) { g_appInstance.PumpMcp(); Sleep(10); continue; }
        g_occluded = false;
        if (g_resizeW && g_resizeH)
        {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }

        if (streamPos < streamOps.size() && frame >= streamStart && (frame - streamStart) % streamEvery == 0)
        {
            const json& op = streamOps[streamPos++];
            std::string err;
            g_appInstance.ApplyAiOp(op, &err);
            g_appInstance.MarkDirty();
            if (op.contains("node") && op["node"].is_object() && op["node"].contains("id") && op["node"]["id"].is_string())
                g_appInstance.Select(op["node"]["id"].get<std::string>());
            if (streamPos == 1) g_appInstance.deferred.push_back([] { g_appInstance.ZoomToFit(false); });
            if (streamPos == streamOps.size())
            {
                // done: deselect and play the menu with its animations
                streamEndFrame = frame;
                g_appInstance.deferred.push_back([] { g_appInstance.selection.clear(); g_appInstance.vars = VarStore(); g_appInstance.ZoomToFit(false); g_appInstance.preview = true; g_appInstance.animClocks.clear(); });
            }
        }
        fonts::ProcessPending();
        gfx::NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        for (const Click& c : clicks)
        {
            if (frame < c.frame || frame > c.frame + 3) continue;
            ImVec2 p;
            if (c.id.size() > 1 && c.id[0] == '@')
                sscanf(c.id.c_str() + 1, "%f,%f", &p.x, &p.y);   // --click @x,y: screen coordinates (title bar, panels)
            else
            {
                Node* n = g_appInstance.doc.Find(c.id);
                if (!n) continue;
                ImVec2 wa, wb;
                NodeWorldRect(n, &g_appInstance.vars, &wa, &wb);
                p = g_appInstance.WorldToScreen(ImVec2((wa.x + wb.x) * 0.5f, (wa.y + wb.y) * 0.5f + c.dy));
            }
            ImGuiIO& cio = ImGui::GetIO();
            cio.AddMousePosEvent(p.x, p.y);
            if (frame == c.frame + 1) cio.AddMouseButtonEvent(0, true);
            if (frame == c.frame + 3) cio.AddMouseButtonEvent(0, false);
        }
        ImGui::NewFrame();

        if (!g_droppedFiles.empty())
        {
            auto files = std::move(g_droppedFiles);
            g_droppedFiles.clear();
            g_appInstance.OnDropFiles(files);
        }
        if (frame == 3 && !aiPrompt.empty())
        {
            snprintf(g_appInstance.aiInput, sizeof(g_appInstance.aiInput), "%s", aiPrompt.c_str());
            g_appInstance.rightTab = Tab_AI;
            g_appInstance.AiSend();
        }
        g_appInstance.Frame();
        if (frame == crashTest) *(volatile int*)nullptr = 0;

        ImGui::Render();
        const float clear[4] = { 0.12f, 0.12f, 0.12f, 1.0f };
        g_context->OMSetRenderTargets(1, &g_rtv, nullptr);
        g_context->ClearRenderTargetView(g_rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_appInstance.RunDeferred();
        frame++;
        if (frame == 5 && !exportDir.empty())
        {
            ExportOptions opt = g_appInstance.exportOpt;
            opt.outDir = exportDir;
            if (noEmbedFonts) opt.embedFonts = false;
            ExportResult r = ExportProject(g_appInstance.doc, opt, platform::ExeDir() + "/runtime");
            platform::WriteText(exportDir + "/export_log.txt", r.message + "\n");
            if (screenshot.empty()) done = true;
        }
        if (frame == 5 && !exportWeb.empty())
        {
            g_appInstance.webOpt.outDir = exportWeb;
            ExportResult r = g_appInstance.RunWebExport();
            std::string log = r.message + "\n";
            for (auto& f : r.files) log += f + "\n";
            platform::WriteText(exportWeb + "/export_log.txt", log);
            if (screenshot.empty() && !webPreview) done = true;
        }
        if (frame == 5 && webPreview) g_appInstance.OpenWebPreview(g_appInstance.WebCurrentPage(), false);   // automated checks: the server only
        if (frame == 5 && exportJava)
        {
            ExportResult r = g_appInstance.RunJavaExport();
            std::string log = r.message + "\n";
            for (auto& f : r.files) log += f + "\n";
            platform::WriteText(g_appInstance.javaOpt.outDir + "/export_log.txt", log);
            if (screenshot.empty()) done = true;
        }
        if (!screenshot.empty() && frame == captureFrame) { CaptureBackbuffer(screenshot); done = true; }
        if (!recordDir.empty() && frame >= streamStart - 2 && frame % recordEvery == 0)
        {
            char name[32];
            snprintf(name, sizeof(name), "/frame_%05d.png", recordIndex++);
            CaptureBackbuffer(recordDir + name);
            if (streamEndFrame >= 0 && frame >= streamEndFrame + recordTail) done = true;
        }
        HRESULT hr = g_swapChain->Present(1, 0);
        g_occluded = hr == DXGI_STATUS_OCCLUDED;
    }

    g_appInstance.Shutdown();
    gfx::Shutdown();
    fim::fx::Shutdown();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDevice();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, hInst);
    CoUninitialize();
    return 0;
}
