#include "gfx.h"
#include "document.h"
#include "../runtime/fimgui_fx.h"
#include "imgui_internal.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <unordered_map>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_WINDOWS_UTF8
#include "../third_party/stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../third_party/stb/stb_image_write.h"
#define NANOSVG_IMPLEMENTATION
#include "../third_party/nanosvg/src/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "../third_party/nanosvg/src/nanosvgrast.h"

namespace gfx
{
    static ID3D11Device* g_dev = nullptr;
    static ID3D11DeviceContext* g_ctx = nullptr;

    struct CacheEntry { ImTextureID id = 0; int w = 0, h = 0; int lastFrame = 0; };
    static std::unordered_map<std::string, CacheEntry> g_cache;
    static int g_frame = 0;

    struct GifEntry { std::vector<ImTextureID> frames; std::vector<int> delays; int total = 0; int w = 0, h = 0; int lastFrame = 0; };
    struct VideoEntry { int handle = -1; int lastFrame = 0; };
    static std::unordered_map<std::string, GifEntry> g_gifs;
    static std::unordered_map<std::string, VideoEntry> g_videos;

    static void FreeGif(GifEntry& g) { for (auto t : g.frames) DestroyTexture(t); g.frames.clear(); }

    void Init(ID3D11Device* dev, ID3D11DeviceContext* ctx) { g_dev = dev; g_ctx = ctx; }

    void Shutdown()
    {
        for (auto& [k, e] : g_cache) DestroyTexture(e.id);
        g_cache.clear();
        for (auto& [k, g] : g_gifs) FreeGif(g);
        g_gifs.clear();
        for (auto& [k, v] : g_videos) fim::fx::VideoClose(v.handle);
        g_videos.clear();
    }

    void NewFrame()
    {
        g_frame++;
        if (g_frame % 120 != 0) return;
        for (auto it = g_cache.begin(); it != g_cache.end();)
        {
            if (g_frame - it->second.lastFrame > 600) { DestroyTexture(it->second.id); it = g_cache.erase(it); }
            else ++it;
        }
        for (auto it = g_gifs.begin(); it != g_gifs.end();)
        {
            if (g_frame - it->second.lastFrame > 600) { FreeGif(it->second); it = g_gifs.erase(it); }
            else ++it;
        }
        // Stop decoding videos that are no longer on screen.
        for (auto it = g_videos.begin(); it != g_videos.end();)
        {
            if (g_frame - it->second.lastFrame > 240) { fim::fx::VideoClose(it->second.handle); it = g_videos.erase(it); }
            else ++it;
        }
    }

    ImTextureID CreateTexture(const unsigned char* rgba, int w, int h)
    {
        if (!g_dev || w <= 0 || h <= 0) return 0;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem = rgba; sub.SysMemPitch = w * 4;
        ID3D11Texture2D* tex = nullptr;
        if (FAILED(g_dev->CreateTexture2D(&desc, &sub, &tex))) return 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
        sd.Format = desc.Format; sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D; sd.Texture2D.MipLevels = 1;
        ID3D11ShaderResourceView* srv = nullptr;
        g_dev->CreateShaderResourceView(tex, &sd, &srv);
        tex->Release();
        return (ImTextureID)srv;
    }

    void DestroyTexture(ImTextureID id)
    {
        if (id) ((ID3D11ShaderResourceView*)id)->Release();
    }

    bool DecodeImage(const unsigned char* data, size_t size, std::vector<unsigned char>& rgba, int* w, int* h)
    {
        int n = 0;
        unsigned char* px = stbi_load_from_memory(data, (int)size, w, h, &n, 4);
        if (!px) return false;
        rgba.assign(px, px + (size_t)(*w) * (*h) * 4);
        stbi_image_free(px);
        return true;
    }

    static void WriteToVec(void* ctx, void* data, int size)
    {
        auto* v = (std::vector<unsigned char>*)ctx;
        v->insert(v->end(), (unsigned char*)data, (unsigned char*)data + size);
    }

