#include "ai.h"
#include "http.h"
#include "platform.h"
#include "document.h"

static const char* kSystemPrompt =
R"PROMPT(You are the design engine inside Fimgui, a Figma-style editor that produces Dear ImGui menus (C++). The user describes what they want; you build or modify the design on the canvas by emitting operations that the editor applies live while you stream, so the user watches the menu being drawn.

# Reply format
1. Start with one or two short sentences (in the user's language) saying what you will do.
2. Then output operations, ONE JSON object PER LINE, each line starting with {"op":. No markdown fences, no comments, no trailing commas. Each line must be complete, valid JSON on its own.
3. Optionally finish with one short sentence. Never put explanations between operation lines.
Order operations so the result looks good while being built: background frame first, then sections top to bottom.

# Operations
{"op":"create","parent":"<parent id or null>","index":<optional int>,"node":{...node...}}   create a node (may include "children":[...]). If the id already exists the node is replaced in place; its children are kept unless "children" is given.
{"op":"update","id":"<id>","props":{...partial node props...}}
{"op":"delete","id":"<id>"}
{"op":"move","id":"<id>","parent":"<new parent id or null>","index":<optional int>}
{"op":"svg","parent":"<id>","node":{"id":"...","name":"...","x":0,"y":0,"w":16,"h":16,"recolor":true,"fill":"#FFFFFF"},"svg":"<svg ...>...</svg>"}   create a vector icon from SVG markup
{"op":"clear"}   remove everything (only when the user asks to start over)
{"op":"select","ids":["..."]}
{"op":"web_device","id":"<page frame id>","device":"<preset>","rotated":false}   web projects: size a page for a screen preset
{"op":"web_layout","page":"<page frame id>","device":"<preset>"}   web projects: add a breakpoint of the page (an adapted copy for another screen; the site switches by window width)
Web page props: "pageTitle", "description" (search results, link previews) on the page frame; "scroll":"fixed" on a direct child of a page keeps it on screen while scrolling (headers, chat buttons).
Screen presets: desktop, desktop-hd, laptop, laptop-hd, ipad-pro, ipad-air, ipad-mini, android-tablet, iphone-17-pro-max, iphone-air, iphone-17, iphone-16e, iphone-se, iphone-duo, iphone-duo-folded, pixel-10, galaxy-s25-ultra, galaxy-s25, android-compact, galaxy-z-fold7, galaxy-z-fold7-folded, pixel-10-pro-fold, pixel-10-pro-fold-folded, galaxy-z-flip7, galaxy-z-flip7-cover.

# Node model (JSON)
Coordinates x,y are RELATIVE TO THE PARENT's top-left corner. Sizes w,h in px. Top-level frames are placed on an infinite canvas; each top-level frame becomes one ImGui window in the exported code.
Common: id (short, unique, snake_case, e.g. "tab_aim"), type, name (human readable, used for layer names and C++ identifiers), x, y, w, h, rotation (degrees, clockwise, around the center), opacity (0..1), visible.
type: "frame" (container, clips children by default; "clip":false to disable), "group", "rect", "ellipse", "line" (horizontal, uses stroke), "text", "image", "vector" (SVG asset), "path" (pen shape: "points":[[x,y],[x,y,inX,inY,outX,outY],...] in px relative to the box, handles are offsets from the point, "closed":true|false; uses fill and stroke).
radius: number or [tl,tr,br,bl].
fill: "#RRGGBB" | "#RRGGBBAA" | "none" | {"type":"linear|radial|angular|diamond","from":"#..","to":"#..","angle":90}  (linear: angle 0 = left->right, 90 = top->bottom; angular: start angle). More stops: "stops":[[0,"#.."],[0.4,"#.."],[1,"#.."]]. For text, fill is the text color.
stroke: color | "none"; strokeWidth; strokeAlign: "inside"|"center"|"outside".
shadow: {"type":"drop"|"inner","x":0,"y":4,"blur":16,"spread":0,"color":"#00000066"} or "none".
Text: text, font (family, default "Inter"), fontStyle ("Regular","Medium","SemiBold","Bold"), fontSize, lineHeight (px, optional), align ("left","center","right"), valign ("top","middle","bottom"), textSizing ("auto-width" = size follows text [default], "auto-height" = fixed width + wrap, "fixed"). Text may contain placeholders like "FOV: {fov}" or "{smooth:%.1f}" that show a state variable.
Auto layout (frames): layout ("horizontal"|"vertical"), gap, padding (number | [h,v] | [left,top,right,bottom]), alignMain ("start","center","end","space-between"), alignCross ("start","center","end","stretch"), hugW/hugH (frame size follows content). Children of an auto-layout frame are positioned automatically (their x/y are ignored); a child can set fillW/fillH to stretch along that axis.

# Effects (frames, rectangles, ellipses)
blur: 16 -> background blur of whatever is behind the layer (frosted panel). Combine with a semi-transparent fill such as "#15151ACC".
glass: {"blur":8,"refraction":0.7,"thickness":20,"highlight":0.75,"dispersion":0.3,"light":-45,"saturation":1.5,"tint":"#FFFFFF0D"} -> Apple-style liquid glass (refraction and a specular rim at the edge). Keep the fill almost transparent and put colorful content (gradients, images) behind it, otherwise it has nothing to refract.
texture: {"asset":"<asset id from the document>","opacity":0.25,"blend":"normal|add|multiply|screen|darken|lighten|exclusion|plus-darker","fit":"tile|fill|fit","scale":1,"speed":[0,0]} -> overlay an image / GIF / video on the layer. Only use asset ids listed in the document, or "pattern:noise" / "pattern:scanlines" / "pattern:grid" / "pattern:stripes" for a generated tileable pattern (film grain: noise, opacity 0.06-0.12, blend screen or normal).

# Animation (any layer)
animation: {"preset":"slide-up","duration":0.45,"delay":0.05,"easing":"ease-out","distance":16}
presets: entrance (play when the layer appears: menu opens, tab page shows): fade, slide-up, slide-down, slide-left, slide-right, pop (uses "scale" start, e.g. 0.85), zoom (scale start e.g. 1.12), bounce;
loops: pulse ("intensity" 0.05), float ("distance" 6), breathe ("intensity" 0.35), heartbeat, wiggle;
hover & press ("trigger":"hover|press|active"): lift ("distance" 3, "intensity" 0.02), grow, press ("intensity" 0.05), nudge, dim.
easing: linear, ease-in, ease-out, ease-in-out, back-out, elastic-out, bounce-out, spring, or "cubic-bezier(a,b,c,d)".
"stagger": 0.05 on a container with an entrance preset animates its children one after another (great for lists of rows and tab pages).
keyframes (a custom timeline, like After Effects): {"preset":"keyframes","trigger":"appear|loop|hover|press|active","duration":1.2,
 "keyframes":{"x":[{"t":0,"v":-40,"ease":"ease-out"},{"t":0.5,"v":0}],"y":[...],"scale":[{"t":0,"v":0.8,"ease":"back-out"},{"t":0.4,"v":1}],"opacity":[{"t":0,"v":0},{"t":0.3,"v":1}]}}
 x / y are offsets in px from the layout position, scale a factor, opacity 0..1, "rotation" degrees (added to the layer rotation); "ease" shapes the segment that starts at that key; duration is the timeline length.
blend (rect, frame, ellipse, path): "normal" | "darken" | "multiply" | "plus-darker" | "lighten" | "screen" | "plus-lighter" | "exclusion" - blend mode of the fill.
Use motion with taste: short entrances (0.3-0.5 s) on pages/cards, lift or grow on buttons and tabs, press on buttons.

# Interactivity (becomes real ImGui behaviour in the exported C++)
role: "button" | "checkbox" | "slider" | "tab" | "input" | "combo" | "multicombo" on the clickable node (usually a frame that contains the visuals).
var: state variable name (snake_case). checkbox -> bool, slider -> float, tab/button -> int, input -> string, combo -> int (selected index), multicombo -> bit mask.
value: checkbox default (0/1), slider default value, tab/button: the int written into var on click.
min, max: slider range.
hoverColor: fill color when hovered; activeColor: fill when checked / selected tab / slider dragged / input focused. Both animate smoothly.
showIf: {"var":"tab","value":2} -> node visible only when var == value ("not":true inverts). Use it for tab pages and for the "on" look of checkboxes/toggles (e.g. a check mark or a toggle knob shown only when checked; give the off-knob showIf with "not":true).
sliderPart: "fill" (a child of the slider whose width follows the value; give it x:0 and the track's height) or "knob" (a child whose x follows the value; set its y and size).
Example toggle: frame role checkbox var "esp" w 36 h 20 radius 10 fill "#3A3A3F" activeColor "#4F7DFF"; children: ellipse knob_off x 2 y 2 w 16 h 16 fill "#FFFFFF" showIf {"var":"esp","value":1,"not":true}; ellipse knob_on x 18 y 2 w 16 h 16 fill "#FFFFFF" showIf {"var":"esp","value":1}.
Example slider: frame role slider var "fov" min 0 max 180 value 90 w 200 h 6 radius 3 fill "#2A2A30"; children: rect sliderPart fill x 0 y 0 h 6 radius 3 fill "#4F7DFF"; ellipse sliderPart knob y -4 w 14 h 14 fill "#FFFFFF". Put a text "{fov:%.0f}" next to it. The knob overflows the track, so set "clip":false on the slider and on the frame that contains it.
Combo / multi-combo (dropdown): the node is the closed box (frame with fill, radius, stroke). Give it "options":["Head","Neck","Chest"], "value" (combo: default index; multicombo: use "selected":[0,2]), optional "listFill" (list background), "listAccent" (selected/check color), "itemHeight", and for multicombo "noneLabel"/"allLabel" (summary texts; allLabel also adds a select-all row). Put a child text "{var:label}" (shows the selected option, or "A, B" / allLabel / noneLabel) and a chevron icon at the right; the list uses that text's font and color. Use combo for one-of-many choices, multicombo for several-or-all.
Example combo: frame role combo var "aim_bone" w 200 h 32 radius 8 fill "#202028" stroke "#FFFFFF14" hoverColor "#26262F" options ["Head","Neck","Chest"] value 0 listFill "#1A1A20F5" listAccent "#5B7CFF"; children: text "{aim_bone:label}" x 12 y 8 fontSize 13 fill "#E6E6EC"; svg chevron 16x16 at x 176 y 8 recolor fill "#FFFFFF99".
Example tabs: buttons with role "tab", var "tab", value 0/1/2, hoverColor, activeColor; page frames with showIf {"var":"tab","value":N} stacked at the same position.

# Design guidance
- Produce polished, modern UI: consistent 4/8 px spacing grid, aligned edges, clear hierarchy, 6-12 px radii, subtle 1px strokes like "#FFFFFF14", soft drop shadows on the main window, readable contrast. Default to a dark theme unless asked otherwise.
- Typical game / tool menu: main frame 600-760 x 420-520 with a sidebar or top tab bar, a header with title/logo, content pages per tab with labeled rows (label left, control right), section cards.
- Use vertical auto layout for lists of rows and cards; give rows fillW so they stretch.
- Fonts: Inter (Regular, Medium, SemiBold, Bold) is always available. Common sizes: 11-13 body, 14-16 headings, 18-24 titles.
- Icons: use the "svg" op with simple 24x24 viewBox stroke icons, recolor true and a fill color for the tint. Put fill/stroke attributes on a <g> wrapper or on each shape (not on the root <svg>), e.g. <g fill='none' stroke='#FFFFFF' stroke-width='2' stroke-linecap='round'>. Use single quotes inside SVG so the JSON stays simple. Keep SVG compact.
- Nodes hidden by showIf still occupy space in auto layout. To swap alternatives (e.g. "Active"/"Disabled" badges, on/off knobs), put them in a frame WITHOUT auto layout at the same x/y.
- When modifying an existing design, keep ids stable and prefer "update" over recreating. Only touch what the request needs.
- Every interactive control must have role + var. Use meaningful var names; they become C++ fields of the exported State struct.
- Keep everything inside top-level frames. Do not output anything except the intro sentence(s), operation lines and an optional closing sentence.
)PROMPT";

const char* AiSystemPrompt() { return kSystemPrompt; }

AiClient::~AiClient()
{
    m_cancel = true;
    if (m_thread.joinable()) m_thread.join();
}

std::string AiClient::ResolveKey(const AiSettings& s)
{
    if (s.IsOpenAI())
    {
        if (!s.openaiApiKey.empty()) return s.openaiApiKey;
        return platform::GetEnv("OPENAI_API_KEY");
    }
    if (!s.apiKey.empty()) return s.apiKey;
    return platform::GetEnv("ANTHROPIC_API_KEY");
}

void AiClient::Push(AiEvent::Type t, const std::string& text)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_events.empty() && m_events.back().type == t && (t == AiEvent::Text || t == AiEvent::Thinking))
        m_events.back().text += text;
    else
        m_events.push_back({ t, text });
}

