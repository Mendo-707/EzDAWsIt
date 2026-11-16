#!/usr/bin/env python3
"""Generates EzDAWsIt UI mockup artboards (static literal markup) for the Design canvas.
Usage: gen.py <BoardName|canvas|check>"""
import math, random, json, sys, os, datetime

OUT = '/mnt/user-data/outputs/artifacts/ezdawsit-ui/project'

C = dict(bg='#1B1D22', panel='#262931', bar='#202329', raised='#2E323B', border='#3A3F4B', line='#15171C',
         display='#101114', lcd='#7FE0A8', lcd2='#7FA592', text='#E6E6E6', dim='#A3A9B4', accent='#4AA3FF',
         play='#2F9A5C', rec='#D03A3A', lane='#20232A', gbeat='#2A2E37', gbar='#3A3F4B', audio='#3B78B8',
         midi='#3FA36B', sel='#F2C94C', loop='rgba(63,163,107,0.33)', status='#16181C', ink='#0E1114',
         selrow='#2F3A4A', midibody='#29493E', audiobody='#283D55', note='#8FE0B0', wave='#8DBDF0')
TC = dict(drums='#E58A3A', b808='#B65BD6', keys='#3FA36B', vox='#4AA3FF', adlibs='#2FB8B0', bus='#8A93A6')
FONT = "Inter, 'Segoe UI', Arial, sans-serif"
BAR = 58  # px per bar in arrange

# ---------------------------------------------------------------- contrast check
def _lum(h):
    h = h.lstrip('#'); r, g, b = [int(h[i:i+2], 16) / 255 for i in (0, 2, 4)]
    f = lambda c: c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)
def contrast(a, b):
    la, lb = sorted([_lum(a), _lum(b)], reverse=True); return (la + 0.05) / (lb + 0.05)
def blend(fg, bg, a):
    f = [int(fg[i:i+2], 16) for i in (1, 3, 5)]; g = [int(bg[i:i+2], 16) for i in (1, 3, 5)]
    return '#' + ''.join('%02X' % round(a * x + (1 - a) * y) for x, y in zip(f, g))

# ---------------------------------------------------------------- icons (24px stroke set)
IC = {
 'play': '<path d="M7 5l12 7-12 7z" fill="currentColor"></path>',
 'rec': '<circle cx="12" cy="12" r="6" fill="currentColor"></circle>',
 'rtz': '<path d="M6 5v14"></path><path d="M18 6l-8 6 8 6z" fill="currentColor"></path>',
 'loop': '<path d="M17 2l4 4-4 4"></path><path d="M3 11v-1a4 4 0 0 1 4-4h14"></path><path d="M7 22l-4-4 4-4"></path><path d="M21 13v1a4 4 0 0 1-4 4H3"></path>',
 'metronome': '<path d="M9 3h6l3 18H6z"></path><path d="M12 15l5-8"></path><path d="M7.5 16h9"></path>',
 'countin': '<circle cx="12" cy="13" r="8"></circle><path d="M12 9v4l2.5 2"></path><path d="M10 2h4"></path>',
 'magnet': '<path d="M5 4v7a7 7 0 0 0 14 0V4"></path><path d="M9 4v7a3 3 0 0 0 6 0V4"></path><path d="M5 8h4"></path><path d="M15 8h4"></path>',
 'undo': '<path d="M9 14L4 9l5-5"></path><path d="M4 9h10.5a5.5 5.5 0 0 1 0 11H11"></path>',
 'redo': '<path d="M15 14l5-5-5-5"></path><path d="M20 9H9.5a5.5 5.5 0 0 0 0 11H13"></path>',
 'new': '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path><path d="M14 3v6h6"></path><path d="M12 12v6"></path><path d="M9 15h6"></path>',
 'open': '<path d="M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"></path>',
 'save': '<path d="M5 3h11l5 5v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"></path><path d="M7 3v5h8"></path><path d="M7 21v-7h10v7"></path>',
 'sliders': '<path d="M4 6h9"></path><path d="M17 6h3"></path><circle cx="15" cy="6" r="2"></circle><path d="M4 12h3"></path><path d="M11 12h9"></path><circle cx="9" cy="12" r="2"></circle><path d="M4 18h11"></path><path d="M19 18h1"></path><circle cx="17" cy="18" r="2"></circle>',
 'headphones': '<path d="M3 15v-3a9 9 0 0 1 18 0v3"></path><path d="M3 15h3v6H4a1 1 0 0 1-1-1z"></path><path d="M21 15h-3v6h2a1 1 0 0 0 1-1z"></path>',
 'speaker': '<path d="M11 5L6 9H2v6h4l5 4z"></path><path d="M15.5 8.5a5 5 0 0 1 0 7"></path><path d="M19 5a10 10 0 0 1 0 14"></path>',
 'down': '<path d="M6 9l6 6 6-6"></path>', 'right': '<path d="M9 6l6 6-6 6"></path>', 'up': '<path d="M6 15l6-6 6 6"></path>',
 'search': '<circle cx="11" cy="11" r="7"></circle><path d="M21 21l-4.3-4.3"></path>',
 'piano': '<rect x="3" y="4" width="18" height="16" rx="2"></rect><path d="M8 4v9"></path><path d="M12 4v9"></path><path d="M16 4v9"></path><path d="M3 13h18"></path>',
 'mic': '<rect x="9" y="3" width="6" height="11" rx="3"></rect><path d="M5 11a7 7 0 0 0 14 0"></path><path d="M12 18v3"></path>',
 'wave': '<path d="M2 12h2"></path><path d="M6 8v8"></path><path d="M10 5v14"></path><path d="M14 9v6"></path><path d="M18 7v10"></path><path d="M22 12h-1"></path>',
 'drum': '<ellipse cx="12" cy="7" rx="8" ry="3"></ellipse><path d="M4 7v9c0 1.7 3.6 3 8 3s8-1.3 8-3V7"></path>',
 'plug': '<path d="M12 22v-5"></path><path d="M9 8V2"></path><path d="M15 8V2"></path><path d="M18 8v5a4 4 0 0 1-4 4h-4a4 4 0 0 1-4-4V8z"></path>',
 'export': '<path d="M12 15V3"></path><path d="M7 8l5-5 5 5"></path><path d="M5 15v4a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-4"></path>',
 'plus': '<path d="M12 5v14"></path><path d="M5 12h14"></path>', 'x': '<path d="M18 6L6 18"></path><path d="M6 6l12 12"></path>',
 'layers': '<path d="M12 3l9 5-9 5-9-5z"></path><path d="M3 13l9 5 9-5"></path>',
 'chain': '<path d="M10 13a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1 1"></path><path d="M14 11a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1-1"></path>',
 'file': '<path d="M14 3H6a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2V9z"></path><path d="M14 3v6h6"></path>',
 'note': '<path d="M9 18V5l12-2v13"></path><circle cx="6" cy="18" r="3"></circle><circle cx="18" cy="16" r="3"></circle>',
 'bookmark': '<path d="M6 3h12v18l-6-4-6 4z"></path>',
 'power': '<path d="M12 2v10"></path><path d="M18.4 6.6a9 9 0 1 1-12.8 0"></path>',
 'help': '<circle cx="12" cy="12" r="9"></circle><path d="M9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3"></path><path d="M12 17h.01"></path>',
 'auto': '<path d="M3 17l5-6 4 3 7-9"></path><circle cx="8" cy="11" r="1.5"></circle><circle cx="12" cy="14" r="1.5"></circle>',
 'takes': '<path d="M4 6h16"></path><path d="M4 12h16"></path><path d="M4 18h10"></path>',
 'wand': '<path d="M15 4V2"></path><path d="M15 16v-2"></path><path d="M8 9h2"></path><path d="M20 9h2"></path><path d="M17.8 11.8L19 13"></path><path d="M17.8 6.2L19 5"></path><path d="M3 21l9-9"></path><path d="M12.2 6.2L11 5"></path>',
 'pencil': '<path d="M17 3l4 4L8 20H4v-4z"></path>', 'pointer': '<path d="M4 4l7 17 2-7 7-2z"></path>',
 'eraser': '<path d="M7 21h10"></path><path d="M5 15l9-9 6 6-6 6H8z"></path>',
 'check': '<path d="M20 6L9 17l-5-5"></path>', 'arrow': '<path d="M5 12h14"></path><path d="M13 6l6 6-6 6"></path>',
 'grip': '<circle cx="9" cy="6" r="1"></circle><circle cx="15" cy="6" r="1"></circle><circle cx="9" cy="12" r="1"></circle><circle cx="15" cy="12" r="1"></circle><circle cx="9" cy="18" r="1"></circle><circle cx="15" cy="18" r="1"></circle>',
 'more': '<circle cx="5" cy="12" r="1"></circle><circle cx="12" cy="12" r="1"></circle><circle cx="19" cy="12" r="1"></circle>',
 'zoomin': '<circle cx="11" cy="11" r="7"></circle><path d="M21 21l-4.3-4.3"></path><path d="M8 11h6"></path><path d="M11 8v6"></path>',
 'zoomout': '<circle cx="11" cy="11" r="7"></circle><path d="M21 21l-4.3-4.3"></path><path d="M8 11h6"></path>',
 'clock': '<circle cx="12" cy="12" r="9"></circle><path d="M12 7v5l3 2"></path>',
 'midi': '<circle cx="12" cy="12" r="9"></circle><circle cx="8" cy="11" r="1"></circle><circle cx="16" cy="11" r="1"></circle><circle cx="12" cy="8" r="1"></circle><circle cx="10" cy="15" r="1"></circle><circle cx="14" cy="15" r="1"></circle>',
 'refresh': '<path d="M21 12a9 9 0 1 1-3-6.7L21 8"></path><path d="M21 3v5h-5"></path>',
 'restore': '<path d="M3 12a9 9 0 1 0 3-6.7L3 8"></path><path d="M3 3v5h5"></path><path d="M12 7v5l3 2"></path>',
 'flag': '<path d="M5 21V4"></path><path d="M5 4h11l-2 4 2 4H5"></path>',
}
def icon(name, size=16, color='currentColor', sw=1.8):
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 24 24" fill="none" stroke="{color}" stroke-width="{sw}" '
            f'stroke-linecap="round" stroke-linejoin="round" aria-hidden="true" style="flex:none">{IC[name]}</svg>')

LOGO = ('<svg width="22" height="22" viewBox="0 0 24 24" aria-hidden="true" style="flex:none"><rect x="1" y="1" width="22" height="22" rx="5" fill="#4AA3FF"></rect>'
        '<path d="M5 12h2M8.5 8v8M12 5.5v13M15.5 9v6M19 12h0" stroke="#0E1114" stroke-width="2.2" stroke-linecap="round" fill="none"></path></svg>')

# ---------------------------------------------------------------- planned (greyed) helpers
def chip(ph, title=None):
    t = f' title="{title}"' if title else ''
    return (f'<span{t} style="display:inline-flex;align-items:center;height:15px;padding:0 4px;border-radius:3px;background:#2B2F38;'
            f'border:1px solid #5B6272;color:#D3D8E0;font-size:9px;font-weight:700;letter-spacing:.04em;line-height:1;white-space:nowrap;flex:none">{ph}</span>')
DIM = 'opacity:.55;filter:grayscale(1)'
CHIPPOS = {'tr': 'top:4px;right:4px', 'tl': 'top:4px;left:4px', 'br': 'bottom:4px;right:4px', 'bl': 'bottom:4px;left:4px',
           'badge': 'top:-7px;right:-6px', 'badgel': 'top:-7px;left:-4px', 'mr': 'top:50%;right:6px;margin-top:-8px'}
def planned(inner, ph, style='', inner_style='', pos='tr', dashed=False):
    outer = f'position:relative;{style}'
    if dashed: outer += ';outline:1px dashed #4A505C;outline-offset:-1px'
    ch = '' if pos is None else f'<span style="position:absolute;{CHIPPOS[pos]};z-index:3;display:flex">{chip(ph)}</span>'
    return f'<div style="{outer}"><div style="{DIM};{inner_style}">{inner}</div>{ch}</div>'
def pinline(inner, ph=None, gap=6, style=''):
    ch = chip(ph) if ph else ''
    return (f'<div style="display:flex;align-items:center;gap:4px;{style}"><div style="{DIM};display:flex;align-items:center;gap:{gap}px">{inner}</div>{ch}</div>')

# ---------------------------------------------------------------- controls
def btn(label='', ic=None, kind='default', aria=None, h=26, style='', size=14, active=False, title=None):
    base = f'display:inline-flex;align-items:center;justify-content:center;gap:6px;height:{h}px;border-radius:4px;font-family:{FONT};font-size:12px;font-weight:500;white-space:nowrap;cursor:pointer;box-sizing:border-box;flex:none;'
    pad = 'padding:0 9px;' if label else f'width:{h + 2}px;padding:0;'
    if kind == 'primary': sk = f'background:{C["accent"]};border:1px solid {C["accent"]};color:{C["ink"]};font-weight:600;'
    elif kind == 'ghost': sk = f'background:transparent;border:1px solid transparent;color:{C["text"]};'
    elif kind == 'play': sk = f'background:{C["play"]};border:1px solid {C["play"]};color:{C["ink"]};'
    elif kind == 'danger': sk = f'background:transparent;border:1px solid {C["border"]};color:#FF8A8A;'
    else: sk = f'background:{C["raised"]};border:1px solid {C["border"]};color:{C["text"]};'
    if active and kind == 'default': sk = f'background:#1D3550;border:1px solid {C["accent"]};color:#FFFFFF;'
    a = f' aria-label="{aria}"' if aria else (f' aria-label="{title}"' if (title and not label) else '')
    t = f' title="{title}"' if title else ''
    pr = ' aria-pressed="true"' if active else ''
    inner = (icon(ic, size) if ic else '') + (f'<span>{label}</span>' if label else '')
    return f'<button type="button"{a}{t}{pr} style="{base}{pad}{sk}{style}">{inner}</button>'

def select(value, w=None, h=24, aria=None, style=''):
    ws = f'width:{w}px;' if w else ''
    a = f' aria-label="{aria}"' if aria else ''
    return (f'<button type="button"{a} style="display:inline-flex;align-items:center;justify-content:space-between;gap:6px;{ws}height:{h}px;padding:0 6px 0 8px;'
            f'box-sizing:border-box;border-radius:4px;background:{C["display"]};border:1px solid {C["border"]};color:{C["text"]};font-family:{FONT};font-size:12px;'
            f'white-space:nowrap;overflow:hidden;flex:none;cursor:pointer;{style}"><span style="overflow:hidden;text-overflow:ellipsis">{value}</span>{icon("down", 12, C["dim"])}</button>')

def field(label, ctrl, lw=None, gap=8):
    l = f'width:{lw}px;' if lw else ''
    return (f'<div style="display:flex;align-items:center;gap:{gap}px"><span style="{l}color:{C["dim"]};font-size:11px;flex:none">{label}</span>{ctrl}</div>')

def heading(t, extra=''):
    return (f'<div style="display:flex;align-items:center;gap:6px;font-size:10px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:{C["dim"]}">{t}{extra}</div>')

def toggle(on=True, aria='Toggle'):
    bg = C['accent'] if on else C['border']; x = 14 if on else 2
    return (f'<button type="button" role="switch" aria-checked="{"true" if on else "false"}" aria-label="{aria}" style="position:relative;width:30px;height:16px;'
            f'border-radius:8px;border:none;background:{bg};padding:0;flex:none;cursor:pointer"><span style="position:absolute;top:2px;left:{x}px;width:12px;height:12px;border-radius:6px;background:#FFFFFF"></span></button>')

def checkbox(label, on=True, id_=None):
    i = id_ or 'cb' + str(abs(hash(label)) % 100000)
    return (f'<label for="{i}" style="display:flex;align-items:center;gap:7px;font-size:12px;color:{C["text"]};cursor:pointer">'
            f'<input id="{i}" type="checkbox"{" checked" if on else ""} style="width:14px;height:14px;margin:0;accent-color:{C["accent"]}"><span>{label}</span></label>')

def radio(label, name, on=False, sub=None, id_=None):
    i = id_ or 'rd' + str(abs(hash(name + label)) % 100000)
    s = f'<span style="color:{C["dim"]};font-size:11px">{sub}</span>' if sub else ''
    return (f'<label for="{i}" style="display:flex;align-items:center;gap:8px;font-size:12px;color:{C["text"]};cursor:pointer">'
            f'<input id="{i}" type="radio" name="{name}"{" checked" if on else ""} style="width:14px;height:14px;margin:0;accent-color:{C["accent"]}"><span>{label}</span>{s}</label>')

