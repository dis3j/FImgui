#pragma once
#include "updater.h"
#include "codegen_java.h"
#include "codegen_web.h"
#include <memory>
#include <set>
#include <map>
#include "document.h"
#include "renderer.h"
#include "codegen.h"
#include "ai.h"
#include "mcp_server.h"
#include <cmath>
#include <functional>
#include <map>
#include <string>
#include <vector>

enum class Tool { Move, Frame, Rect, Ellipse, Line, Text, Hand, Pen };
enum RightTab { Tab_Design, Tab_Prototype, Tab_Motion, Tab_Code, Tab_AI, Tab_Count };

struct ChatMessage
{
    bool user = true;
    std::string text;           // visible text (assistant: without op lines)
    std::string raw;            // assistant: full streamed text (history)
    std::string thinking;
    std::vector<std::vector<unsigned char>> images;
    int ops = 0;
    bool error = false;
    bool streaming = false;
    std::vector<std::string> notes;
    std::string source;         // "mcp" for activity of external MCP clients
};

struct Guide { bool vertical; float pos; float from, to; };

struct ProjectEntry
{
    std::string name, dir, file;
    int type = 0;                // ProjectType
    long long modified = 0;      // unix time
    ImTextureID thumb = 0;
    int thumbW = 0, thumbH = 0;
};

// What a project is made for: chosen when it is created, decides the default export target.
enum ProjectType { Project_ImGui = 0, Project_Minecraft = 1, Project_Web = 2 };
const char* ProjectTypeKey(int type);           // "imgui" / "minecraft" / "web"
int ProjectTypeFromKey(const std::string& key);

std::string ProjectFileName(const std::string& name);           // "Project_A" -> "projecta.fimgui"
bool ValidProjectName(const std::string& name, std::string* err);

class WebPreview;

class App
{
public:
    void Init();
    void Frame();              // build UI (between NewFrame and Render)
    void RunDeferred();        // after rendering: offscreen jobs (PNG export, AI snapshots)
    void Shutdown();
    void OnDropFiles(const std::vector<std::string>& files);
    bool RequestClose();

    // ---- document & selection
    Document doc;
    VarStore vars;
    std::vector<std::string> selection;
    std::string filePath;
    std::string projectDir;      // set when the design lives in a project folder
    int projectType = Project_ImGui;
    bool IsMinecraft() const { return !projectDir.empty() && projectType == Project_Minecraft; }
    bool IsWeb() const { return !projectDir.empty() && projectType == Project_Web; }
    bool modified = false;

    // ---- Minecraft projects (ui_minecraft.cpp): the menu frame inside a game window
    bool mcScreenShown = true;
    int mcWindow = 3;                  // window size preset (1920 x 1080)
    int mcGuiScale = 0;                // 0 auto, like the game
    bool fitPending = false;           // fit the view once the canvas has a size
    Node* McMenuFrame();               // the first top-level frame: the exported Screen
    void McScreen(int* guiW, int* guiH, int* scale);
    float McFit(Node* menu);
    bool McScreenRect(ImVec2* a, ImVec2* b);
    void McFitScreen();
    void DrawMinecraftScreen(ImDrawList* dl);
    void DrawMinecraftPanel(float W);

    // ---- web projects (ui_web.cpp): every top-level frame is a page, previewed live in the browser
    WebOptions webOpt;
    std::shared_ptr<WebPreview> webPreview;
    size_t webBuiltHash = 0;
    double webBuiltTime = 0;
    void ApplyProjectWebSettings(const json& j);
    ExportResult RunWebExport();
    void OpenWebPreview(Node* page, bool openBrowser = true);   // starts the preview server and opens the page in the browser
    void StopWebPreview();
    void WebPreviewTick();             // rebuilds the served site after edits (every frame)
    Node* WebCurrentPage();            // the page of the selection, else the page in the middle of the view
    void FollowLink(Node* n);          // preview: a click on a linked layer
    void DrawWebChrome(ImDrawList* dl);
    void DrawWebPanel(float W);
    void DrawWebPageSection(Node* page, float W);
    bool DrawLinkSection(Node* n, float W);

