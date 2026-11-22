#pragma once
#include "imgui.h"
#include <functional>
#include <string>
#include <vector>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct Asset;

namespace gfx
{
    void Init(ID3D11Device* dev, ID3D11DeviceContext* ctx);
    void Shutdown();
    void NewFrame();   // evicts stale cached textures

    ImTextureID CreateTexture(const unsigned char* rgba, int w, int h);
    void        DestroyTexture(ImTextureID id);

    bool DecodeImage(const unsigned char* data, size_t size, std::vector<unsigned char>& rgba, int* w, int* h);
    std::vector<unsigned char> EncodePNG(const unsigned char* rgba, int w, int h);
    bool SvgSize(const std::string& svg, float* w, float* h);
    bool RasterizeSVG(const std::string& svg, int w, int h, bool recolorWhite, std::vector<unsigned char>& rgba);

    struct TexInfo { ImTextureID id = 0; int w = 0, h = 0; };
    // Returns a texture for an asset. SVGs are rasterized at (approximately) the requested pixel size.
    TexInfo AssetTexture(const Asset& a, float pixelW, float pixelH, bool recolor);
    void    InvalidateAsset(const std::string& id);

    // Renders arbitrary ImDrawList content to an RGBA buffer (straight alpha).
    // Must be called outside of ImGui::NewFrame()/Render() - see App::RunDeferred().
    bool RenderToRGBA(int w, int h, const std::function<void(ImDrawList*)>& draw, std::vector<unsigned char>& out);
    // Same, but keeps the result on the GPU in a reused texture (valid until the next call).
    ImTextureID RenderToTexture(int w, int h, const std::function<void(ImDrawList*)>& draw, ImVec4 clear = ImVec4(0, 0, 0, 0));
}
