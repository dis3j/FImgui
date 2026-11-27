#pragma once
#include "codegen.h"
#include <map>
#include <memory>
#include <utility>

// Web export: every visible top-level frame is an HTML page (the first one is index.html) with a shared
// stylesheet, the Fimgui web runtime (state, interactions, "Show when", fitting) and the design's media and fonts.
struct WebOptions
{
    std::string outDir;
    std::string title;          // site name, empty = the document name
    std::string lang = "en";
    bool fitWidth = true;       // pages wider than the window are scaled down to fit
    bool fonts = true;          // ship the design fonts (@font-face)
    bool stretchPhones = true;  // layouts narrower than 600 px follow the screen width (scaled up to 125%)
};

// One screen size of a page: the page frame itself or a frame with layoutOf = the page.
// The browser shows it while the window is minW..maxW px wide (maxW 0 = no upper limit).
struct WebLayout
{
    Node* frame = nullptr;
    float minW = 0, maxW = 0;
};

// The page a frame belongs to: itself for a page, the page of a layout, nullptr when it is no page frame.
Node* WebLayoutBase(Document& doc, Node* frame);
// The layouts of the page of 'frame', narrowest first (just the page when it has none).
std::vector<WebLayout> WebPageLayouts(Document& doc, Node* frame);

struct WebSite
{
    using Bytes = std::shared_ptr<const std::string>;
    std::map<std::string, Bytes> files;                       // site path ("index.html", "css/site.css") -> content
    std::vector<std::pair<std::string, std::string>> pages;   // frame id -> html file, in page order
    std::vector<std::string> warnings;
};

// liveReload: pages poll /__fimgui/live of the preview server and reload / follow the edited page.
WebSite BuildWebSite(Document& doc, const WebOptions& opt, const std::string& runtimeDir, bool liveReload = false);
ExportResult ExportWeb(Document& doc, const WebOptions& opt, const std::string& runtimeDir);
// The html file of a page frame ("index.html", "pricing.html"), "" when the node is not a page.
std::string WebPageFile(Document& doc, const Node* frame);
// Markup and CSS of one layer (Code tab).
std::string NodeToHTML(Document& doc, Node* n);