    // ---- projects (workspace/<Name>/<name>.fimgui + fonts/ images/ icons/ videos/ export/)
    std::string workspace = "C:/FImgui";
    bool showHomeOnStart = true;
    bool showHome = false;
    std::vector<std::string> recentFiles;
    std::vector<ProjectEntry> projects;
    std::string WorkspaceDir() const;
    bool CreateProject(const std::string& name, bool fromCurrent, std::string* err, int type = Project_ImGui);
    void SaveProject();
    void ScanProjects();
    void AddRecent(const std::string& path);
    void ApplyProjectExportSettings(const json& j);
    void ApplyProjectJavaSettings(const json& j);
    void OpenHome() { showHome = true; homeNeedsScan = true; }
    bool homeNeedsScan = true;
    bool openNewProject = false;
    bool newProjectFromCurrent = false;
    void DrawHome();
    void DrawNewProjectDialog();

    Node* Sel(size_t i = 0) { return i < selection.size() ? doc.Find(selection[i]) : nullptr; }
    std::vector<Node*> SelNodes();
    bool IsSelected(const std::string& id) const;
    void Select(const std::string& id, bool additive = false);
    void SetSelection(const std::vector<std::string>& ids);
    void ClearSelection() { selection.clear(); }
    void RevealNode(Node* n);  // make conditionally hidden ancestors visible (tabs)

    // ---- history
    void MarkDirty() { m_dirty = true; modified = true; }
    void Checkpoint();         // push undo step if the document changed
    void Undo();
    void Redo();
    void ResetHistory();

    // ---- commands
    void NewDocument();
    void NewDocumentConfirm(std::function<void()> action);   // asks before dropping unsaved changes
    void OpenDocument(const std::string& path = "");
    void SaveDocument(bool saveAs);
    void DeleteSelection();
    void DuplicateSelection();
    void CopySelection();
    void CutSelection();
    void Paste();
    void GroupSelection(bool asFrame);
    void Ungroup();
    void ReorderSelection(int delta);  // +1 forward, -1 backward, +100 front, -100 back
    void AlignSelection(int mode);     // 0 left,1 hcenter,2 right,3 top,4 vcenter,5 bottom
    void NudgeSelection(float dx, float dy);
    void ImportImageFile(const std::string& path, Node* parent, ImVec2 localPos);
    void CopyAs(int format);           // 0 CSS, 1 SVG, 2 PNG, 3 ImGui, 4 JSON
    void ExportSelectionFile(int format, float scale); // 0 PNG, 1 SVG
    void ZoomToFit(bool selectionOnly);
    void SetZoom(float z, ImVec2 anchorScreen);
    void Toast(const std::string& msg, bool error = false);
    Node* CreateNode(NodeType t, ImVec2 world, ImVec2 size, Node* parent);
    Node* ContainerAt(ImVec2 world, Node* exclude = nullptr);
    std::string UniqueName(const std::string& base);

    // ---- view
    ImVec2 canvasMin, canvasMax;       // screen rect of the canvas
    ImVec2 pan = ImVec2(0, 0);         // screen offset of world origin from canvasMin
    float zoom = 1.0f;
    Tool tool = Tool::Move;
    bool preview = false;
    bool ignoreConditions = false;
    bool pixelGrid = true;       // pixel grid from 400% zoom (Shift+')
    bool snapToPixel = true;     // round positions and sizes to whole pixels (Ctrl+Shift+')
    bool pixelPreview = false;   // render at 1x and magnify without smoothing (Ctrl+Alt+Y)
    static constexpr float kPixelGridZoom = 4.0f;
    float Snap(float v) const { return snapToPixel ? std::round(v) : std::round(v * 100.0f) / 100.0f; }
    ImTextureID pixelPreviewTex = 0;
    ImVec2 pixelPreviewMin, pixelPreviewMax;   // world rect stored in pixelPreviewTex
    ImVec2 WorldToScreen(ImVec2 w) const { return ImVec2(canvasMin.x + pan.x + w.x * zoom, canvasMin.y + pan.y + w.y * zoom); }
    ImVec2 ScreenToWorld(ImVec2 s) const { return ImVec2((s.x - canvasMin.x - pan.x) / zoom, (s.y - canvasMin.y - pan.y) / zoom); }

    // ---- AI
    AiSettings ai;
    AiClient aiClient;
    std::vector<ChatMessage> chat;
    char aiInput[4096] = {};
    std::vector<std::vector<unsigned char>> aiAttachments;
    void AiSend();
    void AiPump();             // consume streamed events, apply ops
    bool ApplyAiOp(const json& op, std::string* err);
    std::string AiDocContext();

