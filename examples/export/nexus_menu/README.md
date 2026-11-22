# Nexus demo - exported by Fimgui

## Layout

- `menu/menu.h/.cpp` - generated ImGui code (`project_a::State`, `project_a::Init`, `project_a::Render`)
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

project_a::State state;
fim::fx::InitDX11(device, context);          // once, after ImGui_ImplDX11_Init()
project_a::Init(MyCreateTexture, "fonts", ".");   // fonts dir, folder that contains videos/

// every frame, between ImGui::NewFrame() and ImGui::Render():
project_a::Render(state);
```

Add `menu/*.cpp` (one of the two fimgui_fx_*.cpp) to your build. Dear ImGui 1.92 or newer is required (dynamic font sizes).
The DirectX 11 effects link with d3dcompiler, mfplat, mfreadwrite, mfuuid and shlwapi.

## License

The generated code and the Fimgui runtime may be used, modified and sold as part of your own projects (see `LICENSE-FIMGUI.txt`); keep the copyright line at the top of the runtime files. Fimgui itself may not be sold. Your own content (texts, images, layouts) remains yours.

## State

- `aim_autofire` (bool)
- `aim_enabled` (bool)
- `aim_visible` (bool)
- `auto_strafe` (bool)
- `bhop` (bool)
- `bone` (int)
- `box_esp` (bool)
- `cfg_name` (char[256])
- `esp_dist` (float)
- `fov` (float)
- `health_bar` (bool)
- `name_tags` (bool)
- `skeleton` (bool)
- `smooth` (float)
- `streamproof` (bool)
- `tab` (int)
- `watermark` (bool)
- `Save_config_pressed` (bool, true for one frame after a click)
- `Load_config_pressed` (bool, true for one frame after a click)
