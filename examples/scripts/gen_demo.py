import json, sys

ops = []
def op(**kw): ops.append(kw)
def create(parent, node): op(op="create", parent=parent, node=node)
def svg(parent, node, markup): op(op="svg", parent=parent, node=node, svg=markup)

ACCENT = "#5B7CFF"
TXT = "#E6E6EC"
DIM = "#8B8B99"
CARD = "#18181E"

G = "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24' viewBox='0 0 24 24'><g fill='none' stroke='#FFFFFF' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>{}</g></svg>"
ICONS = {
    "aim": G.format("<circle cx='12' cy='12' r='8'/><path d='M12 2v5M12 17v5M2 12h5M17 12h5'/>"),
    "eye": G.format("<path d='M2 12C4.5 7 8 5 12 5s7.5 2 10 7c-2.5 5-6 7-10 7s-7.5-2-10-7z'/><circle cx='12' cy='12' r='3'/>"),
    "misc": G.format("<path d='M4 7h10M18 7h2M4 17h4M12 17h8'/><circle cx='16' cy='7' r='2'/><circle cx='10' cy='17' r='2'/>"),
    "gear": G.format("<circle cx='12' cy='12' r='3'/><circle cx='12' cy='12' r='7.5'/><path d='M12 1.5v3M12 19.5v3M1.5 12h3M19.5 12h3M4.6 4.6l2.1 2.1M17.3 17.3l2.1 2.1M4.6 19.4l2.1-2.1M17.3 6.7l2.1-2.1'/>"),
}

create(None, {"id": "menu", "type": "frame", "name": "Nexus Menu", "x": 0, "y": 0, "w": 720, "h": 480, "radius": 14,
              "fill": "#111115", "stroke": "#FFFFFF12",
              "shadow": {"type": "drop", "x": 0, "y": 18, "blur": 40, "spread": 0, "color": "#00000099"}})

# Sidebar
create("menu", {"id": "sidebar", "type": "frame", "name": "Sidebar", "x": 0, "y": 0, "w": 184, "h": 480, "radius": [14, 0, 0, 14], "fill": "#16161B"})
create("sidebar", {"id": "divider", "type": "rect", "name": "Divider", "x": 183, "y": 0, "w": 1, "h": 480, "fill": "#FFFFFF0D"})
create("sidebar", {"id": "logo_mark", "type": "frame", "name": "Logo", "x": 20, "y": 22, "w": 30, "h": 30, "radius": 8,
                   "fill": {"type": "linear", "from": "#5B7CFF", "to": "#A05BFF", "angle": 45},
                   "layout": "horizontal", "alignMain": "center", "alignCross": "center",
                   "children": [{"id": "logo_letter", "type": "text", "name": "Logo letter", "text": "N", "fontStyle": "Bold", "fontSize": 15, "fill": "#FFFFFF"}]})
create("sidebar", {"id": "logo_title", "type": "text", "name": "Title", "x": 60, "y": 20, "text": "NEXUS", "fontStyle": "Bold", "fontSize": 15, "fill": "#F2F2F5"})
create("sidebar", {"id": "logo_sub", "type": "text", "name": "Version", "x": 60, "y": 39, "text": "build 2.4.1", "fontSize": 11, "fill": DIM})
create("sidebar", {"id": "menu_label", "type": "text", "name": "Section label", "x": 22, "y": 80, "text": "MENU", "fontStyle": "SemiBold", "fontSize": 10, "fill": "#5E5E6E"})
create("sidebar", {"id": "tabs", "type": "frame", "name": "Tabs", "x": 12, "y": 98, "w": 160, "h": 100, "fill": "none",
                   "layout": "vertical", "gap": 4, "hugH": True})
