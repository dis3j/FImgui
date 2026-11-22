#pragma once
#include "document.h"
#include <string>
#include <vector>

struct ExportOptions
{
    std::string outDir;
    std::string ns = "menu";
    std::string fileBase = "menu";
    bool embedFonts = false;
    bool writeExample = true;
    float iconScale = 2.0f;      // PNG rasterization scale for vector icons
};

struct ExportResult
{
    bool ok = false;
    std::string message;
    std::vector<std::string> files;
};

// Writes a complete folder: menu .h/.cpp, runtime, embedded assets, icons/, images/, fonts/, example host.
ExportResult ExportProject(Document& doc, const ExportOptions& opt, const std::string& runtimeDir);

// Self-contained ImGui snippet for one node subtree (Code panel / "Copy as ImGui").
std::string GenerateSnippet(Document& doc, Node* n);