    std::vector<unsigned char> EncodePNG(const unsigned char* rgba, int w, int h)
    {
        std::vector<unsigned char> out;
        stbi_write_png_to_func(WriteToVec, &out, w, h, 4, rgba, w * 4);
        return out;
    }

    bool SvgSize(const std::string& svg, float* w, float* h)
    {
        std::string copy = svg;
        NSVGimage* img = nsvgParse(copy.data(), "px", 96.0f);
        if (!img) return false;
        *w = img->width; *h = img->height;
        bool ok = img->shapes != nullptr || (img->width > 0 && img->height > 0);
        nsvgDelete(img);
        return ok;
    }

    bool RasterizeSVG(const std::string& svg, int w, int h, bool recolorWhite, std::vector<unsigned char>& rgba)
    {
        std::string copy = svg;
        NSVGimage* img = nsvgParse(copy.data(), "px", 96.0f);
        if (!img) return false;
        if (img->width <= 0 || img->height <= 0) { nsvgDelete(img); return false; }
        if (recolorWhite)
            for (NSVGshape* s = img->shapes; s; s = s->next)
            {
                if (s->fill.type != NSVG_PAINT_NONE) { s->fill.type = NSVG_PAINT_COLOR; s->fill.color = 0xFFFFFFFF; }
                if (s->stroke.type != NSVG_PAINT_NONE) { s->stroke.type = NSVG_PAINT_COLOR; s->stroke.color = 0xFFFFFFFF; }
            }
        rgba.assign((size_t)w * h * 4, 0);
        NSVGrasterizer* r = nsvgCreateRasterizer();
        float sx = w / img->width, sy = h / img->height;
        float s = std::fmin(sx, sy);
        float tx = (w - img->width * s) * 0.5f, ty = (h - img->height * s) * 0.5f;
        nsvgRasterize(r, img, tx, ty, s, rgba.data(), w, h, w * 4);
        nsvgDeleteRasterizer(r);
        nsvgDelete(img);
        return true;
    }

    static int Bucket(float px)
    {
        int v = (int)std::ceil(px);
        if (v < 8) v = 8;
        if (v > 2048) v = 2048;
        int step = v <= 64 ? 8 : v <= 256 ? 32 : 128;
        return (v + step - 1) / step * step;
    }

