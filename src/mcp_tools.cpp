#include "app.h"
#include "mcp_server.h"
#include "exporters.h"
#include "gfx.h"
#include "platform.h"
#include "../runtime/fimgui_fx.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

const char* McpInstructions()
{
    return "Fimgui is a live design editor for Dear ImGui menus; everything you change appears on the user's canvas immediately. "
           "Call get_design_guide once before editing to learn the node model, then build or modify designs with apply_operations "
           "(send several operations per call, top-level frame first). Use get_document to read the current design and render_image "
           "to look at the result. export_project writes the C++ menu, icons and fonts to a folder.";
}

static json Schema(json props, std::vector<std::string> required = {})
{
    json s = { { "type", "object" }, { "properties", props } };
    if (!required.empty()) s["required"] = required;
    return s;
}

json McpToolList()
{
    json tools = json::array();
    auto add = [&](const char* name, const char* title, const char* desc, json schema, bool readOnly) {
        tools.push_back({ { "name", name }, { "title", title }, { "description", desc }, { "inputSchema", schema },
                          { "annotations", { { "readOnlyHint", readOnly } } } });
    };
    add("get_design_guide", "Design guide",
        "Returns the Fimgui node model (frames, text, effects such as glass and background blur, animations, interactive roles) and the operation format used by apply_operations. Read it once before editing.",
        Schema(json::object()), true);
    add("get_document", "Get document",
        "Returns the current design as JSON (coordinates are relative to the parent), the selection, state variables, available fonts and the visible canvas area. Pass node_id to get only one subtree.",
        Schema({ { "node_id", { { "type", "string" }, { "description", "Optional node id or name" } } } }), true);
    add("apply_operations", "Apply operations",
        "Applies design operations to the live canvas, in order. Each item is an operation object such as "
        "{\"op\":\"create\",\"parent\":null,\"node\":{\"id\":\"menu\",\"type\":\"frame\",\"w\":640,\"h\":420,\"fill\":\"#121216\"}}, "
        "{\"op\":\"update\",\"id\":\"menu\",\"props\":{\"radius\":16}}, {\"op\":\"delete\",\"id\":\"x\"}, {\"op\":\"move\",\"id\":\"x\",\"parent\":\"y\"}, "
        "{\"op\":\"svg\",\"parent\":\"y\",\"node\":{...},\"svg\":\"<svg ...>\"}, {\"op\":\"select\",\"ids\":[...]}, {\"op\":\"clear\"}. "
        "See get_design_guide for all node properties. Returns per-operation results.",
        Schema({ { "operations", { { "type", "array" }, { "items", { { "type", "object" } } }, { "description", "Operations to apply in order" } } } }, { "operations" }), false);
    add("render_image", "Render image",
        "Renders the whole page or one node to PNG and returns it so you can check the visual result.",
        Schema({ { "node_id", { { "type", "string" }, { "description", "Optional node id; omit for all top-level frames" } } },
                 { "scale", { { "type", "number" }, { "description", "Pixel scale (default: fits 1400 px)" } } } }), true);
    add("get_code", "Get code",
        "Returns code for one node: Dear ImGui C++ snippet, CSS, SVG or JSON.",
        Schema({ { "node_id", { { "type", "string" } } }, { "format", { { "type", "string" }, { "enum", { "imgui", "css", "svg", "json" } } } } }, { "node_id" }), true);
    add("import_media", "Import media",
        "Imports a local file (PNG, JPG, SVG, animated GIF, MP4/MOV/WMV video) as an asset. By default also places it as a layer. "
        "The returned asset id can be used for image/vector nodes and texture overlays.",
        Schema({ { "path", { { "type", "string" }, { "description", "Absolute path to the file" } } },
                 { "parent_id", { { "type", "string" }, { "description", "Frame to place the layer in" } } },
                 { "x", { { "type", "number" } } }, { "y", { { "type", "number" } } },
                 { "place", { { "type", "boolean" }, { "description", "Create a layer (default true)" } } } }, { "path" }), false);
    add("export_project", "Export project",
        "target 'imgui' writes the C++ project for all top-level frames: menu .h/.cpp, runtime, effects, icons, images, videos, fonts and a DirectX 11 example host. "
        "target 'minecraft' writes a Minecraft 1.21.1 GUI for the first top-level frame: a Screen for Fabric or NeoForge, the menu class, the Fimgui Java runtime, baked layer textures and TTF fonts (src/main/java + src/main/resources). "
        "target 'web' writes a static website: every top-level frame is a page (the first is index.html), css/site.css, js/fimgui.js (state, controls, Show when) and assets/; a layer's 'link' (\"page:<frame id>\" or a URL) becomes a link. "
        "Without target, the type of the open project decides.",
        Schema({ { "out_dir", { { "type", "string" }, { "description", "Absolute output folder" } } },
                 { "target", { { "type", "string" }, { "enum", { "imgui", "minecraft", "web" } } } },
                 { "title", { { "type", "string" }, { "description", "web: site title" } } }, { "lang", { { "type", "string" }, { "description", "web: page language, e.g. en" } } },
                 { "namespace", { { "type", "string" } } }, { "file_base", { { "type", "string" } } },
                 { "embed_fonts", { { "type", "boolean" } } },
                 { "loader", { { "type", "string" }, { "enum", { "fabric", "neoforge" } } } },
                 { "mod_id", { { "type", "string" } } }, { "package", { { "type", "string" } } }, { "class_name", { { "type", "string" } } } }, { "out_dir" }), false);
    add("preview", "Preview",
        "Controls the canvas preview: 'on' makes the menu interactive with animations, 'off' returns to editing, 'play' replays the animations of node_id.",
        Schema({ { "mode", { { "type", "string" }, { "enum", { "on", "off", "play" } } } }, { "node_id", { { "type", "string" } } } }, { "mode" }), false);
    add("history", "Undo / redo",
        "Undoes or redoes design changes.",
        Schema({ { "action", { { "type", "string" }, { "enum", { "undo", "redo" } } } }, { "steps", { { "type", "integer" }, { "minimum", 1 } } } }, { "action" }), false);
    add("document_file", "Document file",
        "Projects live in the workspace folder (default C:/FImgui) as <Name>/<name>.fimgui with fonts/, images/, icons/, videos/ and export/. "
        "Actions: list_projects; new_project (name, type imgui|minecraft|web, default imgui); open_project (name or path); save (saves the project, or to path); new (blank unsaved design); open (any .fimgui path). "
        "new/open/new_project/open_project refuse to drop unsaved changes unless discard_changes is true.",
        Schema({ { "action", { { "type", "string" }, { "enum", { "list_projects", "new_project", "open_project", "save", "new", "open" } } } },
                 { "name", { { "type", "string" } } }, { "path", { { "type", "string" } } }, { "discard_changes", { { "type", "boolean" } } },
                 { "type", { { "type", "string" }, { "enum", { "imgui", "minecraft", "web" } }, { "description", "Project type for new_project" } } } }, { "action" }), false);
    return tools;
}