for i, (key, label, icon) in enumerate([("aim", "Aimbot", "aim"), ("vis", "Visuals", "eye"), ("misc", "Misc", "misc"), ("set", "Settings", "gear")]):
    tid = "tab_" + key
    create("tabs", {"id": tid, "type": "frame", "name": "Tab " + label, "w": 160, "h": 38, "radius": 9, "fill": "#FFFFFF00",
                    "role": "tab", "var": "tab", "value": i, "hoverColor": "#FFFFFF0A", "activeColor": "#5B7CFF26", "fillW": True})
    create(tid, {"id": tid + "_bar", "type": "rect", "name": "Indicator", "x": 0, "y": 11, "w": 3, "h": 16, "radius": 2, "fill": ACCENT,
                 "showIf": {"var": "tab", "value": i}})
    svg(tid, {"id": tid + "_icon", "name": "Icon " + icon, "x": 14, "y": 11, "w": 16, "h": 16, "recolor": True, "fill": "#B4B4C3"}, ICONS[icon])
    create(tid, {"id": tid + "_label", "type": "text", "name": "Label", "x": 40, "y": 11, "text": label, "fontStyle": "Medium", "fontSize": 13, "fill": TXT})

create("sidebar", {"id": "user_card", "type": "frame", "name": "User", "x": 12, "y": 420, "w": 160, "h": 46, "radius": 10, "fill": "#1D1D24",
                   "children": [
                       {"id": "avatar", "type": "ellipse", "name": "Avatar", "x": 10, "y": 9, "w": 28, "h": 28, "fill": {"type": "linear", "from": "#F59E0B", "to": "#EF4444", "angle": 45}},
                       {"id": "user_name", "type": "text", "name": "User name", "x": 46, "y": 8, "text": "player_one", "fontStyle": "Medium", "fontSize": 12, "fill": TXT},
                       {"id": "user_plan", "type": "text", "name": "Plan", "x": 46, "y": 25, "text": "Premium · 28 days", "fontSize": 10, "fill": DIM}]})

# Helpers for pages
def page(pid, title, index):
    create("menu", {"id": pid, "type": "frame", "name": "Page " + title, "x": 204, "y": 22, "w": 496, "h": 440, "fill": "none",
                    "layout": "vertical", "gap": 14, "showIf": {"var": "tab", "value": index}})
    create(pid, {"id": pid + "_head", "type": "frame", "name": "Header", "w": 496, "h": 30, "fill": "none", "fillW": True,
                 "layout": "horizontal", "alignMain": "space-between", "alignCross": "center"})
    create(pid + "_head", {"id": pid + "_title", "type": "text", "name": "Title", "text": title, "fontStyle": "SemiBold", "fontSize": 20, "fill": "#F5F5F7"})

def card(parent, cid, title):
    create(parent, {"id": cid, "type": "frame", "name": "Card " + title, "w": 496, "h": 100, "radius": 12, "fill": CARD, "stroke": "#FFFFFF0D",
                    "layout": "vertical", "gap": 14, "padding": 16, "hugH": True, "fillW": True})
    create(cid, {"id": cid + "_title", "type": "text", "name": "Card title", "text": title.upper(), "fontStyle": "SemiBold", "fontSize": 10, "fill": "#6B6B7B"})

def toggle_row(parent, var, label, on):
    rid = "row_" + var
    create(parent, {"id": rid, "type": "frame", "name": label, "w": 464, "h": 22, "fill": "none", "fillW": True,
                    "layout": "horizontal", "alignMain": "space-between", "alignCross": "center"})
    create(rid, {"id": rid + "_label", "type": "text", "name": "Label", "text": label, "fontSize": 13, "fill": TXT})
    create(rid, {"id": "tg_" + var, "type": "frame", "name": "Toggle", "w": 36, "h": 20, "radius": 10, "fill": "#2C2C35",
                 "role": "checkbox", "var": var, "value": 1 if on else 0, "hoverColor": "#363641", "activeColor": ACCENT,
                 "children": [
                     {"id": "tg_" + var + "_off", "type": "ellipse", "name": "Knob off", "x": 2, "y": 2, "w": 16, "h": 16, "fill": "#C9C9D3",
                      "showIf": {"var": var, "value": 1, "not": True}},
                     {"id": "tg_" + var + "_on", "type": "ellipse", "name": "Knob on", "x": 18, "y": 2, "w": 16, "h": 16, "fill": "#FFFFFF",
                      "showIf": {"var": var, "value": 1}}]})