    TexInfo AssetTexture(const Asset& a, float pixelW, float pixelH, bool recolor)
    {
        if (a.kind == 2)
        {
            auto it = g_gifs.find(a.id);
            if (it == g_gifs.end())
            {
                GifEntry g;
                int* delays = nullptr;
                int w = 0, h = 0, frames = 0, comp = 0;
                unsigned char* px = stbi_load_gif_from_memory(a.data.data(), (int)a.data.size(), &delays, &w, &h, &frames, &comp, 4);
                if (px)
                {
                    for (int f = 0; f < frames; f++)
                    {
                        g.frames.push_back(CreateTexture(px + (size_t)f * w * h * 4, w, h));
                        int d = delays && delays[f] > 0 ? delays[f] : 100;
                        g.delays.push_back(d);
                        g.total += d;
                    }
                    g.w = w; g.h = h;
                    stbi_image_free(px);
                    if (delays) stbi_image_free(delays);
                }
                it = g_gifs.emplace(a.id, std::move(g)).first;
            }
            GifEntry& g = it->second;
            g.lastFrame = g_frame;
            if (g.frames.empty()) return {};
            int t = g.total > 0 ? (int)fmod(ImGui::GetTime() * 1000.0, (double)g.total) : 0;
            size_t i = 0;
            for (; i + 1 < g.frames.size(); i++) { t -= g.delays[i]; if (t < 0) break; }
            return { g.frames[i], g.w, g.h };
        }
        if (a.kind == 3)
        {
            auto it = g_videos.find(a.id);
            if (it == g_videos.end()) it = g_videos.emplace(a.id, VideoEntry{ fim::fx::VideoOpenMemory(a.data.data(), a.data.size()), g_frame }).first;
            it->second.lastFrame = g_frame;
            return { fim::fx::VideoFrame(it->second.handle), a.width, a.height };
        }
        std::string key = a.id;
        int bw = 0, bh = 0;
        if (a.kind == 1)
        {
            float sw = a.width > 0 ? (float)a.width : 24.0f, sh = a.height > 0 ? (float)a.height : 24.0f;
            // Rasterize at the aspect of the source, large enough to cover the requested box.
            float k = std::fmax(pixelW / sw, pixelH / sh);
            int target = Bucket(std::fmax(sw, sh) * k);
            float m = std::fmax(sw, sh);
            bw = std::max(1, (int)std::lround(target * sw / m));
            bh = std::max(1, (int)std::lround(target * sh / m));
            key += "|" + std::to_string(bw) + "x" + std::to_string(bh) + (recolor ? "|w" : "");
        }
        auto it = g_cache.find(key);
        if (it != g_cache.end()) { it->second.lastFrame = g_frame; return { it->second.id, it->second.w, it->second.h }; }

        CacheEntry e;
        std::vector<unsigned char> rgba;
        if (a.kind == 0)
        {
            if (DecodeImage(a.data.data(), a.data.size(), rgba, &e.w, &e.h)) e.id = CreateTexture(rgba.data(), e.w, e.h);
        }
        else
        {
            std::string svg(a.data.begin(), a.data.end());
            if (RasterizeSVG(svg, bw, bh, recolor, rgba)) { e.w = bw; e.h = bh; e.id = CreateTexture(rgba.data(), bw, bh); }
        }
        e.lastFrame = g_frame;
        g_cache[key] = e;
        return { e.id, e.w, e.h };
    }

    void InvalidateAsset(const std::string& id)
    {
        if (auto g = g_gifs.find(id); g != g_gifs.end()) { FreeGif(g->second); g_gifs.erase(g); }
        if (auto v = g_videos.find(id); v != g_videos.end()) { fim::fx::VideoClose(v->second.handle); g_videos.erase(v); }
        for (auto it = g_cache.begin(); it != g_cache.end();)
        {
            if (it->first == id || it->first.rfind(id + "|", 0) == 0) { DestroyTexture(it->second.id); it = g_cache.erase(it); }
            else ++it;
        }
    }

    static void DrawInto(ID3D11RenderTargetView* rtv, int w, int h, const std::function<void(ImDrawList*)>& draw, ImVec4 bg)
    {
        ImDrawList dl(ImGui::GetDrawListSharedData());
        dl._ResetForNewFrame();
        dl.PushClipRect(ImVec2(0, 0), ImVec2((float)w, (float)h));
        dl.PushTexture(ImGui::GetIO().Fonts->TexRef);
        draw(&dl);
        dl.PopTexture();
        dl.PopClipRect();
        ImDrawData dd;
        dd.Valid = true;
        dd.AddDrawList(&dl);
        dd.DisplayPos = ImVec2(0, 0);
        dd.DisplaySize = ImVec2((float)w, (float)h);
        dd.FramebufferScale = ImVec2(1, 1);
        dd.Textures = &ImGui::GetPlatformIO().Textures;
        const float clear[4] = { bg.x, bg.y, bg.z, bg.w };
        g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
        g_ctx->ClearRenderTargetView(rtv, clear);
        if (dd.TotalVtxCount > 0) ImGui_ImplDX11_RenderDrawData(&dd);
    }

