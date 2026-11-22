package __PACKAGE__.fimgui;

/**
 * Drawing backend for generated Fimgui menus. The generated screen implements it for its mod loader.
 * All coordinates are design pixels (the same numbers as in the Fimgui editor). Colors are ARGB.
 * Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
 */
public interface FimGfx {
    void push();
    void pop();
    void translate(float x, float y);
    void scale(float s);
    /** Turns everything drawn after it (until pop) by degrees, clockwise, around the current origin. */
    void rotate(float degrees);
    /** Solid rectangle. */
    void fill(float x0, float y0, float x1, float y1, int argb);
    /** Whole texture (assets/MODID/PATH, texW x texH pixels) stretched over the rect. */
    void texture(String path, int texW, int texH, float x0, float y0, float x1, float y1, float alpha);
    /** Text with its top-left at (x, y); size is the design font size in px; font is a font id or null for the vanilla font. */
    void text(String s, float x, float y, float size, int argb, String font);
    float textWidth(String s, float size, String font);
    /** Clips everything drawn until endScissor() (nests). */
    void scissor(float x0, float y0, float x1, float y1);
    void endScissor();
}
