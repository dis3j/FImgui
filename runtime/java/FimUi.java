package __PACKAGE__.fimgui;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

/**
 * Immediate-mode input for generated Fimgui menus: items (buttons, tabs, checkboxes), sliders, dropdowns, text fields
 * and smooth hover / active transitions. The screen feeds mouse and keyboard events, the menu calls the widgets while drawing.
 * Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
 */
public final class FimUi {
    // ---- input (design coordinates), set by the screen
    public double mx = -1e9, my = -1e9;
    public boolean down, pressed, released;
    public float wheel;
    public int frame;
    public double time;
    public float dt = 1 / 60f;
    private final StringBuilder typed = new StringBuilder();
    private int backspaces;
    private boolean enter, escape;

    private String activeId;            // item being pressed / dragged
    private String focusId;             // focused text field
    private String openCombo;           // id of the open dropdown
    private final Map<String, Float> anim = new HashMap<>();
    private final List<Runnable> overlay = new ArrayList<>();   // drawn after the menu (dropdown lists)
    private boolean overlayHovered;

    public static final class Item { public boolean hovered, held, clicked; }

    public void charTyped(char c) { if (c >= 32) typed.append(c); }
    public void keyBackspace() { backspaces++; }
    public void keyEnter() { enter = true; }
    public void keyEscape() { escape = true; }
    /** True while a text field has focus (the screen should not close on E / inventory keys). */
    public boolean wantsKeyboard() { return focusId != null; }

    public void beginFrame(double mouseX, double mouseY, double now) {
        dt = frame == 0 ? 1 / 60f : (float) Math.min(0.1, Math.max(0.0, now - time));
        time = now;
        mx = mouseX; my = mouseY;
        overlayHovered = false;
    }

    /** Draws queued overlays (dropdown lists) and resets per-frame input. */
    public void endFrame() {
        for (Runnable r : overlay) r.run();
        overlay.clear();
        if (released) activeId = null;
        if (pressed && focusId != null && !focusClaimed) focusId = null;
        focusClaimed = false;
        pressed = released = false;
        wheel = 0;
        typed.setLength(0);
        backspaces = 0;
        enter = escape = false;
        frame++;
    }
    private boolean focusClaimed;

    public boolean inside(float x0, float y0, float x1, float y1) { return mx >= x0 && mx < x1 && my >= y0 && my < y1 && !overlayBlocks(); }
    private boolean overlayBlocks() { return overlayHovered; }

    public Item item(String id, float x0, float y0, float x1, float y1) {
        Item s = new Item();
        boolean in = inside(x0, y0, x1, y1) && (openCombo == null || openCombo.equals(id));
        if (in && pressed && activeId == null) activeId = id;
        s.hovered = in && (activeId == null || activeId.equals(id));
        s.held = id.equals(activeId) && down;
        s.clicked = id.equals(activeId) && released && in;
        return s;
    }

    public float slider(String id, float x0, float y0, float x1, float y1, float v, float vmin, float vmax, Item out) {
        Item s = item(id, x0, y0, x1, y1);
        if (id.equals(activeId) && down && x1 > x0) {
            float t = (float) Math.max(0, Math.min(1, (mx - x0) / (x1 - x0)));
            v = vmin + (vmax - vmin) * t;
            s.held = true;
        }
        if (out != null) { out.hovered = s.hovered; out.held = s.held; out.clicked = s.clicked; }
        return v;
    }

    /** Smoothly moves towards target (exponential, like fim::Animate). */
    public float animate(String id, float target, float speed) {
        Float cur = anim.get(id);
        float c = cur == null ? target : cur;
        c += (target - c) * (1 - (float) Math.exp(-speed * dt));
        if (Math.abs(target - c) < 1e-3f) c = target;
        anim.put(id, c);
        return c;
    }

    // ---- text field

    /** Edits value while focused (click to focus, Enter / Escape / click outside to leave). */
    public String input(String id, float x0, float y0, float x1, float y1, String value, Item out) {
        Item s = item(id, x0, y0, x1, y1);
        if (s.clicked) focusId = id;
        if (id.equals(focusId)) {
            focusClaimed = focusClaimed || inside(x0, y0, x1, y1);
            StringBuilder b = new StringBuilder(value);
            b.append(typed);
            for (int i = 0; i < backspaces && b.length() > 0; i++) b.setLength(b.length() - 1);
            value = b.toString();
            if (enter || escape) focusId = null;
        }
        if (out != null) { out.hovered = s.hovered; out.held = id.equals(focusId); out.clicked = s.clicked; }
        return value;
    }
    public boolean focused(String id) { return id.equals(focusId); }

    // ---- dropdowns

    public static final class ComboStyle {
        public int bg = 0xFA1C1C21, hover = 0x14FFFFFF, accent = 0xFF4F7DFF, text = 0xFFE6E6EE, border = 0x1FFFFFFF;
        public float fontSize = 13, itemHeight = 32, padX = 12, rounding = 8, gap = 4;
        public String font;
        public String allLabel = "All";
    }

    /** Single choice. Returns the new index. */
    public int combo(FimGfx g, String id, float x0, float y0, float x1, float y1, int value, String[] options, ComboStyle st, Item out) {
        final int[] result = { value };
        dropdown(g, id, x0, y0, x1, y1, options, st, out, false, i -> i == value, i -> { result[0] = i; openCombo = null; });
        return result[0];
    }