    ImTextureID RenderToTexture(int w, int h, const std::function<void(ImDrawList*)>& draw, ImVec4 clear)
    {
        static ID3D11Texture2D* tex = nullptr;
        static ID3D11RenderTargetView* rtv = nullptr;
        static ID3D11ShaderResourceView* srv = nullptr;
        static int tw = 0, th = 0;
        if (!g_dev || w <= 0 || h <= 0 || w > 8192 || h > 8192) return 0;
        if (!tex || tw != w || th != h)
        {
            if (srv) srv->Release();
            if (rtv) rtv->Release();
            if (tex) tex->Release();
            srv = nullptr; rtv = nullptr; tex = nullptr;
            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(g_dev->CreateTexture2D(&desc, nullptr, &tex))) { tex = nullptr; return 0; }
            g_dev->CreateRenderTargetView(tex, nullptr, &rtv);
            g_dev->CreateShaderResourceView(tex, nullptr, &srv);
            tw = w; th = h;
        }
        DrawInto(rtv, w, h, draw, clear);
        return (ImTextureID)srv;
    }

    bool RenderToRGBA(int w, int h, const std::function<void(ImDrawList*)>& draw, std::vector<unsigned char>& out)
    {
        if (!g_dev || w <= 0 || h <= 0 || w > 8192 || h > 8192) return false;
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = w; desc.Height = h; desc.MipLevels = 1; desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
        ID3D11Texture2D* rt = nullptr;
        if (FAILED(g_dev->CreateTexture2D(&desc, nullptr, &rt))) return false;
        ID3D11RenderTargetView* rtv = nullptr;
        g_dev->CreateRenderTargetView(rt, nullptr, &rtv);
        desc.Usage = D3D11_USAGE_STAGING; desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ID3D11Texture2D* staging = nullptr;
        g_dev->CreateTexture2D(&desc, nullptr, &staging);

        ImDrawList dl(ImGui::GetDrawListSharedData());
        dl._ResetForNewFrame();
        dl.PushClipRect(ImVec2(0, 0), ImVec2((float)w, (float)h));
        dl.PushTexture(ImGui::GetIO().Fonts->TexRef);
        draw(&dl);
        dl.PopTexture();
        dl.PopClipRect();

        ImDrawData dd;
        dd.Valid = true;
        dd.AddDrawList(&dl);   // also resolves callback user-data pointers (blur / glass / blend effects)
        dd.DisplayPos = ImVec2(0, 0);
        dd.DisplaySize = ImVec2((float)w, (float)h);
        dd.FramebufferScale = ImVec2(1, 1);
        dd.Textures = &ImGui::GetPlatformIO().Textures;   // upload glyphs baked while drawing

        const float clear[4] = { 0, 0, 0, 0 };
        g_ctx->OMSetRenderTargets(1, &rtv, nullptr);
        g_ctx->ClearRenderTargetView(rtv, clear);
        if (dd.TotalVtxCount > 0) ImGui_ImplDX11_RenderDrawData(&dd);
        g_ctx->CopyResource(staging, rt);

        bool ok = false;
        D3D11_MAPPED_SUBRESOURCE map;
        if (SUCCEEDED(g_ctx->Map(staging, 0, D3D11_MAP_READ, 0, &map)))
        {
            out.resize((size_t)w * h * 4);
            for (int y = 0; y < h; y++)
            {
                const unsigned char* src = (const unsigned char*)map.pData + (size_t)y * map.RowPitch;
                unsigned char* dst = out.data() + (size_t)y * w * 4;
                for (int x = 0; x < w; x++)
                {
                    // Blending onto a transparent target produces premultiplied color - convert back to straight alpha.
                    unsigned a = src[x * 4 + 3];
                    for (int c = 0; c < 3; c++)
                        dst[x * 4 + c] = a ? (unsigned char)std::min(255u, (src[x * 4 + c] * 255u + a / 2) / a) : 0;
                    dst[x * 4 + 3] = (unsigned char)a;
                }
            }
            g_ctx->Unmap(staging, 0);
            ok = true;
        }
        staging->Release();
        rtv->Release();
        rt->Release();
        return ok;
    }
}