    // ---- MCP server (external AI clients such as Claude Desktop / Claude Code)
    McpServer mcp;
    bool mcpEnabled = true;
    int mcpPort = 7317;
    std::string mcpError;
    double mcpLastLog = 0;
    void StartMcp();
    void PumpMcp() { mcp.Pump([this](const std::string& n, const json& a) { return McpTool(n, a); }); }
    json McpTool(const std::string& name, const json& args);
    void McpLog(const std::string& text);

    // ---- export
    ExportOptions exportOpt;
    bool showExport = false;
    ExportResult lastExport;
    bool showSettings = false;
    bool showShortcuts = false;
    bool persistSettings = true;   // false when settings were overridden from the command line
    void SaveSettings();
    void LoadSettings();

    // deferred GPU jobs
    std::vector<std::function<void()>> deferred;
    bool RenderNodePNG(Node* n, float scale, std::vector<unsigned char>& png, int* w = nullptr, int* h = nullptr, ImVec2* worldMin = nullptr);
    // One layer alone (children hidden), for the Minecraft export. See BakeLayerFn.
    bool BakeLayerPNG(Node* n, int variant, float scale, std::vector<unsigned char>& png, int* w, int* h, ImVec2* offset);
    JavaOptions javaOpt;
    int exportTarget = 0;              // 0 Dear ImGui (C++), 1 Minecraft Java, 2 Web
    ExportResult RunJavaExport();
    bool RenderRegionPNG(ImVec2 worldMin, ImVec2 worldMax, float scale, std::vector<unsigned char>& png);

    // ---- UI (ui_*.cpp)
    void DrawCanvas();
    void DrawLeftPanel();
    void DrawRightPanel();
    void DrawToolbar();
    void DrawDesignTab();
    void DrawPrototypeTab();
    void DrawCodeTab();
    void DrawMotionTab();
    void DrawEffects(Node* n, const std::vector<Node*>& nodes);
    void DrawAiTab();
    void DrawExportDialog();
    void DrawSettingsDialog();
    void DrawContextMenu();
    void DrawMainMenu();
    // Fill picker (ui_picker.cpp)
    int pickerTab = 0, pickerStop = 0;
    ImVec4 shaderC0 = ImVec4(0.3f, 0.5f, 1, 1), shaderC1 = ImVec4(0.9f, 0.3f, 0.8f, 1);
    void CollectPageColors();
    // Tabs on top of a fill's color picker. Returns bit 1 on change, bit 2 to hide the color body.
    int FillPickerHeader(Node* n, Paint* p, ImVec4** target, bool mediaFirst);
    void ApplyImageAdjust(Node* n);   // bakes n->texture.adjust into an image copy (or restores the original)
    // Keyframe timeline (ui_timeline.cpp)
    struct KeySel { std::string node; int prop; float time; };
    bool timelineOpen = false, timelinePlaying = false, timelineLoop = true;
    double timelineTime = 0;
    float timelineH = 0, timelineZoom = 1.0f, timelineScroll = 0;
    std::vector<KeySel> keySel;
    std::vector<float> keyDragOrig;
    float keyDragStartX = 0;
    bool keyDragging = false;
    // rubber-band selection of keys: start in timeline time / row content y, selection it adds to
    bool keyMarquee = false;
    float keyMarqueeT0 = 0, keyMarqueeY0 = 0;
    std::vector<KeySel> keyMarqueeBase;
    std::map<std::string, bool> timelineExpanded;
    std::set<std::string> timelineExpandedInit;
    struct PathDrag { bool active = false; std::string node; float time = 0; ImVec2 baseCenter, grab; } pathDrag;
    void DrawTimeline();
    void TickTimeline();
    float TimelineLength();
    std::vector<Node*> TimelineNodes();
    float SampleProp(const Node* n, int prop, float t) const;
    void EnableKeyframes(Node* n);
    void SetKey(Node* n, int prop, float t, float v);
    bool HasKey(const Node* n, int prop, float t) const;
    void RemoveKey(Node* n, int prop, float t);
    void DeleteSelectedKeys();
    bool MotionPathInput();
    void DrawMotionPath(ImDrawList* dl);
    // Window chrome (ui_chrome.cpp): custom title bar, menus, updates.
    void DrawTitleBar();
    void StartupChecks();   // update check + what's new (skipped for command-line runs)
    float TitleBarHeight() const;
    void MenuFile();
    void MenuEdit();
    void MenuView();
    void MenuHelp();
    void OpenWhatsNew(bool force);
    void PollUpdates();
    void CheckForUpdates();   // manual check: reports "up to date" and errors too
    void DrawUpdateDialogs();
    int openMenu = -1, lastOpenMenu = -1;
    bool showUpdate = false, showWhatsNew = false, showAbout = false;
    bool manualUpdateCheck = false;
    bool checkUpdates = true;
    float uiScale = 1.15f;       // View > Interface scale; multiplies the monitor DPI
    float baseDpi = 1.0f;
    float pendingUiScale = 0;
    std::string skippedVersion, lastSeenVersion, whatsNewText;
    updater::Release latestRelease;
    void HandleShortcuts();
    // Pen tool and path point editing (ui_canvas.cpp)
    std::string penId;                       // path being drawn
    std::vector<fim::PathPoint> penPts;      // its points in world px
    bool penDragging = false;
    std::string pathEditId;                  // path whose points are edited (double-click a path)
    int pathSelPt = -1, pathPtDrag = 0;      // pathPtDrag: 1 anchor, 2 in handle, 3 out handle
    void PenInput(ImVec2 world, bool canvasHovered);
    void FinishPen(bool close);
    bool PathEditInput(ImVec2 world, bool canvasHovered);
    void DrawPenOverlay(ImDrawList* dl);
    void DeletePathPoint();

