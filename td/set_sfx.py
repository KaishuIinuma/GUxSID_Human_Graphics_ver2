# GUxSID FX Player — 効果音wavのセット（既存の /project1/fx_player をその場で書き換える）
# Textport で実行:
# exec(open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/set_sfx.py').read())
#
# イベントごとに Table DAT「sfx_<event>」(ファイル, 長さ秒) を作り、
# 6つのボイスがそれぞれ別の行(=別の鳴らし方)を受け持つ。出来事が起きるたびに次のボイス=次のバリエーションが鳴る。
# 差し替えたいときは sfx_<event> の表を書き換えるだけでよい（ノードはそのまま）。
import csv, os, traceback
TDDIR = '/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td'
SFX = TDDIR + '/sfx'
LOG = []
def log(*a): LOG.append(' '.join(str(x) for x in a))
MAP = {
 'enter': ['Vibraphone_x4', 'Vibraphone_single', 'Vibraphone_x6', 'Vibraphone_single_soft', 'Vibraphone_x9', 'Vibraphone_x8_fast'],
 'exit':  ['Dish_single_D5', 'Dish_single_A4', 'Dish_pair_A3_D4', 'Dish_single_D4', 'Dish_trio_up_Fs4_D5', 'Dish_single_A3'],
 'merge': ['TapedMarimba_burst5_D2', 'TapedMarimba_burst8_D2', 'TapedMarimba_burst14_decel_D2', 'TapedMarimba_burst3_D2', 'TapedMarimba_burst6_A1', 'TapedMarimba_burst5_soft_D2'],
 'split': ['ReverseCymbal_D4', 'ReverseCymbal_A3', 'ReverseCymbal_D3', 'ReverseCymbal_A2', 'ReverseCymbal_Fs2', 'ReverseCymbal_D2'],
}
try:
    dur = {}
    with open(SFX + '/FX_index.csv') as f:
        for r in csv.DictReader(f):
            dur[r['file']] = int(r['duration_ms']) / 1000.0
    files = sorted(dur)
    fx = op('/project1/fx_player')
    if fx is None: raise Exception('/project1/fx_player がありません。先に build_fx_player.py を実行してください')
    VOICES = len([o for o in fx.children if o.name.startswith('snd_enter_')])
    for i, (e, names) in enumerate(MAP.items()):
        t = fx.op('sfx_' + e) or fx.create(tableDAT, 'sfx_' + e)
        t.nodeX, t.nodeY = 350, 760 - i * (160 * VOICES + 120)
        t.clear()
        for n in names:
            fn = next((x for x in files if x.startswith(n)), None)
            if fn is None: log('MISSING', n); continue
            t.appendRow([SFX + '/' + fn, round(dur[fn] + 0.05, 3)])
        for k in range(VOICES):
            a = fx.op('snd_%s_%d' % (e, k)); g = fx.op('gate_%s_%d' % (e, k))
            a.par.file.expr = "op('sfx_%s')[%d %% op('sfx_%s').numRows, 0]" % (e, k, e)
            a.par.file.mode = ParMode.EXPRESSION
            g.par.minsustain.expr = "float(op('sfx_%s')[%d %% op('sfx_%s').numRows, 1])" % (e, k, e)
            g.par.minsustain.mode = ParMode.EXPRESSION
        log(e, 'rows', t.numRows, 'voice0 ->', fx.op('snd_%s_0' % e).par.file.eval(), 'len', fx.op('gate_%s_0' % e).par.minsustain.eval())
    for o in fx.children:
        errs = o.errors(recurse=False)
        if errs: log('ERR', o.name, errs)
    LOG.append('done')
except Exception:
    LOG.append(traceback.format_exc())
open(TDDIR + '/set_sfx_log.txt', 'w').write('\n'.join(LOG))
print('set_sfx finished -> set_sfx_log.txt')