static json TextResult(const std::string& text, bool error = false)
{
    return { { "content", json::array({ { { "type", "text" }, { "text", text } } }) }, { "isError", error } };
}

static Node* FindNode(Document& doc, const json& args)
{
    std::string id = args.value("node_id", std::string());
    if (id.empty()) return nullptr;
    Node* n = doc.Find(id);
    return n ? n : doc.FindByName(id);
}

void App::McpLog(const std::string& text)
{
    // Consecutive tool calls from the same client are grouped in one chat entry.
    std::string client = mcp.ClientName();
    if (client.empty()) client = "MCP client";
    if (!chat.empty() && chat.back().source == "mcp" && chat.back().thinking == client && ImGui::GetTime() - mcpLastLog < 20.0)
        chat.back().notes.push_back(text);
    else
    {
        ChatMessage m;
        m.user = false;
        m.source = "mcp";
        m.thinking = client;   // client name (not model thinking)
        m.notes.push_back(text);
        chat.push_back(m);
    }
    mcpLastLog = ImGui::GetTime();
}

void App::StartMcp()
{
    mcp.Stop();
    mcpError.clear();
    if (!mcpEnabled) return;
    if (!mcp.Start(mcpPort, &mcpError)) Toast("MCP server: " + mcpError, true);
}

