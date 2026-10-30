#!/usr/bin/env python3
"""Pre-flight checks for RhythmFX Sync. Run from the project folder:   python3 Tools/preflight.py
It needs no JUCE. It catches the mistakes that are easy to make when editing this project without a compiler:
  1. parameter IDs agree between RFX_PARAM_LIST, the APVTS layout, setParam() calls and the editor, and every parameter has a control
  2. the editor layout (resized()) has no overlapping panels, nothing outside the window, no control spilling out of its panel
  3. braces / brackets / #if blocks are balanced in every source file
  4. every unit-test program in Tests/ compiles and passes (needs g++ or clang++)
Exit code 0 = all good. Warnings (text width guesses) never fail the run; problems do."""
import glob, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'Source')
problems, warnings = [], []
def bad(msg): problems.append(msg); print('  PROBLEM:', msg)
def warn(msg): warnings.append(msg); print('  warning:', msg)
def read(p): return open(os.path.join(SRC, p), encoding='utf-8').read()

# ------------------------------------------------------------------------------------------ 1. parameter IDs
def check_ids():
    print('1. parameter IDs')
    h, c, e = read('PluginProcessor.h'), read('PluginProcessor.cpp'), read('PluginEditor.cpp')
    listed = set(re.findall(r'X\((\w+)\)', h[h.index('RFX_PARAM_LIST'):h.index('class ')]))
    layout = c[c.index('createLayout()\n{'):]
    declared = set(re.findall(r'\b[FCBI] \("(\w+)"', layout))
    for i in sorted(listed - declared): bad('in RFX_PARAM_LIST but not in the layout: ' + i)
    for i in sorted(declared - listed): bad('in the layout but not in RFX_PARAM_LIST: ' + i)
    for i in sorted(set(re.findall(r'setParam \("(\w+)"', c)) - declared): bad('setParam() uses an unknown id: ' + i)
    knobs = set(re.findall(r'\{ "(\w+)", "[^"]+" \}', e))
    combos = set(re.findall(r'"(\w+)"', e[e.index('for (auto id : { "rate"'):e.index('}) addCombo (id);')]))
    toggles = set(re.findall(r'addToggle \("(\w+)"', e))
    controls = knobs | combos | toggles
    for i in sorted(controls - declared): bad('editor creates a control for an unknown parameter: ' + i)
    for i in sorted(declared - controls): bad('parameter with no editor control: ' + i)
    used = set(re.findall(r'(?:getRawParameterValue|getParameter) \("(\w+)"', e)) | set(re.findall(r'(?:combos|toggles)\["(\w+)"\]', e))
    for i in sorted(used - declared): bad('editor refers to an unknown parameter: ' + i)
    print('   %d parameters, %d controls' % (len(declared), len(controls)))

# ------------------------------------------------------------------------------------------ 2. editor layout
def parse_layout():
    """Reads PluginEditor.cpp's resized() and returns (W, H, panels, items, captions).
    panels   = (page, x, y, w, h, title)             page -1 = visible on every page
    items    = (page, id, x0, y0, x1, y1, kind)      kind = knob | combo | toggle
    captions = (page, x, y, text)
    Also used by Tools/preview_gui.py."""
    e = read('PluginEditor.cpp'); hdr = read('PluginEditor.h')
    m = re.search(r'kBaseW = (\d+), kBaseH = (\d+)', hdr); W, H = int(m.group(1)), int(m.group(2))      # the design size the layout is made for
    body = e[e.index('void RhythmFXEditor::layoutContent()'):e.index('void RhythmFXEditor::paint (')]
    env = {'kKnobW': 80, 'kBaseW': W, 'kBaseH': H}; page = None; panels = []; items = []; captions = []
    for line in body.split('\n'):
        t = re.sub(r'\s*//.*$', '', line.strip())
        mm = re.match(r'curPage = (-?\d+);', t)
        if mm: page = int(mm.group(1)); continue
        mm = re.match(r'(?:const )?int (.+);$', t)
        if mm and '(' not in mm.group(1):
            for part in mm.group(1).split(','):
                k, v = part.split('=', 1); env[k.strip()] = eval(v, {}, env)
            continue
        if re.match(r'^(\w+ = [^;(]+;\s*)+$', t) and '(' not in t:
            for part in [p for p in t.split(';') if p.strip()]:
                k, v = part.split('=', 1); env[k.strip()] = eval(v, {}, env)
            continue
        mm = re.search(r'addPanel \(([^,]+), ([^,]+), ([^,]+), ([^,]+), "([^"]+)"', t)
        if mm:
            x, y, w, h_ = [eval(g, {}, env) for g in mm.groups()[:4]]; panels.append((page, x, y, w, h_, mm.group(5)))
        mm = re.search(r'addCaption \(([^,]+), ([^,]+), "([^"]+)"', t)
        if mm: captions.append((page, eval(mm.group(1), {}, env), eval(mm.group(2), {}, env), mm.group(3)))
        for mm in re.finditer(r'placeKnob \("(\w+)", ([^,]+), ([^)]+)\)', t):
            x, y = eval(mm.group(2), {}, env), eval(mm.group(3), {}, env); items.append((page, mm.group(1), x, y, x + 80, y + 82, 'knob'))
        mm = re.search(r'for \(auto id : \{ ([^}]*) \}\) \{ placeKnob \(id, x, ky\)', t)
        if mm:
            for i, pid in enumerate(re.findall(r'"(\w+)"', mm.group(1))):
                x, y = env['x'] + 80 * i, env['ky']; items.append((page, pid, x, y, x + 80, y + 82, 'knob'))
        for mm in re.finditer(r'(combo|toggle) \("(\w+)", ([^,]+), ([^,]+), (\d+)\)', t):
            x, y, w = eval(mm.group(3), {}, env), eval(mm.group(4), {}, env), int(mm.group(5)); items.append((page, mm.group(2), x, y, x + w, y + 22, mm.group(1)))
    return W, H, panels, items, captions

