// fimgui_fx_dx11.cpp - DirectX 11 implementation of fimgui_fx.h.
// Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
// Link: d3d11, d3dcompiler, mfplat, mfreadwrite, mfuuid, shlwapi (pragmas below handle MSVC).
#ifndef NOMINMAX
#define NOMINMAX   // keep std::min/std::max usable next to <windows.h>
#endif
#include "fimgui_fx.h"
#include "fimgui_runtime.h"
#include "imgui_internal.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <shlwapi.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "d3dcompiler")
#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "shlwapi")

namespace fim
{
    namespace fx
    {
        template <typename T> static void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

        static ID3D11Device* g_dev = nullptr;
        static ID3D11DeviceContext* g_ctx = nullptr;
        static ID3D11VertexShader* g_vsFull = nullptr;
        static ID3D11PixelShader* g_psBlur = nullptr;
        static ID3D11PixelShader* g_psComposite = nullptr;
        static ID3D11PixelShader* g_psBlend[Blend_Count] = {};
        static ID3D11BlendState* g_blend[Blend_Count] = {};
        static ID3D11Buffer* g_cbBlur = nullptr;
        static ID3D11Buffer* g_cbComposite = nullptr;
        static ID3D11SamplerState* g_sampClamp = nullptr;
        static ID3D11SamplerState* g_sampWrap = nullptr;
        static ID3D11RasterizerState* g_rsScissor = nullptr;
        static bool g_mfStarted = false;
        static int VideoCount();

        // ------------------------------------------------------------ shaders

        static const char* kCommon = R"(
struct VO { float4 pos : SV_POSITION; float4 col : COLOR0; float2 uv : TEXCOORD0; };
)";

        static const char* kVsFull = R"(
VO main(uint id : SV_VertexID)
{
    VO o;
    float2 uv = float2((id << 1) & 2, id & 2);
    o.uv = uv;
    o.pos = float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
    o.col = float4(1, 1, 1, 1);
    return o;
}
)";

        static const char* kPsBlur = R"(
cbuffer B : register(b0) { float2 dir; float sigma; float pad; };
Texture2D src : register(t0);
SamplerState s0 : register(s0);
float4 main(VO i) : SV_Target
{
    float step = max(1.0, sigma * 3.0 / 7.0);
    float4 acc = 0;
    float wsum = 0;
    [unroll] for (int k = -7; k <= 7; k++)
    {
        float x = k * step;
        float w = exp(-x * x / (2.0 * sigma * sigma));
        acc += src.Sample(s0, i.uv + dir * x) * w;
        wsum += w;
    }
    return acc / wsum;
}
)";

        static const char* kPsComposite = R"(
cbuffer C : register(b0)
{
    float4 rect;    // x0 y0 x1 y1 (px)
    float4 radii;   // tl tr br bl
    float4 tint;
    float4 glass;   // refraction, thickness, highlight, dispersion
    float4 misc;    // light dir xy, saturation, mode
    float4 misc2;   // 1/w, 1/h, shape (1 = ellipse), alpha
};
Texture2D tex0 : register(t0);
Texture2D backdrop : register(t1);
SamplerState s0 : register(s0);

float sdShape(float2 p, float2 hb)
{
    if (misc2.z > 0.5)
    {
        float k = length(p / hb);
        return (k - 1.0) * min(hb.x, hb.y);
    }
    float r = p.x < 0 ? (p.y < 0 ? radii.x : radii.w) : (p.y < 0 ? radii.y : radii.z);
    float2 q = abs(p) - hb + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

float4 main(VO i) : SV_Target
{
    float2 c = (rect.xy + rect.zw) * 0.5;
    float2 hb = max((rect.zw - rect.xy) * 0.5, 0.5);
    float2 p = i.pos.xy - c;
    float d = sdShape(p, hb);
    float a = saturate(0.5 - d);
    if (a <= 0.0) discard;
    float2 inv = misc2.xy;
    float3 col;
    if (misc.w > 0.5)
    {
        float thick = max(glass.y, 1.0);
        float e = saturate(1.0 + d / thick);                 // 0 inside the flat area, 1 at the rim
        float2 g = float2(sdShape(p + float2(1, 0), hb) - sdShape(p - float2(1, 0), hb),
                          sdShape(p + float2(0, 1), hb) - sdShape(p - float2(0, 1), hb));
        g = g / max(length(g), 1e-4);
        float bend = glass.x * thick * pow(e, 2.2);
        float2 off = -g * bend;                               // lens: pull the background in from the edge
        float disp = glass.w * 0.35;
        col.r = backdrop.Sample(s0, (i.pos.xy + off * (1.0 + disp)) * inv).r;
        col.g = backdrop.Sample(s0, (i.pos.xy + off) * inv).g;
        col.b = backdrop.Sample(s0, (i.pos.xy + off * (1.0 - disp)) * inv).b;
        float2 L = misc.xy;
        float rim = pow(e, 5.0);
        float lit = saturate(dot(g, -L));
        float shade = saturate(dot(g, L));
        col += glass.z * rim * (0.18 + 0.85 * lit);
        col -= glass.z * 0.22 * rim * shade;
        col += glass.z * 0.45 * saturate(1.0 - abs(d + 0.8)) * (0.35 + 0.65 * lit);   // thin bright edge
        col += glass.z * 0.05 * (1.0 - e);                                             // faint inner sheen
    }
    else
    {
        col = backdrop.Sample(s0, i.pos.xy * inv).rgb;
    }
    float l = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(l.xxx, col, misc.z);
    col = lerp(col, tint.rgb, tint.a);
    return float4(saturate(col), a * misc2.w);
}
)";

        static const char* kPsBlend = R"(
