#pragma once
#include "imgui.h"
#include <string>
#include <vector>

struct FontFace
{
    std::string family;
    std::string style;
    std::string path;
    int weight = 400;
    bool italic = false;
};

namespace fonts
{
    void Init(const std::string& resourceDir);
    void ProcessPending();                       // load queued fonts; call before ImGui::NewFrame()

    const std::vector<std::string>& Families();
    std::vector<std::string> Styles(const std::string& family);
    const FontFace* Find(const std::string& family, const std::string& style);   // never null if any font exists
    ImFont* Get(const std::string& family, const std::string& style);            // may return the default font while loading
    bool AddFontFile(const std::string& path, std::string* familyOut = nullptr);

    // Editor UI fonts
    enum UiFont { UI_Regular, UI_Medium, UI_SemiBold, UI_Count };
    ImFont* UI(UiFont f);
}