def slider_block(parent, var, label, vmin, vmax, value, fmt):
    bid = "blk_" + var
    create(parent, {"id": bid, "type": "frame", "name": label, "w": 464, "h": 40, "fill": "none", "fillW": True, "layout": "vertical", "gap": 10, "hugH": True, "clip": False})
    create(bid, {"id": bid + "_row", "type": "frame", "name": "Row", "w": 464, "h": 18, "fill": "none", "fillW": True,
                 "layout": "horizontal", "alignMain": "space-between", "alignCross": "center",
                 "children": [
                     {"id": bid + "_label", "type": "text", "name": "Label", "text": label, "fontSize": 13, "fill": TXT},
                     {"id": bid + "_value", "type": "text", "name": "Value", "text": fmt, "fontStyle": "Medium", "fontSize": 12, "fill": "#A9B8FF"}]})
    create(bid, {"id": "sl_" + var, "type": "frame", "name": "Slider", "w": 464, "h": 6, "radius": 3, "fill": "#2A2A33", "clip": False, "fillW": True,
                 "role": "slider", "var": var, "min": vmin, "max": vmax, "value": value,
                 "children": [
                     {"id": "sl_" + var + "_fill", "type": "rect", "name": "Fill", "x": 0, "y": 0, "w": 100, "h": 6, "radius": 3,
                      "fill": {"type": "linear", "from": "#5B7CFF", "to": "#9B6BFF", "angle": 0}, "sliderPart": "fill"},
                     {"id": "sl_" + var + "_knob", "type": "ellipse", "name": "Knob", "x": 0, "y": -5, "w": 16, "h": 16, "fill": "#FFFFFF",
                      "stroke": ACCENT, "strokeWidth": 3, "sliderPart": "knob"}]})

# Aimbot page
page("page_aim", "Aimbot", 0)
create("page_aim_head", {"id": "badge", "type": "frame", "name": "Status badge", "w": 74, "h": 22, "fill": "none", "clip": False})
create("badge", {"id": "badge_on", "type": "frame", "name": "Badge active", "w": 60, "h": 22, "radius": 11, "fill": "#22C55E1F",
                         "x": 14, "layout": "horizontal", "padding": [10, 4], "alignMain": "center", "alignCross": "center", "hugW": True,
                         "showIf": {"var": "aim_enabled", "value": 1},
                         "children": [{"id": "badge_on_t", "type": "text", "name": "Text", "text": "Active", "fontStyle": "SemiBold", "fontSize": 11, "fill": "#4ADE80"}]})
create("badge", {"id": "badge_off", "type": "frame", "name": "Badge disabled", "w": 60, "h": 22, "radius": 11, "fill": "#FFFFFF0F",
                         "layout": "horizontal", "padding": [10, 4], "alignMain": "center", "alignCross": "center", "hugW": True,
                         "showIf": {"var": "aim_enabled", "value": 1, "not": True},
                         "children": [{"id": "badge_off_t", "type": "text", "name": "Text", "text": "Disabled", "fontStyle": "SemiBold", "fontSize": 11, "fill": DIM}]})
