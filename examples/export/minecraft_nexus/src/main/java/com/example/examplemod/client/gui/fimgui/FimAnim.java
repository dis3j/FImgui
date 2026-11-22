package com.example.examplemod.client.gui.fimgui;

import java.util.HashMap;
import java.util.Map;

/**
 * Animation engine of Fimgui menus, ported from fimgui_runtime.cpp so the Minecraft screen moves exactly like the editor:
 * presets (entrance, loops, hover / press states) and keyframe timelines.
 * Part of Fimgui (c) 2026 Philip, https://github.com/dis3j/FImgui. Free to use in your projects, including commercial ones; see LICENSE-FIMGUI.txt.
 */
public final class FimAnim {
    // presets
    public static final int NONE = 0, FADE = 1, SLIDE_UP = 2, SLIDE_DOWN = 3, SLIDE_LEFT = 4, SLIDE_RIGHT = 5, POP = 6, ZOOM = 7, BOUNCE = 8,
        PULSE = 9, FLOAT = 10, BREATHE = 11, HEARTBEAT = 12, WIGGLE = 13, LIFT = 14, GROW = 15, PRESS = 16, NUDGE = 17, DIM = 18, KEYFRAMES = 19;
    // triggers
    public static final int APPEAR = 0, HOVER = 1, PRESSED = 2, ACTIVE = 3, LOOP = 4;
    // easings
    public static final int LINEAR = 0, EASE_IN = 1, EASE_OUT = 2, EASE_IN_OUT = 3, BACK_OUT = 4, ELASTIC_OUT = 5, BOUNCE_OUT = 6, SPRING = 7, BEZIER = 8;
    // keyframe properties
    public static final int PROP_X = 0, PROP_Y = 1, PROP_SCALE = 2, PROP_OPACITY = 3;

    /** One keyframe track: times (seconds), values and the easing of the segment that starts at each key. */
    public static final class Track {
        public final int prop;
        public final float[] t, v;
        public final int[] e;
        public Track(int prop, float[] t, float[] v, int[] e) { this.prop = prop; this.t = t; this.v = v; this.e = e; }
    }

    public static final class Spec {
        public int preset, trigger, easing = EASE_OUT;
        public float duration = 0.4f, delay, distance = 16, scale = 0.9f, intensity = 0.05f;
        public float[] bezier = { 0.25f, 0.1f, 0.25f, 1.0f };
        public Track[] tracks = new Track[0];
        public Spec(int preset, int trigger, int easing, float duration, float delay, float distance, float scale, float intensity, float[] bezier, Track... tracks) {
            this.preset = preset; this.trigger = trigger; this.easing = easing; this.duration = duration; this.delay = delay;
            this.distance = distance; this.scale = scale; this.intensity = intensity; this.bezier = bezier; this.tracks = tracks;
        }
    }

    public static final class Value {
        public float dx, dy, scale = 1, alpha = 1;
        public boolean isIdentity() { return dx == 0 && dy == 0 && scale == 1 && alpha == 1; }
    }

    private static final float PI = (float) Math.PI;