def knob(size, frac, color=None, bipolar=False):
    color = color or C['accent']
    r = size / 2 - 3; cx = cy = size / 2
    def pt(a, rr=r):
        rad = math.radians(a - 90); return cx + rr * math.cos(rad), cy + rr * math.sin(rad)
    def arc(s, e):
        if abs(e - s) < 0.5: return ''
        if e < s: s, e = e, s
        x1, y1 = pt(s); x2, y2 = pt(e); large = 1 if e - s > 180 else 0
        return f'M{x1:.1f} {y1:.1f}A{r:.1f} {r:.1f} 0 {large} 1 {x2:.1f} {y2:.1f}'
    av = -135 + frac * 270
    val = arc(0, av) if bipolar else arc(-135, av)
    px, py = pt(av, r * 0.55)
    sw = 2.5 if size >= 34 else 2
    v = f'<path d="{val}" stroke="{color}" stroke-width="{sw}" fill="none" stroke-linecap="round"></path>' if val else ''
    return (f'<svg width="{size}" height="{size}" viewBox="0 0 {size} {size}" aria-hidden="true" style="flex:none">'
            f'<circle cx="{cx}" cy="{cy}" r="{r - 3.5:.1f}" fill="{C["raised"]}" stroke="{C["line"]}"></circle>'
            f'<path d="{arc(-135, 135)}" stroke="{C["border"]}" stroke-width="{sw}" fill="none" stroke-linecap="round"></path>{v}'
            f'<path d="M{cx} {cy}L{px:.1f} {py:.1f}" stroke="{C["text"]}" stroke-width="1.8" stroke-linecap="round"></path></svg>')

def knob_btn(size, frac, aria, bipolar=False, color=None):
    return (f'<button type="button" aria-label="{aria}" style="width:{size}px;height:{size}px;padding:0;border:none;background:transparent;flex:none;cursor:pointer;display:flex">'
            f'{knob(size, frac, color, bipolar)}</button>')

def hslider(frac, w=110, aria='Volume', color=None):
    color = color or C['accent']; fx = round(frac * (w - 10))
    return (f'<button type="button" aria-label="{aria}" style="position:relative;width:{w}px;height:14px;padding:0;border:none;background:transparent;flex:none;cursor:pointer">'
            f'<span style="position:absolute;left:0;right:0;top:5px;height:4px;border-radius:2px;background:{C["display"]};border:1px solid {C["border"]}"></span>'
            f'<span style="position:absolute;left:1px;top:6px;height:4px;width:{fx}px;border-radius:2px;background:{color}"></span>'
            f'<span style="position:absolute;left:{fx}px;top:1px;width:10px;height:12px;border-radius:2px;background:#D9DDE3"></span></button>')

def meter_v(h, l=0.7, r=0.66, w=4):
    def bar(v):
        g = round(h * min(v, .8)); y = round(h * max(0, min(v, .93) - .8)); rd = round(h * max(0, v - .93))
        return (f'<div style="width:{w}px;height:{h}px;background:{C["display"]};display:flex;flex-direction:column-reverse">'
                f'<div style="height:{g}px;background:#2F9A5C"></div><div style="height:{y}px;background:#D9B23A"></div><div style="height:{rd}px;background:#D03A3A"></div></div>')
    return f'<div style="display:flex;gap:1px" aria-hidden="true">{bar(l)}{bar(r)}</div>'

def msr(m=False, s=False, r=False, show_r=True, h=20, w=22):
    def b(t, on, onbg, onfg, name):
        bg = onbg if on else C['raised']; fg = onfg if on else C['text']
        return (f'<button type="button" aria-label="{name}" aria-pressed="{"true" if on else "false"}" style="width:{w}px;height:{h}px;padding:0;border-radius:3px;'
                f'border:1px solid {onbg if on else C["border"]};background:{bg};color:{fg};font-family:{FONT};font-size:11px;font-weight:700;cursor:pointer;flex:none">{t}</button>')
    out = b('M', m, '#D9A441', C['ink'], 'Mute') + b('S', s, C['accent'], C['ink'], 'Solo')
    if show_r: out += b('R', r, C['rec'], '#FFFFFF', 'Record arm')
    return f'<div style="display:flex;gap:3px">{out}</div>'

def card(inner, style=''):
    return f'<div style="background:{C["panel"]};border:1px solid {C["border"]};border-radius:6px;{style}">{inner}</div>'

# ---------------------------------------------------------------- waveforms / notes
def wave_path(w, h, seed, gaps=True):
    rnd = random.Random(seed); mid = h / 2; pts = []
    silence = []
    if gaps:
        x = rnd.uniform(10, 40)
        while x < w:
            g = rnd.uniform(6, 18); silence.append((x, x + g)); x += g + rnd.uniform(40, 110)
    env = 0.6
    for x in range(0, int(w) + 1, 2):
        env += rnd.uniform(-0.12, 0.12); env = max(0.25, min(1.0, env))
        a = mid * 0.92 * env * (0.35 + 0.65 * rnd.random())
        if any(s0 <= x <= s1 for s0, s1 in silence): a = mid * 0.04
        pts.append((x, a))
    top = ' '.join(f'L{x} {mid - a:.1f}' for x, a in pts)
    bot = ' '.join(f'L{x} {mid + a:.1f}' for x, a in reversed(pts))
    return f'M0 {mid:.1f} {top} {bot} Z'

def notes_for(kind, bars):
    """returns list of (start_bar, len_bar, y 0..1 from top, h)"""
    n = []
    for b in range(int(math.ceil(bars))):
        if kind.startswith('drums'):
            hats = range(0, 16, 2) if kind == 'drums' else range(0, 16)
            for s in hats: n.append((b + s / 16, 1 / 32, 0.12))
            for s in (4, 12): n.append((b + s / 16, 1 / 32, 0.45))
            for s in ((0, 7, 10) if kind == 'drums' else (0, 3, 6, 10)): n.append((b + s / 16, 1 / 32, 0.8))
        elif kind.startswith('808'):
            seq = [(0, .4, .55), (.44, .12, .55), (.625, .3, .75)] if kind == '808' else [(0, .3, .6), (.375, .2, .45), (.625, .35, .7)]
            for s, l, y in seq: n.append((b + s, l, y))
        elif kind == 'keys':
            roots = [0.55, 0.75, 0.62, 0.68]; r = roots[b % 4]
            for s, l in ((0, .36), (.625, .2), (.875, .1)):
                for dy in (0, -.17, -.32): n.append((b + s, l, r + dy))
    return [x for x in n if x[0] < bars]

def clip(x, w, h, kind, name, seed=1, pattern='keys', selected=False, compact=False):
    hdr = 13 if compact else 15
    bh = h - hdr - 2
    if kind == 'audio':
        body_bg, col, txt = C['audiobody'], C['audio'], '#FFFFFF'
        inner = f'<path d="{wave_path(w, bh, seed)}" fill="{C["wave"]}" opacity="0.85"></path>'
    else:
        body_bg, col, txt = C['midibody'], C['midi'], C['ink']
        bars = w / BAR; inner = ''
        for s, l, y in notes_for(pattern, bars):
            inner += f'<rect x="{s * BAR:.1f}" y="{max(1, y * (bh - 3)):.1f}" width="{max(2, l * BAR - 1):.1f}" height="2.5" rx="1" fill="{C["note"]}"></rect>'
    ol = f'outline:2px solid {C["sel"]};outline-offset:0;z-index:2;' if selected else ''
    return (f'<div style="position:absolute;left:{x}px;top:3px;width:{w - 1}px;height:{h - 7}px;box-sizing:border-box;border-radius:3px;overflow:hidden;'
            f'background:{body_bg};border:1px solid {col};{ol}">'
            f'<div style="height:{hdr}px;background:{col};color:{txt};font-size:10.5px;font-weight:600;line-height:{hdr}px;padding:0 5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{name}</div>'
            f'<svg width="{w - 3}" height="{bh - 4}" viewBox="0 0 {w - 3} {bh - 4}" preserveAspectRatio="none" aria-hidden="true" style="display:block">{inner}</svg></div>')

# ---------------------------------------------------------------- song data
TRACKS = [
    dict(id='drums', name='Drums', kind='inst', dev='Sampler', color=TC['drums'], vol=.68, db='-4.0 dB', pan=.5, panl='C', fx=1,
         clips=[(3, 7, 'Boom Bap A', 'midi', 'drums'), (7, 11, 'Boom Bap A', 'midi', 'drums'), (11, 15, 'Hook Beat', 'midi', 'drums2'), (15, 17, 'Boom Bap A', 'midi', 'drums')]),
    dict(id='808', name='808', kind='inst', dev='Synth (4OSC)', color=TC['b808'], vol=.62, db='-6.0 dB', pan=.5, panl='C', fx=2,
         clips=[(3, 11, '808 Verse', 'midi', '808'), (11, 15, '808 Hook', 'midi', '808b'), (15, 17, '808 Verse', 'midi', '808')]),
    dict(id='keys', name='Keys', kind='inst', dev='Surge XT', color=TC['keys'], vol=.55, db='-9.0 dB', pan=.42, panl='L15', fx=2,
         clips=[(1, 11, 'Keys Chords', 'midi', 'keys'), (11, 15, 'Keys Hook', 'midi', 'keys'), (15, 17, 'Keys Chords', 'midi', 'keys')]),
    dict(id='vox', name='Lead Vox', kind='audio', dev='In 1 (mono)', color=TC['vox'], vol=.74, db='-2.0 dB', pan=.5, panl='C', fx=3, arm=True,
         clips=[(3, 11, 'Verse 1 (Take 3)', 'audio', 11), (11, 15, 'Hook Vox', 'audio', 12), (15, 17, 'Verse 2 (Take 1)', 'audio', 13)]),
    dict(id='adlibs', name='Adlibs', kind='audio', dev='In 1 (mono)', color=TC['adlibs'], vol=.6, db='-7.5 dB', pan=.62, panl='R20', fx=1,
         clips=[(4.5, 5.25, 'Yeah', 'audio', 21), (6.5, 7.25, 'Uh', 'audio', 22), (8.5, 9.5, "Let's go", 'audio', 23), (12.5, 13.25, 'Ayy', 'audio', 24), (14.25, 15, 'Woo', 'audio', 25)]),
]
def bx(bar): return round((bar - 1) * BAR)

# ---------------------------------------------------------------- shell: app bar, transport, browser, status
def seg_btn(label, active, first, last, ph=None, aria=None):
    r = f'border-radius:{"5px" if first else "0"} {"5px" if last else "0"} {"5px" if last else "0"} {"5px" if first else "0"};'
    bg = '#1D3550' if active else C['raised']; bd = C['accent'] if active else C['border']
    b = (f'<button type="button" aria-pressed="{"true" if active else "false"}" style="height:26px;padding:0 14px;{r}background:{bg};border:1px solid {bd};'
         f'color:{"#FFFFFF" if active else C["text"]};font-family:{FONT};font-size:12px;font-weight:{600 if active else 500};cursor:pointer;white-space:nowrap">{label}</button>')
    return planned(b, ph, pos='badge', style='display:flex') if ph else b

def app_bar(view='arrange', start=False):
    sep = f'<div style="width:1px;height:20px;background:{C["border"]};flex:none"></div>'
    left = (f'<div style="display:flex;align-items:center;gap:8px">{LOGO}<span style="font-size:13px;font-weight:700;letter-spacing:.01em">EzDAWsIt</span></div>{sep}'
            f'<div style="display:flex;gap:2px">{btn("New", "new", "ghost", h=26)}{btn("Open", "open", "ghost", h=26)}{btn("Save", "save", "ghost", h=26)}</div>{sep}')
    if start:
        song = f'<span style="font-size:13px;color:{C["dim"]}">No song open</span>'
    else:
        song = (f'<div style="display:flex;align-items:baseline;gap:8px;min-width:0"><span style="font-size:13px;font-weight:600">Late Night Cypher</span>'
                f'<span style="font-size:11px;color:{C["dim"]};display:inline-flex;align-items:center;gap:4px">{icon("check", 11, "#7FE0A8", 2.4)}Saved</span></div>')
    views = '' if start else (f'<div role="group" aria-label="Main view" style="display:flex;align-items:center">'
                              f'{seg_btn("Arrange", view == "arrange", True, False)}{seg_btn("Mix", view == "mix", False, False, "P3")}{seg_btn("Master", view == "master", False, True, "P4")}</div>')
    right = (f'<button type="button" aria-label="Main output: Headphones" title="Where you hear EzDAWsIt" style="display:inline-flex;align-items:center;gap:6px;height:26px;padding:0 8px;border-radius:4px;'
             f'background:{C["display"]};border:1px solid {C["border"]};color:{C["text"]};font-family:{FONT};font-size:12px;cursor:pointer">{icon("headphones", 14)}<span>Headphones</span>{icon("down", 12, C["dim"])}</button>'
             f'{btn("Setup", "sliders", h=26)}'
             + ('' if start else planned(btn('Export', 'export', h=26), 'P4', pos='badge', style='display:flex'))
             + planned(f'<div role="group" aria-label="Easy or Pro view" style="display:flex">{seg_btn("Easy", True, True, False)}{seg_btn("Pro", False, False, True)}</div>', 'P5', pos='badge', style='display:flex')
             + planned(btn('', 'help', 'ghost', aria='Guided tour and help', h=26), 'P5', pos='badge', style='display:flex'))
    return (f'<header style="height:40px;flex:none;display:flex;align-items:center;gap:10px;padding:0 10px;background:{C["bar"]};border-bottom:1px solid {C["line"]}">'
            f'{left}{song}<div style="flex:1"></div>{views}<div style="flex:1"></div><div style="display:flex;align-items:center;gap:10px">{right}</div></header>')

def lcd_box(big, small, label, w=None, aria=''):
    ws = f'width:{w}px;' if w else ''
    return (f'<div aria-label="{aria}" style="{ws}height:32px;box-sizing:border-box;padding:0 10px;border-radius:4px;background:{C["display"]};border:1px solid {C["line"]};'
            f'display:flex;align-items:center;gap:10px;font-variant-numeric:tabular-nums;flex:none">{big}{small}</div>')

