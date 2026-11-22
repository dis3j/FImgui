# Aurora glass demo - exported by Fimgui

## Layout

- `menu/menu.h/.cpp` - generated ImGui code (`menu::State`, `menu::Init`, `menu::Render`)
- `menu/menu_assets.cpp` - embedded textures and GIFs
- `menu/fimgui_runtime.h/.cpp` - drawing, animation and GIF helpers used by the generated code
- `menu/fimgui_fx.h` + `fimgui_fx_dx11.cpp` - background blur, glass, blend modes, video (DirectX 11). Use `fimgui_fx_null.cpp` instead on other renderers.
- `icons/` - SVG + PNG icons, `images/` - raster images and GIFs, `videos/` - video files, `fonts/` - font files
- `example/` - Win32 + DirectX 11 host (set `IMGUI_DIR` in CMake)
- `menu.fimgui` - design source, open it in Fimgui to keep editing

## Usage

```cpp
#include "menu.h"
#include "fimgui_fx.h"

menu::State state;
fim::fx::InitDX11(device, context);          // once, after ImGui_ImplDX11_Init()
menu::Init(MyCreateTexture, "fonts", ".");   // fonts dir, folder that contains videos/

// every frame, between ImGui::NewFrame() and ImGui::Render():
menu::Render(state);
```

Add `menu/*.cpp` (one of the two fimgui_fx_*.cpp) to your build. Dear ImGui 1.92 or newer is required (dynamic font sizes).
The DirectX 11 effects link with d3dcompiler, mfplat, mfreadwrite, mfuuid and shlwapi.

## License

The generated code and the Fimgui runtime are for non-commercial use only (see `LICENSE-FIMGUI.txt`). Your own content (texts, images, layouts) remains yours.

## State

- `tab` (int)
- `Play_pressed` (bool, true for one frame after a click)
- `More_pressed` (bool, true for one frame after a click)
- `HDR_pressed` (bool, true for one frame after a click)
- `Bloom_pressed` (bool, true for one frame after a click)
- `Motion_blur_pressed` (bool, true for one frame after a click)
