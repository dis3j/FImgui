// Projects: a workspace folder (default C:/FImgui) with one folder per project:
//   <workspace>/<Name>/<name>.fimgui, fonts/, images/, icons/, videos/, export/, thumbnail.png
#include "app.h"
#include "crash.h"
#include "exporters.h"
#include "fonts.h"
#include "gfx.h"
#include "platform.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <set>

namespace fs = std::filesystem;

static const char* kSubdirs[] = { "fonts", "images", "icons", "videos", "export" };

std::string App::WorkspaceDir() const
{
    return workspace.empty() ? std::string("C:/FImgui") : workspace;
}

const char* ProjectTypeKey(int type) { return type == Project_Minecraft ? "minecraft" : "imgui"; }
int ProjectTypeFromKey(const std::string& key) { return key == "minecraft" ? Project_Minecraft : Project_ImGui; }

std::string ProjectFileName(const std::string& name)
{
    std::string f;
    for (unsigned char c : name)
        if (c < 128 && isalnum(c)) f += (char)tolower(c);
    return (f.empty() ? std::string("project") : f) + ".fimgui";
}

bool ValidProjectName(const std::string& name, std::string* err)
{
    if (name.empty()) { *err = "Enter a name"; return false; }
    for (unsigned char c : name)
        if (c < 32 || strchr("<>:\"/\\|?*", c)) { *err = "The name can't contain < > : \" / \\ | ? *"; return false; }
    if (name.back() == '.' || name.back() == ' ') { *err = "The name can't end with a dot or space"; return false; }
    return true;
}

// Relative path of an asset inside the project, e.g. images/logo.png
static std::string AssetRelPath(const Asset& a)
{
    const auto& d = a.data;
    switch (a.kind)
    {
    case 1: return "icons/" + a.name + ".svg";
    case 2: return "images/" + a.name + ".gif";
    case 3:
    {
        std::string ext = ".mp4";
        if (d.size() > 12 && !memcmp(d.data() + 4, "ftyp", 4) && !memcmp(d.data() + 8, "qt", 2)) ext = ".mov";
        else if (d.size() > 4 && d[0] == 0x30 && d[1] == 0x26) ext = ".wmv";
        else if (d.size() > 4 && !memcmp(d.data(), "RIFF", 4)) ext = ".avi";
        return "videos/" + a.name + ext;
    }
    default:
        return "images/" + a.name + ((d.size() > 2 && d[0] == 0xFF && d[1] == 0xD8) ? ".jpg" : ".png");
    }
}

// Output folders inside the project are stored relative to it.
static std::string ProjectRelative(const std::string& path, const std::string& projectDir)
{
    std::error_code ec;
    fs::path rel = fs::relative(fs::u8path(path), fs::u8path(projectDir), ec);
    return !ec && !rel.empty() && rel.native().find(L"..") != 0 ? rel.generic_u8string() : path;
}

static std::string ProjectAbsolute(const std::string& path, const std::string& projectDir)
{
    fs::path p = fs::u8path(path);
    return (p.is_absolute() ? p : fs::u8path(projectDir) / p).u8string();
}

static json ExportJson(const ExportOptions& o, const std::string& projectDir)
{
    return { { "outDir", ProjectRelative(o.outDir, projectDir) }, { "namespace", o.ns }, { "fileBase", o.fileBase }, { "embedFonts", o.embedFonts }, { "writeExample", o.writeExample }, { "iconScale", o.iconScale } };
}

static json JavaJson(const JavaOptions& o, const std::string& projectDir)
{
    return { { "outDir", ProjectRelative(o.outDir, projectDir) }, { "loader", o.loader == 1 ? "neoforge" : "fabric" }, { "modId", o.modId }, { "package", o.package },
             { "className", o.className }, { "textureScale", o.textureScale }, { "ttfFonts", o.ttfFonts } };
}

// Minecraft mod ids (and Java package segments): lowercase letters, digits and _, starting with a letter.
static std::string ModIdFromName(const std::string& name)
{
    std::string id;
    for (unsigned char c : name)
        id += c < 128 && isalnum(c) ? (char)tolower(c) : '_';
    while (id.find("__") != std::string::npos) id.erase(id.find("__"), 1);
    while (!id.empty() && id.back() == '_') id.pop_back();
    while (!id.empty() && id[0] == '_') id.erase(0, 1);
    if (id.empty() || !isalpha((unsigned char)id[0])) id = "menu" + (id.empty() ? std::string() : "_" + id);
    return id.substr(0, 64);
}

