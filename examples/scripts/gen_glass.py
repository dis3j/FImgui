import json, sys

ops = []
def create(parent, node): ops.append({"op": "create", "parent": parent, "node": node})

W, H = 760, 480
create(None, {"id": "glass_menu", "type": "frame", "name": "Glass Menu", "x": 0, "y": 0, "w": W, "h": H, "radius": 28,
              "fill": {"type": "linear", "from": "#1B1033", "to": "#0B1A2E", "angle": 60},
              "shadow": {"type": "drop", "x": 0, "y": 24, "blur": 48, "color": "#00000099"}})
# Colorful blobs behind the glass
blobs = [("blob_a", 40, 40, 300, 300, "#FF4D8D", "#FF9F43"), ("blob_b", 430, 210, 320, 320, "#3D7BFF", "#24D1FF"),
         ("blob_c", 260, 300, 220, 220, "#8B5CFF", "#FF5CE1"), ("blob_d", 560, -60, 220, 220, "#22E3A2", "#2BB0FF")]
for i, (bid, x, y, w, h, c0, c1) in enumerate(blobs):
    create("glass_menu", {"id": bid, "type": "ellipse", "name": "Blob", "x": x, "y": y, "w": w, "h": h,
                          "fill": {"type": "linear", "from": c0, "to": c1, "angle": 45},
                          "animation": {"preset": "float", "duration": 4 + i, "distance": 14 + 4 * i}})
create("glass_menu", {"id": "grain", "type": "rect", "name": "Grain", "x": 0, "y": 0, "w": W, "h": H, "fill": "none",
                      "texture": {"asset": "pattern:noise", "opacity": 0.07, "blend": "screen", "fit": "tile", "scale": 1}})

# Frosted sidebar
create("glass_menu", {"id": "side", "type": "frame", "name": "Sidebar", "x": 20, "y": 20, "w": 190, "h": H - 40, "radius": 20,
                      "fill": "#0E0E1A66", "stroke": "#FFFFFF1F", "blur": 28, "layout": "vertical", "gap": 6, "padding": [14, 18, 14, 18],
                      "animation": {"preset": "slide-right", "duration": 0.5, "distance": 24, "easing": "ease-out"}})
create("side", {"id": "side_title", "type": "text", "name": "Title", "text": "AURORA", "fontStyle": "Bold", "fontSize": 18, "fill": "#FFFFFF"})
create("side", {"id": "side_sub", "type": "text", "name": "Subtitle", "text": "glass edition", "fontSize": 11, "fill": "#FFFFFF99"})
create("side", {"id": "side_gap", "type": "rect", "name": "Spacer", "w": 10, "h": 14, "fill": "none"})
for i, label in enumerate(["Overview", "Visuals", "Audio", "Settings"]):
    tid = "stab_" + str(i)
    create("side", {"id": tid, "type": "frame", "name": "Tab " + label, "w": 162, "h": 38, "radius": 12, "fill": "#FFFFFF00", "fillW": True,
                    "role": "tab", "var": "tab", "value": i, "hoverColor": "#FFFFFF14", "activeColor": "#FFFFFF2E",
                    "animation": {"preset": "nudge", "trigger": "hover", "duration": 0.18, "distance": 4},
                    "children": [{"id": tid + "_t", "type": "text", "name": "Label", "x": 14, "y": 11, "text": label, "fontStyle": "Medium", "fontSize": 13, "fill": "#FFFFFF"}]})

# Liquid glass card
create("glass_menu", {"id": "card", "type": "frame", "name": "Glass card", "x": 240, "y": 40, "w": 480, "h": 250, "radius": 30,
                      "fill": "#FFFFFF08", "glass": {"blur": 6, "refraction": 0.75, "thickness": 24, "highlight": 0.8, "dispersion": 0.35, "light": -50, "saturation": 1.6, "tint": "#FFFFFF08"},
                      "layout": "vertical", "gap": 14, "padding": 26, "clip": False,
                      "animation": {"preset": "pop", "duration": 0.55, "delay": 0.1, "scale": 0.9, "easing": "back-out"}})