json App::McpTool(const std::string& name, const json& args)
{
    doc.ApplyLayout();
    if (name == "get_design_guide")
    {
        std::string guide = "You are connected to Fimgui over MCP. Pass operations to apply_operations as a JSON array instead of writing them as text lines; "
                            "the rest of this guide (node model, roles, effects, animation, design advice) applies as written.\n\n";
        guide += AiSystemPrompt();
        return TextResult(guide);
    }
    if (name == "get_document")
    {
        if (Node* n = FindNode(doc, args)) return TextResult(NodeToJson(n).dump());
        if (args.contains("node_id") && !args["node_id"].get<std::string>().empty()) return TextResult("Unknown node: " + args["node_id"].get<std::string>(), true);
        return TextResult(AiDocContext());
    }
    if (name == "apply_operations")
    {
        json ops = args.contains("operations") ? args["operations"] : json();
        if (ops.is_string())
        {
            // Tolerate JSON Lines passed as one string.
            std::string text = ops.get<std::string>();
            ops = json::array();
            size_t p = 0;
            while (p < text.size())
            {
                size_t nl = text.find('\n', p);
                json op = json::parse(text.substr(p, nl == std::string::npos ? std::string::npos : nl - p), nullptr, false);
                if (!op.is_discarded()) ops.push_back(op);
                if (nl == std::string::npos) break;
                p = nl + 1;
            }
        }
        if (ops.is_object()) ops = json::array({ ops });
        if (!ops.is_array() || ops.empty()) return TextResult("operations must be a non-empty array", true);
        json results = json::array();
        int ok = 0;
        for (size_t i = 0; i < ops.size(); i++)
        {
            std::string err;
            bool success = ops[i].is_object() && ApplyAiOp(ops[i], &err);
            if (success) ok++;
            json r = { { "index", i }, { "ok", success } };
            if (!success) r["error"] = err.empty() ? std::string("operation must be an object") : err;
            else if (ops[i].contains("node") && ops[i]["node"].is_object() && ops[i]["node"].contains("id")) r["id"] = ops[i]["node"]["id"];
            results.push_back(r);
        }
        doc.ApplyLayout();
        EnsureVars(doc, vars);
        MarkDirty();
        McpLog("apply_operations: " + std::to_string(ok) + "/" + std::to_string(ops.size()) + " changes");
        json out = { { "applied", ok }, { "failed", (int)ops.size() - ok }, { "results", results } };
        return TextResult(out.dump(), ok == 0);
    }
    if (name == "render_image")
    {
        Node* n = FindNode(doc, args);
        std::vector<unsigned char> png;
        int w = 0, h = 0;
        float scale = args.value("scale", 0.0f);
        bool ok;
        if (n)
        {
            if (scale <= 0) scale = std::min(2.0f, 1400.0f / std::max(1.0f, std::max(n->w, n->h)));
            ok = RenderNodePNG(n, scale, png, &w, &h);
        }
        else
        {
            float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
            for (auto& r : doc.roots)
                if (r->visible) { x0 = std::min(x0, r->x); y0 = std::min(y0, r->y); x1 = std::max(x1, r->x + r->w); y1 = std::max(y1, r->y + r->h); }
            if (x1 <= x0) return TextResult("The page is empty", true);
            ImVec2 a(x0 - 16, y0 - 16), b(x1 + 16, y1 + 16);
            if (scale <= 0) scale = std::min(1.0f, 1400.0f / std::max(b.x - a.x, b.y - a.y));
            ok = RenderRegionPNG(a, b, scale, png);
            w = (int)std::ceil((b.x - a.x) * scale);
            h = (int)std::ceil((b.y - a.y) * scale);
        }
        if (!ok) return TextResult("Render failed", true);
        McpLog("render_image" + std::string(n ? ": " + n->name : ""));
        return { { "content", json::array({
                       { { "type", "image" }, { "data", Base64Encode(png.data(), png.size()) }, { "mimeType", "image/png" } },
                       { { "type", "text" }, { "text", std::to_string(w) + "x" + std::to_string(h) + " px" } } }) },
                 { "isError", false } };
    }
    if (name == "get_code")
    {
        Node* n = FindNode(doc, args);
        if (!n) return TextResult("Unknown node", true);
        std::string f = args.value("format", std::string("imgui"));
        if (f == "css") return TextResult(NodeToCSS(doc, n, true));
        if (f == "svg") return TextResult(NodeToSVG(doc, n));
        if (f == "json") return TextResult(NodeToJson(n).dump(1));
        return TextResult(GenerateSnippet(doc, n));
    }
    if (name == "import_media")
    {
        std::string path = args.value("path", std::string());
        std::vector<unsigned char> bytes;
        if (path.empty() || !platform::ReadFile(path, bytes)) return TextResult("Cannot read " + path, true);
        Node* parent = nullptr;
        if (args.contains("parent_id")) { parent = doc.Find(args["parent_id"].get<std::string>()); while (parent && !parent->IsContainer()) parent = parent->parent; }
        ImVec2 pos(args.value("x", 0.0f), args.value("y", 0.0f));
        std::string ext = fs::u8path(path).extension().u8string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        std::string stem = fs::u8path(path).stem().u8string();
        bool video = ext == ".mp4" || ext == ".mov" || ext == ".wmv" || ext == ".m4v" || ext == ".avi";
        Node* n = nullptr;
        std::string asset;
        if (args.value("place", true))
        {
            if (ext == ".svg") n = ImportSVG(doc, std::string(bytes.begin(), bytes.end()), stem, parent, pos);
            else if (video) n = ImportVideo(doc, bytes, stem, parent, pos);
            else n = ImportImage(doc, bytes, stem, parent, pos);
            if (!n) return TextResult("Unsupported or corrupt file", true);
            asset = n->asset;
            Select(n->id);
        }
        else
        {
            int w = 0, h = 0;
            std::vector<unsigned char> rgba;
            if (ext == ".svg") asset = AddAsset(doc, 1, stem, bytes);
            else if (video) { if (fim::fx::VideoProbe(bytes.data(), bytes.size(), &w, &h)) asset = AddAsset(doc, 3, stem, bytes); }
            else if (gfx::DecodeImage(bytes.data(), bytes.size(), rgba, &w, &h)) asset = AddAsset(doc, IsGif(bytes) ? 2 : 0, stem, bytes);
            if (asset.empty()) return TextResult("Unsupported or corrupt file", true);
        }
        MarkDirty();
        const Asset& a = doc.assets[asset];
        McpLog("import_media: " + a.name);
        json out = { { "asset", asset }, { "kind", AssetKindName(a.kind) }, { "width", a.width }, { "height", a.height } };
        if (n) out["node_id"] = n->id;
        return TextResult(out.dump());
    }
    if (name == "export_project" && args.value("target", std::string(!projectDir.empty() ? ProjectTypeKey(projectType) : "imgui")) == "web")
    {
        WebOptions saved = webOpt;
        webOpt.outDir = args.value("out_dir", std::string());
        if (webOpt.outDir.empty()) { webOpt = saved; return TextResult("out_dir is required", true); }
        if (args.contains("title")) webOpt.title = args["title"].get<std::string>();
        if (args.contains("lang")) webOpt.lang = args["lang"].get<std::string>();
        ExportResult r = RunWebExport();
        McpLog("export_project (web): " + r.message);
        json out = { { "ok", r.ok }, { "message", r.message }, { "files", r.files } };
        return TextResult(out.dump(1), !r.ok);
    }
    if (name == "export_project" && args.value("target", std::string(!projectDir.empty() ? ProjectTypeKey(projectType) : "imgui")) == "minecraft")
    {
        JavaOptions saved = javaOpt;
        javaOpt.outDir = args.value("out_dir", std::string());
        if (javaOpt.outDir.empty()) { javaOpt = saved; return TextResult("out_dir is required", true); }
        if (args.contains("loader")) javaOpt.loader = args["loader"].get<std::string>() == "neoforge" ? 1 : 0;
        if (args.contains("mod_id")) javaOpt.modId = args["mod_id"].get<std::string>();
        if (args.contains("package")) javaOpt.package = args["package"].get<std::string>();
        if (args.contains("class_name")) javaOpt.className = args["class_name"].get<std::string>();
        ExportResult r = RunJavaExport();
        McpLog("export_project (minecraft): " + r.message);
        json out = { { "ok", r.ok }, { "message", r.message }, { "files", r.files } };
        return TextResult(out.dump(1), !r.ok);
    }
    if (name == "export_project")
    {
        ExportOptions opt = exportOpt;
        opt.outDir = args.value("out_dir", std::string());
        if (opt.outDir.empty()) return TextResult("out_dir is required", true);
        if (args.contains("namespace")) opt.ns = args["namespace"].get<std::string>();
        if (args.contains("file_base")) opt.fileBase = args["file_base"].get<std::string>();
        if (args.contains("embed_fonts")) opt.embedFonts = args["embed_fonts"].get<bool>();
        ExportResult r = ExportProject(doc, opt, platform::ExeDir() + "/runtime");
        McpLog("export_project: " + r.message);
        json out = { { "ok", r.ok }, { "message", r.message }, { "files", r.files } };
        return TextResult(out.dump(1), !r.ok);
    }
    if (name == "preview")
    {
        std::string mode = args.value("mode", std::string("on"));
        if (mode == "on") { preview = true; animClocks.clear(); }
        else if (mode == "off") preview = false;
        else if (mode == "play")
        {
            Node* n = FindNode(doc, args);
            if (!n) return TextResult("node_id is required for play", true);
            preview = false;
            PlayAnimation(n);
        }
        McpLog("preview: " + mode);
        return TextResult("ok");
    }
    if (name == "history")
    {
        int steps = std::clamp(args.value("steps", 1), 1, 100);
        bool undo = args.value("action", std::string("undo")) == "undo";
        for (int i = 0; i < steps; i++) { if (undo) Undo(); else Redo(); }
        McpLog(std::string(undo ? "undo" : "redo") + " x" + std::to_string(steps));
        return TextResult("ok");
    }
    if (name == "document_file")
    {
        std::string action = args.value("action", std::string());
        std::string path = args.value("path", std::string());
        bool discard = args.value("discard_changes", false);
        if (action == "list_projects")
        {
            ScanProjects();
            homeNeedsScan = true;
            json list = json::array();
            for (auto& p : projects) list.push_back({ { "name", p.name }, { "file", p.file } });
            return TextResult(json{ { "workspace", WorkspaceDir() }, { "projects", list } }.dump(1));
        }
        if ((action == "new" || action == "open" || action == "new_project" || action == "open_project") && modified && !discard)
            return TextResult("The document has unsaved changes. Save it first or pass discard_changes: true.", true);
        if (action == "new_project")
        {
            std::string name = args.value("name", std::string()), err;
            modified = false;
            if (!CreateProject(name, false, &err, ProjectTypeFromKey(args.value("type", std::string("imgui"))))) return TextResult(err, true);
            showHome = false;
            McpLog("new project: " + name);
            return TextResult(json{ { "project", projectDir }, { "file", filePath }, { "type", ProjectTypeKey(projectType) } }.dump());
        }
        if (action == "open_project")
        {
            std::string name = args.value("name", std::string());
            std::string p = !path.empty() ? path : WorkspaceDir() + "/" + name + "/" + ProjectFileName(name);
            std::string text;
            if (!platform::ReadText(p, text)) return TextResult("Cannot read " + p, true);
            modified = false;
            OpenDocument(p);
            McpLog("open project: " + p);
            return TextResult(json{ { "project", projectDir }, { "file", filePath } }.dump());
        }
        if (action == "new")
        {
            modified = false;
            NewDocument();
        }
        else if (action == "open")
        {
            std::string text;
            if (path.empty() || !platform::ReadText(path, text)) return TextResult("Cannot read " + path, true);
            modified = false;
            OpenDocument(path);
        }
        else if (action == "save" && path.empty() && !projectDir.empty())
        {
            SaveProject();
            path = filePath;
        }
        else if (action == "save")
        {
            if (path.empty()) path = filePath;
            if (path.empty()) return TextResult("path is required for an unsaved document", true);
            if (!platform::WriteText(path, doc.ToJson(true).dump(1))) return TextResult("Cannot write " + path, true);
            filePath = path;
            modified = false;
        }
        else return TextResult("Unknown action", true);
        McpLog("document_file: " + action + (path.empty() ? "" : " " + path));
        return TextResult("ok");
    }
    return TextResult("Unknown tool: " + name, true);
}