def check_layout():
    print('2. editor layout')
    c, e = read('PluginProcessor.cpp'), read('PluginEditor.cpp')
    W, H, panels, items, _captions = parse_layout()
    arrays = {mm.group(1): re.findall(r'"([^"]*)"', mm.group(2)) for mm in re.finditer(r'const juce::StringArray (\w+)\s*\{([^}]*)\}', c)}
    combo_items = {}
    for mm in re.finditer(r'\bC \("(\w+)",\s*"[^"]*",\s*(\w+),', c): combo_items[mm.group(1)] = arrays.get(mm.group(2), [])
    toggle_text = dict(re.findall(r'addToggle \("(\w+)", "([^"]+)"\)', e))
    pages = sorted({p[0] for p in panels if p[0] is not None and p[0] >= 0})
    for pg in pages:
        ps = [p for p in panels if p[0] in (pg, -1)]
        for i, a in enumerate(ps):
            if a[1] < 0 or a[2] < 0 or a[1] + a[3] > W or a[2] + a[4] > H: bad('panel "%s" (page %d) leaves the %dx%d window' % (a[5], pg, W, H))
            for b in ps[i + 1:]:
                if a[1] < b[1] + b[3] and b[1] < a[1] + a[3] and a[2] < b[2] + b[4] and b[2] < a[2] + a[4]:
                    bad('panels overlap on page %d: "%s" and "%s"' % (pg, a[5], b[5]))
    for (pg, pid, x0, y0, x1, y1, kind) in items:
        cand = [p for p in panels if p[0] in (pg, -1) and p[1] <= x0 + 12 < p[1] + p[3] and p[2] <= y0 < p[2] + p[4]]
        if not cand: bad('%s "%s" (page %s) is not inside any panel' % (kind, pid, pg)); continue
        p = cand[0]
        if x1 > p[1] + p[3] or y1 > p[2] + p[4]: bad('%s "%s" spills out of panel "%s"' % (kind, pid, p[5]))
        if kind == 'combo':
            longest = max([len(s) for s in combo_items.get(pid, [''])] or [0])
            if 8 + longest * 6.5 + 22 > x1 - x0: warn('combo "%s" may be too narrow for its longest item (%d chars in %d px)' % (pid, longest, x1 - x0))
        if kind == 'toggle' and 24 + 6.5 * len(toggle_text.get(pid, pid)) + 4 > x1 - x0: warn('toggle "%s" text may be clipped (%d px box)' % (pid, x1 - x0))
    for (pg, x, y, w, h_, title) in panels:
        if 7.0 * len(title) > w - 32: warn('panel title "%s" may be wider than its panel (%d px)' % (title, w))
    print('   %d panels, %d controls on %d pages, window %dx%d' % (len(panels), len(items), len(pages), W, H))