def transport_bar(disabled_song=False):
    sep = f'<div style="width:1px;height:22px;background:{C["border"]};flex:none"></div>'
    tbtn = lambda ic, aria, kind='default', active=False, color=None: (
        f'<button type="button" aria-label="{aria}"{" aria-pressed=\"true\"" if active else ""} style="width:34px;height:28px;display:inline-flex;align-items:center;justify-content:center;'
        f'border-radius:4px;cursor:pointer;flex:none;padding:0;'
        + (f'background:{C["play"]};border:1px solid {C["play"]};color:{C["ink"]};' if kind == 'play' else
           f'background:#1D3550;border:1px solid {C["accent"]};color:#FFFFFF;' if active else
           f'background:{C["raised"]};border:1px solid {C["border"]};color:{color or C["text"]};')
        + f'">{icon(ic, 15)}</button>')
    g1 = (f'<div style="display:flex;gap:4px">{tbtn("rtz", "Return to start (Home)")}{tbtn("play", "Play (Space)", "play")}'
          f'{tbtn("rec", "Record (R)", color="#FF6B6B")}{tbtn("loop", "Loop (L)", active=True)}</div>')
    pos = lcd_box(f'<div style="display:flex;flex-direction:column;line-height:1"><span style="font-size:9px;font-weight:600;color:{C["lcd2"]};letter-spacing:.06em">BAR</span>'
                  f'<span style="font-size:19px;font-weight:600;color:{C["lcd"]}">5.1.1</span></div>',
                  f'<div style="display:flex;flex-direction:column;line-height:1"><span style="font-size:9px;font-weight:600;color:{C["lcd2"]};letter-spacing:.06em">TIME</span>'
                  f'<span style="font-size:14px;font-weight:500;color:{C["lcd"]}">0:10.43</span></div>', '', w=170, aria='Position 5.1.1, 0 minutes 10.43 seconds')
    bpm = lcd_box(f'<span style="font-size:16px;font-weight:600;color:{C["lcd"]}">92.00</span>', f'<span style="font-size:10px;font-weight:600;color:{C["lcd2"]}">BPM</span>', '', aria='Tempo 92 BPM')
    sig = planned(lcd_box(f'<span style="font-size:14px;font-weight:600;color:{C["lcd"]}">4/4</span>', '', ''), '2E', pos='badge', style='display:flex')
    key = planned(lcd_box(f'<span style="font-size:13px;font-weight:600;color:{C["lcd"]}">G minor</span>', f'<span style="font-size:10px;font-weight:600;color:{C["lcd2"]}">KEY</span>', ''), '2E', pos='badge', style='display:flex')
    g3 = (f'<div style="display:flex;gap:6px">{btn("Click", "metronome", active=True, h=28)}{btn("Count-in", "countin", h=28)}</div>'
          + planned(f'<div style="display:flex;gap:4px">{btn("Punch in", h=28)}{btn("Punch out", h=28)}</div>', 'P5', pos='badge', style='display:flex'))
    g4 = f'<div style="display:flex;gap:4px">{btn("Snap", "magnet", active=True, h=28)}{btn("", "undo", aria="Undo (Ctrl+Z)", h=28)}{btn("", "redo", aria="Redo (Ctrl+Y)", h=28)}</div>'
    right = (planned(btn('Sync', 'clock', h=28, title='Send or follow MIDI clock'), 'P5', pos='badge', style='display:flex')
             + planned(f'<div style="display:flex;align-items:center;gap:6px;height:28px;padding:0 8px;border-radius:4px;background:{C["display"]};border:1px solid {C["line"]}">'
                       f'<span style="font-size:10px;font-weight:600;color:{C["text"]}">MAIN</span>'
                       f'<div style="display:flex;flex-direction:column;gap:2px"><div style="width:110px;height:5px;background:#2A2E37"><div style="width:72%;height:5px;background:#2F9A5C"></div></div>'
                       f'<div style="width:110px;height:5px;background:#2A2E37"><div style="width:68%;height:5px;background:#2F9A5C"></div></div></div>'
                       f'<span style="font-size:11px;color:{C["text"]};font-variant-numeric:tabular-nums">-14.2 LUFS</span></div>', 'P3', pos='badge', style='display:flex'))
    return (f'<div role="toolbar" aria-label="Transport" style="height:44px;flex:none;display:flex;align-items:center;gap:12px;padding:0 10px;background:{C["panel"]};border-bottom:1px solid {C["line"]}">'
            f'{g1}{pos}{bpm}{sig}{key}{sep}{g3}{sep}{g4}<div style="flex:1"></div><div style="display:flex;gap:12px;align-items:center">{right}</div></div>')

BROWSER_CATS = [('templates', 'Song templates', 'layers', 'P4'), ('trackpresets', 'Track presets', 'bookmark', 'P4'), ('chains', 'Effect chains', 'chain', 'P4'),
                ('instruments', 'Instruments', 'piano', None), ('effects', 'Effects', 'plug', None), ('kits', 'Drum kits', 'drum', '2D'),
                ('samples', 'Samples &amp; loops', 'note', 'P4'), ('files', 'Files', 'file', 'P4')]
def brow_item(name, sub=None, ic='plug', active=False, live=True):
    bg = C['selrow'] if active else 'transparent'
    s = f'<span style="font-size:10.5px;color:{C["dim"]};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{sub}</span>' if sub else ''
    return (f'<button type="button" style="display:flex;align-items:center;gap:8px;width:100%;min-height:{40 if sub else 26}px;padding:4px 10px;box-sizing:border-box;border:none;'
            f'border-radius:4px;background:{bg};color:{C["text"]};font-family:{FONT};text-align:left;cursor:pointer">{icon(ic, 14, C["dim"])}'
            f'<span style="display:flex;flex-direction:column;min-width:0;gap:1px"><span style="font-size:12px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{name}</span>{s}</span></button>')
def group_label(t, ph=None):
    return (f'<div style="display:flex;align-items:center;gap:6px;padding:8px 10px 3px;font-size:10px;font-weight:700;letter-spacing:.08em;text-transform:uppercase;color:{C["dim"]}">'
            f'{t}{chip(ph) if ph else ""}</div>')

def browser(sel='instruments'):
    cats = ''
    for key, label, ic, ph in BROWSER_CATS:
        active = key == sel
        row = (f'<button type="button" aria-pressed="{"true" if active else "false"}" style="display:flex;align-items:center;gap:8px;width:100%;height:26px;padding:0 10px;box-sizing:border-box;border:none;border-radius:4px;'
               f'background:{C["selrow"] if active else "transparent"};color:{C["text"]};font-family:{FONT};font-size:12px;font-weight:{600 if active else 400};text-align:left;cursor:pointer">'
               f'{icon(ic, 14, C["dim"] if not active else C["accent"])}<span style="flex:1">{label}</span>{icon("right", 12, C["dim"])}</button>')
        cats += pinline(row, ph, style='padding-right:4px') if ph else f'<div style="padding-right:4px;display:flex">{row}</div>'
    if sel == 'instruments':
        body = (group_label('Built-in') + brow_item('Synth (4OSC)', 'Four-oscillator synth: bass, keys, leads', 'piano')
                + brow_item('Sampler', 'Play any sound across the keys', 'piano')
                + group_label('VST3 (scanned)') + brow_item('Surge XT', 'Surge Synth Team', 'plug', True) + brow_item('Vital', 'Matt Tytel', 'plug') + brow_item('Decent Sampler', 'Decent Samples', 'plug')
                + planned(group_label('Coming') + brow_item('Drum Machine', '16 pads, kits per genre', 'drum'), '2D', pos='tr'))
    elif sel == 'effects':
        body = (group_label('Built-in') + ''.join(brow_item(n, None, 'plug') for n in ['EQ', 'Compressor', 'Reverb', 'Delay', 'Chorus', 'Phaser', 'Filter'])
                + group_label('VST3 (scanned)') + brow_item('TDR Nova', 'Tokyo Dawn Records', 'plug') + brow_item('Valhalla Supermassive', 'Valhalla DSP', 'plug')
                + planned(group_label('More built-in') + ''.join(brow_item(n, None, 'plug') for n in ['Limiter', 'De-esser', 'Saturation']), 'P3'))
    elif sel == 'chains':
        body = planned(group_label('Vocals') + brow_item('Bright, upfront rap vocal', 'Lead vocal · hip hop', 'chain', True) + brow_item('Warm R&amp;B lead', 'Lead vocal · smooth', 'chain')
                       + brow_item('Tight hook double', 'Doubles and stacks', 'chain') + brow_item('Phone-filter adlib', 'Lo-fi, narrow', 'chain') + brow_item('Wide backing vocals', 'Group vocals', 'chain')
                       + group_label('Instruments') + brow_item('Punchy 808', 'Saturate + clip', 'chain') + brow_item('Dusty keys', 'Lo-fi tape tone', 'chain'), 'P4', pos=None)
    elif sel == 'kits':
        body = planned(group_label('Hip hop') + brow_item('Boom Bap Dust', '16 pads · 90 BPM vibe', 'drum', True) + brow_item('Trap 808 Kit', '16 pads · hats + rolls', 'drum')
                       + group_label('Drum &amp; bass') + brow_item('Amen Breaks', 'Sliced break, 16 pads', 'drum') + brow_item('Liquid Kit', 'Soft, rolling', 'drum')
                       + group_label('House') + brow_item('Warehouse 909', 'Classic house kit', 'drum') + group_label('Lo-fi') + brow_item('Tape Kit', 'Warm, saturated', 'drum'), '2D', pos=None)
    else:  # mastering
        body = planned(group_label('Mastering chains') + brow_item('Streaming, loud &amp; clean', 'EQ · Multiband · Limiter', 'chain', True)
                       + brow_item('Warm and gentle', 'Tape colour, soft limit', 'chain') + brow_item('Club ready', 'Tight low end, loud', 'chain')
                       + brow_item('Podcast loudness', '-16 LUFS speech', 'chain') + group_label('Reference tracks') + brow_item('Boom Bap Classic ref.wav', '-9.8 LUFS', 'wave'), 'P4', pos=None)
    title = dict(instruments='Instruments', effects='Effects', chains='Effect chains', kits='Drum kits', mastering='Effect chains')[sel]
    hint = {'instruments': 'Double-click to add as a new track', 'effects': 'Double-click to add to the selected track'}.get(sel)
    foot = (f'<div style="padding:8px 12px;border-top:1px solid {C["line"]};font-size:11px;color:{C["dim"]}">{hint}</div>' if hint else
            planned(f'<div style="padding:8px 12px;border-top:1px solid {C["line"]};font-size:11px;color:{C["text"]}">Drag onto a track, or double-click</div>', '', pos=None))
    search = planned(f'<label for="bsearch" style="display:flex;align-items:center;gap:6px;height:26px;padding:0 8px;border-radius:4px;background:{C["display"]};border:1px solid {C["border"]}">'
                     f'{icon("search", 13, C["dim"])}<input id="bsearch" type="search" placeholder="Search sounds, presets, chains" style="flex:1;min-width:0;background:transparent;border:none;color:{C["text"]};font-family:{FONT};font-size:12px;outline:none"></label>', 'P4', pos='mr', style='margin:0 10px')
    return (f'<aside aria-label="Browser" style="width:240px;flex:none;display:flex;flex-direction:column;background:{C["panel"]};border-right:1px solid {C["line"]};min-height:0">'
            f'<div style="height:34px;flex:none;display:flex;align-items:center;justify-content:space-between;padding:0 6px 0 12px"><span style="font-size:12px;font-weight:700">Browser</span>'
            f'{btn("", "x", "ghost", aria="Hide browser (B)", h=24, size=12)}</div>{search}'
            f'<nav aria-label="Browser categories" style="display:flex;flex-direction:column;gap:1px;padding:8px 6px;border-bottom:1px solid {C["line"]}">{cats}</nav>'
            f'<div style="display:flex;align-items:center;justify-content:space-between;padding:8px 12px 2px"><span style="font-size:12px;font-weight:600">{title}</span></div>'
            f'<div style="flex:1;min-height:0;overflow:hidden;display:flex;flex-direction:column;gap:1px;padding:0 6px 6px">{body}</div>{foot}</aside>')

def status_bar(msg='Saved. Autosave every 2 minutes; last autosave 40 s ago.'):
    return (f'<footer style="height:22px;flex:none;display:flex;align-items:center;gap:16px;padding:0 10px;background:{C["status"]};border-top:1px solid {C["line"]};font-size:11px;color:{C["dim"]}">'
            f'<span style="color:{C["text"]}">{msg}</span><div style="flex:1"></div>'
            f'<span>Space play · R record · L loop · G snap</span><span>Windows Audio · Headphones (Realtek USB) · 48 kHz · 256 samples · 5.3 ms</span>'
            f'<span style="color:{C["text"]}">CPU 11%</span></footer>')

# ---------------------------------------------------------------- arrange
def track_header(t, h, selected=False, compact=False, first=False):
    bg = '#2C313B' if selected else C['panel']
    typ = icon('mic' if t['kind'] == 'audio' else 'piano', 14, C['dim'])
    fxb = (f'<button type="button" aria-label="Effects and instrument for {t["name"]}" style="display:inline-flex;align-items:center;gap:4px;height:20px;padding:0 6px;border-radius:3px;'
           f'background:{C["raised"]};border:1px solid {C["border"]};color:{C["text"]};font-family:{FONT};font-size:10.5px;font-weight:700;cursor:pointer;flex:none">FX<span style="color:{C["dim"]};font-weight:500">{t["fx"]}</span></button>')
    name = (f'<div style="display:flex;align-items:center;gap:6px;min-width:0">{typ}<span style="font-size:12.5px;font-weight:600;white-space:nowrap">{t["name"]}</span>'
            f'<span style="font-size:11px;color:{C["dim"]};white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{t["dev"]}</span><div style="flex:1"></div>{fxb}</div>')
    extra = planned(f'<div style="display:flex;gap:3px">{btn("", "auto", h=20, size=12, aria="Show automation")}'
                    + (btn('', 'takes', h=20, size=12, aria='Show take lanes') if t['kind'] == 'audio' else '') + '</div>', 'P3' if first else '', pos='badge' if first else None, style='display:flex')
    pan = f'<div style="display:flex;align-items:center;gap:3px">{knob_btn(20, t["pan"], "Pan " + t["panl"], bipolar=True)}<span style="font-size:10.5px;color:{C["dim"]};width:24px">{t["panl"]}</span></div>'
    if compact:
        rows = (name + f'<div style="display:flex;align-items:center;gap:8px">{msr(r=t.get("arm", False), show_r=True, h=18, w=20)}'
                f'{hslider(t["vol"], 92, "Volume " + t["db"])}<span style="font-size:10.5px;color:{C["dim"]};font-variant-numeric:tabular-nums">{t["db"]}</span>{pan}</div>')
        gap = 6
    else:
        rows = (name + f'<div style="display:flex;align-items:center;gap:8px">{msr(r=t.get("arm", False))}<div style="flex:1"></div>{extra}</div>'
                f'<div style="display:flex;align-items:center;gap:8px">{hslider(t["vol"], 118, "Volume " + t["db"])}<span style="font-size:10.5px;color:{C["dim"]};width:44px;font-variant-numeric:tabular-nums">{t["db"]}</span><div style="flex:1"></div>{pan}</div>')
        gap = 6
    meter = planned(meter_v(h - 14, .62, .58, 3), 'P3', style='display:flex;align-items:center;padding:0 4px', pos=None)
    return (f'<div style="height:{h}px;box-sizing:border-box;display:flex;border-bottom:1px solid {C["line"]};background:{bg}">'
            f'<div style="width:4px;flex:none;background:{t["color"]}"></div><div style="flex:1;min-width:0;padding:6px 6px 6px 8px;display:flex;flex-direction:column;justify-content:center;gap:{gap}px">{rows}</div>{meter}</div>')

def lane(t, h, sel_clip=None, compact=False):
    cl = ''
    for (a, b, nm, kind, pat) in t['clips']:
        seed = pat if isinstance(pat, int) else hash(nm) % 97
        pattern = pat if isinstance(pat, str) else 'keys'
        cl += clip(bx(a), round((b - a) * BAR), h, kind, nm, seed, pattern, selected=(nm == sel_clip), compact=compact)
    return f'<div style="position:relative;height:{h}px;box-sizing:border-box;border-bottom:1px solid {C["line"]}">{cl}</div>'

GRID_BG = (f'background-color:{C["lane"]};background-image:repeating-linear-gradient(to right,{C["gbar"]} 0 1px,transparent 1px {BAR}px),'
           f'repeating-linear-gradient(to right,{C["gbeat"]} 0 1px,transparent 1px {BAR / 4}px);')

def sections_lane(h=22):
    secs = [(1, 3, 'Intro'), (3, 11, 'Verse 1'), (11, 15, 'Hook'), (15, 17, 'Verse 2')]
    cols = ['#5A6170', '#4B6A8A', '#8A6A3A', '#4B6A8A']
    s = ''
    for (a, b, n), c in zip(secs, cols):
        extra = f'<span style="margin-left:auto;display:flex;align-items:center;gap:3px;font-weight:500;font-size:10px">{icon("grip", 10)}drag to repeat</span>' if n == 'Hook' else ''
        s += (f'<div style="position:absolute;left:{bx(a) + 1}px;top:2px;width:{round((b - a) * BAR) - 2}px;height:{h - 4}px;border-radius:3px;background:{c};'
              f'color:#FFFFFF;font-size:10.5px;font-weight:600;display:flex;align-items:center;padding:0 6px;box-sizing:border-box;overflow:hidden;white-space:nowrap">{n}{extra}</div>')
    return planned(s, '2E', style=f'height:{h}px;border-bottom:1px solid {C["line"]};background:{C["bar"]}', inner_style='position:relative;height:100%', pos=None)

def tempo_lane(h=22):
    s = (f'<svg width="930" height="{h}" viewBox="0 0 930 {h}" aria-hidden="true" style="position:absolute;left:0;top:0"><path d="M0 {h - 7}H{bx(15)}L{bx(17)} {h - 12}" stroke="#7FE0A8" stroke-width="1.5" fill="none"></path></svg>'
         f'<div style="position:absolute;left:4px;top:3px;display:flex;gap:8px;font-size:10.5px;font-weight:600;color:#FFFFFF">'
         f'<span style="display:flex;align-items:center;gap:3px">{icon("flag", 10)}92 BPM</span><span>4/4</span><span>G minor</span></div>'
         f'<div style="position:absolute;left:{bx(15) + 4}px;top:3px;font-size:10.5px;font-weight:600;color:#FFFFFF">ramp to 94 BPM</div>'
         f'<div style="position:absolute;left:{bx(11) + 4}px;top:3px;font-size:10.5px;font-weight:600;color:#FFFFFF;display:flex;align-items:center;gap:3px">{icon("flag", 10)}Beat switch</div>')
    return planned(s, '2E', style=f'height:{h}px;border-bottom:1px solid {C["line"]};background:{C["bar"]}', inner_style='position:relative;height:100%', pos=None)