create("card", {"id": "card_title", "type": "text", "name": "Heading", "text": "Liquid Glass", "fontStyle": "SemiBold", "fontSize": 26, "fill": "#FFFFFF"})
create("card", {"id": "card_body", "type": "text", "name": "Body", "w": 420, "textSizing": "auto-height", "fillW": True,
                "text": "Background blur, refraction at the rim and a specular edge, rendered live by fimgui_fx in the editor and in the exported ImGui code.",
                "fontSize": 13, "lineHeight": 20, "fill": "#FFFFFFCC"})
create("card", {"id": "card_btns", "type": "frame", "name": "Buttons", "w": 420, "h": 44, "fill": "none", "fillW": True, "layout": "horizontal", "gap": 10,
                "animation": {"preset": "slide-up", "duration": 0.4, "stagger": 0.08, "distance": 12}})
create("card_btns", {"id": "btn_play", "type": "frame", "name": "Play", "w": 130, "h": 44, "radius": 22, "fill": "#FFFFFFE6", "role": "button",
                     "hoverColor": "#FFFFFF", "layout": "horizontal", "alignMain": "center", "alignCross": "center",
                     "children": [{"id": "btn_play_t", "type": "text", "name": "Text", "text": "Play", "fontStyle": "SemiBold", "fontSize": 14, "fill": "#14141F"}]})
create("card_btns", {"id": "btn_more", "type": "frame", "name": "More", "w": 130, "h": 44, "radius": 22, "fill": "#FFFFFF1A", "stroke": "#FFFFFF33", "role": "button",
                     "hoverColor": "#FFFFFF2A", "layout": "horizontal", "alignMain": "center", "alignCross": "center",
                     "children": [{"id": "btn_more_t", "type": "text", "name": "Text", "text": "Details", "fontStyle": "Medium", "fontSize": 14, "fill": "#FFFFFF"}]})

# Small glass pills
create("glass_menu", {"id": "pill_row", "type": "frame", "name": "Pills", "x": 240, "y": 316, "w": 480, "h": 56, "fill": "none", "layout": "horizontal", "gap": 12, "clip": False,
                      "animation": {"preset": "slide-up", "duration": 0.45, "delay": 0.25, "stagger": 0.07, "distance": 16}})
for i, (label, c) in enumerate([("HDR", "#FFFFFF"), ("Bloom", "#FFFFFF"), ("Motion blur", "#FFFFFF")]):
    create("pill_row", {"id": "pill_" + str(i), "type": "frame", "name": label, "w": 140, "h": 56, "radius": 28, "fill": "#FFFFFF06",
                        "glass": {"blur": 4, "refraction": 0.8, "thickness": 18, "highlight": 0.85, "dispersion": 0.4, "saturation": 1.5, "tint": "#FFFFFF05"},
                        "layout": "horizontal", "alignMain": "center", "alignCross": "center", "role": "button", "hoverColor": "#FFFFFF14",
                        "animation": {"preset": "lift", "trigger": "hover", "duration": 0.18, "distance": 3, "intensity": 0.03},
                        "children": [{"id": "pill_t" + str(i), "type": "text", "name": "Text", "text": label, "fontStyle": "Medium", "fontSize": 14, "fill": c}]})
# Pulsing status dot
create("glass_menu", {"id": "status", "type": "ellipse", "name": "Status", "x": 700, "y": 410, "w": 18, "h": 18, "fill": "#4ADE80",
                      "animation": {"preset": "pulse", "duration": 1.2, "intensity": 0.25}})
create("glass_menu", {"id": "status_t", "type": "text", "name": "Status text", "x": 620, "y": 411, "text": "Online", "fontStyle": "Medium", "fontSize": 12, "fill": "#FFFFFFB3"})

with open(sys.argv[1], "w", encoding="utf-8", newline="\n") as f:
    for o in ops:
        f.write(json.dumps(o, ensure_ascii=False, separators=(",", ":")) + "\n")
print(len(ops), "ops")
