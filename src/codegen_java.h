#pragma once
#include "codegen.h"
#include <functional>

// Minecraft (Java Edition 1.21.1) GUI export: a Screen for Fabric (Yarn names) or NeoForge (Mojang names),
// a loader-independent menu class, the Fimgui Java runtime, baked layer textures and TTF font providers.
struct JavaOptions
{
    std::string outDir;
    int loader = 0;                       // 0 Fabric 1.21.1 (Yarn), 1 NeoForge 1.21.1 (Mojang mappings)
    std::string modId = "examplemod";
    std::string package = "com.example.examplemod.client.gui";
    std::string className;                // empty = from the frame name
    float textureScale = 2.0f;            // baked layer resolution (GUI scale it stays sharp at)
    bool ttfFonts = true;                 // ship the design fonts as TTF font providers (else the vanilla font)
};

// Renders one layer alone (children hidden) to PNG. variant: 0 normal, 1 hover color, 2 active color,
// 3 slider fill at the full width of its slider. offset = top-left of the image relative to the layer, in design px.
using BakeLayerFn = std::function<bool(Node* n, int variant, float scale, std::vector<unsigned char>& png, int* w, int* h, ImVec2* offset)>;

ExportResult ExportJava(Document& doc, const JavaOptions& opt, const std::string& runtimeDir, const BakeLayerFn& bake);