void App::ApplyProjectExportSettings(const json& j)
{
    std::string name = fs::u8path(projectDir).filename().u8string();
    exportOpt.outDir = (fs::u8path(projectDir) / "export").u8string();
    exportOpt.ns = SanitizeIdent(name, "menu");
    std::transform(exportOpt.ns.begin(), exportOpt.ns.end(), exportOpt.ns.begin(), ::tolower);
    exportOpt.fileBase = "menu";
    if (!j.is_object()) return;
    exportOpt.outDir = ProjectAbsolute(j.value("outDir", std::string("export")), projectDir);
    exportOpt.ns = j.value("namespace", exportOpt.ns);
    exportOpt.fileBase = j.value("fileBase", exportOpt.fileBase);
    exportOpt.embedFonts = j.value("embedFonts", exportOpt.embedFonts);
    exportOpt.writeExample = j.value("writeExample", exportOpt.writeExample);
    exportOpt.iconScale = j.value("iconScale", exportOpt.iconScale);
}

void App::ApplyProjectJavaSettings(const json& j)
{
    std::string id = ModIdFromName(fs::u8path(projectDir).filename().u8string());
    javaOpt.outDir = (fs::u8path(projectDir) / "export").u8string();
    javaOpt.modId = id;
    javaOpt.package = "com.example." + id + ".client.gui";
    javaOpt.className.clear();
    mcScreenShown = true;
    mcWindow = 3;
    mcGuiScale = 0;
    if (!j.is_object()) return;
    json screen = j.value("screen", json());
    if (screen.is_object())
    {
        mcScreenShown = screen.value("show", mcScreenShown);
        mcWindow = std::clamp(screen.value("window", mcWindow), 0, 5);
        mcGuiScale = std::clamp(screen.value("guiScale", mcGuiScale), 0, 6);
    }
    javaOpt.outDir = ProjectAbsolute(j.value("outDir", std::string("export")), projectDir);
    javaOpt.loader = j.value("loader", std::string("fabric")) == "neoforge" ? 1 : 0;
    javaOpt.modId = j.value("modId", javaOpt.modId);
    javaOpt.package = j.value("package", javaOpt.package);
    javaOpt.className = j.value("className", javaOpt.className);
    javaOpt.textureScale = j.value("textureScale", javaOpt.textureScale);
    javaOpt.ttfFonts = j.value("ttfFonts", javaOpt.ttfFonts);
}

bool App::CreateProject(const std::string& name, bool fromCurrent, std::string* err, int type)
{
    if (!ValidProjectName(name, err)) return false;
    fs::path dir = fs::u8path(WorkspaceDir()) / fs::u8path(name);
    std::error_code ec;
    if (fs::exists(dir, ec))
    {
        for (auto& e : fs::directory_iterator(dir, ec))
            if (e.path().extension() == ".fimgui") { *err = "A project with this name already exists"; return false; }
    }
    for (const char* sd : kSubdirs) fs::create_directories(dir / sd, ec);
    if (ec) { *err = "Cannot create " + dir.u8string() + ": " + ec.message(); return false; }

    if (!fromCurrent)
    {
        for (auto& [id, a] : doc.assets) gfx::InvalidateAsset(id);
        doc.Clear();
        selection.clear();
        vars = VarStore();
        chat.clear();
        zoom = 1.0f;
        pan = ImVec2(80, 80);
    }
    doc.name = name;
    projectDir = dir.u8string();
    filePath = (dir / ProjectFileName(name)).u8string();
    projectType = type;
    exportTarget = type == Project_Minecraft ? 1 : 0;
    ApplyProjectExportSettings(json());
    if (type == Project_Minecraft) ApplyProjectJavaSettings(json());
    if (type == Project_Minecraft && !fromCurrent)
    {
        // A starter screen: small enough for GUI scale 4 at 1920 x 1080 (480 x 270 GUI px).
        Node* f = CreateNode(NodeType::Frame, ImVec2(0, 0), ImVec2(256, 176), nullptr);
        f->name = name;
        f->fill = Paint{ 1, ImVec4(0.09f, 0.09f, 0.11f, 0.94f) };
        for (float& r : f->radius) r = 6;
        fitPending = true;
    }
    crash::SetContext("Editing " + filePath);
    SaveProject();
    ResetHistory();
    AddRecent(filePath);
    return true;
}