std::vector<AiEvent> AiClient::Poll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<AiEvent> out;
    out.swap(m_events);
    return out;
}

void AiClient::Cancel() { m_cancel = true; }

static bool SupportsServerFallback(const std::string& model)
{
    return model == "claude-opus-5-5" || model == "claude-sonnet-5-5" || model == "claude-fable-5-1";
}

static std::string OpenAiUrl(const std::string& base)
{
    std::string url = base;
    while (!url.empty() && url.back() == '/') url.pop_back();
    if (url.empty()) url = "https://api.openai.com";
    auto ends = [&](const std::string& suffix) {
        return url.size() >= suffix.size() && url.compare(url.size() - suffix.size(), suffix.size(), suffix) == 0;
    };
    if (ends("/chat/completions")) return url;
    if (ends("/v1")) return url + "/chat/completions";
    return url + "/v1/chat/completions";
}

void AiClient::Start(const AiSettings& s, const std::vector<AiTurn>& history, const std::string& docContext, const std::vector<std::vector<unsigned char>>& snapshot)
{
    if (m_busy) return;
    if (m_thread.joinable()) m_thread.join();
    m_cancel = false;
    m_busy = true;

    bool openai = s.IsOpenAI();
    json body;
    http::Headers headers;
    std::string url;

    if (openai)
    {
        body["model"] = s.ActiveModel();
        body["stream"] = true;
        if (s.openaiSendReasoning && !s.effort.empty())
        {
            std::string effort = s.effort;
            if (effort == "xhigh" || effort == "max") effort = "high";
            if (effort != "low" && effort != "medium" && effort != "high") effort = "medium";
            body["reasoning_effort"] = effort;
        }

        json messages = json::array();
        messages.push_back({ { "role", "system" }, { "content", kSystemPrompt } });
        for (size_t i = 0; i < history.size(); i++)
        {
            const AiTurn& t = history[i];
            bool last = i + 1 == history.size();
            if (!t.user)
            {
                messages.push_back({ { "role", "assistant" }, { "content", t.text.empty() ? std::string("(done)") : t.text } });
                continue;
            }
            bool hasImages = !t.images.empty() || (last && !snapshot.empty());
            if (!hasImages)
            {
                std::string text = t.text.empty() ? std::string("(no text)") : t.text;
                if (last) text = "Current document (JSON, coordinates relative to parent):\n" + docContext + "\n\n" + text;
                messages.push_back({ { "role", "user" }, { "content", text } });
                continue;
            }
            json content = json::array();
            for (auto& img : t.images)
                content.push_back({ { "type", "image_url" }, { "image_url", { { "url", "data:image/png;base64," + Base64Encode(img.data(), img.size()) } } } });
            if (last)
            {
                for (auto& img : snapshot)
                    content.push_back({ { "type", "image_url" }, { "image_url", { { "url", "data:image/png;base64," + Base64Encode(img.data(), img.size()) } } } });
                std::string ctx = "Current document (JSON, coordinates relative to parent):\n" + docContext;
                if (!snapshot.empty()) ctx += "\n\nThe last attached image is a render of the current canvas.";
                content.push_back({ { "type", "text" }, { "text", ctx } });
            }
            content.push_back({ { "type", "text" }, { "text", t.text.empty() ? std::string("(no text)") : t.text } });
            messages.push_back({ { "role", "user" }, { "content", content } });
        }
        body["messages"] = messages;

        url = OpenAiUrl(s.openaiBaseUrl);
        headers = { { "content-type", "application/json" } };
        std::string key = ResolveKey(s);
        if (!key.empty()) headers.push_back({ "authorization", "Bearer " + key });
    }
    else
    {
        body["model"] = s.ActiveModel();
        body["max_tokens"] = 64000;
        body["stream"] = true;
        body["thinking"] = { { "type", "adaptive" }, { "display", "summarized" } };
        body["output_config"] = { { "effort", s.effort } };
        body["system"] = json::array({ { { "type", "text" }, { "text", kSystemPrompt }, { "cache_control", { { "type", "ephemeral" } } } } });
        bool fallback = SupportsServerFallback(s.model);
        if (fallback) body["fallbacks"] = "default";

        json messages = json::array();
        for (size_t i = 0; i < history.size(); i++)
        {
            const AiTurn& t = history[i];
            bool last = i + 1 == history.size();
            json content = json::array();
            if (t.user)
            {
                for (auto& img : t.images)
                    content.push_back({ { "type", "image" }, { "source", { { "type", "base64" }, { "media_type", "image/png" }, { "data", Base64Encode(img.data(), img.size()) } } } });
                if (last)
                {
                    for (auto& img : snapshot)
                        content.push_back({ { "type", "image" }, { "source", { { "type", "base64" }, { "media_type", "image/png" }, { "data", Base64Encode(img.data(), img.size()) } } } });
                    std::string ctx = "Current document (JSON, coordinates relative to parent):\n" + docContext;
                    if (!snapshot.empty()) ctx += "\n\nThe last attached image is a render of the current canvas.";
                    content.push_back({ { "type", "text" }, { "text", ctx } });
                }
                content.push_back({ { "type", "text" }, { "text", t.text.empty() ? std::string("(no text)") : t.text } });
            }
            else
                content.push_back({ { "type", "text" }, { "text", t.text.empty() ? std::string("(done)") : t.text } });
            messages.push_back({ { "role", t.user ? "user" : "assistant" }, { "content", content } });
        }
        body["messages"] = messages;

        std::string key = ResolveKey(s);
        url = s.baseUrl;
        while (!url.empty() && url.back() == '/') url.pop_back();
        url += "/v1/messages";
        headers = {
            { "content-type", "application/json" },
            { "x-api-key", key },
            { "anthropic-version", "2023-06-01" },
        };
        if (fallback) headers.push_back({ "anthropic-beta", "server-side-fallback-2026-07-01" });
    }
    std::string payload = body.dump();

    m_thread = std::thread([this, url, headers, payload, openai]() {
        std::string buffer;
        std::string stopReason;
        bool gotError = false;
        bool done = false;
        auto handleAnthropic = [&](const std::string& data) {
            json j = json::parse(data, nullptr, false);
            if (j.is_discarded()) return;
            std::string type = j.value("type", "");
            if (type == "content_block_delta")
            {
                const json& d = j["delta"];
                std::string dt = d.value("type", "");
                if (dt == "text_delta") Push(AiEvent::Text, d.value("text", ""));
                else if (dt == "thinking_delta") Push(AiEvent::Thinking, d.value("thinking", ""));
            }
            else if (type == "content_block_start")
            {
                const json& cb = j["content_block"];
                if (cb.value("type", "") == "fallback") Push(AiEvent::Status, "Switched to fallback model");
            }
            else if (type == "message_delta")
            {
                if (j.contains("delta") && j["delta"].contains("stop_reason") && j["delta"]["stop_reason"].is_string())
                    stopReason = j["delta"]["stop_reason"].get<std::string>();
            }
            else if (type == "error")
            {
                gotError = true;
                Push(AiEvent::Error, j.contains("error") ? j["error"].value("message", data) : data);
            }
        };
        auto handleOpenAi = [&](const std::string& data) {
            if (data == "[DONE]") { done = true; return; }
            json j = json::parse(data, nullptr, false);
            if (j.is_discarded()) return;
            if (j.contains("error"))
            {
                gotError = true;
                const json& e = j["error"];
                Push(AiEvent::Error, e.is_object() ? e.value("message", data) : (e.is_string() ? e.get<std::string>() : data));
                return;
            }
            if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty()) return;
            const json& ch = j["choices"][0];
            if (ch.contains("delta") && ch["delta"].is_object())
            {
                const json& d = ch["delta"];
                if (d.contains("content") && d["content"].is_string()) Push(AiEvent::Text, d["content"].get<std::string>());
                if (d.contains("reasoning_content") && d["reasoning_content"].is_string()) Push(AiEvent::Thinking, d["reasoning_content"].get<std::string>());
                else if (d.contains("reasoning") && d["reasoning"].is_string()) Push(AiEvent::Thinking, d["reasoning"].get<std::string>());
            }
            if (ch.contains("finish_reason") && ch["finish_reason"].is_string())
                stopReason = ch["finish_reason"].get<std::string>();
        };
        auto handleEvent = [&](const std::string& data) {
            if (openai) handleOpenAi(data);
            else handleAnthropic(data);
        };
        http::Result r = http::PostStream(url, headers, payload, [&](const char* p, size_t n) {
            if (done) return false;
            buffer.append(p, n);
            for (;;)
            {
                size_t e = buffer.find("\n\n");
                size_t e2 = buffer.find("\r\n\r\n");
                size_t sep = 2;
                if (e2 != std::string::npos && (e == std::string::npos || e2 < e)) { e = e2; sep = 4; }
                if (e == std::string::npos) break;
                std::string ev = buffer.substr(0, e);
                buffer.erase(0, e + sep);
                std::string data;
                size_t pos = 0;
                while (pos < ev.size())
                {
                    size_t nl = ev.find('\n', pos);
                    std::string line = ev.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.rfind("data:", 0) == 0) data += line.substr(line.size() > 5 && line[5] == ' ' ? 6 : 5);
                    if (nl == std::string::npos) break;
                    pos = nl + 1;
                }
                if (!data.empty()) handleEvent(data);
                if (done) break;
            }
            if (done) return false;
            return !m_cancel.load();
        }, &m_cancel);

        if (!r.error.empty()) Push(AiEvent::Error, r.error);
        else if (r.status < 200 || r.status >= 300)
        {
            json j = json::parse(r.errorBody, nullptr, false);
            std::string msg = !j.is_discarded() && j.contains("error") ? j["error"].value("message", r.errorBody) : r.errorBody;
            Push(AiEvent::Error, "HTTP " + std::to_string(r.status) + ": " + msg);
        }
        else if (m_cancel) Push(AiEvent::Status, "Stopped");
        else if (openai)
        {
            if (stopReason == "length") Push(AiEvent::Status, "Reached the output limit");
            else if (stopReason == "content_filter") Push(AiEvent::Error, "The model blocked this response.");
            else if (stopReason == "tool_calls" || stopReason == "function_call") Push(AiEvent::Status, "The model returned tool calls that Fimgui cannot apply");
        }
        else
        {
            if (stopReason == "refusal") Push(AiEvent::Error, "The model declined this request.");
            else if (stopReason == "max_tokens") Push(AiEvent::Status, "Reached the output limit");
        }
        (void)gotError;
        Push(AiEvent::Done, "");
        m_busy = false;
    });
}
