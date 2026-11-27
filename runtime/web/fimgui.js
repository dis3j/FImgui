/*
 * Fimgui web runtime: state variables, buttons, tabs, toggles, sliders, text fields, dropdowns,
 * "Show when" and fitting pages into narrow windows, for sites exported by Fimgui.
 * Copyright (c) Fimgui. Use, modify and sell it as part of your own site (see LICENSE-FIMGUI.txt);
 * keep this notice. Fimgui itself may not be sold.
 *
 * From your own scripts:
 *   fimgui.get('volume'); fimgui.set('volume', 40);
 *   document.addEventListener('fim:change', e => console.log(e.detail.name, e.detail.value));
 *   document.addEventListener('fim:press', e => console.log('pressed', e.detail.name));
 */
(() => {
  'use strict';

  const meta = (() => {
    try { return JSON.parse(document.getElementById('fim-state')?.textContent || '{}'); } catch { return {}; }
  })();
  const vars = meta.vars || {};
  const combos = meta.combos || {};
  const storeKey = 'fimgui:' + (meta.site || '');
  const state = {};
  for (const [name, v] of Object.entries(vars)) state[name] = v.def;
  try {
    // The state survives moving between the pages of the site (this tab only).
    const saved = JSON.parse(sessionStorage.getItem(storeKey) || '{}');
    for (const name of Object.keys(saved)) if (name in state) state[name] = saved[name];
  } catch { /* storage blocked */ }

  const isInt = name => vars[name] && vars[name].kind !== 'float' && vars[name].kind !== 'string';

  // ---- text placeholders: {var}, {var:%.1f}, {var:label}
  function printf(fmt, v) {
    return fmt.replace(/%([-+0 #]*)(\d*)(?:\.(\d+))?([dif])/, (m, flags, width, prec, type) => {
      let s = type === 'f' ? Number(v).toFixed(prec === undefined ? 6 : +prec) : String(Math.trunc(v));
      if (flags.includes('+') && v >= 0) s = '+' + s;
      if (width && s.length < +width) s = (flags.includes('0') ? s.padStart(+width, '0') : s.padStart(+width));
      return s;
    });
  }

  function comboLabel(name) {
    const c = combos[name];
    if (!c) return '';
    const v = state[name] >>> 0;
    if (!c.multi) return c.options[v] ?? '';
    const on = c.options.filter((_, i) => i < 32 && (v & (1 << i)));
    if (!on.length) return c.none;
    if (on.length === c.options.length && on.length > 1 && c.all) return c.all;
    return on.join(', ');
  }

  function expand(text) {
    return text.replace(/\{([A-Za-z0-9_]+)(?::([^}]*))?\}/g, (m, name, fmt) => {
      if (fmt === 'label' && combos[name]) return comboLabel(name);
      if (!(name in state)) return m;
      const v = state[name];
      if (typeof v === 'string') return v;
      if (fmt) return printf(fmt, v);
      return Number.isInteger(v) ? String(v) : v.toFixed(2);
    });
  }

  // ---- applying the state to the page
  const near = (a, b) => Math.abs(Number(a) - Number(b)) < 1e-4;

  function apply() {
    for (const el of document.querySelectorAll('[data-show]')) {
      const eq = near(state[el.dataset.show] ?? 0, el.dataset.eq);
      el.hidden = el.hasAttribute('data-not') ? eq : !eq;
    }
    for (const el of document.querySelectorAll('[data-set]')) {
      if (el.getAttribute('role') !== 'tab') continue;
      const on = near(state[el.dataset.set], el.dataset.value);
      el.classList.toggle('is-active', on);
      el.setAttribute('aria-selected', on);
    }
    for (const el of document.querySelectorAll('[data-toggle]')) {
      const on = !!state[el.dataset.toggle];
      el.classList.toggle('is-active', on);
      el.setAttribute('aria-checked', on);
    }
    for (const el of document.querySelectorAll('[data-slider]')) {
      const min = +el.dataset.min, max = +el.dataset.max, w = +el.dataset.w;
      const v = state[el.dataset.slider];
      const t = max === min ? 0.5 : Math.min(1, Math.max(0, (v - min) / (max - min)));
      el.setAttribute('aria-valuenow', v);
      for (const part of el.querySelectorAll('[data-part]')) {
        if (part.closest('[data-slider]') !== el) continue;
        const ox = +part.dataset.ox;
        if (part.dataset.part === 'fill') { part.style.left = -ox + 'px'; part.style.width = Math.round(w * t) + 'px'; }
        else part.style.left = Math.round(-ox + w * t - part.offsetWidth / 2) + 'px';
      }
    }
    for (const el of document.querySelectorAll('[data-input]')) {
      const v = state[el.dataset.input] ?? '';
      if (document.activeElement !== el && el.value !== v) el.value = v;
    }
    for (const el of document.querySelectorAll('[data-text]')) {
      const s = expand(el.dataset.text);
      if (el.textContent !== s) el.textContent = s;
    }
    for (const list of document.querySelectorAll('.fim-list')) renderList(list);
  }

  function set(name, value) {
    if (!(name in state)) state[name] = value;
    if (isInt(name)) value = Math.round(value);
    if (state[name] === value) return;
    state[name] = value;
    try { sessionStorage.setItem(storeKey, JSON.stringify(state)); } catch { /* storage blocked */ }
    apply();
    document.dispatchEvent(new CustomEvent('fim:change', { detail: { name, value } }));
  }

  // ---- dropdowns (Combo / Multi-combo): a list styled like the design, below the field
  function closeLists(except) {
    for (const list of document.querySelectorAll('.fim-list')) {
      if (list === except) continue;
      list.parentElement.setAttribute('aria-expanded', 'false');
      list.remove();
    }
  }

  function renderList(list) {
    const host = list.parentElement, name = host.dataset.combo, c = combos[name];
    if (!c) return;
    const v = state[name] >>> 0;
    list.querySelectorAll('[data-index]').forEach(item => {
      const i = +item.dataset.index;
      const on = i < 0 ? (c.options.length > 0 && (v & ((1 << c.options.length) - 1)) === ((1 << c.options.length) - 1)) : c.multi ? !!(v & (1 << i)) : v === i;
      item.classList.toggle('is-on', on);
      item.setAttribute('aria-selected', on);
    });
  }

  function openList(host) {
    const name = host.dataset.combo, c = combos[name];
    if (!c) return;
    const list = document.createElement('div');
    list.className = 'fim-list';
    list.setAttribute('role', 'listbox');
    if (c.multi) list.setAttribute('aria-multiselectable', 'true');
    const items = c.options.map((label, i) => [label, i]);
    if (c.multi && c.all && c.options.length > 1) items.unshift([c.all, -1]);
    for (const [label, i] of items) {
      const item = document.createElement('div');
      item.className = 'fim-item' + (c.multi ? ' fim-check' : '');
      item.dataset.index = i;
      item.setAttribute('role', 'option');
      item.textContent = label;
      list.append(item);
    }
    host.append(list);
    host.setAttribute('aria-expanded', 'true');
    renderList(list);
  }

  // ---- events
  document.addEventListener('click', e => {
    const item = e.target.closest('.fim-item');
    if (item) {
      const host = item.closest('[data-combo]'), name = host.dataset.combo, c = combos[name], i = +item.dataset.index;
      if (c.multi) {
        const all = (2 ** Math.min(32, c.options.length) - 1) >>> 0, v = state[name] >>> 0;
        set(name, i < 0 ? ((v & all) === all ? 0 : all) : (v ^ (1 << i)) >>> 0);
      } else { set(name, i); closeLists(); }
      e.stopPropagation();
      return;
    }
    const combo = e.target.closest('[data-combo]');
    if (combo) {
      const open = combo.querySelector(':scope > .fim-list');
      closeLists(open);
      if (open) { if (!e.target.closest('.fim-list')) { open.remove(); combo.setAttribute('aria-expanded', 'false'); } }
      else openList(combo);
      return;
    }
    closeLists();
    const btn = e.target.closest('[data-set], [data-toggle], [data-press]');
    if (!btn) return;
    if (btn.dataset.set) set(btn.dataset.set, +btn.dataset.value);
    if (btn.dataset.toggle) set(btn.dataset.toggle, state[btn.dataset.toggle] ? 0 : 1);
    if (btn.dataset.press) document.dispatchEvent(new CustomEvent('fim:press', { detail: { name: btn.dataset.press } }));
  });

  document.addEventListener('keydown', e => {
    if (e.key === 'Escape') closeLists();
    const sl = e.target.closest?.('[data-slider]');
    if (sl && /^Arrow(Left|Right|Up|Down)$/.test(e.key)) {
      const min = +sl.dataset.min, max = +sl.dataset.max, name = sl.dataset.slider;
      const step = (max - min) / 100 * (e.key === 'ArrowLeft' || e.key === 'ArrowDown' ? -1 : 1);
      set(name, Math.min(Math.max(min, max), Math.max(Math.min(min, max), state[name] + step)));
      e.preventDefault();
    }
    const combo = e.target.closest?.('[data-combo]');
    if (combo && (e.key === 'Enter' || e.key === ' ')) { combo.click(); e.preventDefault(); }
  });

  document.addEventListener('pointerdown', e => {
    const sl = e.target.closest('[data-slider]');
    if (!sl || e.button !== 0) return;
    const min = +sl.dataset.min, max = +sl.dataset.max, name = sl.dataset.slider;
    const move = ev => {
      const r = sl.getBoundingClientRect();
      const t = Math.min(1, Math.max(0, (ev.clientX - r.left) / Math.max(1, r.width)));
      set(name, min + (max - min) * t);
    };
    sl.setPointerCapture(e.pointerId);
    move(e);
    sl.addEventListener('pointermove', move);
    sl.addEventListener('pointerup', () => sl.removeEventListener('pointermove', move), { once: true });
    e.preventDefault();
  });

  document.addEventListener('input', e => {
    const el = e.target.closest('[data-input]');
    if (el) set(el.dataset.input, el.value);
  });

  // ---- pages wider than the window are scaled down to fit (body[data-fit]);
  // phone layouts (data-stretch) follow the screen width, up to 125%
  function fit() {
    const vw = document.documentElement.clientWidth, down = document.body.hasAttribute('data-fit');
    document.documentElement.style.setProperty('--fim-vw', vw + 'px');   // fixed layers center on the page with it
    for (const page of document.querySelectorAll('.page')) {
      const k = vw / +page.dataset.w;
      const z = page.hasAttribute('data-stretch') ? Math.min(k, 1.25) : down ? Math.min(1, k) : 1;
      page.style.zoom = z === 1 ? '' : z;
      page.style.setProperty('--fim-z', z);
    }
  }

  addEventListener('resize', fit);
  fit();
  apply();

  window.fimgui = {
    state,
    get: name => state[name],
    set,
    label: comboLabel,
  };
})();