void App::SaveProject()
{
    if (projectDir.empty() || filePath.empty()) return;
    fs::path dir = fs::u8path(projectDir);
    std::error_code ec;
    for (const char* sd : kSubdirs) fs::create_directories(dir / sd, ec);

    // Assets as files (written only when missing or changed).
    for (auto& [id, a] : doc.assets)
    {
        std::string rel = AssetRelPath(a);
        fs::path p = dir / fs::u8path(rel);
        bool write = true;
        if (fs::exists(p, ec) && fs::file_size(p, ec) == a.data.size())
        {
            if (a.kind == 3) write = false;   // large videos: size match is enough
            else
            {
                std::vector<unsigned char> cur;
                write = !(platform::ReadFile(p.u8string(), cur) && cur == a.data);
            }
        }
        if (write && !platform::WriteFile(p.u8string(), a.data.data(), a.data.size())) { Toast("Cannot write " + rel, true); continue; }
        a.file = rel;
    }

    // Fonts used by text layers are copied into the project so it opens the same on another PC.
    std::set<std::string> copied;
    doc.ForEach([&](Node* n) {
        if (n->type != NodeType::Text) return;
        const FontFace* f = fonts::Find(n->fontFamily, n->fontStyle);
        if (!f || f->path.empty() || copied.count(f->path)) return;
        copied.insert(f->path);
        fs::path src = fs::u8path(f->path);
        fs::path dst = dir / "fonts" / src.filename();
        if (!fs::exists(dst, ec)) fs::copy_file(src, dst, ec);
        if (f->family == "Inter")
        {
            fs::path lic = src.parent_path() / "Inter-LICENSE.txt";
            if (fs::exists(lic, ec) && !fs::exists(dir / "fonts" / "Inter-LICENSE.txt", ec)) fs::copy_file(lic, dir / "fonts" / "Inter-LICENSE.txt", ec);
        }
    });

    json j = doc.ToJson(true, true);
    j["project"] = { { "version", 1 }, { "type", ProjectTypeKey(projectType) }, { "export", ExportJson(exportOpt, projectDir) } };
    if (projectType == Project_Minecraft)
    {
        j["project"]["minecraft"] = JavaJson(javaOpt, projectDir);
        j["project"]["minecraft"]["screen"] = { { "show", mcScreenShown }, { "window", mcWindow }, { "guiScale", mcGuiScale } };
    }
    if (!platform::WriteText(filePath, j.dump(1))) { Toast("Save failed", true); return; }
    modified = false;
    Toast("Saved to " + fs::u8path(projectDir).filename().u8string());

    // Thumbnail for the Projects screen (rendered after this frame).
    std::string thumb = (dir / "thumbnail.png").u8string();
    deferred.push_back([this, thumb] {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (auto& r : doc.roots)
            if (r->visible) { x0 = std::min(x0, r->x); y0 = std::min(y0, r->y); x1 = std::max(x1, r->x + r->w); y1 = std::max(y1, r->y + r->h); }
        std::error_code ec2;
        if (x1 <= x0) { fs::remove(fs::u8path(thumb), ec2); return; }
        ImVec2 a(x0 - 24, y0 - 24), b(x1 + 24, y1 + 24);
        float scale = std::min(1.0f, 640.0f / std::max(b.x - a.x, b.y - a.y));
        std::vector<unsigned char> png;
        if (RenderRegionPNG(a, b, scale, png)) platform::WriteFile(thumb, png.data(), png.size());
    });
}

void App::AddRecent(const std::string& path)
{
    recentFiles.erase(std::remove(recentFiles.begin(), recentFiles.end(), path), recentFiles.end());
    recentFiles.insert(recentFiles.begin(), path);
    if (recentFiles.size() > 12) recentFiles.resize(12);
}

void App::ScanProjects()
{
    for (auto& p : projects) if (p.thumb) gfx::DestroyTexture(p.thumb);
    projects.clear();
    std::error_code ec;
    fs::path root = fs::u8path(WorkspaceDir());
    fs::create_directories(root, ec);
    for (auto& e : fs::directory_iterator(root, ec))
    {
        if (!e.is_directory(ec)) continue;
        ProjectEntry pe;
        pe.dir = e.path().u8string();
        pe.name = e.path().filename().u8string();
        fs::path preferred = e.path() / ProjectFileName(pe.name);
        if (fs::exists(preferred, ec)) pe.file = preferred.u8string();
        else
            for (auto& f : fs::directory_iterator(e.path(), ec))
                if (f.path().extension() == ".fimgui") { pe.file = f.path().u8string(); break; }
        if (pe.file.empty()) continue;
        std::string text;
        if (platform::ReadText(pe.file, text))
        {
            json j = json::parse(text, nullptr, false);
            if (j.is_object() && j.contains("project") && j["project"].is_object() && j["project"].contains("type") && j["project"]["type"].is_string())
                pe.type = ProjectTypeFromKey(j["project"]["type"].get<std::string>());
        }
        auto t = fs::last_write_time(fs::u8path(pe.file), ec);
        // MSVC's file clock counts from 1601-01-01; convert to Unix time for display.
        pe.modified = (long long)std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch()).count() - 11644473600LL;
        std::vector<unsigned char> png, rgba;
        if (platform::ReadFile((e.path() / "thumbnail.png").u8string(), png) && gfx::DecodeImage(png.data(), png.size(), rgba, &pe.thumbW, &pe.thumbH))
            pe.thumb = gfx::CreateTexture(rgba.data(), pe.thumbW, pe.thumbH);
        projects.push_back(pe);
    }
    std::sort(projects.begin(), projects.end(), [](const ProjectEntry& a, const ProjectEntry& b) { return a.modified > b.modified; });
}