    /** Multiple choice; value is a bit mask. Returns the new mask. */
    public int multiCombo(FimGfx g, String id, float x0, float y0, float x1, float y1, int mask, String[] options, ComboStyle st, Item out) {
        final int[] result = { mask };
        int all = options.length >= 31 ? -1 : (1 << options.length) - 1;
        dropdown(g, id, x0, y0, x1, y1, options, st, out, true, i -> i < 0 ? (mask & all) == all : (mask & (1 << i)) != 0,
                 i -> result[0] = i < 0 ? ((mask & all) == all ? 0 : all) : (mask ^ (1 << i)));
        return result[0];
    }

    private interface IntPred { boolean test(int i); }
    private interface IntAct { void run(int i); }

    private void dropdown(FimGfx g, String id, float x0, float y0, float x1, float y1, String[] options, ComboStyle st, Item out, boolean multi, IntPred selected, IntAct pick) {
        boolean open = id.equals(openCombo);
        int rows = options.length + (multi && st.allLabel != null && !st.allLabel.isEmpty() ? 1 : 0);
        float lx0 = x0, lx1 = x1, ly0 = y1 + st.gap, ly1 = ly0 + rows * st.itemHeight + 8;
        if (open) overlayHovered = mx >= lx0 && mx < lx1 && my >= ly0 && my < ly1;
        Item s = item(id, x0, y0, x1, y1);
        if (s.clicked) openCombo = open ? null : id;
        if (open && pressed && !overlayHovered && !(mx >= x0 && mx < x1 && my >= y0 && my < y1)) openCombo = null;
        if (out != null) { out.hovered = s.hovered; out.held = id.equals(openCombo); out.clicked = s.clicked; }
        if (!id.equals(openCombo)) return;
        // pick on press inside the list
        int hoverRow = overlayHovered ? (int) ((my - ly0 - 4) / st.itemHeight) : -1;
        if (hoverRow >= rows) hoverRow = -1;
        if (pressed && hoverRow >= 0) {
            int opt = multi && rows > options.length ? hoverRow - 1 : hoverRow;
            pick.run(opt);
            pressed = false;
        }
        final int hr = hoverRow;
        overlay.add(() -> {
            g.fill(lx0, ly0, lx1, ly1, st.bg);
            g.fill(lx0, ly0, lx1, ly0 + 1, st.border);
            g.fill(lx0, ly1 - 1, lx1, ly1, st.border);
            for (int r = 0; r < rows; r++) {
                int opt = multi && rows > options.length ? r - 1 : r;
                float ry = ly0 + 4 + r * st.itemHeight;
                boolean sel = selected.test(opt);
                if (r == hr) g.fill(lx0 + 4, ry, lx1 - 4, ry + st.itemHeight, st.hover);
                String label = opt < 0 ? st.allLabel : options[opt];
                float tx = lx0 + st.padX;
                if (multi) {
                    float bs = st.fontSize;
                    float by = ry + (st.itemHeight - bs) / 2;
                    g.fill(tx, by, tx + bs, by + bs, sel ? st.accent : 0x33FFFFFF);
                    tx += bs + 8;
                } else if (sel) g.fill(lx0 + 4, ry + 6, lx0 + 6, ry + st.itemHeight - 6, st.accent);
                g.text(label, tx, ry + (st.itemHeight - st.fontSize * 1.2f) / 2, st.fontSize, sel && !multi ? st.accent : st.text, st.font);
            }
        });
    }

    // ---- helpers for generated code

    public static int lerpColor(int a, int b, float t) {
        t = Math.max(0, Math.min(1, t));
        int r = 0;
        for (int sh = 0; sh <= 24; sh += 8) {
            int ca = (a >>> sh) & 255, cb = (b >>> sh) & 255;
            r |= Math.round(ca + (cb - ca) * t) << sh;
        }
        return r;
    }

    /** Multiplies the alpha of an ARGB color. */
    public static int fade(int argb, float alpha) {
        int a = Math.round(((argb >>> 24) & 255) * Math.max(0, Math.min(1, alpha)));
        return (a << 24) | (argb & 0xFFFFFF);
    }

    /** printf-style number formatting for "{var:%.1f}" placeholders ("%d" works with floats too). */
    public static String fmt(String f, float v) {
        if (f == null || f.isEmpty()) return Math.abs(v - Math.round(v)) < 1e-4f ? Integer.toString(Math.round(v)) : String.format(Locale.ROOT, "%.2f", v);
        if (f.contains("d") || f.contains("i")) return String.format(Locale.ROOT, f.replace("i", "d"), Math.round(v));
        return String.format(Locale.ROOT, f, v);
    }

    public static String option(String[] options, int index) { return index >= 0 && index < options.length ? options[index] : ""; }

    public static String options(String[] options, int mask, String none, String all) {
        int count = 0;
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < options.length; i++) if ((mask & (1 << i)) != 0) { if (count++ > 0) b.append(", "); b.append(options[i]); }
        if (count == 0) return none;
        if (count == options.length && all != null && !all.isEmpty()) return all;
        return b.toString();
    }
}