    float leftW = 0, rightW = 0;
    int rightTab = 0;
    int codeLang = 2;
    int codeScope = 0;                 // Code tab: 0 the whole frame with the selected layer marked, 1 the layer only
    std::string codeHoverId;           // layer under the mouse in the Code tab (outlined on the canvas)
    int codeHoverFrame = -10, codeFocusFrame = -10;

    // Animation playback
    AnimClocks animClocks;
    // Own animation presets ("My presets" in Motion): saved in the settings, shareable as .json files
    struct UserPreset { std::string name; AnimFx anim; };
    std::vector<UserPreset> userPresets;
    void DrawUserPresets(Node* n, const std::vector<Node*>& nodes, float W);
    bool ExportPresets(const std::string& path);
    int  ImportPresets(const std::string& path);   // returns how many were added
    std::string animPlayId;
    double animPlayUntil = 0;
    void PlayAnimation(Node* n);
    bool openContextMenu = false;
    bool openMainMenu = false;
    bool scrollLayersToSelection = false;
    std::string renamingId;
    char renameBuf[256] = {};
    std::string editingText;   // node id with inline text editor
    char textBuf[8192] = {};
    bool textEditFocus = false;
    std::string toast;
    bool toastError = false;
    double toastTime = 0;

private:
    // canvas interaction
    enum class Drag { None, Move, Resize, Marquee, Create, Pan, Pending, Rotate };
    Drag m_drag = Drag::None;
    int m_handle = -1;
    ImVec2 m_dragStartWorld, m_dragStartScreen;
    std::map<std::string, ImVec4> m_dragOrig;          // id -> x,y,w,h
    std::map<std::string, float> m_rotOrig;             // id -> rotation at the start of a rotate drag
    ImVec2 m_rotCenter;                                 // world pivot of a rotate drag
    float m_rotStartAngle = 0;
    std::map<std::string, std::vector<std::pair<std::string, ImVec4>>> m_dragOrigChildren;
    std::string m_createdId;
    std::string m_pendingClickId;
    bool m_dragDuplicated = false;
    std::vector<Guide> m_guides;
    bool m_spaceHeld = false;
    std::string m_hoverId;

    // history
    std::vector<std::string> m_undo, m_redo;
    std::string m_snapshot;
    bool m_dirty = false;

    // AI streaming
    std::string m_aiLine;
    bool m_aiApplying = false;
    bool m_aiPendingSend = false;

    void CanvasInput();
    void CanvasOverlay(ImDrawList* dl);
    std::string HitTest(ImVec2 world, bool deep);
    void BeginMove();
    void UpdateMove(ImVec2 world);
    void UpdateResize(ImVec2 world);
    void UpdateRotate(ImVec2 world);
    void ApplySnapping(ImVec2& delta, const std::vector<Node*>& moving, bool resize, int handle);
    void LoadFromJsonText(const std::string& text, const std::string& path);
    void StartTextEdit(Node* n);
    void CommitTextEdit();
    void PasteText(const std::string& text);
    void PasteNodes(const json& nodes);
    std::string m_lastTitle;
};

extern App* g_app;