def ruler(h=28, loop=(11, 15), play=5):
    nums = ''.join(f'<span style="position:absolute;left:{bx(b) + 4}px;top:2px;font-size:10.5px;font-weight:{600 if (b - 1) % 4 == 0 else 400};color:{C["text"] if (b - 1) % 4 == 0 else C["dim"]}">{b}</span>' for b in range(1, 17))
    lp = (f'<div aria-label="Loop region bars {loop[0]} to {loop[1]}" style="position:absolute;left:{bx(loop[0])}px;width:{round((loop[1] - loop[0]) * BAR)}px;bottom:0;height:{h // 2 - 1}px;'
          f'background:{C["loop"]};border-left:2px solid {C["midi"]};border-right:2px solid {C["midi"]};box-sizing:border-box"></div>')
    ph = (f'<svg width="12" height="8" viewBox="0 0 12 8" aria-hidden="true" style="position:absolute;left:{bx(play) - 6}px;bottom:0"><path d="M0 0h12L6 8z" fill="#F2F2F2"></path></svg>')
    return (f'<div style="position:relative;height:{h}px;box-sizing:border-box;border-bottom:1px solid {C["line"]};background:#1E2026;'
            f'background-image:repeating-linear-gradient(to right,{C["gbar"]} 0 1px,transparent 1px {BAR}px);background-size:100% {h // 2}px;background-repeat:repeat-x;background-position:0 {h // 2}px">{nums}{lp}{ph}</div>')

def header_cell(label, h, ph=None, right=''):
    inner = f'<span style="font-size:11px;font-weight:600;color:{C["text"]}">{label}</span>'
    if ph:
        return (f'<div style="height:{h}px;box-sizing:border-box;display:flex;align-items:center;gap:6px;padding:0 8px 0 12px;border-bottom:1px solid {C["line"]};background:{C["bar"]}">'
                f'<div style="{DIM};display:flex;align-items:center;gap:6px;flex:1">{inner}{right}</div>{chip(ph)}</div>')
    return (f'<div style="height:{h}px;box-sizing:border-box;display:flex;align-items:center;gap:6px;padding:0 8px 0 12px;border-bottom:1px solid {C["line"]};background:{C["bar"]}">{inner}<div style="flex:1"></div>{right}</div>')

def auto_lane_pair(h=50):
    hdr = (f'<div style="height:{h}px;box-sizing:border-box;border-bottom:1px solid {C["line"]};background:#23262D;display:flex;align-items:center;gap:6px;padding:0 8px 0 22px">'
           f'<div style="{DIM};display:flex;align-items:center;gap:6px;flex:1">{icon("auto", 13, C["text"])}<span style="font-size:11px;color:{C["text"]}">Keys</span>{select("Volume", 96, 22)}'
           f'{btn("", "x", "ghost", aria="Hide lane", h=20, size=11)}</div>{chip("P3")}</div>')
    pts = [(0, .3), (bx(9), .3), (bx(11), .7), (bx(15), .7), (bx(15) + 10, .4), (930, .4)]
    poly = ' '.join(f'{x},{(1 - y) * (h - 8) + 4:.0f}' for x, y in pts)
    dots = ''.join(f'<circle cx="{x}" cy="{(1 - y) * (h - 8) + 4:.0f}" r="3" fill="#7FBFFF"></circle>' for x, y in pts[1:-1])
    ln = planned(f'<svg width="930" height="{h}" viewBox="0 0 930 {h}" aria-hidden="true"><polyline points="{poly}" fill="none" stroke="#7FBFFF" stroke-width="1.6"></polyline>{dots}</svg>', '', pos=None,
                 style=f'height:{h}px;box-sizing:border-box;border-bottom:1px solid {C["line"]};background:rgba(0,0,0,.18)')
    return hdr, ln

def take_lanes_pair(row=22):
    labels = ''.join(f'<div style="height:{row}px;display:flex;align-items:center;gap:6px;font-size:11px;color:{C["text"]}">{icon("check", 11, C["text"]) if i == 3 else "<span style=\"width:11px\"></span>"}Take {i}</div>' for i in (1, 2, 3))
    hdr = (f'<div style="height:{row * 3 + 6}px;box-sizing:border-box;border-bottom:1px solid {C["line"]};background:#23262D;display:flex;align-items:flex-start;gap:6px;padding:3px 8px 3px 22px">'
           f'<div style="{DIM};display:flex;flex-direction:column;flex:1">{labels}</div><div style="display:flex;flex-direction:column;gap:4px;align-items:flex-end">{chip("P5")}'
           f'<div style="{DIM}">{btn("Comp", h=20, style="font-size:11px")}</div></div></div>')
    comp = {1: [(3, 5)], 2: [(9, 11)], 3: [(5, 9)]}
    rows = ''
    for i in (1, 2, 3):
        segs = ''
        for a, b in comp[i]:
            segs += f'<div style="position:absolute;left:{bx(a)}px;top:2px;width:{round((b - a) * BAR)}px;height:{row - 4}px;background:rgba(74,163,255,.35);border:1px solid #7FBFFF;box-sizing:border-box;border-radius:2px"></div>'
        rows += (f'<div style="position:relative;height:{row}px">'
                 f'<div style="position:absolute;left:{bx(3)}px;top:2px;width:{8 * BAR - 1}px;height:{row - 4}px;background:{C["audiobody"]};border:1px solid {C["audio"]};box-sizing:border-box;border-radius:2px;overflow:hidden">'
                 f'<svg width="{8 * BAR}" height="{row - 6}" viewBox="0 0 {8 * BAR} {row - 6}" preserveAspectRatio="none" aria-hidden="true"><path d="{wave_path(8 * BAR, row - 6, 40 + i)}" fill="{C["wave"]}" opacity=".7"></path></svg></div>{segs}</div>')
    ln = planned(f'<div style="padding:3px 0;display:flex;flex-direction:column">{rows}</div>', '', pos=None,
                 style=f'height:{row * 3 + 6}px;box-sizing:border-box;border-bottom:1px solid {C["line"]};background:rgba(0,0,0,.18)')
    return hdr, ln

def arrange(compact=False, sel_track='vox', sel_clip=None, extras=True, total_h=None, legend=False):
    th = 56 if compact else 84
    sh, tph, rh = (20, 20, 26) if compact else (22, 22, 28)
    zoom = f'<div style="display:flex;gap:2px">{btn("", "zoomout", "ghost", aria="Zoom out (-)", h=20, size=13)}{btn("", "zoomin", "ghost", aria="Zoom in (=)", h=20, size=13)}</div>'
    addt = btn('Track', 'plus', h=20, style='font-size:11px;padding:0 7px;gap:4px', size=12, title='Add track: audio or instrument')
    left = header_cell('Sections', sh, '2E') + header_cell('Tempo · Key', tph, '2E') + header_cell('', rh, None, f'{addt}<div style="flex:1"></div>{zoom}')
    right = sections_lane(sh) + tempo_lane(tph) + ruler(rh)
    hdrs = ''; lanes = ''
    for i, t in enumerate(TRACKS):
        hdrs += track_header(t, th, t['id'] == sel_track, compact, first=(i == 0))
        lanes += lane(t, th, sel_clip, compact)
        if extras and not compact:
            if t['id'] == 'keys':
                a, b = auto_lane_pair(); hdrs += a; lanes += b
            if t['id'] == 'vox':
                a, b = take_lanes_pair(); hdrs += a; lanes += b
    hint = (f'<div style="position:absolute;left:24px;bottom:{18 if not compact else 6}px;font-size:11px;color:{C["dim"]};display:flex;flex-direction:column;gap:4px">'
            f'<span>Double-click empty space: new 1-bar MIDI clip</span><span>Drag audio files here from Explorer</span></div>')
    leg = legend_card() if legend else ''
    playhead = f'<div style="position:absolute;left:{bx(5)}px;top:0;bottom:0;width:1px;background:#F2F2F2;z-index:4"></div>'
    hscroll = (f'<div style="height:10px;flex:none;background:{C["bar"]};border-top:1px solid {C["line"]};position:relative">'
               f'<div style="position:absolute;left:4px;top:2px;width:640px;height:6px;border-radius:3px;background:#4A505C"></div></div>')
    return (f'<section aria-label="Arrange" style="flex:1;min-height:0;display:flex;background:{C["bg"]}">'
            f'<div style="width:260px;flex:none;display:flex;flex-direction:column;background:{C["panel"]};border-right:1px solid {C["line"]}">{left}<div style="flex:1;overflow:hidden">{hdrs}</div></div>'
            f'<div style="flex:1;min-width:0;display:flex;flex-direction:column">{right}'
            f'<div style="flex:1;position:relative;overflow:hidden;{GRID_BG}">{lanes}{playhead}{'' if compact else hint}{leg}</div>{hscroll}</div>'
            f'<div style="width:10px;flex:none;background:{C["bar"]};border-left:1px solid {C["line"]};position:relative"><div style="position:absolute;top:80px;left:2px;width:6px;height:260px;border-radius:3px;background:#4A505C"></div></div></section>')

def legend_card():
    row = lambda sample, text: f'<div style="display:flex;align-items:center;gap:10px">{sample}<span style="font-size:12px;color:#1B1D22">{text}</span></div>'
    live = f'<span style="display:inline-flex;align-items:center;height:20px;padding:0 8px;border-radius:4px;background:#2E323B;border:1px solid #4AA3FF;color:#FFFFFF;font-size:11px;font-weight:600">Built</span>'
    plan = (f'<span style="position:relative;display:inline-flex"><span style="display:inline-flex;align-items:center;height:20px;padding:0 8px;border-radius:4px;background:#2E323B;border:1px solid #3A3F4B;color:#E6E6E6;'
            f'font-size:11px;font-weight:600;{DIM}">Planned</span></span>')
    phases = [('2C', 'Piano roll'), ('2D', 'Drum machine'), ('2E', 'Tempo, key, sections'), ('P3', 'Mixer, automation, effects'), ('P4', 'Master, export, presets'), ('P5', 'Pro polish, Easy/Pro, MIDI maps')]
    ph = ''.join(f'<div style="display:flex;align-items:center;gap:6px">{chip(p)}<span style="font-size:11px;color:#1B1D22">{t}</span></div>' for p, t in phases)
    return (f'<div style="position:absolute;right:16px;bottom:12px;width:560px;padding:12px 14px;border-radius:6px;background:#F2F2EE;border:1px solid #C9CCD2;z-index:6;display:flex;flex-direction:column;gap:8px">'
            f'<div style="display:flex;justify-content:space-between;align-items:center"><span style="font-size:12px;font-weight:700;color:#1B1D22">Legend</span>'
            f'<span style="font-size:10.5px;color:#4A4F59">Mockup note, not part of the app</span></div>'
            f'<div style="display:flex;gap:18px">{row(live, "Works today, full colour")}{row(plan, "Roadmap: greyed, inert")}</div>'
            f'<div style="display:grid;grid-template-columns:repeat(3, minmax(0, 1fr));gap:5px 12px">{ph}</div></div>')

# ---------------------------------------------------------------- detail panel
def detail_header(ctx_name, ctx_color, tabs, collapsed=False, right=''):
    t = ''
    for label, active, ph in tabs:
        b = (f'<button type="button" aria-pressed="{"true" if active else "false"}" style="height:30px;padding:0 12px;border:none;border-bottom:2px solid {C["accent"] if active else "transparent"};'
             f'background:transparent;color:{C["text"]};font-family:{FONT};font-size:12px;font-weight:{600 if active else 500};cursor:pointer;white-space:nowrap">{label}</button>')
        t += pinline(b, ph, gap=0) if ph else b
    chev = btn('', 'up' if collapsed else 'down', 'ghost', aria='Expand detail panel' if collapsed else 'Collapse detail panel', h=22, size=13)
    ctx = (f'<div style="display:flex;align-items:center;gap:6px;padding:0 10px 0 4px"><span style="width:8px;height:8px;border-radius:2px;background:{ctx_color}"></span>'
           f'<span style="font-size:12px;font-weight:600">{ctx_name}</span></div>')
    return (f'<div style="height:32px;flex:none;box-sizing:border-box;display:flex;align-items:center;gap:6px;padding:0 8px;background:{C["bar"]};border-top:1px solid {C["line"]};border-bottom:1px solid {C["line"]}">'
            f'{chev}{ctx}<div style="display:flex;align-items:center;gap:4px">{t}</div><div style="flex:1"></div>{right}</div>')

def inspector_col(inner, w=210):
    return f'<div style="width:{w}px;flex:none;box-sizing:border-box;padding:10px 12px;display:flex;flex-direction:column;gap:10px;background:{C["panel"]};border-right:1px solid {C["line"]};overflow:hidden">{inner}</div>'

# --- piano roll
def piano_roll():
    rows = 24; rh = 10; top_pitch = 71  # B4 .. C3
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    scale = {7, 9, 10, 0, 2, 3, 5}  # G minor
    pxb = 232; gw = pxb * 4
    keys = ''; rowsbg = ''
    for i in range(rows):
        p = top_pitch - i; pc = p % 12; black = '#' in names[pc]
        keys += (f'<div style="height:{rh}px;box-sizing:border-box;border-bottom:1px solid #15171C;background:{"#2A2D34" if black else "#D9DCE1"};'
                 f'font-size:8px;line-height:{rh}px;text-align:right;padding-right:3px;color:#1B1D22">{"C" + str(p // 12 - 1) if pc == 0 else ""}</div>')
        bg = '#262A31' if pc in scale else '#1C1E23'
        if pc == 7: bg = '#2C3340'
        rowsbg += f'<div style="height:{rh}px;box-sizing:border-box;border-bottom:1px solid #191B20;background:{bg}"></div>'
    chords = [[55, 58, 62], [51, 55, 58], [58, 62, 65], [53, 57, 60]]
    rhythm = [(0, 1.4, 100), (1.5, .9, 84), (3, .75, 92)]
    notes = ''; vel = ''
    for b, ch in enumerate(chords):
        for (s, l, v) in rhythm:
            x = b * pxb + s * pxb / 4
            selected = b == 2
            for k, p in enumerate(ch):
                y = (top_pitch - p) * rh
                bd = f'border:1.5px solid {C["sel"]};' if selected else 'border:1px solid #1E5C3A;'
                notes += f'<div style="position:absolute;left:{x:.0f}px;top:{y + 1}px;width:{l * pxb / 4 - 2:.0f}px;height:{rh - 2}px;box-sizing:border-box;border-radius:2px;background:{C["midi"]};{bd}"></div>'
            vel += f'<div style="position:absolute;left:{x:.0f}px;bottom:0;width:4px;height:{v * 0.52:.0f}px;background:{C["sel"] if selected else C["midi"]}"></div>'
            vel += f'<div style="position:absolute;left:{x - 2:.0f}px;bottom:{v * 0.52 - 3:.0f}px;width:8px;height:3px;border-radius:2px;background:{C["sel"] if selected else C["note"]}"></div>'
    gridlines = (f'background-image:repeating-linear-gradient(to right,{C["gbar"]} 0 1px,transparent 1px {pxb}px),repeating-linear-gradient(to right,#2A2E37 0 1px,transparent 1px {pxb / 4}px),'
                 f'repeating-linear-gradient(to right,#22252B 0 1px,transparent 1px {pxb / 16}px);')
    bars = ''.join(f'<span style="position:absolute;left:{i * pxb + 4}px;top:2px;font-size:10.5px;font-weight:600;color:{C["text"]}">{11 + i}</span>' for i in range(4))
    toolbar = (f'<div style="height:34px;flex:none;display:flex;align-items:center;gap:10px;padding:0 10px;border-bottom:1px solid {C["line"]};background:{C["panel"]}">'
               f'<div role="group" aria-label="Tool" style="display:flex">{seg_btn("Draw", True, True, False)}{seg_btn("Select", False, False, False)}{seg_btn("Erase", False, False, True)}</div>'
               f'{field("Grid", select("1/16", 64))}{btn("Quantise", "magnet", h=24)}{field("Strength", select("100%", 64))}{field("Swing", select("0%", 56))}'
               f'<div style="flex:1"></div>{checkbox("Highlight scale", True, "pr-hl")}{checkbox("Fold to scale", False, "pr-fold")}'
               f'<div style="display:flex;gap:2px">{btn("", "zoomout", "ghost", aria="Zoom out", h=22, size=13)}{btn("", "zoomin", "ghost", aria="Zoom in", h=22, size=13)}</div></div>')
    insp = inspector_col(
        heading('Clip') + f'<label for="pr-name" style="display:flex;flex-direction:column;gap:4px;font-size:11px;color:{C["dim"]}">Name<input id="pr-name" value="Keys Hook" style="height:24px;padding:0 8px;border-radius:4px;background:{C["display"]};border:1px solid {C["border"]};color:{C["text"]};font-family:{FONT};font-size:12px"></label>'
        + field('Start', f'<span style="font-size:12px;font-variant-numeric:tabular-nums">11.1.1</span>', 54) + field('Length', f'<span style="font-size:12px">4 bars</span>', 54)
        + f'<div style="display:flex;align-items:center;justify-content:space-between"><span style="font-size:12px">Loop clip</span>{toggle(True, "Loop clip")}</div>'
        + heading('Scale') + field('Key', select('G', 70), 54) + field('Scale', select('Minor', 96), 54)
        + f'<div style="font-size:11px;color:{C["text"]};line-height:1.4">Lighter rows are notes in G minor. Notes outside the scale still play.</div>'
        + heading('Selected') + f'<div style="font-size:12px">3 notes · velocity 100</div>')
    roll = (f'<div style="flex:1;min-width:0;display:flex;flex-direction:column">{toolbar}'
            f'<div style="flex:1;display:flex;min-height:0">'
            f'<div style="width:46px;flex:none;display:flex;flex-direction:column;border-right:1px solid {C["line"]}"><div style="height:20px;flex:none;background:{C["bar"]}"></div>{keys}'
            f'<div style="flex:1;display:flex;align-items:center;justify-content:center;font-size:10px;color:{C["text"]};background:{C["bar"]};border-top:1px solid {C["line"]}">Vel</div></div>'
            f'<div style="flex:1;min-width:0;display:flex;flex-direction:column;overflow:hidden">'
            f'<div style="position:relative;height:20px;flex:none;background:{C["bar"]};border-bottom:1px solid {C["line"]}">{bars}</div>'
            f'<div style="position:relative;height:{rows * rh}px;flex:none">{rowsbg}<div style="position:absolute;inset:0;{gridlines}"></div>{notes}</div>'
            f'<div style="position:relative;flex:1;background:#1C1E23;border-top:1px solid {C["line"]};{gridlines}">{vel}</div></div></div></div>')
    content = f'<div style="display:flex;height:100%">{insp}{roll}</div>'
    return planned(content, '', style='flex:1;min-height:0', inner_style='height:100%', pos=None, dashed=True)

