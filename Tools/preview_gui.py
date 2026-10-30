#!/usr/bin/env python3
"""Draws pictures of the editor pages WITHOUT building the plugin, so you can judge a look before compiling.
    python3 Tools/preview_gui.py                      # modern style, all four pages, PNGs into ./preview_out
    python3 Tools/preview_gui.py --style classic      # the old flat look, for comparison
    python3 Tools/preview_gui.py --page 0 --out /tmp  # just the Sequencer page
    python3 Tools/preview_gui.py --scale 0.9          # how it opens on a 13-inch laptop;  --scale 2 = a retina rendering
Panel and control positions come from PluginEditor.cpp (through preflight.parse_layout), colours from Theme.h, parameter
defaults and drop-down items from PluginProcessor.cpp. It is a DRAWING made with Pillow, not the real JUCE rendering:
fonts and anti-aliasing differ slightly, but sizes, spacing, colours and layout match the code.
Needs:  pip install pillow"""
import argparse, math, os, re, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import preflight
from PIL import Image, ImageDraw, ImageFilter, ImageFont

S = 2
ROOT = preflight.ROOT
CLASSIC = dict(bgTop=0xff15161a, bgBottom=0xff15161a, panelTop=0xff202228, panelBottom=0xff202228, edge=0xff363943, edgeStrong=0xff363943,
               well=0xff1d1f25, cellA=0xff25272e, cellB=0xff1d1f25, button=0xff2a2d36, buttonHover=0xff2a2d36, text=0xffe9e9ee,
               subText=0xff8e93a1, dim=0xff8e93a1, onAccent=0xff000000, accent=0xfff0902f, knobTrack=0xff3a3d48, knobFace=0xff202228,
               teal=0xff2fb4d8, rose=0xffe0467d, sage=0xff7bd34a, drumKick=0xfff0902f, drumSnare=0xffe6c93b, drumHat=0xff3dbb4a, drumClap=0xff8a3fe0)

def load_theme():
    t = {}
    for m in re.finditer(r'inline const juce::Colour (\w+)\s*\(0x([0-9a-fA-F]{8})\)', open(os.path.join(preflight.SRC, 'Theme.h'), encoding='utf-8').read()):
        t[m.group(1)] = int(m.group(2), 16)
    return t

def rgba(v): return ((v >> 16) & 255, (v >> 8) & 255, v & 255, (v >> 24) & 255)
def rgb(v): return rgba(v)[:3]
def over(base, top):                                    # colour 'top' (with its alpha) drawn over opaque 'base'
    r, g, b, a = rgba(top) if isinstance(top, int) else top
    return tuple(int(base[i] * (1 - a / 255) + (r, g, b)[i] * a / 255) for i in range(3))
def mixc(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))

def font(sz, bold=False):
    for p in ('/usr/share/fonts/truetype/dejavu/DejaVuSans%s.ttf' % ('-Bold' if bold else ''), '/Library/Fonts/Arial%s.ttf' % (' Bold' if bold else ''), 'C:/Windows/Fonts/%s.ttf' % ('arialbd' if bold else 'arial')):
        if os.path.exists(p): return ImageFont.truetype(p, int(sz * S))
    return ImageFont.load_default()

