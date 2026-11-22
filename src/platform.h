#pragma once
#include <string>
#include <vector>

namespace platform
{
    std::string ExeDir();
    std::string AppDataDir();                       // %APPDATA%/Fimgui (created on demand)
    std::string GetEnv(const char* name);

    bool ReadFile(const std::string& path, std::vector<unsigned char>& out);
    bool ReadText(const std::string& path, std::string& out);
    bool WriteFile(const std::string& path, const void* data, size_t size);
    bool WriteText(const std::string& path, const std::string& text);

    // filters: "Images|*.png;*.jpg|All|*.*"
    std::string OpenFileDialog(const char* title, const char* filters);
    std::vector<std::string> OpenFilesDialog(const char* title, const char* filters);
    std::string SaveFileDialog(const char* title, const char* filters, const char* defaultName, const char* defExt);
    std::string PickFolderDialog(const char* title);
    void OpenInExplorer(const std::string& path);
    void OpenUrl(const std::string& url);
    void SetWindowTitle(const std::string& title);

    // Clipboard
    bool SetClipboardText(const std::string& utf8);
    std::string GetClipboardText();
    bool SetClipboardImage(const unsigned char* rgba, int w, int h);     // PNG + DIBV5
    bool GetClipboardImage(std::vector<unsigned char>& encoded);         // encoded PNG/DIB->PNG bytes
    bool ClipboardHasImage();
    std::vector<std::string> GetClipboardFiles();

    std::wstring Widen(const std::string& s);
    std::string Narrow(const std::wstring& s);
}
