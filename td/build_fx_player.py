# GUxSID FX Player builder (TouchDesigner) — ボイスプール版
# Textport で1回だけ実行:
# exec(open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/build_fx_player.py').read())
# /project1/fx_player を作り直す。中身はノードとパラメータ式だけ（実行時に動くスクリプトは無し）。
#
# 仕組み（イベントごと）:
#   trig_<ev>(出来事パルス) → count_<ev>(何回目か)
#   voice_<ev>_k  : Constant。「今回の番号 % VOICES == k」のときだけ trig を通す
#   gate_<ev>_k   : Trigger。鳴り始めたら <Ev>len 秒間 1 を保持（その間は再トリガーされない＝巻き戻し無し）
#   snd_<ev>_k    : Audio File In。gate が 0 の間は Cue で頭に待機、1 になると最後まで再生
#   sum_<ev> → vol_<ev> → mix → master → out → audioout
import os, traceback
VOICES = 6
TDDIR = '/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td'
LOG = []
def log(*a): LOG.append(' '.join(str(x) for x in a))

def findpar(o, label):
    for p in o.pars():
        if p.label.lower() == label.lower() or p.name.lower() == label.lower():
            return p
    log('MISSING', o.OPType, o.name, label)
    return None

def setv(o, label, val):
    p = findpar(o, label)
    if p is None: return
    try:
        if p.style == 'Menu' and isinstance(val, str) and val not in p.menuNames:
            for n, l in zip(p.menuNames, p.menuLabels):
                if l.lower() == val.lower():
                    val = n; break
            else:
                log('MENU?', o.name, label, val, list(p.menuLabels)); return
        p.val = val
    except Exception as e:
        log('ERR', o.name, label, e)

def setx(o, label, expr):
    p = findpar(o, label)
    if p is None: return
    try:
        p.expr = expr
        p.mode = ParMode.EXPRESSION
    except Exception as e:
        log('ERR expr', o.name, label, e)

def mk(parent, T, name, x, y, inputs=()):
    o = parent.create(T, name)
    o.nodeX, o.nodeY = x, y
    for i, src in enumerate(inputs):
        o.inputConnectors[i].connect(src)
    return o

root = op('/project1')
old = root.op('fx_player')
if old:
    prev = root.op('fx_player_old')
    if prev: prev.destroy()
    old.name = 'fx_player_old'      # 前の版は消さずに残す
    old.nodeY = old.nodeY + 300
    old.bypass = True
    for ch in old.findChildren(type=audiodeviceoutCHOP):
        ch.bypass = True
    for ch in old.findChildren(type=oscinCHOP):
        ch.par.active = False        # ポート7400の取り合いを防ぐ
fx = root.create(baseCOMP, 'fx_player')
fx.nodeX, fx.nodeY = 0, -400
fx.color = (0.35, 0.25, 0.45)

# ---- テスト用の既定ファイル（TD付属サンプル） ----
defaults = ['', '', '', '']
try:
    sd = os.path.join(app.samplesFolder, 'Audio')
    files = [os.path.join(sd, f) for f in sorted(os.listdir(sd)) if f.lower().endswith(('.wav', '.aif', '.aiff', '.mp3'))]
    log('sample audio:', [os.path.basename(f) for f in files][:20])
    for i in range(4):
        if files: defaults[i] = files[i % len(files)]
except Exception as e:
    log('no samples', e)

EVENTS = [('Enter', '入場'), ('Exit', '退場'), ('Merge', 'くっつく'), ('Split', '離れる')]

# ---- カスタムパラメータ（fx_player を選ぶと Sounds ページで設定） ----
pg = fx.appendCustomPage('Sounds')
for i, (ev, jp) in enumerate(EVENTS):
    f = pg.appendFile(ev + 'file', label=ev + ' File (' + jp + ')')[0]
    f.val = defaults[i]
    v = pg.appendFloat(ev + 'vol', label=ev + ' Volume')[0]
    v.val = 0.8; v.normMin = 0; v.normMax = 1.5
    L = pg.appendFloat(ev + 'len', label=ev + ' Length (sec)')[0]
    L.val = 4.0; L.normMin = 0.1; L.normMax = 20
m = pg.appendFloat('Mastervol', label='Master Volume')[0]
m.val = 0.6; m.normMin = 0; m.normMax = 1.5
h = pg.appendFloat('Hold', label='Event Pulse (sec)')[0]
h.val = 0.05; h.normMin = 0.02; h.normMax = 0.3

# ---- 入力 ----
src_osc = root.op('oscin2') or root.op('oscin1')
if src_osc is None:
    src_osc = mk(fx, oscinCHOP, 'oscin_fx', -1100, 200)
    setv(src_osc, 'Network Port', 7400)
    setv(src_osc, 'OSC Address Scope', '/gux/slot/* /gux/count /gux/note/*')
    log('created own oscin (no /project1/oscin2)')
srcpath = src_osc.path
log('osc source:', srcpath)

cnt = mk(fx, selectCHOP, 'in_count', -900, 200)
setv(cnt, 'CHOPs', srcpath)
setv(cnt, 'Channel Names', 'gux/count1 gux/count2')
setv(cnt, 'Rename from', 'gux/count1 gux/count2')
setv(cnt, 'Rename to', 'people groups')

# oFが止まっていてもチャンネルが消えないよう、people=0 groups=0 を下敷きにする
dflt = mk(fx, constantCHOP, 'count_default', -900, 50)
try:
    dflt.seq.const.numBlocks = 2
except Exception as ex:
    log('seq err', ex)