# ------------------------------------------------------------------------------------------ 2b. scaling
def check_scaling():
    print('2b. window scaling / high-DPI')
    e, hdr = read('PluginEditor.cpp'), read('PluginEditor.h')
    if not re.search(r'kBaseW = \d+, kBaseH = \d+', hdr): bad('PluginEditor.h has no kBaseW / kBaseH design size'); return
    lay = e[e.index('void RhythmFXEditor::layoutContent()'):e.index('void RhythmFXEditor::paint (')]
    pnt = e[e.index('void RhythmFXEditor::paintContent'):e.index('void RhythmFXEditor::timerCallback')]
    for name, body in (('layoutContent()', lay), ('paintContent()', pnt)):
        for mm in re.finditer(r'\bget(Width|Height)\s*\(\)', body):
            ctx = body[max(0, mm.start() - 12):mm.start()]
            if re.search(r'(r|p\.r|area|bounds|rect)\.$', ctx): continue                  # a Rectangle's own size is fine
            bad('%s uses the live window size (%s): the layout must use kBaseW / kBaseH so scaling keeps working' % (name, mm.group(0)))
    ctor = e[e.index('RhythmFXEditor::RhythmFXEditor ('):e.index('RhythmFXEditor::~RhythmFXEditor')]
    # the editor's own setSize only (content.setSize is fine: it does not call the editor's resized())
    if re.search(r'(?<![.\w])setSize\s*\(', ctor.split('layoutContent();')[0]):
        bad('the constructor calls setSize before layoutContent(): resized() would run with no controls yet')
    if 'layoutContent();' not in ctor: bad('the constructor never calls layoutContent()')
    loose = [l.strip() for l in e.split('\n') if re.search(r'(?<![.\w])addAndMakeVisible \(', l) and 'addAndMakeVisible (content)' not in l]
    for l in loose: bad('control added to the editor instead of `content` (it would not scale): ' + l[:90])
    for need in ('content.setTransform', 'setConstrainer', 'setResizable', 'setFixedAspectRatio', 'uiScale'):
        if need not in e: bad('scaling code is missing: ' + need)
    if 'uiScale' not in read('PluginProcessor.cpp'): bad('the window scale is not saved with the session (PluginProcessor.cpp)')
    print('   design size %s, content-transform scaling, fixed aspect, size saved with the session' % re.search(r'kBaseW = (\d+), kBaseH = (\d+)', hdr).expand(r'\1x\2'))

# ------------------------------------------------------------------------------------------ 3. balance
def strip(s):
    out = []; i = 0; n = len(s)
    while i < n:
        if s.startswith('//', i): j = s.find('\n', i); i = n if j < 0 else j; continue
        if s.startswith('/*', i): j = s.find('*/', i); i = n if j < 0 else j + 2; continue
        if s[i] == '"':
            i += 1
            while i < n and s[i] != '"': i += 2 if s[i] == '\\' else 1
            i += 1; out.append('""'); continue
        if s[i] == "'":
            j = i + 1
            while j < n and s[j] != "'": j += 2 if s[j] == '\\' else 1
            i = j + 1; out.append("''"); continue
        out.append(s[i]); i += 1
    return ''.join(out)

def check_balance():
    print('3. brace / #if balance')
    files = sorted(glob.glob(os.path.join(SRC, '*.h')) + glob.glob(os.path.join(SRC, '*.cpp')) + glob.glob(os.path.join(ROOT, 'Tests', '*.cpp')))
    for f in files:
        s = open(f, encoding='utf-8').read(); name = os.path.relpath(f, ROOT); pairs = {'(': ')', '[': ']', '{': '}'}; st = []; ok = True
        for ln, line in enumerate(strip(s).split('\n'), 1):
            for ch in line:
                if ch in pairs: st.append((ch, ln))
                elif ch in pairs.values():
                    if not st or pairs[st[-1][0]] != ch: bad('%s line %d: unmatched "%s"' % (name, ln, ch)); ok = False; break
                    st.pop()
            if not ok: break
        if ok and st: bad('%s: "%s" opened on line %d is never closed' % (name, st[-1][0], st[-1][1]))
        depth = 0
        for ln, line in enumerate(s.split('\n'), 1):
            mm = re.match(r'\s*#\s*(if|ifdef|ifndef|endif)\b', line)
            if mm:
                depth += -1 if mm.group(1) == 'endif' else 1
                if depth < 0: bad('%s line %d: #endif without #if' % (name, ln)); break
        if depth > 0: bad('%s: %d #if block(s) never closed' % (name, depth))
    print('   %d files' % len(files))

# ------------------------------------------------------------------------------------------ 4. unit tests
def check_tests():
    print('4. unit tests')
    cxx = shutil.which('g++') or shutil.which('clang++') or shutil.which('c++')
    if not cxx: warn('no C++ compiler found: unit tests skipped'); return
    tmp = tempfile.mkdtemp()
    for t in sorted(glob.glob(os.path.join(ROOT, 'Tests', '*test*.cpp'))):
        name = os.path.basename(t); exe = os.path.join(tmp, name + '.out')
        r = subprocess.run([cxx, '-std=c++17', '-O2', t, '-I', SRC, '-o', exe], capture_output=True, text=True)
        if r.returncode: bad('%s does not compile:\n%s' % (name, r.stderr[:800])); continue
        r = subprocess.run([exe], capture_output=True, text=True)
        last = (r.stdout.strip().split('\n') or [''])[-1]
        if r.returncode: bad('%s FAILED (exit %d). Last line: %s' % (name, r.returncode, last))
        else: print('   %-26s passed  (%s)' % (name, last.strip()[:40]))

if __name__ == '__main__':
    for step in (check_ids, check_layout, check_scaling, check_balance, check_tests):
        try: step()
        except Exception as ex: bad('%s crashed: %r (the code layout may have changed; update this script)' % (step.__name__, ex))
    print('\n%d problem(s), %d warning(s)' % (len(problems), len(warnings)))
    sys.exit(1 if problems else 0)