# --- drum machine
def drum_machine():
    pads = ['Kick', 'Snare', 'Clap', 'Rim', 'Closed Hat', 'Open Hat', 'Shaker', 'Perc', 'Tom Low', 'Tom High', 'Crash', 'Ride', 'Vox Chop', '808 Hit', 'Snap', 'FX Riser']
    pc = ['#E58A3A', '#E0C341', '#E0C341', '#E0C341', '#3FB8C8', '#3FB8C8', '#3FB8C8', '#3FB8C8', '#B65BD6', '#B65BD6', '#7F8CFF', '#7F8CFF', '#3FA36B', '#E58A3A', '#E0C341', '#7F8CFF']
    order = [12, 13, 14, 15, 8, 9, 10, 11, 4, 5, 6, 7, 0, 1, 2, 3]  # MPC-style: pad 1 bottom-left
    pg = ''
    for idx in order:
        sel = idx == 1
        pg += (f'<button type="button" aria-label="Pad {idx + 1} {pads[idx]}" style="height:56px;border-radius:5px;background:#2A2E36;border:{"2px solid " + C["sel"] if sel else "1px solid " + C["border"]};'
               f'color:{C["text"]};font-family:{FONT};display:flex;flex-direction:column;justify-content:space-between;align-items:flex-start;padding:5px 7px;box-sizing:border-box;cursor:pointer;text-align:left">'
               f'<span style="width:18px;height:4px;border-radius:2px;background:{pc[idx]}"></span><span style="font-size:11px;font-weight:600">{pads[idx]}</span>'
               f'<span style="font-size:9.5px;color:{C["dim"]}">{idx + 1}</span></button>')
    colA = (f'<div style="width:300px;flex:none;display:flex;flex-direction:column;gap:8px">'
            f'<div style="display:flex;gap:6px">{select("Boom Bap Dust", 170, aria="Kit")}{select("Hip hop", 112, aria="Genre")}</div>'
            f'<div style="display:grid;grid-template-columns:repeat(4, minmax(0, 1fr));gap:6px">{pg}</div>'
            f'<div style="font-size:11px;color:{C["text"]}">Click a pad to hear it. Drag a sample onto a pad to replace it.</div></div>')
    kn = lambda lab, val, f, bip=False: (f'<div style="display:flex;flex-direction:column;align-items:center;gap:3px">{knob_btn(40, f, lab + " " + val, bip)}'
                                          f'<span style="font-size:11px">{lab}</span><span style="font-size:10.5px;color:{C["text"]}">{val}</span></div>')
    colB = (f'<div style="width:200px;flex:none;display:flex;flex-direction:column;gap:10px;padding:0 14px;border-left:1px solid {C["line"]};border-right:1px solid {C["line"]}">'
            + heading('Pad 2 · Snare') + f'<div style="display:flex;align-items:center;gap:6px;height:44px;padding:0 8px;border-radius:4px;background:{C["display"]};border:1px solid {C["border"]}">'
            f'<svg width="150" height="30" viewBox="0 0 150 30" aria-hidden="true"><path d="{wave_path(150, 30, 7, False)}" fill="#8DBDF0"></path></svg></div>'
            f'<div style="font-size:11px">Snare - Dusty 03.wav</div>'
            f'<div style="display:grid;grid-template-columns:repeat(2, minmax(0, 1fr));gap:8px">{kn("Tune", "0 st", .5, True)}{kn("Decay", "62%", .62)}{kn("Pan", "C", .5, True)}{kn("Level", "-3 dB", .7)}</div>'
            + field('Choke', select('None', 100), 44) + '</div>')
    rows = [('Kick', [0, 7, 10], '#E58A3A'), ('Snare', [4, 12], '#E0C341'), ('Clap', [12], '#E0C341'), ('Closed Hat', [0, 2, 4, 6, 8, 10, 12], '#3FB8C8'),
            ('Open Hat', [14], '#3FB8C8'), ('Rim', [11], '#E0C341'), ('Shaker', [1, 5, 9, 13], '#3FB8C8'), ('Perc', [15], '#3FB8C8')]
    grid = ''
    for name, on, col in rows:
        selr = name == 'Snare'
        cells = ''
        for s in range(16):
            o = s in on
            base = '#2E323B' if (s // 4) % 2 == 0 else '#272A31'
            cells += (f'<button type="button" aria-label="{name} step {s + 1} {"on" if o else "off"}" aria-pressed="{"true" if o else "false"}" style="height:22px;padding:0;border-radius:3px;'
                      f'border:1px solid {C["line"]};background:{col if o else base};cursor:pointer"></button>')
        grid += (f'<div style="display:flex;align-items:center;gap:6px;height:24px"><span style="width:74px;flex:none;font-size:11px;font-weight:{600 if selr else 400};color:{C["sel"] if selr else C["text"]}">{name}</span>'
                 f'<div style="flex:1;display:grid;grid-template-columns:repeat(16, minmax(0, 1fr));gap:3px">{cells}</div>{msr(show_r=False, h=18, w=18)}</div>')
    velo = ''.join(f'<div style="height:{(70 if s == 4 else 92 if s == 12 else 0) * .5:.0f}px;align-self:end;background:#E0C341;border-radius:2px 2px 0 0"></div>' for s in range(16))
    nums = ''.join(f'<span style="font-size:9.5px;color:{C["text"]};text-align:center">{s + 1}</span>' for s in range(16))
    colC = (f'<div style="flex:1;min-width:0;display:flex;flex-direction:column;gap:6px">'
            f'<div style="display:flex;align-items:center;gap:10px"><div role="group" aria-label="Pattern" style="display:flex">{seg_btn("A", True, True, False)}{seg_btn("B", False, False, False)}{seg_btn("C", False, False, False)}{seg_btn("D", False, False, True)}</div>'
            f'{field("Steps", select("16", 56))}{field("Swing", select("56%", 64))}{field("Length", select("1 bar", 70))}<div style="flex:1"></div>{btn("Write to clip", "arrow", h=24)}</div>'
            f'<div style="display:flex;gap:6px"><span style="width:74px;flex:none"></span><div style="flex:1;display:grid;grid-template-columns:repeat(16, minmax(0, 1fr));gap:3px">{nums}</div><span style="width:39px"></span></div>'
            f'{grid}<div style="display:flex;gap:6px;align-items:stretch;height:50px;margin-top:2px"><span style="width:74px;flex:none;font-size:10.5px;color:{C["text"]};align-self:center">Snare velocity</span>'
            f'<div style="flex:1;display:grid;grid-template-columns:repeat(16, minmax(0, 1fr));gap:3px;background:{C["display"]};border-radius:3px;padding:2px">{velo}</div><span style="width:39px"></span></div></div>')
    content = f'<div style="display:flex;gap:14px;height:100%;box-sizing:border-box;padding:12px">{colA}{colB}{colC}</div>'
    return planned(content, '', style='flex:1;min-height:0', inner_style='height:100%', pos=None, dashed=True)

# --- devices / vocal chain
def live_fx_card(name, graphic, w=112):
    return (f'<div style="width:{w}px;flex:none;box-sizing:border-box;display:flex;flex-direction:column;gap:8px;padding:10px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
            f'<div style="display:flex;align-items:center;gap:6px">{planned(btn("", "power", "ghost", aria="Bypass " + name, h=18, size=12), "", pos=None, style="display:flex")}'
            f'<span style="font-size:12px;font-weight:600">{name}</span></div>'
            f'<span style="font-size:10.5px;color:{C["dim"]}">Built-in</span>'
            f'<div style="height:64px;border-radius:4px;background:{C["display"]};display:flex;align-items:center;justify-content:center">{graphic}</div>'
            f'<div style="flex:1"></div>{btn("Open", h=24, style="width:100%")}{btn("Remove", "x", "ghost", h=22, size=11, style="width:100%;color:" + C["dim"])}</div>')

def eq_svg(w=80, h=40):
    return f'<svg width="{w}" height="{h}" viewBox="0 0 80 40" aria-hidden="true"><path d="M2 30C14 30 16 22 24 22S34 28 44 26 56 12 64 14 74 18 78 18" stroke="#7FBFFF" stroke-width="1.6" fill="none"></path></svg>'
def comp_svg():
    return '<svg width="60" height="44" viewBox="0 0 60 44" aria-hidden="true"><path d="M4 40L34 14 56 8" stroke="#7FBFFF" stroke-width="1.6" fill="none"></path><path d="M4 40L56 4" stroke="#4A505C" stroke-dasharray="2 3" fill="none"></path></svg>'
def verb_svg():
    return '<svg width="80" height="40" viewBox="0 0 80 40" aria-hidden="true"><path d="M4 6V36M10 14V36M16 18V36M22 22V36M28 25V36M34 27V36M40 29V36M46 31V36M52 32V36M58 33V36M64 34V36" stroke="#7FBFFF" stroke-width="2"></path></svg>'

def vocal_chain_panel():
    insp = inspector_col(
        heading('Track') + f'<div style="display:flex;align-items:center;gap:6px"><span style="width:10px;height:10px;border-radius:2px;background:{TC["vox"]}"></span><span style="font-size:13px;font-weight:600">Lead Vox</span></div>'
        + f'<span style="font-size:11px;color:{C["dim"]}">Audio track · record armed</span>'
        + f'<div style="display:flex;flex-direction:column;gap:4px"><span style="font-size:11px;color:{C["dim"]}">Input</span>{select("In 1 · Mic (mono)", 184, aria="Input")}</div>'
        + f'<div style="display:flex;flex-direction:column;gap:4px"><span style="font-size:11px;color:{C["dim"]}">Output</span>{select("Main out", 184, aria="Output")}</div>'
        + planned(btn('Check input level', 'mic', h=26, style='width:100%'), 'P5', pos='badge', style='display:flex;margin-top:4px')
        + planned(f'<div style="font-size:11px;color:{C["text"]};line-height:1.4">Sing your loudest line. We set the gain so peaks land near -10 dB.</div>', '', pos=None), 200)
    macros = [('Clean', '40%', .4, 'Cuts rumble and room noise'), ('Tune', '25%', .25, 'Pulls notes to G minor'), ('Punch', '60%', .6, 'Evens out loud and quiet words'),
              ('Bright', '55%', .55, 'Adds clarity and presence'), ('Space', '20%', .2, 'Puts the voice in a room'), ('Width', '30%', .3, 'Spreads doubles left and right')]
    mk = ''.join(f'<div style="display:flex;flex-direction:column;align-items:center;gap:3px;text-align:center">{knob_btn(54, f, n + " " + v)}'
                 f'<span style="font-size:12px;font-weight:600">{n}</span><span style="font-size:11px;color:{C["text"]};font-variant-numeric:tabular-nums">{v}</span>'
                 f'<span style="font-size:10.5px;color:{C["text"]};line-height:1.3">{h}</span></div>' for n, v, f, h in macros)
    mods = ['Clean-up (HPF, gate)', 'Pitch correction', 'Tone EQ', 'Comp 1', 'De-esser', 'Comp 2', 'Colour', 'Air EQ', 'Sends']
    chainm = ''
    for i, m in enumerate(mods):
        chainm += (f'<span style="display:inline-flex;align-items:center;gap:4px;height:22px;padding:0 7px;border-radius:4px;background:{C["raised"]};border:1px solid {C["border"]};font-size:11px;white-space:nowrap">'
                   f'<span style="width:6px;height:6px;border-radius:3px;background:#7FE0A8"></span>{m}</span>')
        if i < len(mods) - 1: chainm += icon('right', 10, C['text'])
    rack = (f'<div style="width:500px;flex:none;box-sizing:border-box;display:flex;flex-direction:column;gap:12px;padding:10px 12px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
            f'<div style="display:flex;align-items:center;gap:8px">{icon("power", 14, "#7FE0A8")}<span style="font-size:13px;font-weight:700">Vocal Chain</span>{select("Bright, upfront rap vocal", 190, aria="Chain preset")}'
            f'<div style="flex:1"></div>{btn("Show chain", "chain", h=24)}</div>'
            f'<div style="display:grid;grid-template-columns:repeat(6, minmax(0, 1fr));gap:8px">{mk}</div>'
            f'<div style="display:flex;flex-direction:column;gap:6px;padding-top:8px;border-top:1px solid {C["line"]}"><span style="font-size:10px;font-weight:700;letter-spacing:.08em;color:{C["text"]}">INSIDE THE RACK (PRO VIEW)</span>'
            f'<div style="display:flex;flex-wrap:wrap;align-items:center;gap:4px">{chainm}</div></div></div>')
    rack = planned(rack, 'P3', pos='tr', style='display:flex')
    addcard = (f'<button type="button" style="width:92px;flex:none;border-radius:6px;border:1px dashed #5B6272;background:#1D3550;color:{C["text"]};font-family:{FONT};'
               f'font-size:12px;font-weight:600;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:6px;cursor:pointer">{icon("plus", 18)}Add effect</button>')
    chain = (f'<div style="flex:1;min-width:0;display:flex;align-items:stretch;gap:8px;padding:10px;overflow:hidden">'
             f'{rack}{live_fx_card("EQ", eq_svg())}{live_fx_card("Compressor", comp_svg())}{live_fx_card("Reverb", verb_svg())}{addcard}</div>')
    return f'<div style="flex:1;min-height:0;display:flex">{insp}{chain}</div>'

def add_fx_menu(x, y):
    item = lambda t, sub=False: (f'<button type="button" style="display:flex;align-items:center;justify-content:space-between;width:100%;height:22px;padding:0 10px;border:none;border-radius:3px;'
                                 f'background:transparent;color:{C["text"]};font-family:{FONT};font-size:12px;text-align:left;cursor:pointer"><span>{t}</span>{icon("right", 11, C["dim"]) if sub else ""}</button>')
    sep = f'<div style="height:1px;background:{C["border"]};margin:4px 0"></div>'
    live = ''.join(item(t) for t in ['EQ', 'Compressor', 'Reverb', 'Delay', 'Chorus', 'Phaser', 'Filter'])
    plan = ''.join(item(t, True) for t in ['Dynamics', 'EQ &amp; Filter', 'Space', 'Colour', 'Pitch', 'Utility', 'Modulation'])
    return (f'<div role="menu" aria-label="Add effect" style="position:absolute;left:{x}px;top:{y}px;width:230px;padding:6px;box-sizing:border-box;border-radius:6px;background:#2B2F37;border:1px solid #4A505C;z-index:20;'
            f'box-shadow:0 8px 24px rgba(0,0,0,.5)">'
            + group_label('Built-in') + live + sep + item('VST3 plugins', True) + item('Rescan plugins…') + sep
            + planned(group_label('All built-in effects') + plan, 'P3') + planned(item('Vocal Chain rack', False) + item('Effect chains', True), 'P4') + '</div>')

# ---------------------------------------------------------------- mixer
def strip(t, first=False, kind='track'):
    W = 96
    planned_strip = kind != 'track'
    color = t['color']
    name = (f'<div style="height:26px;flex:none;display:flex;align-items:center;gap:5px;padding:0 6px;border-bottom:3px solid {color}">'
            f'<span style="font-size:12px;font-weight:600;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">{t["name"]}</span></div>')
    io = (f'<div style="display:flex;flex-direction:column;gap:3px;padding:0 5px">{select(t.get("in", "In 1"), W - 12, 20, aria="Input", style="font-size:11px")}'
          f'{select(t.get("out", "Main out"), W - 12, 20, aria="Output", style="font-size:11px")}</div>')
    ins = ''
    for s in t.get('inserts', []) + [''] * (4 - len(t.get('inserts', []))):
        if s == '':
            ins += f'<div style="height:19px;border-radius:3px;border:1px dashed #3E434E"></div>'
        elif isinstance(s, tuple):
            ins += planned(f'<div style="height:19px;box-sizing:border-box;border-radius:3px;background:{C["raised"]};border:1px solid {C["border"]};font-size:10.5px;padding:0 5px;line-height:17px;white-space:nowrap;overflow:hidden">{s[0]}</div>', s[1], pos=None)
        else:
            ins += f'<div style="height:19px;box-sizing:border-box;border-radius:3px;background:{C["raised"]};border:1px solid {C["border"]};font-size:10.5px;padding:0 5px;line-height:17px;white-space:nowrap;overflow:hidden">{s}</div>'
    inserts = f'<div style="display:flex;flex-direction:column;gap:2px;padding:0 5px"><span style="font-size:9.5px;font-weight:700;letter-spacing:.06em;color:{C["dim"]}">INSERTS</span>{ins}</div>'
    sends = planned(f'<div style="display:flex;flex-direction:column;gap:3px;padding:0 5px"><span style="font-size:9.5px;font-weight:700;letter-spacing:.06em;color:{C["text"]}">SENDS</span>'
                    f'<div style="display:flex;justify-content:space-around">'
                    f'<div style="display:flex;flex-direction:column;align-items:center">{knob(24, t.get("s1", .3), "#7FBFFF")}<span style="font-size:9.5px">Reverb</span></div>'
                    f'<div style="display:flex;flex-direction:column;align-items:center">{knob(24, t.get("s2", .1), "#7FBFFF")}<span style="font-size:9.5px">Delay</span></div></div></div>', 'P3' if first else '', pos='tr' if first else None)
    pan = f'<div style="display:flex;flex-direction:column;align-items:center;gap:2px">{knob_btn(30, t["pan"], "Pan " + t["panl"], bipolar=True)}<span style="font-size:10.5px;color:{C["dim"]}">{t["panl"]}</span></div>'
    FH = 270
    thumb_y = round((1 - t['vol']) * (FH - 20))
    scale = ''.join(f'<span style="position:absolute;right:0;top:{round(y * (FH - 20)) + 4}px;font-size:9px;color:{C["dim"]}">{l}</span>' for l, y in [('+6', 0), ('0', .26), ('-6', .4), ('-12', .54), ('-24', .72), ('-48', .9)])
    fader = (f'<div style="position:relative;width:34px;height:{FH}px;flex:none">'
             f'<div style="position:absolute;left:15px;top:6px;bottom:6px;width:4px;border-radius:2px;background:{C["display"]};border:1px solid {C["border"]}"></div>'
             f'<button type="button" aria-label="Volume {t["db"]}" style="position:absolute;left:3px;top:{thumb_y}px;width:28px;height:20px;padding:0;border-radius:3px;border:1px solid #15171C;background:#D9DDE3;cursor:pointer">'
             f'<span style="display:block;height:2px;margin:0 4px;background:#1B1D22"></span></button></div>')
    meter = planned(meter_v(FH - 12, t.get('ml', .7), t.get('mr', .66), 5), 'P3' if first else '', pos='badgel' if first else None, style='display:flex;align-items:flex-start;padding-top:6px')
    fsec = (f'<div style="display:flex;justify-content:center;gap:4px;padding:0 4px"><div style="position:relative;width:22px;height:{FH}px">{scale}</div>{fader}{meter}</div>')
    val = (f'<div style="display:flex;justify-content:space-between;padding:0 6px;font-size:10.5px;font-variant-numeric:tabular-nums"><span>{t["db"]}</span>'
           f'{pinline("<span>" + t.get("peak", "-6.1") + "</span>", None)}</div>')
    buttons = f'<div style="display:flex;justify-content:center">{msr(r=t.get("arm", False), show_r=(kind == "track"), h=20, w=24)}</div>'
    lufs = planned(f'<div style="text-align:center;font-size:10px;color:{C["text"]};font-variant-numeric:tabular-nums">{t.get("lufs", "-18.4")} LUFS</div>', '', pos=None)
    inner = f'{name}{io}{inserts}{sends}{pan}{fsec}{val}{buttons}{lufs}'
    box = f'width:{W}px;flex:none;box-sizing:border-box;display:flex;flex-direction:column;gap:8px;padding-bottom:8px;background:{C["panel"]};border:1px solid {C["border"]};border-radius:5px'
    if planned_strip:
        return planned(inner, kind, style=box, inner_style='display:flex;flex-direction:column;gap:8px', pos='tr' if t.get('chip') else None)
    return f'<div style="{box}">{inner}</div>'

def mixer_view():
    data = {
        'drums': dict(inserts=['EQ'], peak='-5.2', lufs='-17.9', ml=.74, mr=.72),
        '808': dict(inserts=['EQ', ('Comp ← Kick (sidechain)', 'P3')], peak='-6.8', lufs='-19.2', ml=.7, mr=.7, s1=.0, s2=.0),
        'keys': dict(inserts=['Surge XT', 'Chorus'], peak='-11.0', lufs='-24.6', ml=.55, mr=.6, s1=.45),
        'vox': dict(inserts=['EQ', 'Compressor', 'Reverb'], peak='-4.9', lufs='-16.8', ml=.78, mr=.78, s1=.35, s2=.2),
        'adlibs': dict(inserts=['EQ'], peak='-9.7', lufs='-23.1', ml=.6, mr=.52, s1=.5, s2=.4),
    }
    strips = ''
    for i, t in enumerate(TRACKS):
        tt = dict(t); tt.update(data[t['id']])
        tt['in'] = 'In 1 (mono)' if t['kind'] == 'audio' else 'MIDI: All'
        tt['inserts'] = ([t['dev']] if t['kind'] == 'inst' and t['dev'] not in data[t['id']]['inserts'] else []) + data[t['id']]['inserts']
        tt['inserts'] = tt['inserts'][:4]
        strips += strip(tt, first=(i == 0))
    bus = dict(name='Vox Bus', color=TC['bus'], vol=.7, db='0.0 dB', pan=.5, panl='C', inserts=['Comp', 'Saturation'], **{'in': 'Lead Vox, Adlibs', 'out': 'Main out', 'chip': True})
    ret = dict(name='Reverb Return', color='#7FBFFF', vol=.6, db='-6.0 dB', pan=.5, panl='C', inserts=['Plate Reverb', 'EQ'], **{'in': 'Send A', 'out': 'Main out', 'chip': True})
    mst = dict(name='Master', color='#E6E6E6', vol=.72, db='0.0 dB', pan=.5, panl='C', inserts=['Mastering chain'], peak='-1.2', lufs='-14.1', **{'in': 'All tracks', 'out': 'Headphones', 'chip': True})
    strips += strip(bus, kind='P3') + strip(ret, kind='P3') + f'<div style="width:6px;flex:none"></div>' + strip(mst, kind='P3')
    hdr = (f'<div style="height:34px;flex:none;display:flex;align-items:center;gap:10px;padding:0 12px;background:{C["bar"]};border-bottom:1px solid {C["line"]}">'
           f'<span style="font-size:13px;font-weight:700">Mix</span>{chip("P3")}'
           + pinline(f'<span style="font-size:11px;color:{C["text"]}">Meters</span><div style="display:flex">{seg_btn("Peak", False, True, False)}{seg_btn("RMS", False, False, False)}{seg_btn("LUFS", True, False, True)}</div>'
                     f'<span style="font-size:11px;color:{C["text"]};margin-left:8px">Show</span>{checkbox("Sends", True, "mx-s")}{checkbox("Inputs/outputs", True, "mx-io")}{btn("Add bus", "plus", h=24)}')
           + f'<div style="flex:1"></div><span style="font-size:11px;color:{C["dim"]}">Track volume, pan, mute, solo, arm, inputs, outputs and effect lists are the same data as the Arrange track headers.</span></div>')
    assistant = planned(
        f'<div style="display:flex;flex-direction:column;gap:12px;padding:12px;height:100%;box-sizing:border-box">'
        f'<div style="display:flex;align-items:center;gap:8px">{icon("wand", 16)}<span style="font-size:13px;font-weight:700">Mix assistant</span></div>'
        f'<span style="font-size:11px;color:{C["text"]}">Listens to the whole song and suggests one fix at a time.</span>'
        f'<div style="display:flex;flex-direction:column;gap:8px;padding:10px;border-radius:6px;background:{C["raised"]};border:1px solid {C["border"]}">'
        f'<span style="font-size:10px;font-weight:700;letter-spacing:.06em">1 OF 4 · LOW END</span>'
        f'<span style="font-size:13px;font-weight:600;line-height:1.35">Kick and 808 are fighting around 55 Hz</span>'
        f'<span style="font-size:11.5px;line-height:1.45">When the kick hits, duck the 808 for a moment so both are heard.</span>'
        f'<div style="display:flex;gap:6px">{btn("Apply sidechain", "check", "primary", h=26)}{btn("Skip", h=26)}</div></div>'
        + ''.join(f'<div style="display:flex;align-items:center;gap:8px;font-size:12px;padding:6px 2px;border-bottom:1px solid {C["border"]}"><span style="width:8px;height:8px;border-radius:4px;background:{c}"></span>{t}</div>'
                  for t, c in [('Mud: Keys build up at 300 Hz', '#D9B23A'), ('Lead Vox sits 2 dB under the beat in the hook', '#D9B23A'), ('No clipping found', '#2F9A5C')])
        + f'<div style="flex:1"></div>{btn("Listen again", "refresh", h=26)}</div>', 'P4', style=f'width:280px;flex:none;background:{C["panel"]};border-left:1px solid {C["line"]}', inner_style='height:100%')
    return (f'<section aria-label="Mix" style="flex:1;min-height:0;display:flex;flex-direction:column;background:{C["bg"]}">{hdr}'
            f'<div style="flex:1;min-height:0;display:flex"><div style="flex:1;min-width:0;display:flex;gap:4px;padding:10px;overflow:hidden;align-items:flex-start">{strips}</div>{assistant}</div></section>')

# ---------------------------------------------------------------- master
def master_view():
    def mod(title, body, w):
        return (f'<div style="width:{w}px;flex:none;box-sizing:border-box;display:flex;flex-direction:column;gap:8px;padding:10px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
                f'<div style="display:flex;align-items:center;gap:6px">{icon("power", 13, "#7FE0A8")}<span style="font-size:12px;font-weight:700">{title}</span></div>{body}</div>')
    eq = mod('EQ', f'<div style="height:110px;border-radius:4px;background:{C["display"]};position:relative;overflow:hidden">'
             f'<svg width="236" height="110" viewBox="0 0 236 110" aria-hidden="true"><path d="M0 92C30 70 50 84 80 80S140 60 170 70 220 56 236 60V110H0Z" fill="#2A3B52"></path>'
             f'<path d="M0 60C30 62 40 52 60 56S110 60 140 58 190 46 236 44" stroke="#7FBFFF" stroke-width="1.8" fill="none"></path>'
             f'<circle cx="60" cy="56" r="4" fill="#E6E6E6"></circle><circle cx="140" cy="58" r="4" fill="#E6E6E6"></circle><circle cx="200" cy="46" r="4" fill="#E6E6E6"></circle></svg></div>'
             f'<span style="font-size:11px">Low shelf +1.0 dB · Air +1.5 dB @ 12 kHz</span>', 260)
    mb = ''.join(f'<div style="display:flex;flex-direction:column;align-items:center;gap:4px;flex:1"><div style="width:100%;height:84px;background:{C["display"]};border-radius:3px;display:flex;flex-direction:column-reverse">'
                 f'<div style="height:{h}px;background:#3B78B8"></div></div><span style="font-size:11px">{n}</span><span style="font-size:10.5px">{v}</span></div>'
                 for n, h, v in [('Low', 52, '-2.1 dB'), ('Mid', 40, '-1.0 dB'), ('High', 30, '-0.6 dB')])
    mbm = mod('Multiband', f'<div style="display:flex;gap:6px">{mb}</div>', 200)
    st = mod('Stereo', f'<div style="display:flex;justify-content:center;gap:16px"><div style="display:flex;flex-direction:column;align-items:center;gap:3px">{knob(48, .55)}<span style="font-size:11px">Width 110%</span></div></div>'
             f'{checkbox("Mono below 120 Hz", True, "ms-mono")}', 170)
    lim = mod('Limiter', f'<div style="display:flex;gap:12px;justify-content:center"><div style="display:flex;flex-direction:column;align-items:center;gap:3px">{knob(44, .6)}<span style="font-size:11px">Gain +4.5 dB</span></div>'
              f'<div style="display:flex;flex-direction:column;align-items:center;gap:3px">{knob(44, .9)}<span style="font-size:11px">Ceiling -1.0</span></div></div>'
              f'<div style="display:flex;align-items:center;gap:6px;font-size:10.5px">Reduction<div style="flex:1;height:6px;background:{C["display"]}"><div style="width:28%;height:6px;background:#D9B23A;margin-left:auto"></div></div>-2.4</div>', 200)
    dith = mod('Dither', f'{radio("Auto", "dith", True, "only when bit depth drops", "dith-a")}{radio("Off", "dith", False, None, "dith-o")}{radio("TPDF", "dith", False, None, "dith-t")}', 170)
    chain = (f'<div style="display:flex;flex-direction:column;gap:8px"><div style="display:flex;align-items:center;gap:10px"><span style="font-size:13px;font-weight:700">Mastering chain</span>'
             f'{select("Streaming, loud &amp; clean", 200, aria="Mastering preset")}{btn("A/B chain on and off", h=24)}</div>'
             f'<div style="display:flex;gap:8px;align-items:stretch">{eq}{icon("right", 14)}{mbm}{icon("right", 14)}{st}{icon("right", 14)}{lim}{icon("right", 14)}{dith}</div></div>')
    targets = [('Spotify', '-14 LUFS · -1 dBTP', True), ('Apple Music', '-16 LUFS · -1 dBTP', False), ('YouTube', '-14 LUFS · -1 dBTP', False), ('Club', '-8 LUFS · -0.3 dBTP', False), ('CD', '-9 LUFS · -0.1 dBTP', False)]
    tg = ''.join(f'<button type="button" aria-pressed="{"true" if a else "false"}" style="display:flex;flex-direction:column;align-items:flex-start;gap:2px;padding:7px 10px;border-radius:5px;'
                 f'background:{"#1D3550" if a else C["raised"]};border:1px solid {C["accent"] if a else C["border"]};color:{C["text"]};font-family:{FONT};cursor:pointer;text-align:left">'
                 f'<span style="font-size:12px;font-weight:600">{n}</span><span style="font-size:10.5px">{v}</span></button>' for n, v, a in targets)
    big = lambda lab, val, unit, ok=True: (f'<div style="display:flex;flex-direction:column;gap:2px;padding:10px 12px;border-radius:5px;background:{C["display"]};border:1px solid {C["line"]};min-width:110px">'
                                           f'<span style="font-size:10px;font-weight:700;letter-spacing:.06em;color:{C["lcd2"]}">{lab}</span>'
                                           f'<span style="font-size:24px;font-weight:600;color:{C["lcd"] if ok else "#FFB35C"};font-variant-numeric:tabular-nums">{val}<span style="font-size:11px;margin-left:4px">{unit}</span></span></div>')
    hist_pts = ' '.join(f'{x},{y}' for x, y in [(0, 90), (60, 84), (120, 46), (240, 42), (420, 44), (480, 30), (600, 26), (660, 40), (720, 44)])
    hist = (f'<div style="position:relative;height:120px;border-radius:5px;background:{C["display"]};overflow:hidden">'
            f'<svg width="720" height="120" viewBox="0 0 720 120" preserveAspectRatio="none" aria-hidden="true">'
            f'<path d="M0 36H720" stroke="#4AA3FF" stroke-dasharray="4 4"></path><polyline points="{hist_pts}" fill="none" stroke="#7FE0A8" stroke-width="2"></polyline>'
            f'<path d="M120 0V120M480 0V120M660 0V120" stroke="#3A3F4B"></path></svg>'
            f'<span style="position:absolute;left:6px;top:4px;font-size:10px;color:#C8CDD5">Intro</span><span style="position:absolute;left:126px;top:4px;font-size:10px;color:#C8CDD5">Verse 1</span>'
            f'<span style="position:absolute;left:486px;top:4px;font-size:10px;color:#C8CDD5">Hook</span><span style="position:absolute;left:666px;top:4px;font-size:10px;color:#C8CDD5">Verse 2</span>'
            f'<span style="position:absolute;right:6px;top:24px;font-size:10px;color:#7FBFFF">target -14</span></div>')
    loud = (f'<div style="flex:1;min-width:0;display:flex;flex-direction:column;gap:10px;padding:12px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
            f'<div style="display:flex;align-items:center;gap:10px"><span style="font-size:13px;font-weight:700">Loudness</span><span style="font-size:11px">Where will this be played?</span></div>'
            f'<div style="display:flex;gap:6px;flex-wrap:wrap">{tg}</div>'
            f'<div style="display:flex;gap:8px">{big("INTEGRATED", "-14.1", "LUFS")}{big("SHORT-TERM", "-12.8", "LUFS")}{big("TRUE PEAK", "-0.4", "dBTP", False)}{big("RANGE", "6.2", "LU")}</div>'
            f'<div style="display:flex;align-items:center;gap:10px;font-size:12px">{icon("help", 14, "#FFB35C")}<span>Loudness is on target. True peak is 0.6 dB too hot for Spotify.</span>{btn("Match target", "check", "primary", h=26)}</div>'
            f'{hist}</div>')
    ref = (f'<div style="display:flex;flex-direction:column;gap:8px;padding:12px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
           f'<span style="font-size:13px;font-weight:700">Reference A/B</span>'
           f'<div style="display:flex;align-items:center;gap:8px;height:32px;padding:0 8px;border-radius:4px;background:{C["display"]}">{icon("wave", 14)}<span style="font-size:12px;flex:1">Boom Bap Classic ref.wav</span><span style="font-size:10.5px">-9.8 LUFS</span></div>'
           f'<div style="display:flex;gap:6px;align-items:center"><div style="display:flex">{seg_btn("A: My mix", True, True, False)}{seg_btn("B: Reference", False, False, True)}</div></div>'
           f'{checkbox("Match loudness (fair comparison)", True, "ref-m")}</div>')
    asst = (f'<div style="flex:1;display:flex;flex-direction:column;gap:8px;padding:12px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
            f'<div style="display:flex;align-items:center;gap:8px">{icon("wand", 15)}<span style="font-size:13px;font-weight:700">Master assistant</span></div>'
            f'<span style="font-size:12.5px;font-weight:600;line-height:1.35">Your low end is 2.1 dB louder than the reference.</span>'
            f'<span style="font-size:11.5px;line-height:1.45">Lower the Multiband low band by 2 dB, then listen with A/B.</span>'
            f'<div style="display:flex;gap:6px">{btn("Try it", "check", "primary", h=26)}{btn("Skip", h=26)}</div></div>')
    right = f'<div style="width:330px;flex:none;display:flex;flex-direction:column;gap:10px">{ref}{asst}</div>'
    plannedpart = planned(f'<div style="display:flex;flex-direction:column;gap:12px;height:100%">{chain}<div style="flex:1;min-height:0;display:flex;gap:10px">{loud}{right}</div></div>', '', pos=None, style='flex:1;min-height:0', inner_style='height:100%')
    out = (f'<div style="height:48px;flex:none;display:flex;align-items:center;gap:12px;padding:0 12px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">'
           f'{icon("headphones", 16)}<span style="font-size:12px;font-weight:600">Main output</span>{select("Headphones · Realtek USB Audio (Out 3-4)", 300, aria="Main output")}{btn("Test beep", "speaker", h=26)}'
           f'<span style="font-size:11px;color:{C["dim"]}">Same setting as Setup › Audio &amp; Routing.</span><div style="flex:1"></div>'
           + planned(btn('Export…', 'export', 'primary', h=28), 'P4', pos='badge', style='display:flex') + '</div>')
    hdr = (f'<div style="height:34px;flex:none;display:flex;align-items:center;gap:10px;padding:0 12px;background:{C["bar"]};border-bottom:1px solid {C["line"]}">'
           f'<span style="font-size:13px;font-weight:700">Master</span>{chip("P4")}<span style="font-size:11px;color:{C["dim"]}">Final polish for the whole song. Only the main output below works today.</span></div>')
    return (f'<section aria-label="Master" style="flex:1;min-height:0;display:flex;flex-direction:column;background:{C["bg"]}">{hdr}'
            f'<div style="flex:1;min-height:0;display:flex;flex-direction:column;gap:10px;padding:12px">{plannedpart}{out}</div></section>')

# ---------------------------------------------------------------- dialogs
def dialog(title, ph, body, footer, w, h, sub=''):
    x = (1440 - w) // 2; y = (900 - h) // 2
    ttl = f'<span style="font-size:15px;font-weight:700">{title}</span>' + (chip(ph) if ph else '') + (f'<span style="font-size:11.5px;color:{C["dim"]}">{sub}</span>' if sub else '')
    return (f'<div style="position:absolute;inset:0;background:rgba(8,9,12,.62);z-index:30"></div>'
            f'<div role="dialog" aria-modal="true" aria-label="{title}" style="position:absolute;left:{x}px;top:{y}px;width:{w}px;height:{h}px;z-index:31;box-sizing:border-box;display:flex;flex-direction:column;'
            f'background:{C["panel"]};border:1px solid #4A505C;border-radius:8px;box-shadow:0 16px 48px rgba(0,0,0,.55);overflow:hidden">'
            f'<div style="height:46px;flex:none;display:flex;align-items:center;gap:10px;padding:0 10px 0 18px;border-bottom:1px solid {C["line"]};background:{C["bar"]}">{ttl}<div style="flex:1"></div>'
            f'{btn("", "x", "ghost", aria="Close", h=28)}</div>{body}'
            f'<div style="height:56px;flex:none;display:flex;align-items:center;gap:8px;padding:0 18px;border-top:1px solid {C["line"]};background:{C["bar"]}">{footer}</div></div>')

def export_dialog():
    presets = [('Streaming release', 'WAV 24-bit · -14 LUFS', True), ('Send to a friend', 'MP3 320 kbps', False), ('Stems for a collab', 'WAV per track', False),
               ('Club / DJ set', 'WAV 24-bit · -8 LUFS', False), ('CD master', 'WAV 16-bit · dither', False), ('MIDI for MC-707', 'MIDI per track', False)]
    pl = ''.join(f'<button type="button" aria-pressed="{"true" if a else "false"}" style="display:flex;flex-direction:column;align-items:flex-start;gap:2px;width:100%;padding:8px 10px;border:none;border-radius:5px;'
                 f'background:{C["selrow"] if a else "transparent"};color:{C["text"]};font-family:{FONT};text-align:left;cursor:pointer"><span style="font-size:12.5px;font-weight:600">{n}</span>'
                 f'<span style="font-size:11px">{s}</span></button>' for n, s, a in presets)
    left = (f'<div style="width:220px;flex:none;display:flex;flex-direction:column;gap:2px;padding:12px;border-right:1px solid {C["line"]}">{heading("Export presets")}{pl}'
            f'<div style="flex:1"></div>{btn("Save as preset", "bookmark", h=26)}</div>')
    sec = lambda t, inner: f'<div style="display:flex;flex-direction:column;gap:8px">{heading(t)}{inner}</div>'
    what = (f'<div style="display:grid;grid-template-columns:repeat(2, minmax(0, 1fr));gap:8px">{radio("Full mix", "what", True, "one stereo file", "ex-w1")}{radio("Stems", "what", False, "one file per track", "ex-w2")}'
            f'{radio("Bus stems", "what", False, "one file per bus", "ex-w3")}{radio("MIDI", "what", False, "notes only", "ex-w4")}</div>')
    rng = f'<div style="display:flex;gap:16px">{radio("Whole song", "rng", True, "1.1 to 17.1", "ex-r1")}{radio("Loop", "rng", False, "bars 11 to 15", "ex-r2")}{radio("Selection", "rng", False, None, "ex-r3")}</div>'
    fmt = (f'<div style="display:flex;align-items:center;gap:14px;flex-wrap:wrap"><div style="display:flex">{seg_btn("WAV", True, True, False)}{seg_btn("FLAC", False, False, False)}{seg_btn("MP3", False, False, True)}</div>'
           f'{field("Sample rate", select("48 kHz", 86))}{field("Bit depth", select("24-bit", 76))}{field("Dither", select("Auto", 70))}</div>')
    loud = (f'<div style="display:flex;flex-direction:column;gap:8px">{checkbox("Use the mastering chain", True, "ex-mc")}'
            f'<div style="display:flex;align-items:center;gap:10px">{checkbox("Match loudness target", True, "ex-lt")}{select("Spotify · -14 LUFS · -1 dBTP", 220, aria="Loudness target")}</div>'
            f'<span style="font-size:11.5px">Measured now: -14.1 LUFS, true peak -0.4 dBTP. The limiter will pull peaks down to -1.0.</span></div>')
    fil = (f'<div style="display:flex;flex-direction:column;gap:8px"><label for="ex-name" style="display:flex;align-items:center;gap:8px;font-size:11px">File name'
           f'<input id="ex-name" value="Late Night Cypher - Master.wav" style="flex:1;height:26px;padding:0 8px;border-radius:4px;background:{C["display"]};border:1px solid {C["border"]};color:{C["text"]};font-family:{FONT};font-size:12px"></label>'
           f'<div style="display:flex;align-items:center;gap:8px;font-size:11px">Folder<span style="flex:1;height:26px;line-height:26px;padding:0 8px;border-radius:4px;background:{C["display"]};border:1px solid {C["border"]};font-size:12px">C:\\Users\\Music\\EzDAWsIt\\Exports</span>{btn("Browse", "open", h=26)}</div></div>')
    right = f'<div style="flex:1;display:flex;flex-direction:column;gap:18px;padding:16px 20px">{sec("What to export", what)}{sec("Range", rng)}{sec("Format", fmt)}{sec("Loudness", loud)}{sec("Save to", fil)}</div>'
    body = planned(f'<div style="display:flex;height:100%">{left}{right}</div>', '', pos=None, style='flex:1;min-height:0', inner_style='height:100%')
    foot = (pinline(f'<span style="font-size:12px">About 12 MB · 0:42 · faster than real time</span>') + '<div style="flex:1"></div>' + btn('Cancel', h=30)
            + planned(btn('Export', 'export', 'primary', h=30), '', pos=None, style='display:flex'))
    return dialog('Export song', 'P4', body, foot, 880, 660, 'Late Night Cypher')

def setup_dialog():
    tabs = [('Audio &amp; Routing', None, True), ('Plugins', None, False), ('MIDI &amp; Controllers', 'P5', False), ('Recording', 'P5', False), ('General', None, False)]
    tl = ''
    for n, ph, a in tabs:
        b = (f'<button type="button" aria-pressed="{"true" if a else "false"}" style="display:flex;align-items:center;width:100%;height:32px;padding:0 12px;border:none;border-radius:5px;'
             f'background:{C["selrow"] if a else "transparent"};color:{C["text"]};font-family:{FONT};font-size:12.5px;font-weight:{600 if a else 500};text-align:left;cursor:pointer">{n}</button>')
        tl += pinline(b, ph, style='width:100%') if ph else b
    left = (f'<nav aria-label="Setup sections" style="width:200px;flex:none;display:flex;flex-direction:column;gap:2px;padding:12px;border-right:1px solid {C["line"]}">{tl}<div style="flex:1"></div>'
            + planned(f'<div style="display:flex;flex-direction:column;gap:6px;padding:10px;border-radius:6px;background:{C["raised"]}"><span style="font-size:12px;font-weight:600">New here?</span>'
                      f'<span style="font-size:11px;line-height:1.4">The setup wizard checks your output, a test beep and your mic level.</span>{btn("Run setup wizard", "wand", h=26)}</div>', 'P5', pos='tr') + '</nav>')
    dev = (f'<div style="display:flex;flex-direction:column;gap:10px">{heading("Audio device")}'
           f'<div style="display:grid;grid-template-columns:110px 1fr;gap:8px 10px;align-items:center;font-size:12px">'
           f'<span style="color:{C["dim"]}">Driver</span>{select("Windows Audio", 230, aria="Driver")}'
           f'<span style="color:{C["dim"]}">Output device</span>{select("Realtek USB Audio", 230, aria="Output device")}'
           f'<span style="color:{C["dim"]}">Input device</span>{select("Realtek USB Audio", 230, aria="Input device")}'
           f'<span style="color:{C["dim"]}">Sample rate</span>{select("48000 Hz", 140, aria="Sample rate")}'
           f'<span style="color:{C["dim"]}">Buffer size</span><div style="display:flex;align-items:center;gap:10px">{select("256 samples", 140, aria="Buffer size")}<span style="color:{C["dim"]};font-size:11px">5.3 ms in · 5.3 ms out</span></div></div>'
           f'<span style="font-size:11px;color:{C["dim"]};line-height:1.4">Smaller buffer: less delay when recording. Bigger buffer: fewer clicks with heavy plugins.</span></div>')
    outs = [('Headphones', 'Realtek USB Audio · Out 3-4', True, 'headphones'), ('Speakers', 'Realtek USB Audio · Out 1-2', False, 'speaker')]
    ol = ''.join(f'<div style="display:flex;align-items:center;gap:10px;padding:10px 12px;border-radius:6px;background:{"#1D3550" if a else C["raised"]};border:1px solid {C["accent"] if a else C["border"]}">'
                 f'<input id="out-{i}" type="radio" name="mainout"{" checked" if a else ""} style="margin:0;width:14px;height:14px;accent-color:{C["accent"]}">'
                 f'<label for="out-{i}" style="display:flex;align-items:center;gap:8px;flex:1;cursor:pointer">{icon(ic, 16)}<span style="display:flex;flex-direction:column;gap:1px"><span style="font-size:12.5px;font-weight:600">{n}</span>'
                 f'<span style="font-size:11px;color:{"#C9D6E6" if a else C["dim"]}">{s}</span></span></label>{btn("Test beep", "speaker", h=26)}</div>' for i, (n, s, a, ic) in enumerate(outs))
    mo = (f'<div style="display:flex;flex-direction:column;gap:8px">{heading("Main output: where you hear EzDAWsIt")}{ol}</div>')
    rows = [('Drums', 'MIDI: All inputs', 'Main out'), ('808', 'MIDI: All inputs', 'Main out'), ('Keys', 'MIDI: All inputs', 'Main out'), ('Lead Vox', 'In 1 · Mic (mono)', 'Main out'), ('Adlibs', 'In 1 · Mic (mono)', 'Main out')]
    cols = {'Drums': TC['drums'], '808': TC['b808'], 'Keys': TC['keys'], 'Lead Vox': TC['vox'], 'Adlibs': TC['adlibs']}
    tr = ''.join(f'<div style="display:grid;grid-template-columns:150px 200px 160px 1fr;gap:10px;align-items:center;height:32px;border-bottom:1px solid {C["line"]}">'
                 f'<span style="display:flex;align-items:center;gap:6px;font-size:12px"><span style="width:8px;height:8px;border-radius:2px;background:{cols[n]}"></span>{n}</span>'
                 f'{select(i, 190, aria=n + " input")}{select(o, 150, aria=n + " output")}'
                 + (planned(btn('Check level', 'mic', h=24), 'P5', pos='badge', style='display:flex;justify-self:start') if n == 'Lead Vox' else '<span></span>') + '</div>' for n, i, o in rows)
    tio = (f'<div style="display:flex;flex-direction:column;gap:6px">{heading("Track inputs and outputs")}'
           f'<div style="display:grid;grid-template-columns:150px 200px 160px 1fr;gap:10px;font-size:11px;color:{C["dim"]}"><span>Track</span><span>Input</span><span>Output</span><span></span></div>{tr}</div>')
    body = (f'<div style="flex:1;min-height:0;display:flex">{left}<div style="flex:1;display:flex;flex-direction:column;gap:20px;padding:16px 22px;overflow:hidden">'
            f'<div style="display:grid;grid-template-columns:repeat(2, minmax(0, 1fr));gap:24px">{dev}{mo}</div>{tio}</div></div>')
    foot = f'<span style="font-size:11.5px;color:{C["dim"]}">Changes apply right away. Playback pauses while the device restarts.</span><div style="flex:1"></div>{btn("Done", kind="primary", h=30, style="padding:0 18px")}'
    return dialog('Setup', None, body, foot, 1000, 660)

# ---------------------------------------------------------------- start screen
def start_view():
    recent = [('Late Night Cypher', '92 BPM · G minor · 5 tracks', 'Today 14:02'), ('Dusty Loop Idea', '88 BPM · 3 tracks', 'Yesterday'),
              ('Roller 174', '174 BPM · 6 tracks', '3 Nov'), ('MC-707 Jam Export', '120 BPM · 8 tracks', '28 Oct'), ('Sunday Voice Memo', 'Audio · 1 track', '19 Oct')]
    rl = ''.join(f'<button type="button" style="display:flex;align-items:center;gap:10px;width:100%;padding:8px 10px;border:none;border-radius:5px;background:{C["selrow"] if i == 0 else "transparent"};'
                 f'color:{C["text"]};font-family:{FONT};text-align:left;cursor:pointer">{icon("note", 16, C["dim"])}<span style="display:flex;flex-direction:column;gap:1px;flex:1;min-width:0">'
                 f'<span style="font-size:13px;font-weight:600">{n}</span><span style="font-size:11px;color:{C["dim"]}">{s}</span></span><span style="font-size:11px;color:{C["dim"]}">{d}</span></button>'
                 for i, (n, s, d) in enumerate(recent))
    recover = (f'<div style="display:flex;flex-direction:column;gap:8px;padding:12px;border-radius:6px;background:#3A2A1A;border:1px solid #D99A4A">'
               f'<div style="display:flex;align-items:center;gap:8px">{icon("restore", 16, "#FFC27A")}<span style="font-size:12.5px;font-weight:600">Unsaved work found</span></div>'
               f'<span style="font-size:12px;line-height:1.45">EzDAWsIt closed unexpectedly while "Late Night Cypher" had changes from 23:41 yesterday.</span>'
               f'<div style="display:flex;gap:6px">{btn("Restore it", "restore", "primary", h=28)}{btn("Discard", h=28)}</div></div>')
    leftc = (f'<div style="width:380px;flex:none;display:flex;flex-direction:column;gap:16px">'
             f'<div style="display:flex;flex-direction:column;gap:4px"><span style="font-size:22px;font-weight:700">Make something</span>'
             f'<span style="font-size:12.5px;color:{C["dim"]}">Start empty, pick up where you left off, or use a template.</span></div>'
             f'<div style="display:flex;gap:8px">{btn("New empty song", "new", "primary", h=36, style="font-size:13px;padding:0 14px")}{btn("Open song…", "open", h=36, style="font-size:13px")}</div>'
             f'{recover}<div style="display:flex;flex-direction:column;gap:2px">{heading("Recent songs")}{rl}</div></div>')
    tpls = [('Hip hop beat', '92 BPM', ['Drums', '808', 'Keys', 'Lead Vox', 'Adlibs'], [TC['drums'], TC['b808'], TC['keys'], TC['vox'], TC['adlibs']]),
            ('Drum &amp; bass', '174 BPM', ['Break', 'Reese bass', 'Pads', 'FX'], ['#E58A3A', '#B65BD6', '#3FA36B', '#7F8CFF']),
            ('House', '124 BPM', ['Kick', 'Hats', 'Bass', 'Chords', 'Vocal'], ['#E58A3A', '#3FB8C8', '#B65BD6', '#3FA36B', '#4AA3FF']),
            ('Pop vocal song', '100 BPM', ['Drums', 'Bass', 'Piano', 'Lead Vox', 'Backing Vox'], ['#E58A3A', '#B65BD6', '#3FA36B', '#4AA3FF', '#2FB8B0']),
            ('Singer-songwriter', '84 BPM', ['Acoustic Gtr', 'Lead Vox', 'Harmony'], ['#D9A441', '#4AA3FF', '#2FB8B0']),
            ('Band demo', '120 BPM', ['Drums', 'Bass', 'Elec Gtr', 'Vox'], ['#E58A3A', '#B65BD6', '#D9A441', '#4AA3FF']),
            ('Lo-fi beat', '80 BPM', ['Drums', 'Keys', 'Vinyl', 'Bass'], ['#E58A3A', '#3FA36B', '#8A93A6', '#B65BD6']),
            ('Podcast', 'Speech', ['Host', 'Guest', 'Music bed'], ['#4AA3FF', '#2FB8B0', '#8A93A6'])]
    cards = ''
    for n, bpm, trs, cols in tpls:
        stripes = ''.join(f'<div style="height:9px;border-radius:2px;background:{c};width:{60 + (i * 37) % 40}%"></div>' for i, c in enumerate(cols))
        cards += (f'<button type="button" style="display:flex;flex-direction:column;gap:8px;padding:12px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]};'
                  f'color:{C["text"]};font-family:{FONT};text-align:left;cursor:pointer;min-height:200px"><div style="display:flex;flex-direction:column;gap:4px;width:100%;padding:8px;box-sizing:border-box;border-radius:4px;background:{C["lane"]}">{stripes}</div>'
                  f'<span style="font-size:13px;font-weight:600">{n}</span><span style="font-size:11px">{bpm} · {", ".join(trs)}</span></button>')
    gallery = (f'<div style="display:flex;flex-direction:column;gap:12px"><div style="display:flex;align-items:center;gap:10px"><span style="font-size:15px;font-weight:700">Start from a template</span>'
               f'<span style="font-size:11.5px">Tracks, sounds and effect chains ready to record into.</span></div>'
               f'<div style="display:grid;grid-template-columns:repeat(4, minmax(0, 1fr));gap:10px">{cards}</div></div>')
    gallery = planned(gallery, 'P4', pos='tr')
    wiz = planned(f'<div style="display:flex;align-items:center;gap:14px;padding:14px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]}">{icon("wand", 22)}'
                  f'<div style="display:flex;flex-direction:column;gap:3px;flex:1"><span style="font-size:13px;font-weight:600">First time? Set up in three steps</span>'
                  f'<span style="font-size:11.5px">Pick where you hear sound, play a test beep, check your mic level.</span></div>{btn("Start setup wizard", h=30)}{btn("Take the tour", h=30)}</div>', 'P5', pos='tr', style='flex:1')
    audio = (f'<div style="display:flex;align-items:center;gap:12px;padding:14px;border-radius:6px;background:{C["panel"]};border:1px solid {C["border"]};width:420px;box-sizing:border-box">{icon("headphones", 20)}'
             f'<div style="display:flex;flex-direction:column;gap:3px;flex:1"><span style="font-size:13px;font-weight:600">Headphones · Realtek USB Audio</span>'
             f'<span style="font-size:11.5px;color:{C["dim"]}">48 kHz · 256 samples · 5.3 ms</span></div>{btn("Test beep", "speaker", h=30)}{btn("Setup", "sliders", h=30)}</div>')
    rightc = f'<div style="flex:1;min-width:0;display:flex;flex-direction:column;gap:20px">{gallery}<div style="display:flex;gap:12px;align-items:stretch">{wiz}{audio}</div></div>'
    return f'<main style="flex:1;min-height:0;display:flex;gap:36px;padding:28px 36px;background:{C["bg"]}">{leftc}{rightc}</main>'

# ---------------------------------------------------------------- screens
def shell(view, center, detail, browser_sel='instruments', msg=None):
    return (app_bar(view) + transport_bar() + f'<div style="flex:1;min-height:0;display:flex">{browser(browser_sel)}'
            f'<div style="flex:1;min-width:0;display:flex;flex-direction:column">{center}{detail}</div></div>' + status_bar(msg or 'Saved. Autosave every 2 minutes; last autosave 40 s ago.'))

def collapsed_detail(name='Lead Vox', color=TC['vox']):
    return detail_header(name, color, [('Devices', False, None), ('Clip editor', False, '2C')], collapsed=True,
                         right=f'<span style="font-size:11px;color:{C["dim"]}">Select a clip or track to edit it here. Double-click a clip to open its editor.</span>')

def screen(name):
    if name == 'Main':
        return shell('arrange', arrange(sel_clip='Verse 1 (Take 3)', legend=True), collapsed_detail(), msg='Selected: Verse 1 (Take 3) on Lead Vox. Ctrl+D duplicates, arrows nudge.')
    if name == 'PianoRoll':
        det = (detail_header('Keys Hook', TC['keys'], [('Devices', False, None), ('Piano roll', True, '2C')], right=f'<span style="font-size:11px;color:{C["dim"]}">MIDI clip on Keys · bars 11 to 15</span>')
               + f'<div style="height:388px;flex:none;display:flex;flex-direction:column">{piano_roll()}</div>')
        return shell('arrange', arrange(compact=True, sel_track='keys', sel_clip='Keys Hook'), det, msg='Double-click opened Keys Hook in the piano roll.')
    if name == 'DrumMachine':
        det = (detail_header('Drums · Hook Beat', TC['drums'], [('Devices', False, None), ('Drum machine', True, '2D'), ('Steps', False, '2D')], right=f'<span style="font-size:11px;color:{C["dim"]}">Drum machine instrument on Drums · clip bars 11 to 15</span>')
               + f'<div style="height:388px;flex:none;display:flex;flex-direction:column">{drum_machine()}</div>')
        return shell('arrange', arrange(compact=True, sel_track='drums', sel_clip='Hook Beat'), det, browser_sel='kits', msg='Selected: Hook Beat on Drums.')
    if name == 'VocalChain':
        det = (detail_header('Lead Vox', TC['vox'], [('Devices', True, None), ('Clip editor', False, '2C')], right=f'<span style="font-size:11px;color:{C["dim"]}">Signal flows left to right: input, rack, effects, output</span>')
               + f'<div style="height:388px;flex:none;display:flex;flex-direction:column">{vocal_chain_panel()}</div>')
        body = shell('arrange', arrange(compact=True, sel_track='vox'), det, browser_sel='chains', msg='Lead Vox: 3 effects. Click Open to show an effect window.')
        return body + add_fx_menu(1084, 400)
    if name == 'Mixer':
        return shell('mix', mixer_view(), collapsed_detail(), browser_sel='effects', msg='Mix view. Drag an effect from the browser onto an insert slot.')
    if name == 'Master':
        return shell('master', master_view(), collapsed_detail('Master', '#E6E6E6'), browser_sel='mastering', msg='Master view.')
    if name == 'Export':
        return f'<div aria-hidden="true" style="display:flex;flex-direction:column;height:100%">{shell("arrange", arrange(sel_clip=None), collapsed_detail())}</div>' + export_dialog()
    if name == 'Setup':
        return f'<div aria-hidden="true" style="display:flex;flex-direction:column;height:100%">{shell("arrange", arrange(sel_clip=None), collapsed_detail())}</div>' + setup_dialog()
    if name == 'Start':
        return app_bar(start=True) + start_view() + status_bar('Ready. Audio: Headphones (Realtek USB) at 48 kHz.')
    raise SystemExit('unknown ' + name)

TITLES = dict(Main='Arrange', PianoRoll='Arrange + Piano roll', DrumMachine='Arrange + Drum machine', VocalChain='Track devices + Vocal Chain',
              Mixer='Mix view', Master='Master view', Export='Export dialog', Start='Start screen', Setup='Setup: Audio & Routing')

def page(name):
    body = screen(name)
    title = TITLES[name].replace('&', '&amp;')
    return f'''<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>EzDAWsIt: {title}</title>
<script src="./support.js"></script>
</head>
<body>
<x-dc>
<helmet>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600;700&display=swap">
<style>
body{{margin:0}}
a{{color:#4AA3FF}}a:hover{{color:#7FBFFF}}
</style>
</helmet>
<div style="width: 1440px; height: 900px; font-family: {FONT}; font-size: 12px; color: #E6E6E6; background: #1B1D22; display: flex; flex-direction: column; overflow: hidden; position: relative; box-sizing: border-box">
{body}
</div>
</x-dc>
<script type="text/x-dc" data-dc-script data-props='{{"$preview":{{"width":1440,"height":900}}}}'>
class Component extends DCLogic {{
renderVals() {{ return {{}}; }}
}}
</script>
</body>
</html>
'''

ROWS = [('Arrange &amp; editors', ['Main', 'PianoRoll', 'DrumMachine', 'VocalChain']), ('Mix, master &amp; export', ['Mixer', 'Master', 'Export']), ('Start &amp; setup', ['Start', 'Setup'])]

def canvas():
    boards = {}; order = []; notes = {}
    y = 0
    for ri, (rt, names) in enumerate(ROWS):
        x = 0
        for n in names:
            f = f'{n}.dc.html'; boards[f] = dict(x=x, y=y, w=1440, h=900, title=TITLES[n]); order.append(f); x += 1440 + 80
        notes[f'row{ri + 1}'] = dict(x=0, y=y - 260, text=rt.replace('&amp;', '&'), kind='title1', maxW=x - 80)
        y += 900 + 120 + 300
    now = datetime.datetime.now(datetime.timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')
    return dict(v=3, createdOnFiles=dict(v=1, at=now), title='EzDAWsIt UI', launch=dict(view='canvas'), pages=[], boards=boards, order=order, notes=notes, designSystems=[])

if __name__ == '__main__':
    what = sys.argv[1]
    if what == 'check':
        for fg, bg in [(C['dim'], C['panel']), (C['dim'], C['raised']), (C['dim'], C['bar']), (C['dim'], C['status']), ('#8A8F99', C['panel']), (C['lcd2'], C['display']),
                       (C['ink'], C['midi']), ('#FFFFFF', C['audio']), (C['ink'], C['play']), (C['ink'], C['accent']), ('#D3D8E0', '#2B2F38'),
                       (blend(C['text'], C['panel'], .5), C['panel']), (blend(C['text'], C['bar'], .5), C['bar']), ('#FF8A8A', C['panel'])]:
            print(fg, bg, round(contrast(fg, bg), 2))
    elif what == 'canvas':
        open(os.path.join(OUT, 'canvas.json'), 'w').write(json.dumps(canvas(), indent=1))
        print('canvas.json written')
    else:
        html = page(what)
        assert '{{' not in html.split('<script type="text/x-dc"')[0], 'hole syntax leaked'
        import re
        bad = re.findall(r'&(?![a-zA-Z#][a-zA-Z0-9]*;)', html.split('<link rel')[0] + html.split('</helmet>')[1].split('<script type')[0])
        assert not bad, f'raw ampersand: {len(bad)}'
        open(os.path.join(OUT, f'{what}.dc.html'), 'w').write(html)
        print(what, len(html))