class Preview:
    def __init__(self, style):
        th = load_theme()
        if style == 'classic': th = dict(CLASSIC)
        self.t = {k: v for k, v in th.items()}; self.modern = style != 'classic'
        c, e = preflight.read('PluginProcessor.cpp'), preflight.read('PluginEditor.cpp')
        self.W, self.H, self.panels, self.items, self.captions = preflight.parse_layout()
        self.arrays = {m.group(1): re.findall(r'"([^"]*)"', m.group(2)) for m in re.finditer(r'const juce::StringArray (\w+)\s*\{([^}]*)\}', c)}
        self.params = {}
        for m in re.finditer(r'\b([FCBI]) \("(\w+)",\s*"([^"]*)",\s*([^;]*?)\);', c[c.index('createLayout()\n{'):]):
            self.params[m.group(2)] = (m.group(1), [a.strip().rstrip('f') for a in m.group(4).split(',')])
        self.labels = dict(re.findall(r'\{ "(\w+)", "([^"]+)" \}', e))
        self.toggle_text = dict(re.findall(r'addToggle \("(\w+)", "([^"]+)"\)', e))
        self.f = dict(title=font(23, True), pan=font(11.5, True), lbl=font(12.5), cmb=font(13), tab=font(13, True), lane=font(13, True), cap=font(11, True))
        self.demo = {'fMix': .9, 'fRes': .62, 'tDepth': .7, 'pDepth': .6, 'dMix': .38, 'dfxDepth': .55, 'rvMix': .3, 'rvShim': .35, 'drive': .4,
                     'eqHigh': .62, 'eqLow': .55, 'pxMix': .6, 'spSpace': .5, 'spWidth': .65, 'spDepth': .3, 'phMix': .55, 'lfoAmt': .72,
                     'eAmt': .8, 'fEnv': .65, 'pkAmt': .6, 'nAmt': .5, 'swAmt': .5, 'mfMix': .5, 'comp': .4, 'rmMix': .25, 'fLow': .35, 'fHigh': .7}
        self.shown = {'fType': 'Bandpass', 'fSlope': '24 dB', 'drType': 'Tube', 'pxMode': 'Thicken', 'dfxMode': 'Tremolo', 'mfType': 'Rotary',
                      'lfoShape': 'Sine', 'trigSrc': 'Both', 'phStg': '8', 'arpScale': 'Minor Pent', 'arpRoot': 'A'}
        self.on = {'hostSync', 'dPing', 'lfoSync', 'drOn', 'mfSync'}

    def c(self, name): return rgb(self.t[name])
    def sc(self, v): return int(round(v * S))

    def norm(self, pid):
        if pid in self.demo: return self.demo[pid]
        kind, a = self.params.get(pid, ('F', ['0', '1', '0.5']))
        try:
            if kind == 'F':
                lo, hi, d = float(a[0]), float(a[1]), float(a[2]); sk = float(a[3]) if len(a) > 3 else 1.0
                return max(0.0, min(1.0, ((d - lo) / (hi - lo)) ** sk))
            if kind == 'I': lo, hi, d = float(a[0]), float(a[1]), float(a[2]); return (d - lo) / (hi - lo)
        except ValueError: pass
        return 0.5

    def combo_text(self, pid):
        if pid in self.shown: return self.shown[pid]
        kind, a = self.params.get(pid, ('C', ['', '0']))
        arr = self.arrays.get(a[0], []); i = int(float(a[1])) if len(a) > 1 else 0
        return arr[i] if i < len(arr) else '-'

    # ---------------------------------------------------------------- primitives
    def rr(self, d, x, y, w, h, r, fill=None, outline=None, width=1):
        d.rounded_rectangle([self.sc(x), self.sc(y), self.sc(x + w) - 1, self.sc(y + h) - 1], radius=self.sc(r), fill=fill, outline=outline, width=max(1, int(width * S)) if outline else 0)

    def text(self, d, x, y, s, f, fill, anchor='la'): d.text((self.sc(x), self.sc(y)), s, font=f, fill=fill, anchor=anchor)

    def gradient_rr(self, img, x, y, w, h, r, top, bottom):
        W_, H_ = self.sc(w), self.sc(h); strip = Image.new('RGB', (W_, H_)); sd = ImageDraw.Draw(strip)
        for i in range(H_): sd.line([(0, i), (W_, i)], fill=mixc(top, bottom, i / max(1, H_ - 1)))
        mask = Image.new('L', (W_, H_), 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, W_ - 1, H_ - 1], radius=self.sc(r), fill=255)
        img.paste(strip, (self.sc(x), self.sc(y)), mask)

    def thick_arc(self, d, cx, cy, r, a0, a1, width, fill):          # angles: radians from 12 o'clock, clockwise (JUCE convention)
        n = max(8, int(abs(a1 - a0) * 24)); pts = [(self.sc(cx + r * math.sin(a0 + (a1 - a0) * i / n)), self.sc(cy - r * math.cos(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
        d.line(pts, fill=fill, width=int(width * S), joint='curve')
        for (px, py) in (pts[0], pts[-1]): d.ellipse([px - width * S / 2, py - width * S / 2, px + width * S / 2, py + width * S / 2], fill=fill)

    # ---------------------------------------------------------------- widgets
    def knob(self, d, x, y, pid, v):
        cx, cy = x + 40, y + 32; radius = 29.0
        a0, a1 = 1.2 * math.pi, 2.8 * math.pi; ang = a0 + v * (a1 - a0)
        acc, trk = self.c('accent'), self.c('knobTrack')
        if self.modern:
            fr = radius - 10; face = self.c('knobFace')
            for i in range(int(fr * 2 * S), 0, -2):                      # soft vertical gradient on the disc
                t = i / (fr * 2 * S); col = mixc(tuple(min(255, int(c * 1.10)) for c in face), tuple(int(c * 0.7) for c in face), 1 - t)
            d.ellipse([self.sc(cx - fr), self.sc(cy - fr), self.sc(cx + fr), self.sc(cy + fr)], fill=mixc(face, (0, 0, 0), 0.12), outline=None)
            d.ellipse([self.sc(cx - fr + 1), self.sc(cy - fr), self.sc(cx + fr - 1), self.sc(cy + fr - 2)], fill=face)
            self.thick_arc(d, cx, cy, radius - 2.5, a0, a1, 4.5, trk)
            if v > 0.002: self.thick_arc(d, cx, cy, radius - 2.5, a0, ang, 4.5, acc)
            p0 = (cx + fr * .28 * math.sin(ang), cy - fr * .28 * math.cos(ang)); p1 = (cx + fr * .82 * math.sin(ang), cy - fr * .82 * math.cos(ang))
            d.line([(self.sc(p0[0]), self.sc(p0[1])), (self.sc(p1[0]), self.sc(p1[1]))], fill=self.c('text'), width=int(2.4 * S))
            for p in (p0, p1): d.ellipse([self.sc(p[0] - 1.2), self.sc(p[1] - 1.2), self.sc(p[0] + 1.2), self.sc(p[1] + 1.2)], fill=self.c('text'))
        else:                                                             # classic: stock JUCE rotary look
            box = [self.sc(cx - 26), self.sc(cy - 26), self.sc(cx + 26), self.sc(cy + 26)]
            d.arc(box, math.degrees(a0) - 90, math.degrees(a1) - 90, fill=trk, width=4 * S)
            if v > 0.01: d.arc(box, math.degrees(a0) - 90, math.degrees(ang) - 90, fill=acc, width=4 * S)
            d.line([(self.sc(cx + 9 * math.sin(ang)), self.sc(cy - 9 * math.cos(ang))), (self.sc(cx + 21 * math.sin(ang)), self.sc(cy - 21 * math.cos(ang)))], fill=self.c('text'), width=2 * S)
            d.ellipse([self.sc(cx - 5), self.sc(cy - 5), self.sc(cx + 5), self.sc(cy + 5)], fill=(0x2c, 0x2f, 0x38), outline=self.c('edge'))
        self.text(d, x + 40, y + 73, self.labels.get(pid, pid), self.f['lbl'], self.c('subText'), 'mm')

    def combo(self, d, x, y, w, s):
        self.rr(d, x, y, w, 22, 6 if self.modern else 4, fill=self.c('button'), outline=over(self.c('button'), self.t['edgeStrong']) if self.modern else self.c('edge'))
        self.text(d, x + 9, y + 11, s, self.f['cmb'], self.c('text'), 'lm')
        a = self.c('accent')
        if self.modern: d.line([(self.sc(x + w - 18), self.sc(y + 9)), (self.sc(x + w - 14), self.sc(y + 13)), (self.sc(x + w - 10), self.sc(y + 9))], fill=a, width=int(1.8 * S), joint='curve')
        else: d.polygon([(self.sc(x + w - 16), self.sc(y + 9)), (self.sc(x + w - 8), self.sc(y + 9)), (self.sc(x + w - 12), self.sc(y + 14))], fill=a)

    def toggle(self, d, x, y, w, pid):
        on = pid in self.on; a = self.c('accent'); yy = y + 3
        self.rr(d, x + 2, yy, 16, 16, 5 if self.modern else 3, fill=a if on else self.c('well' if self.modern else 'button'), outline=a if on else over(self.c('button'), self.t['edgeStrong']))
        if on: d.line([(self.sc(x + 6), self.sc(yy + 8)), (self.sc(x + 9), self.sc(yy + 11.2)), (self.sc(x + 14), self.sc(yy + 4.5))], fill=self.c('onAccent'), width=int(2 * S), joint='curve')
        self.text(d, x + 24, y + 11, self.toggle_text.get(pid, pid), self.f['cmb'], self.c('text'), 'lm')

    # ---------------------------------------------------------------- pages
    def lane(self, d, x, y, w, h, col, vals, n, length, bip=False, play=5):
        cw = w / n; bg = self.c('cellA'); bg2 = self.c('cellB'); rad = 4 if self.modern else 3
        for i in range(n):
            cx = x + i * cw; cell = (cx + 2, y + 1, cw - 4, h - 2)
            self.rr(d, *cell, rad, fill=bg if (i // 4) % 2 == 0 else bg2)
            act = i < length; v = vals[i % len(vals)]; a = (0.88 if self.modern else 0.95) if act else 0.22
            c = mixc(bg2, col, a)
            if bip:
                mid = y + h / 2; vy = y + 1 + (h - 2) * (1 - v)
                self.rr(d, cell[0], min(mid, vy), cell[2], max(2, abs(vy - mid)), 2, fill=c)
            elif v > 0.001:
                top = y + 1 + (h - 2) * (1 - v); self.rr(d, cell[0], top, cell[2], y + h - 1 - top, rad, fill=c)
            if i == play and act: self.rr(d, *cell, rad, outline=(255, 255, 255) if not self.modern else mixc(c, (255, 255, 255), .8), width=1.5)
            elif not act: self.rr(d, *cell, rad, fill=mixc(bg2, (0, 0, 0), .35) if False else None)

    def render(self, pg, scale=1.0):
        W, H = self.W, self.H
        img = Image.new('RGB', (W * S, H * S)); d = ImageDraw.Draw(img)
        top, bot = self.c('bgTop'), self.c('bgBottom')
        for i in range(H * S): d.line([(0, i), (W * S, i)], fill=mixc(top, bot, i / (H * S)))
        # header
        a = self.c('accent'); t = self.c('text')
        if self.modern:
            for (bx, by, bh) in ((16, 24, 12), (23, 15, 21), (30, 20, 16)): self.rr(d, bx, by, 4, bh, 2, fill=a)
            self.text(d, 44, 26, 'RhythmFX Sync', self.f['title'], t, 'lm')
        else:
            self.text(d, 16, 26, 'RHYTHMFX SYNC', self.f['title'], t, 'lm'); d.rectangle([self.sc(16), self.sc(46), self.sc(70) - 1, self.sc(49) - 1], fill=a)
        for i, n in enumerate(['SEQUENCER', 'MODULATION', 'EFFECTS', 'SPACE']):
            on = i == pg; bx = 340 + i * 128
            if self.modern:
                if on: self.rr(d, bx, 12, 122, 30, 15, fill=a)
                else: self.rr(d, bx, 12, 122, 30, 15, outline=over(top, self.t['edge']))
            else: self.rr(d, bx, 12, 122, 30, 4, fill=a if on else self.c('button'), outline=self.c('edge'))
            self.text(d, bx + 61, 27, n, self.f['tab'], self.c('onAccent') if on else (self.c('subText') if self.modern else t), 'mm')
        self.rr(d, 1240, 14, 256, 28, 6 if self.modern else 4, fill=self.c('button'), outline=over(self.c('button'), self.t['edgeStrong']))
        self.text(d, 1252, 28, 'Pluck Wah (Envelope)', self.f['cmb'], t, 'lm')
        if self.modern: d.line([(self.sc(1474), self.sc(25)), (self.sc(1478), self.sc(29)), (self.sc(1482), self.sc(25))], fill=a, width=int(1.8 * S), joint='curve')
        else: d.polygon([(self.sc(1478), self.sc(25)), (self.sc(1486), self.sc(25)), (self.sc(1482), self.sc(31))], fill=a)
        # panels
        vis = [p for p in self.panels if p[0] in (pg, -1)]
        if self.modern:
            sh = Image.new('RGBA', img.size, (0, 0, 0, 0)); sd = ImageDraw.Draw(sh)
            for (_p, x, y, w, h, _t) in vis: sd.rounded_rectangle([self.sc(x), self.sc(y + 4), self.sc(x + w), self.sc(y + h + 4)], radius=self.sc(10), fill=(0, 0, 0, 80))
            sh = sh.filter(ImageFilter.GaussianBlur(self.sc(6))); img.paste(sh, (0, 0), sh); d = ImageDraw.Draw(img)
        for (_p, x, y, w, h, title) in vis:
            if self.modern:
                self.gradient_rr(img, x, y, w, h, 10, self.c('panelTop'), self.c('panelBottom')); d = ImageDraw.Draw(img)
                self.rr(d, x + .5, y + .5, w - 1, h - 1, 10, outline=over(self.c('panelTop'), self.t['edge']))
                self.rr(d, x + 11, y + 10, 3, 12, 1.5, fill=a)
                self.text(d, x + 20, y + 8, title, self.f['pan'], self.c('dim'))
            else:
                self.rr(d, x, y, w, h, 6, fill=self.c('panelTop'), outline=self.c('edge')); self.text(d, x + 10, y + 6, title, self.f['pan'], self.c('dim'))
        for (p, x, y, s) in self.captions:
            if p in (pg, -1): self.text(d, x, y, s, self.f['cap'], self.c('dim'))
        for (p, pid, x0, y0, x1, y1, kind) in self.items:
            if p not in (pg, -1): continue
            if kind == 'knob': self.knob(d, x0, y0, pid, self.norm(pid))
            elif kind == 'combo': self.combo(d, x0, y0, x1 - x0, self.combo_text(pid))
            else: self.toggle(d, x0, y0, x1 - x0, pid)
        if pg == 0: self.sequencer_extras(d)
        scale = max(0.25, min(2.0, scale))          # the canvas is drawn at 2x, so up to 200 % is true detail, not enlargement
        return img.resize((int(round(self.W * scale)), int(round(self.H * scale))), Image.LANCZOS)

    def sequencer_extras(self, d):
        c = self.c; lw = self.W - 114; lx = 100
        filt = [0.1 + 0.9 * abs(math.sin(math.pi * i / 8.0)) ** 1.3 for i in range(32)]
        amp = ([1, .3, 1, .3, 1, 0, 1, .6] * 4); pan = ([0, 1, .25, .8, .5, 1, 0, .7] * 4)
        self.lane(d, lx, 214, lw, 70, c('teal'), filt, 32, 32); self.lane(d, lx, 290, lw, 70, c('rose'), amp, 32, 24)
        self.lane(d, lx, 366, lw, 70, c('sage'), pan, 32, 16, bip=True)
        for k, (nm, cl, bits) in enumerate([('Kick', 'drumKick', (0, 6, 8, 11)), ('Snare', 'drumSnare', (4, 12)), ('Hat', 'drumHat', tuple(range(0, 16, 2))), ('Clap', 'drumClap', (7, 15))]):
            self.lane(d, lx, 446 + k * 32, lw, 28, c(cl), [1.0 if i in bits else 0.0 for i in range(16)], 16, 16, play=3)
        for nm, yy, bright in (('Filter', 249, 1), ('Amp', 325, 1), ('Pan (up=R)', 401, 1), ('Kick', 460, 0), ('Snare', 492, 0), ('Hat', 524, 0), ('Clap', 556, 0)):
            self.text(d, 16, yy, nm, self.f['lane'], c('text') if bright else c('dim'), 'lm')
        self.rr(d, 104, 652, 90, 30, 6 if self.modern else 4, fill=c('button'), outline=over(c('button'), self.t['edgeStrong']))
        self.text(d, 149, 667, 'RESTART', self.f['tab'], c('text'), 'mm')

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--style', choices=['modern', 'classic'], default='modern'); ap.add_argument('--page', type=int, default=-1); ap.add_argument('--out', default='preview_out')
    ap.add_argument('--scale', type=float, default=1.0, help='output size as a fraction of the design size, 0.5 - 2.0 (2.0 = what a retina display shows)')
    a = ap.parse_args(); os.makedirs(a.out, exist_ok=True); pv = Preview(a.style)
    for pg in ([a.page] if a.page >= 0 else range(4)):
        suffix = '' if abs(a.scale - 1.0) < 1e-6 else '_x%g' % a.scale
        path = os.path.join(a.out, '%s_page%d%s.png' % (a.style, pg, suffix)); pv.render(pg, a.scale).save(path); print('wrote', path)

if __name__ == '__main__':
    main()