Texture2D tex0 : register(t0);
SamplerState s0 : register(s0);
float4 main(VO i) : SV_Target
{
    float4 t = tex0.Sample(s0, i.uv) * i.col;
#if MODE == 0
    return t;
#elif MODE == 2 || MODE == 4
    return float4(lerp(float3(1, 1, 1), t.rgb, t.a), 0);
#elif MODE == 7
    return float4((1 - t.rgb) * t.a, 0);
#else
    return float4(t.rgb * t.a, 0);
#endif
}
)";

        static ID3DBlob* Compile(const char* body, const char* target, const char* mode = nullptr)
        {
            std::string src = std::string(kCommon) + body;
            D3D_SHADER_MACRO macros[2] = { { "MODE", mode }, { nullptr, nullptr } };
            ID3DBlob* blob = nullptr;
            ID3DBlob* err = nullptr;
            HRESULT hr = D3DCompile(src.c_str(), src.size(), nullptr, mode ? macros : nullptr, nullptr, "main", target, 0, 0, &blob, &err);
            if (err) err->Release();
            return SUCCEEDED(hr) ? blob : nullptr;
        }

        static ID3D11PixelShader* MakePS(const char* body, const char* mode = nullptr)
        {
            ID3DBlob* b = Compile(body, "ps_4_0", mode);
            if (!b) return nullptr;
            ID3D11PixelShader* ps = nullptr;
            g_dev->CreatePixelShader(b->GetBufferPointer(), b->GetBufferSize(), nullptr, &ps);
            b->Release();
            return ps;
        }

        static ID3D11Buffer* MakeCB(UINT size)
        {
            D3D11_BUFFER_DESC d = {};
            d.ByteWidth = (size + 15) & ~15u;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            ID3D11Buffer* b = nullptr;
            g_dev->CreateBuffer(&d, nullptr, &b);
            return b;
        }

        bool InitDX11(ID3D11Device* device, ID3D11DeviceContext* context)
        {
            g_dev = device;
            g_ctx = context;
            ID3DBlob* vs = Compile(kVsFull, "vs_4_0");
            if (!vs) return false;
            g_dev->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(), nullptr, &g_vsFull);
            vs->Release();
            g_psBlur = MakePS(kPsBlur);
            g_psComposite = MakePS(kPsComposite);
            const char* modes[Blend_Count] = { "0", "1", "2", "3", "4", "5", "6", "7" };
            for (int i = 0; i < Blend_Count; i++) g_psBlend[i] = MakePS(kPsBlend, modes[i]);
            g_cbBlur = MakeCB(16);
            g_cbComposite = MakeCB(96);

            for (int i = 0; i < Blend_Count; i++)
            {
                D3D11_BLEND_DESC bd = {};
                auto& rt = bd.RenderTarget[0];
                rt.BlendEnable = TRUE;
                rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
                rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
                rt.SrcBlendAlpha = D3D11_BLEND_ZERO;
                rt.DestBlendAlpha = D3D11_BLEND_ONE;
                switch (i)
                {
                case Blend_Normal: rt.SrcBlend = D3D11_BLEND_SRC_ALPHA; rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; rt.SrcBlendAlpha = D3D11_BLEND_ONE; rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA; break;
                case Blend_Add: rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; break;
                case Blend_Multiply: rt.SrcBlend = D3D11_BLEND_DEST_COLOR; rt.DestBlend = D3D11_BLEND_ZERO; break;
                case Blend_Screen: rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_INV_SRC_COLOR; break;
                // min / max ignore the factors: the shader fades the source toward the neutral color instead
                case Blend_Darken: rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; rt.BlendOp = D3D11_BLEND_OP_MIN; break;
                case Blend_Lighten: rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; rt.BlendOp = D3D11_BLEND_OP_MAX; break;
                // s + d - 2sd
                case Blend_Exclusion: rt.SrcBlend = D3D11_BLEND_INV_DEST_COLOR; rt.DestBlend = D3D11_BLEND_INV_SRC_COLOR; break;
                // d - (1 - s): the shader outputs (1 - s) * a
                case Blend_PlusDarker: rt.SrcBlend = D3D11_BLEND_ONE; rt.DestBlend = D3D11_BLEND_ONE; rt.BlendOp = D3D11_BLEND_OP_REV_SUBTRACT; break;
                }
                g_dev->CreateBlendState(&bd, &g_blend[i]);
            }
            D3D11_SAMPLER_DESC sd = {};
            sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sd.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
            g_dev->CreateSamplerState(&sd, &g_sampClamp);
            sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
            g_dev->CreateSamplerState(&sd, &g_sampWrap);
            D3D11_RASTERIZER_DESC rd = {};
            rd.FillMode = D3D11_FILL_SOLID;
            rd.CullMode = D3D11_CULL_NONE;
            rd.ScissorEnable = TRUE;
            rd.DepthClipEnable = TRUE;
            g_dev->CreateRasterizerState(&rd, &g_rsScissor);

            if (!g_mfStarted) g_mfStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
            return Available();
        }

        bool Available() { return g_dev && g_vsFull && g_psBlur && g_psComposite && g_psBlend[Blend_Count - 1]; }

        // ------------------------------------------------------------ render targets

        struct Target
        {
            int w = 0, h = 0;
            ID3D11Texture2D* tex = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            ID3D11RenderTargetView* rtv = nullptr;
            void Release() { SafeRelease(tex); SafeRelease(srv); SafeRelease(rtv); w = h = 0; }
        };
        static Target g_scene;
        static DXGI_FORMAT g_sceneFormat = DXGI_FORMAT_UNKNOWN;
        static Target g_blurA[3], g_blurB[3];   // downsample 1, 2, 4

        static bool EnsureTarget(Target& t, int w, int h, DXGI_FORMAT fmt, bool rtv)
        {
            if (t.tex && t.w == w && t.h == h) return true;
            t.Release();
            D3D11_TEXTURE2D_DESC d = {};
            d.Width = w; d.Height = h; d.MipLevels = 1; d.ArraySize = 1; d.Format = fmt; d.SampleDesc.Count = 1;
            d.Usage = D3D11_USAGE_DEFAULT;
            d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (rtv ? D3D11_BIND_RENDER_TARGET : 0);
            if (FAILED(g_dev->CreateTexture2D(&d, nullptr, &t.tex))) return false;
            g_dev->CreateShaderResourceView(t.tex, nullptr, &t.srv);
            if (rtv) g_dev->CreateRenderTargetView(t.tex, nullptr, &t.rtv);
            t.w = w; t.h = h;
            return true;
        }

        void Shutdown()
        {
            for (int i = 0; i < VideoCount(); i++) VideoClose(i);
            g_scene.Release();
            for (int i = 0; i < 3; i++) { g_blurA[i].Release(); g_blurB[i].Release(); }
            SafeRelease(g_vsFull); SafeRelease(g_psBlur); SafeRelease(g_psComposite);
            for (int i = 0; i < Blend_Count; i++) { SafeRelease(g_psBlend[i]); SafeRelease(g_blend[i]); }
            SafeRelease(g_cbBlur); SafeRelease(g_cbComposite);
            SafeRelease(g_sampClamp); SafeRelease(g_sampWrap); SafeRelease(g_rsScissor);
            g_dev = nullptr;
            g_ctx = nullptr;
        }

        // Saves/restores the pipeline state touched by the blur passes.
        struct StateBackup
        {
            UINT numViewports = 1, numScissors = 1;
            D3D11_VIEWPORT viewport;
            D3D11_RECT scissor;
            ID3D11RasterizerState* rs = nullptr;
            ID3D11BlendState* blend = nullptr;
            FLOAT blendFactor[4];
            UINT sampleMask = 0;
            ID3D11PixelShader* ps = nullptr;
            ID3D11VertexShader* vs = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            ID3D11SamplerState* sampler = nullptr;
            ID3D11Buffer* psCB = nullptr;
            ID3D11InputLayout* layout = nullptr;
            D3D11_PRIMITIVE_TOPOLOGY topology;
            ID3D11Buffer* vb = nullptr;
            UINT vbStride = 0, vbOffset = 0;
            ID3D11Buffer* ib = nullptr;
            DXGI_FORMAT ibFormat;
            UINT ibOffset = 0;

            void Save(ID3D11DeviceContext* c)
            {
                c->RSGetViewports(&numViewports, &viewport);
                c->RSGetScissorRects(&numScissors, &scissor);
                c->RSGetState(&rs);
                c->OMGetBlendState(&blend, blendFactor, &sampleMask);
                c->PSGetShader(&ps, nullptr, nullptr);
                c->VSGetShader(&vs, nullptr, nullptr);
                c->PSGetShaderResources(0, 1, &srv);
                c->PSGetSamplers(0, 1, &sampler);
                c->PSGetConstantBuffers(0, 1, &psCB);
                c->IAGetInputLayout(&layout);
                c->IAGetPrimitiveTopology(&topology);
                c->IAGetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
                c->IAGetIndexBuffer(&ib, &ibFormat, &ibOffset);
            }
            void Restore(ID3D11DeviceContext* c)
            {
                c->RSSetViewports(numViewports, &viewport);
                c->RSSetScissorRects(numScissors, &scissor);
                c->RSSetState(rs);
                c->OMSetBlendState(blend, blendFactor, sampleMask);
                c->PSSetShader(ps, nullptr, 0);
                c->VSSetShader(vs, nullptr, 0);
                c->PSSetShaderResources(0, 1, &srv);
                c->PSSetSamplers(0, 1, &sampler);
                c->PSSetConstantBuffers(0, 1, &psCB);
                c->IASetInputLayout(layout);
                c->IASetPrimitiveTopology(topology);
                c->IASetVertexBuffers(0, 1, &vb, &vbStride, &vbOffset);
                c->IASetIndexBuffer(ib, ibFormat, ibOffset);
                SafeRelease(rs); SafeRelease(blend); SafeRelease(ps); SafeRelease(vs); SafeRelease(srv); SafeRelease(sampler);
                SafeRelease(psCB); SafeRelease(layout); SafeRelease(vb); SafeRelease(ib);
            }
        };

        // ------------------------------------------------------------ backdrop

        struct BackdropCmd
        {
            Backdrop p;
            float rect[4];
            float radii[4];
            float alpha;
            int ellipse;
        };

        struct CompositeCB { float rect[4], radii[4], tint[4], glass[4], misc[4], misc2[4]; };
        struct BlurCB { float dir[2]; float sigma; float pad; };

        static void BackdropCallback(const ImDrawList*, const ImDrawCmd* cmd)
        {
            const BackdropCmd& c = *(const BackdropCmd*)cmd->UserCallbackData;
            ID3D11DeviceContext* ctx = g_ctx;
            ID3D11RenderTargetView* rtv = nullptr;
            ID3D11DepthStencilView* dsv = nullptr;
            ctx->OMGetRenderTargets(1, &rtv, &dsv);
            if (!rtv) { SafeRelease(dsv); return; }
            ID3D11Resource* res = nullptr;
            rtv->GetResource(&res);
            ID3D11Texture2D* rt = nullptr;
            res->QueryInterface(IID_PPV_ARGS(&rt));
            SafeRelease(res);
            if (!rt) { SafeRelease(rtv); SafeRelease(dsv); return; }
            D3D11_TEXTURE2D_DESC rd;
            rt->GetDesc(&rd);
            int W = (int)rd.Width, H = (int)rd.Height;

            if (g_sceneFormat != rd.Format) { g_scene.Release(); g_sceneFormat = rd.Format; }
            bool ok = EnsureTarget(g_scene, W, H, rd.Format, false) && rd.SampleDesc.Count == 1;
            ID3D11ShaderResourceView* result = g_scene.srv;

            float blur = c.p.mode == Backdrop_Glass ? c.p.blur : c.p.blur;
            float sigma = blur * 0.5f;
            float extra = c.p.mode == Backdrop_Glass ? c.p.thickness * c.p.refraction + 8.0f : 2.0f;
            float margin = sigma * 3.0f + extra + 4.0f;
            int x0 = (int)ImClamp(floorf(c.rect[0] - margin), 0.0f, (float)W), y0 = (int)ImClamp(floorf(c.rect[1] - margin), 0.0f, (float)H);
            int x1 = (int)ImClamp(ceilf(c.rect[2] + margin), 0.0f, (float)W), y1 = (int)ImClamp(ceilf(c.rect[3] + margin), 0.0f, (float)H);
            if (ok && x1 > x0 && y1 > y0)
            {
                StateBackup backup;
                backup.Save(ctx);
                ID3D11ShaderResourceView* nullSrv[2] = { nullptr, nullptr };
                ctx->PSSetShaderResources(0, 2, nullSrv);

                D3D11_BOX box = { (UINT)x0, (UINT)y0, 0, (UINT)x1, (UINT)y1, 1 };
                ctx->CopySubresourceRegion(g_scene.tex, 0, x0, y0, 0, rt, 0, &box);

                if (sigma > 0.5f)
                {
                    int di = sigma > 12.0f ? 2 : sigma > 4.0f ? 1 : 0;
                    int d = 1 << di;
                    int tw = (W + d - 1) / d, th = (H + d - 1) / d;
                    if (EnsureTarget(g_blurA[di], tw, th, DXGI_FORMAT_R8G8B8A8_UNORM, true) && EnsureTarget(g_blurB[di], tw, th, DXGI_FORMAT_R8G8B8A8_UNORM, true))
                    {
                        D3D11_VIEWPORT vp = { 0, 0, (float)tw, (float)th, 0, 1 };
                        D3D11_RECT sc = { ImMax(0, x0 / d - 1), ImMax(0, y0 / d - 1), ImMin(tw, x1 / d + 2), ImMin(th, y1 / d + 2) };
                        ctx->RSSetViewports(1, &vp);
                        ctx->RSSetScissorRects(1, &sc);
                        ctx->RSSetState(g_rsScissor);
                        ctx->OMSetBlendState(nullptr, nullptr, 0xffffffff);
                        ctx->IASetInputLayout(nullptr);
                        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
                        ID3D11Buffer* nullVb = nullptr;
                        UINT zero = 0;
                        ctx->IASetVertexBuffers(0, 1, &nullVb, &zero, &zero);
                        ctx->VSSetShader(g_vsFull, nullptr, 0);
                        ctx->PSSetShader(g_psBlur, nullptr, 0);
                        ctx->PSSetSamplers(0, 1, &g_sampClamp);
                        ctx->PSSetConstantBuffers(0, 1, &g_cbBlur);

                        BlurCB cb = { { 1.0f / W, 0.0f }, sigma, 0 };
                        ctx->UpdateSubresource(g_cbBlur, 0, nullptr, &cb, 0, 0);
                        ctx->OMSetRenderTargets(1, &g_blurA[di].rtv, nullptr);
                        ctx->PSSetShaderResources(0, 1, &g_scene.srv);
                        ctx->Draw(3, 0);

                        cb = { { 0.0f, 1.0f / H }, sigma, 0 };
                        ctx->UpdateSubresource(g_cbBlur, 0, nullptr, &cb, 0, 0);
                        ctx->PSSetShaderResources(0, 2, nullSrv);
                        ctx->OMSetRenderTargets(1, &g_blurB[di].rtv, nullptr);
                        ctx->PSSetShaderResources(0, 1, &g_blurA[di].srv);
                        ctx->Draw(3, 0);
                        ctx->PSSetShaderResources(0, 2, nullSrv);
                        result = g_blurB[di].srv;
                    }
                }
                ctx->OMSetRenderTargets(1, &rtv, dsv);
                backup.Restore(ctx);

                CompositeCB cc = {};
                memcpy(cc.rect, c.rect, sizeof(cc.rect));
                memcpy(cc.radii, c.radii, sizeof(cc.radii));
                ImVec4 t = ImGui::ColorConvertU32ToFloat4(c.p.tint);
                cc.tint[0] = t.x; cc.tint[1] = t.y; cc.tint[2] = t.z; cc.tint[3] = t.w;
                cc.glass[0] = c.p.refraction; cc.glass[1] = c.p.thickness; cc.glass[2] = c.p.highlight; cc.glass[3] = c.p.dispersion;
                float ang = c.p.lightAngle * 3.14159265f / 180.0f;
                cc.misc[0] = cosf(ang); cc.misc[1] = sinf(ang); cc.misc[2] = c.p.saturation; cc.misc[3] = (float)c.p.mode;
                cc.misc2[0] = 1.0f / W; cc.misc2[1] = 1.0f / H; cc.misc2[2] = (float)c.ellipse; cc.misc2[3] = c.alpha;
                ctx->UpdateSubresource(g_cbComposite, 0, nullptr, &cc, 0, 0);
                ctx->PSSetShader(g_psComposite, nullptr, 0);
                ctx->PSSetConstantBuffers(0, 1, &g_cbComposite);
                ctx->PSSetShaderResources(1, 1, &result);
            }
            SafeRelease(rt);
            SafeRelease(rtv);
            SafeRelease(dsv);
        }

        void DrawBackdrop(ImDrawList* dl, ImVec2 a, ImVec2 b, const float r[4], const Backdrop& p, float alpha, bool ellipse)
        {
            if (b.x <= a.x || b.y <= a.y || alpha <= 0.0f) return;
            ImU32 fallback = (p.tint >> IM_COL32_A_SHIFT) ? p.tint : IM_COL32(255, 255, 255, 24);
            if (!Available())
            {
                Paint fill;
                fill.type = Paint_Solid;
                fill.c0 = fallback;
                if (ellipse) FillEllipse(dl, a, b, fill, alpha); else FillRect(dl, a, b, r, fill, alpha);
                return;
            }
            BackdropCmd c;
            c.p = p;
            c.rect[0] = a.x; c.rect[1] = a.y; c.rect[2] = b.x; c.rect[3] = b.y;
            float maxr = ImMin(b.x - a.x, b.y - a.y) * 0.5f;
            for (int i = 0; i < 4; i++) c.radii[i] = ImClamp(r ? r[i] : 0.0f, 0.0f, maxr);
            c.alpha = alpha;
            c.ellipse = ellipse ? 1 : 0;
            dl->AddCallback(BackdropCallback, &c, sizeof(c));
            dl->AddRectFilled(a, b, Fade(fallback, alpha));    // shaded by the composite shader
            dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
        }

        // ------------------------------------------------------------ texture blend

        struct BlendCmd { int blend; int wrap; };

        static void BlendCallback(const ImDrawList*, const ImDrawCmd* cmd)
        {
            const BlendCmd& c = *(const BlendCmd*)cmd->UserCallbackData;
            const float factor[4] = { 0, 0, 0, 0 };
            g_ctx->PSSetShader(g_psBlend[c.blend], nullptr, 0);
            g_ctx->OMSetBlendState(g_blend[c.blend], factor, 0xffffffff);
            g_ctx->PSSetSamplers(0, 1, c.wrap ? &g_sampWrap : &g_sampClamp);
        }

        static void ImageShape(ImDrawList* dl, ImTextureRef tex, ImVec2 a, ImVec2 b, const float r[4], ImVec2 uv0, ImVec2 uv1, ImU32 col, bool clampUV)
        {
            bool rounded = r && (r[0] > 0.5f || r[1] > 0.5f || r[2] > 0.5f || r[3] > 0.5f);
            if (!rounded) { dl->AddImage(tex, a, b, uv0, uv1, col); return; }
            dl->PushTexture(tex);
            int start = dl->VtxBuffer.Size;
            PathRoundRect(dl, a, b, r);
            dl->PathFillConvex(col);
            ImGui::ShadeVertsLinearUV(dl, start, dl->VtxBuffer.Size, a, b, uv0, uv1, clampUV);
            dl->PopTexture();
        }

        void DrawTexture(ImDrawList* dl, ImTextureRef tex, ImVec2 a, ImVec2 b, const float r[4], ImVec2 uv0, ImVec2 uv1, ImU32 tint, int blend, bool wrap, float alpha)
        {
            if (b.x <= a.x || b.y <= a.y || alpha <= 0.0f) return;
            blend = ImClamp(blend, 0, Blend_Count - 1);
            ImU32 col = Fade(tint, alpha);
            if (!Available() || (blend == Blend_Normal && !wrap)) { ImageShape(dl, tex, a, b, r, uv0, uv1, col, true); return; }
            BlendCmd c = { blend, wrap ? 1 : 0 };
            dl->AddCallback(BlendCallback, &c, sizeof(c));
            ImageShape(dl, tex, a, b, r, uv0, uv1, col, !wrap);
            dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
        }

        void BeginBlend(ImDrawList* dl, int blend)
        {
            blend = ImClamp(blend, 0, Blend_Count - 1);
            if (blend == Blend_Normal || !Available()) return;
            BlendCmd c = { blend, 0 };
            dl->AddCallback(BlendCallback, &c, sizeof(c));
        }

        void EndBlend(ImDrawList* dl, int blend)
        {
            if (ImClamp(blend, 0, Blend_Count - 1) == Blend_Normal || !Available()) return;
            dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
        }

        // ------------------------------------------------------------ video (Media Foundation)

        struct Video
        {
            std::vector<unsigned char> source;
            std::thread worker;
            std::atomic<bool> quit{ false };
            std::mutex mutex;
            std::vector<unsigned char> frame;   // RGBA
            int frameW = 0, frameH = 0;
            bool fresh = false;
            ID3D11Texture2D* tex = nullptr;
            ID3D11ShaderResourceView* srv = nullptr;
            int texW = 0, texH = 0;
        };
        static std::vector<std::unique_ptr<Video>> g_videos;
        static int VideoCount() { return (int)g_videos.size(); }

        static IMFSourceReader* CreateReader(const std::vector<unsigned char>& data, bool processing)
        {
            IStream* stream = SHCreateMemStream(data.data(), (UINT)data.size());
            if (!stream) return nullptr;
            IMFByteStream* bs = nullptr;
            IMFSourceReader* reader = nullptr;
            if (SUCCEEDED(MFCreateMFByteStreamOnStream(stream, &bs)))
            {
                IMFAttributes* attr = nullptr;
                MFCreateAttributes(&attr, 2);
                if (processing) attr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
                MFCreateSourceReaderFromByteStream(bs, attr, &reader);
                SafeRelease(attr);
                SafeRelease(bs);
            }
            stream->Release();
            return reader;
        }

        static bool ConfigureRGB32(IMFSourceReader* reader, UINT32* w, UINT32* h, LONG* stride)
        {
            reader->SetStreamSelection((DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
            reader->SetStreamSelection((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
            IMFMediaType* mt = nullptr;
            MFCreateMediaType(&mt);
            mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            mt->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
            HRESULT hr = reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, mt);
            SafeRelease(mt);
            if (FAILED(hr)) return false;
            IMFMediaType* cur = nullptr;
            if (FAILED(reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &cur))) return false;
            MFGetAttributeSize(cur, MF_MT_FRAME_SIZE, w, h);
            UINT32 s = 0;
            *stride = SUCCEEDED(cur->GetUINT32(MF_MT_DEFAULT_STRIDE, &s)) ? (LONG)s : (LONG)(*w * 4);
            SafeRelease(cur);
            return *w > 0 && *h > 0;
        }

        // Decoders pad surfaces (e.g. 4500x3000 -> 4512x3008); find the row pitch that matches the buffer size.
        static LONG GuessPitch(DWORD len, UINT32 w, UINT32 h, LONG stride)
        {
            LONG s = stride < 0 ? -stride : stride;
            if (s >= (LONG)w * 4 && (DWORD)s * h == len) return s;
            const UINT32 aligns[] = { 1, 2, 4, 8, 16, 32, 64, 128 };
            for (UINT32 pa : aligns)
                for (UINT32 ha : aligns)
                {
                    LONG p = (LONG)(((w + pa - 1) / pa) * pa * 4);
                    UINT32 hh = (h + ha - 1) / ha * ha;
                    if ((DWORD)p * hh == len) return p;
                }
            return h ? std::max((LONG)w * 4, (LONG)(len / h)) : (LONG)w * 4;
        }

        static void VideoWorker(Video* v)
        {
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            MFStartup(MF_VERSION, MFSTARTUP_FULL);
            IMFSourceReader* reader = CreateReader(v->source, true);
            UINT32 w = 0, h = 0;
            LONG stride = 0;
            if (reader && ConfigureRGB32(reader, &w, &h, &stride))
            {
                auto t0 = std::chrono::steady_clock::now();
                int emptyLoops = 0;
                while (!v->quit)
                {
                    DWORD flags = 0;
                    LONGLONG ts = 0;
                    IMFSample* sample = nullptr;
                    if (FAILED(reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &ts, &sample))) break;
                    if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
                    {
                        SafeRelease(sample);
                        if (++emptyLoops > 3) break;
                        PROPVARIANT pos;
                        PropVariantInit(&pos);
                        pos.vt = VT_I8;
                        pos.hVal.QuadPart = 0;
                        reader->SetCurrentPosition(GUID_NULL, pos);
                        PropVariantClear(&pos);
                        t0 = std::chrono::steady_clock::now();
                        continue;
                    }
                    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) ConfigureRGB32(reader, &w, &h, &stride);
                    if (!sample) continue;
                    emptyLoops = 0;
                    // Present at the sample timestamp (100 ns units).
                    for (;;)
                    {
                        auto el = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count() * 10;
                        if (el >= ts || v->quit) break;
                        long long waitMs = (ts - el) / 10000;
                        std::this_thread::sleep_for(std::chrono::milliseconds(waitMs > 10 ? 10 : (waitMs < 1 ? 1 : waitMs)));
                    }
                    IMFMediaBuffer* buf = nullptr;
                    if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buf)))
                    {
                        // Prefer the 2D lock: it reports the real (possibly padded / negative) row pitch.
                        IMF2DBuffer* buf2d = nullptr;
                        BYTE* scan0 = nullptr;
                        LONG pitch = 0;
                        BYTE* p = nullptr;
                        DWORD len = 0;
                        bool locked2d = SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch));
                        if (!locked2d)
                        {
                            if (SUCCEEDED(buf->Lock(&p, nullptr, &len)))
                            {
                                LONG absStride = GuessPitch(len, w, h, stride);
                                pitch = stride < 0 ? -absStride : absStride;
                                scan0 = stride < 0 ? p + (size_t)absStride * (h - 1) : p;
                            }
                        }
                        if (scan0)
                        {
                            // Full HD plays as is; larger videos are decimated so the per-frame upload stays cheap.
                            UINT32 step = 1;
                            while (std::max(w, h) / step > 1920) step++;
                            UINT32 ow = w / step, oh = h / step;
                            std::lock_guard<std::mutex> lock(v->mutex);
                            v->frame.resize((size_t)ow * oh * 4);
                            for (UINT32 y = 0; y < oh; y++)
                            {
                                const BYTE* src = scan0 + (ptrdiff_t)pitch * (ptrdiff_t)(y * step);
                                unsigned char* dst = &v->frame[(size_t)y * ow * 4];
                                for (UINT32 x = 0; x < ow; x++)
                                {
                                    const BYTE* sp = src + (size_t)x * step * 4;
                                    dst[x * 4 + 0] = sp[2];
                                    dst[x * 4 + 1] = sp[1];
                                    dst[x * 4 + 2] = sp[0];
                                    dst[x * 4 + 3] = 255;
                                }
                            }
                            v->frameW = (int)ow;
                            v->frameH = (int)oh;
                            v->fresh = true;
                        }
                        if (locked2d) buf2d->Unlock2D();
                        else if (p) buf->Unlock();
                        SafeRelease(buf2d);
                        buf->Release();
                    }
                    sample->Release();
                }
            }
            SafeRelease(reader);
            MFShutdown();
            CoUninitialize();
        }

        int VideoOpenMemory(const void* data, size_t size)
        {
            if (!data || !size) return -1;
            auto v = std::make_unique<Video>();
            v->source.assign((const unsigned char*)data, (const unsigned char*)data + size);
            Video* raw = v.get();
            raw->worker = std::thread(VideoWorker, raw);
            for (size_t i = 0; i < g_videos.size(); i++)
                if (!g_videos[i]) { g_videos[i] = std::move(v); return (int)i; }
            g_videos.push_back(std::move(v));
            return (int)g_videos.size() - 1;
        }

        int VideoOpenFile(const char* path)
        {
            int n = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
            std::wstring w(n, 0);
            MultiByteToWideChar(CP_UTF8, 0, path, -1, w.data(), n);
            FILE* f = _wfopen(w.c_str(), L"rb");
            if (!f) return -1;
            fseek(f, 0, SEEK_END);
            long sz = ftell(f);
            fseek(f, 0, SEEK_SET);
            std::vector<unsigned char> d(sz > 0 ? sz : 0);
            size_t rd = sz > 0 ? fread(d.data(), 1, sz, f) : 0;
            fclose(f);
            return rd == d.size() ? VideoOpenMemory(d.data(), d.size()) : -1;
        }

        void VideoClose(int h)
        {
            if (h < 0 || h >= (int)g_videos.size() || !g_videos[h]) return;
            Video* v = g_videos[h].get();
            v->quit = true;
            if (v->worker.joinable()) v->worker.join();
            SafeRelease(v->srv);
            SafeRelease(v->tex);
            g_videos[h].reset();
        }

        ImTextureID VideoFrame(int h)
        {
            if (h < 0 || h >= (int)g_videos.size() || !g_videos[h] || !g_dev) return ImTextureID_Invalid;
            Video* v = g_videos[h].get();
            std::lock_guard<std::mutex> lock(v->mutex);
            if (v->fresh)
            {
                if (!v->tex || v->texW != v->frameW || v->texH != v->frameH)
                {
                    SafeRelease(v->srv);
                    SafeRelease(v->tex);
                    D3D11_TEXTURE2D_DESC d = {};
                    d.Width = v->frameW; d.Height = v->frameH; d.MipLevels = 1; d.ArraySize = 1;
                    d.Format = DXGI_FORMAT_R8G8B8A8_UNORM; d.SampleDesc.Count = 1;
                    d.Usage = D3D11_USAGE_DYNAMIC; d.BindFlags = D3D11_BIND_SHADER_RESOURCE; d.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    if (SUCCEEDED(g_dev->CreateTexture2D(&d, nullptr, &v->tex))) g_dev->CreateShaderResourceView(v->tex, nullptr, &v->srv);
                    v->texW = v->frameW;
                    v->texH = v->frameH;
                }
                D3D11_MAPPED_SUBRESOURCE m;
                if (v->tex && SUCCEEDED(g_ctx->Map(v->tex, 0, D3D11_MAP_WRITE_DISCARD, 0, &m)))
                {
                    for (int y = 0; y < v->frameH; y++)
                        memcpy((unsigned char*)m.pData + (size_t)y * m.RowPitch, &v->frame[(size_t)y * v->frameW * 4], (size_t)v->frameW * 4);
                    g_ctx->Unmap(v->tex, 0);
                }
                v->fresh = false;
            }
            return v->srv ? (ImTextureID)v->srv : ImTextureID_Invalid;
        }

        bool VideoProbe(const void* data, size_t size, int* width, int* height)
        {
            if (!g_mfStarted) g_mfStarted = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_FULL));
            std::vector<unsigned char> d((const unsigned char*)data, (const unsigned char*)data + size);
            IMFSourceReader* reader = CreateReader(d, false);
            if (!reader) return false;
            IMFMediaType* nt = nullptr;
            bool ok = false;
            if (SUCCEEDED(reader->GetNativeMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nt)))
            {
                UINT32 w = 0, h = 0;
                if (SUCCEEDED(MFGetAttributeSize(nt, MF_MT_FRAME_SIZE, &w, &h)) && w && h) { *width = (int)w; *height = (int)h; ok = true; }
                nt->Release();
            }
            reader->Release();
            return ok;
        }
    }
}