    private static float sat(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

    public static int category(Spec s) {
        if (s.preset == KEYFRAMES) return s.trigger == LOOP ? 1 : s.trigger == APPEAR ? 0 : 2;
        if (s.preset >= PULSE && s.preset <= WIGGLE) return 1;
        if (s.preset >= LIFT && s.preset <= DIM) return 2;
        return 0;
    }

    private static float bezierCoord(float t, float p1, float p2) { float u = 1 - t; return 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t; }

    private static float bezierEase(float[] b, float x) {
        float lo = 0, hi = 1, t = x;
        for (int i = 0; i < 24; i++) { t = (lo + hi) * 0.5f; if (bezierCoord(t, b[0], b[2]) < x) lo = t; else hi = t; }
        return bezierCoord(t, b[1], b[3]);
    }

    public static float ease(int e, float[] bz, float t) {
        t = sat(t);
        switch (e) {
            case LINEAR: return t;
            case EASE_IN: return t * t * t;
            case EASE_OUT: { float u = 1 - t; return 1 - u * u * u; }
            case EASE_IN_OUT: return t < 0.5f ? 4 * t * t * t : 1 - (float) Math.pow(-2 * t + 2, 3) / 2;
            case BACK_OUT: { float c1 = 1.70158f, c3 = c1 + 1, u = t - 1; return 1 + c3 * u * u * u + c1 * u * u; }
            case ELASTIC_OUT: if (t <= 0 || t >= 1) return t; return (float) (Math.pow(2, -10 * t) * Math.sin((t * 10 - 0.75f) * (2 * PI / 3)) + 1);
            case BOUNCE_OUT: {
                float n1 = 7.5625f, d1 = 2.75f;
                if (t < 1 / d1) return n1 * t * t;
                if (t < 2 / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
                if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
                t -= 2.625f / d1;
                return n1 * t * t + 0.984375f;
            }
            case SPRING: return t >= 1 ? 1 : (float) (1 - Math.exp(-6 * t) * Math.cos(12 * t));
            case BEZIER: return bz != null ? bezierEase(bz, t) : t;
            default: return t;
        }
    }

    public static float trackAt(Track tr, float time, float[] bz) {
        int n = tr.t.length;
        if (n == 0) return tr.prop >= PROP_SCALE ? 1 : 0;
        if (time <= tr.t[0]) return tr.v[0];
        for (int i = 0; i + 1 < n; i++) {
            if (time > tr.t[i + 1]) continue;
            float span = tr.t[i + 1] - tr.t[i];
            float p = span > 1e-5f ? (time - tr.t[i]) / span : 1;
            return tr.v[i] + (tr.v[i + 1] - tr.v[i]) * ease(tr.e[i], bz, p);
        }
        return tr.v[n - 1];
    }

    public static Value keyframesAt(Spec s, float time) {
        Value v = new Value();
        for (Track tr : s.tracks) {
            float x = trackAt(tr, time, s.bezier);
            switch (tr.prop) {
                case PROP_X: v.dx = x; break;
                case PROP_Y: v.dy = x; break;
                case PROP_SCALE: v.scale = x; break;
                case PROP_OPACITY: v.alpha = sat(x); break;
            }
        }
        return v;
    }

    /** time: seconds since the trigger (entrance / loop); progress: 0..1 state progress (hover / press / active). */
    public static Value sample(Spec s, float time, float progress) {
        Value v = new Value();
        if (s.preset == NONE) return v;
        if (s.preset == KEYFRAMES) {
            float len = Math.max(0.01f, s.duration);
            int kc = category(s);
            float t = kc == 2 ? sat(progress) * len : kc == 1 ? (Math.max(time - s.delay, 0) % len) : Math.min(Math.max(time - s.delay, 0), len);
            return keyframesAt(s, t);
        }
        int cat = category(s);
        if (cat == 0) {
            float raw = s.duration > 0 ? sat((time - s.delay) / s.duration) : 1;
            float p = ease(s.easing, s.bezier, raw), q = 1 - p;
            switch (s.preset) {
                case FADE: v.alpha = sat(p); break;
                case SLIDE_UP: v.dy = s.distance * q; v.alpha = sat(raw * 1.6f); break;
                case SLIDE_DOWN: v.dy = -s.distance * q; v.alpha = sat(raw * 1.6f); break;
                case SLIDE_LEFT: v.dx = s.distance * q; v.alpha = sat(raw * 1.6f); break;
                case SLIDE_RIGHT: v.dx = -s.distance * q; v.alpha = sat(raw * 1.6f); break;
                case POP: case ZOOM: v.scale = s.scale + (1 - s.scale) * p; v.alpha = sat(raw * 2); break;
                case BOUNCE: v.dy = -s.distance * q; v.alpha = sat(raw * 3); break;
            }
        } else if (cat == 1) {
            float cycle = Math.max(0.01f, s.duration + s.delay);
            float lt = Math.max(time, 0) % cycle;
            float ph = lt < s.duration ? lt / Math.max(0.01f, s.duration) : 0;
            float w = 0.5f - 0.5f * (float) Math.cos(ph * 2 * PI);
            switch (s.preset) {
                case PULSE: v.scale = 1 + s.intensity * w; break;
                case FLOAT: v.dy = -s.distance * w; break;
                case BREATHE: v.alpha = 1 - s.intensity * w; break;
                case HEARTBEAT: {
                    float b = (float) (Math.exp(-Math.pow((ph - 0.15f) / 0.06f, 2)) + 0.7f * Math.exp(-Math.pow((ph - 0.38f) / 0.07f, 2)));
                    v.scale = 1 + s.intensity * b;
                    break;
                }
                case WIGGLE: v.dx = ph < 0.5f ? s.distance * (float) Math.sin(ph * 2 * PI * 4) * (1 - ph * 2) : 0; break;
            }
        } else {
            float p = ease(s.easing, s.bezier, sat(progress));
            switch (s.preset) {
                case LIFT: v.dy = -s.distance * p; v.scale = 1 + s.intensity * p; break;
                case GROW: v.scale = 1 + s.intensity * p; break;
                case PRESS: v.scale = 1 - s.intensity * p; break;
                case NUDGE: v.dx = s.distance * p; break;
                case DIM: v.alpha = 1 - s.intensity * p; break;
            }
        }
        return v;
    }

    private static final class Clock { int last = -100; double start; float progress; }
    private final Map<String, Clock> clocks = new HashMap<>();

    /** Stateful version: call every frame the element is drawn. An element that was not drawn last frame restarts its entrance. */
    public Value update(String id, Spec s, boolean hovered, boolean held, boolean active, FimUi ui) {
        Clock c = clocks.computeIfAbsent(id, k -> new Clock());
        if (c.last < ui.frame - 1) { c.start = ui.time; c.progress = 0; }
        c.last = ui.frame;
        if (category(s) == 2) {
            boolean on = s.trigger == PRESSED ? held : s.trigger == ACTIVE ? active : hovered;
            float len = s.preset == KEYFRAMES ? Math.max(0.01f, s.duration) : Math.max(0.01f, s.duration);
            float step = ui.dt / len;
            c.progress = on ? Math.min(1, c.progress + step) : Math.max(0, c.progress - step);
            return sample(s, 0, c.progress);
        }
        return sample(s, (float) (ui.time - c.start), 0);
    }

    /** Restarts every entrance animation (call when the screen opens). */
    public void restart() { clocks.clear(); }
}
