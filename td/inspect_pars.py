# TouchDesigner Textport: exec(open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/inspect_pars.py').read())
import os
_dir = '/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td'
_out = []
_tmp = op('/').create(baseCOMP, 'zz_inspect_tmp')
try:
    for T in (audiofileinCHOP, triggerCHOP, mathCHOP, slopeCHOP, selectCHOP, oscinCHOP, audiodeviceoutCHOP, nullCHOP, logicCHOP, audiofilterCHOP):
        o = _tmp.create(T)
        _out.append('## ' + o.OPType)
        for p in o.pars():
            if getattr(p.page, 'name', '') == 'Common':
                continue
            line = '%s | %s | %s | %s' % (p.page.name, p.name, p.label, p.style)
            if p.isMenu:
                line += ' | ' + ','.join('%s=%s' % (n, l) for n, l in zip(p.menuNames, p.menuLabels))
            line += ' | default=' + str(p.default)
            _out.append(line)
    _out.append('## project ' + str(project.name) + ' ' + str(project.folder))
    _out.append('## children /project1: ' + ', '.join(c.name for c in op('/project1').children) if op('/project1') else 'no project1')
    _out.append('## samples ' + str(app.samplesFolder))
    sf = app.samplesFolder + '/Audio'
    if os.path.isdir(sf):
        _out.append('## sample audio: ' + ', '.join(sorted(os.listdir(sf))[:40]))
finally:
    _tmp.destroy()
open(_dir + '/inspect_log.txt', 'w').write('\n'.join(_out))
print('inspect done', len(_out))
