# exec(open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/test_voices.py').read())
# trig_enter を手動で3回（0.3秒おき）発火し、各ボイスの gate / 再生位置を記録する（テスト専用）
_P = '/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/test_log.txt'
_fx = op('/project1/fx_player')
open(_P, 'w').write('test start\n')
def _snap(tag):
    g = [int(_fx.op('gate_enter_%d' % k)[0].eval() > 0.5) for k in range(6)]
    c = int(_fx.op('count_enter')[0].eval())
    s = []
    for k in range(6):
        a = _fx.op('snd_enter_%d' % k)
        s.append('%d:%s' % (k, 'cue' if a.par.cue.eval() else 'play'))
    open(_P, 'a').write('%s t=%.2f frame=%d count=%d gates=%s %s\n' % (tag, absTime.seconds, absTime.frame, c, g, ' '.join(s)))
def _on(): _fx.op('trig_enter').par.trigger = True
def _off(): _fx.op('trig_enter').par.trigger = False
open(_P, 'a').write('timeline play=%s rate=%s realtime=%s\n' % (op('/project1').time.play, op('/project1').time.rate, project.realTime))
for i in range(3):
    d = 100 + i * 400
    run("args[0]()", _on, delayMilliSeconds=d)
    run("args[0]()", _off, delayMilliSeconds=d + 80)
for f in (50, 150, 300, 550, 700, 950, 1100, 1500, 3000):
    run("args[0](args[1])", _snap, 'ms+%d' % f, delayMilliSeconds=f)
print('test scheduled')