setv(dflt, 'const0name', 'people'); setv(dflt, 'const0value', 0)
setv(dflt, 'const1name', 'groups'); setv(dflt, 'const1value', 0)
rep = mk(fx, replaceCHOP, 'count_safe', -800, 150, [dflt, cnt])
cnt = rep
ppl = mk(fx, selectCHOP, 'sel_people', -700, 300, [cnt])
setv(ppl, 'Channel Names', 'people')
link = mk(fx, mathCHOP, 'link', -700, 100, [cnt])
setv(link, 'Combine Channels', 'Subtract')   # people - groups = くっついているペア数

slope_p = mk(fx, slopeCHOP, 'slope_people', -550, 300, [ppl])
slope_l = mk(fx, slopeCHOP, 'slope_link', -550, 100, [link])
neg_p = mk(fx, mathCHOP, 'neg_people', -400, 250, [slope_p]); setv(neg_p, 'Multiply', -1)
neg_l = mk(fx, mathCHOP, 'neg_link', -400, 50, [slope_l]); setv(neg_l, 'Multiply', -1)

trig_src = {'Enter': slope_p, 'Exit': neg_p, 'Merge': slope_l, 'Split': neg_l}
vols = []
ROW = 160 * VOICES + 120
for i, (ev, jp) in enumerate(EVENTS):
    e = ev.lower()
    ybase = 600 - i * ROW
    t = mk(fx, triggerCHOP, 'trig_' + e, -250, ybase, [trig_src[ev]])
    setv(t, 'Trigger Threshold', 0.5)
    setv(t, 'Attack Length', 0)
    setv(t, 'Peak Level', 1)
    setv(t, 'Decay Length', 0)
    setv(t, 'Sustain Level', 1)
    setx(t, 'Min Sustain Length', "parent().par.Hold")
    setv(t, 'Release Length', 0)
    c = mk(fx, countCHOP, 'count_' + e, -100, ybase, [t])
    setv(c, 'Trigger Threshold', 0.5)

    snds = []
    for k in range(VOICES):
        y = ybase - k * 160
        vc = mk(fx, constantCHOP, 'voice_%s_%d' % (e, k), 50, y)
        setv(vc, 'const0name', 'v')
        setx(vc, 'const0value', "op('trig_%s')[0] * (int(op('count_%s')[0]) %% %d == %d)" % (e, e, VOICES, (k + 1) % VOICES))
        g = mk(fx, triggerCHOP, 'gate_%s_%d' % (e, k), 200, y, [vc])
        setv(g, 'Trigger Threshold', 0.5)
        setv(g, 'Attack Length', 0)
        setv(g, 'Peak Level', 1)
        setv(g, 'Decay Length', 0)
        setv(g, 'Sustain Level', 1)
        setx(g, 'Min Sustain Length', "parent().par.%slen" % ev)
        setv(g, 'Release Length', 0)
        a = mk(fx, audiofileinCHOP, 'snd_%s_%d' % (e, k), 350, y)
        setx(a, 'File', "parent().par.%sfile" % ev)
        setv(a, 'Play Mode', 'Sequential')
        setv(a, 'Play', True)
        setv(a, 'Repeat', 'Off')     # ループさせない（1回で終わり）
        setv(a, 'Cue Point', 0)
        setx(a, 'Cue', "op('gate_%s_%d')[0] < 0.5" % (e, k))
        snds.append(a)
    s = mk(fx, mathCHOP, 'sum_' + e, 500, ybase - 80, snds)
    setv(s, 'Combine CHOPs', 'Add')
    v = mk(fx, mathCHOP, 'vol_' + e, 650, ybase - 80, [s])
    setx(v, 'Multiply', "parent().par.%svol" % ev)
    vols.append(v)

mix = mk(fx, mathCHOP, 'mix', 800, 0, vols)
setv(mix, 'Combine CHOPs', 'Add')
master = mk(fx, mathCHOP, 'master', 950, 0, [mix])
setx(master, 'Multiply', "parent().par.Mastervol")
out = mk(fx, nullCHOP, 'out', 1100, 0, [master])
dev = mk(fx, audiodeviceoutCHOP, 'audioout', 1250, 0, [out])

# ---- 情報ログ ----
for o in fx.children:
    try:
        errs, warns = o.errors(recurse=False), o.warnings(recurse=False)
    except Exception:
        errs = warns = ''
    if errs or warns: log('STATE', o.name, 'E:', errs, 'W:', warns)
log('audiofilein pars:', [(p.name, p.label) for p in fx.op('snd_enter_0').pars() if getattr(p.page, 'name', '') != 'Common'])
log('trigger pars:', [(p.name, p.label) for p in fx.op('trig_enter').pars() if getattr(p.page, 'name', '') != 'Common'])
log('count pars:', [(p.name, p.label) for p in fx.op('count_enter').pars() if getattr(p.page, 'name', '') != 'Common'])
log('counts:', [(ch.name, ch.eval()) for ch in cnt.chans()])
for e in ('enter', 'merge'):
  try:
    log('check', e, 'trig.threshup=', fx.op('trig_'+e).par.threshup.eval(), 'voice0 expr=', fx.op('voice_%s_0' % e).par.const0value.expr, 'snd0 repeat=', fx.op('snd_%s_0' % e).par.repeat.eval(), 'cue=', fx.op('snd_%s_0' % e).par.cue.eval())
  except Exception as ex:
    log('check failed', e, ex)
log('in_count chans:', [(ch.name, ch.eval()) for ch in cnt.chans()], 'channames=', fx.op('in_count').par.channames.eval(), 'raw=', [(ch.name, ch.eval()) for ch in fx.op('in_count').chans()])
LOG.append('done')
open(TDDIR + '/build_log.txt', 'w').write('\n'.join(LOG))
print('fx_player built (%d voices/event). log -> %s' % (VOICES, TDDIR + '/build_log.txt'))