card("page_aim", "card_general", "General")
toggle_row("card_general", "aim_enabled", "Enable aimbot", True)
toggle_row("card_general", "aim_visible", "Visibility check", True)
toggle_row("card_general", "aim_autofire", "Auto fire", False)
card("page_aim", "card_precision", "Precision")
slider_block("card_precision", "fov", "Field of view", 1, 180, 90, "{fov:%.0f}°")
slider_block("card_precision", "smooth", "Smoothing", 0, 20, 6, "{smooth:%.1f}")
create("card_precision", {"id": "bone_label", "type": "text", "name": "Bone label", "text": "Target bone", "fontSize": 13, "fill": TXT})
create("card_precision", {"id": "bone_row", "type": "frame", "name": "Bone selector", "w": 464, "h": 32, "fill": "none", "fillW": True, "layout": "horizontal", "gap": 6})
for i, b in enumerate(["Head", "Neck", "Chest"]):
    create("bone_row", {"id": "bone_" + b.lower(), "type": "frame", "name": b, "w": 100, "h": 32, "radius": 8, "fill": "#22222A", "fillW": True,
                        "role": "tab", "var": "bone", "value": i, "hoverColor": "#2B2B34", "activeColor": ACCENT,
                        "layout": "horizontal", "alignMain": "center", "alignCross": "center",
                        "children": [{"id": "bone_" + b.lower() + "_t", "type": "text", "name": "Text", "text": b, "fontStyle": "Medium", "fontSize": 12, "fill": TXT}]})

# Visuals page
page("page_vis", "Visuals", 1)
card("page_vis", "card_esp", "ESP")
toggle_row("card_esp", "box_esp", "Box ESP", True)
toggle_row("card_esp", "skeleton", "Skeleton", False)
toggle_row("card_esp", "health_bar", "Health bar", True)
toggle_row("card_esp", "name_tags", "Name tags", True)
card("page_vis", "card_range", "Range")
slider_block("card_range", "esp_dist", "Max distance", 0, 500, 250, "{esp_dist:%.0f} m")

# Misc page
page("page_misc", "Misc", 2)
card("page_misc", "card_move", "Movement")
toggle_row("card_move", "bhop", "Bunny hop", False)
toggle_row("card_move", "auto_strafe", "Auto strafe", False)
card("page_misc", "card_cfg", "Config")
create("card_cfg", {"id": "cfg_input", "type": "frame", "name": "Config name", "w": 464, "h": 36, "radius": 8, "fill": "#1F1F26", "stroke": "#FFFFFF14",
                    "fillW": True, "role": "input", "var": "cfg_name", "hoverColor": "#23232B", "activeColor": "#24243A",
                    "children": [{"id": "cfg_placeholder", "type": "text", "name": "Placeholder", "x": 12, "y": 10, "text": "Config name, e.g. legit_v2", "fontSize": 13, "fill": "#6B6B7B"}]})

# Settings page
page("page_set", "Settings", 3)
card("page_set", "card_ui", "Interface")
toggle_row("card_ui", "watermark", "Show watermark", True)
toggle_row("card_ui", "streamproof", "Hide from screen capture", False)
create("card_ui", {"id": "btn_row", "type": "frame", "name": "Buttons", "w": 464, "h": 36, "fill": "none", "fillW": True, "layout": "horizontal", "gap": 8})
create("btn_row", {"id": "btn_save", "type": "frame", "name": "Save config", "w": 120, "h": 36, "radius": 8, "fill": ACCENT, "fillW": True,
                   "role": "button", "hoverColor": "#6E8BFF", "activeColor": "#4A68E8", "layout": "horizontal", "alignMain": "center", "alignCross": "center",
                   "children": [{"id": "btn_save_t", "type": "text", "name": "Text", "text": "Save config", "fontStyle": "SemiBold", "fontSize": 13, "fill": "#FFFFFF"}]})
create("btn_row", {"id": "btn_load", "type": "frame", "name": "Load config", "w": 120, "h": 36, "radius": 8, "fill": "#24242C", "fillW": True,
                   "role": "button", "hoverColor": "#2E2E38", "activeColor": "#1E1E25", "layout": "horizontal", "alignMain": "center", "alignCross": "center",
                   "children": [{"id": "btn_load_t", "type": "text", "name": "Text", "text": "Load config", "fontStyle": "Medium", "fontSize": 13, "fill": TXT}]})

with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as f:
    for o in ops:
        f.write(json.dumps(o, ensure_ascii=False, separators=(",", ":")) + "\n")
print(len(ops), "ops")
