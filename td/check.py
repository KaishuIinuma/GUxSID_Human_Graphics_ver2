# exec(open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/check.py').read())
_o = []
_o.append('project: %s/%s' % (project.folder, project.name))
_o.append('/project1 children: ' + ', '.join('%s(%s)' % (c.name, c.OPType) for c in op('/project1').children))
_o.append('oscin anywhere: ' + ', '.join('%s port=%s active=%s scope=%s' % (c.path, c.par.port.eval(), c.par.active.eval(), getattr(c.par, 'addscope', None) and c.par.addscope.eval()) for c in op('/').findChildren(type=oscinCHOP)))
_t = op('/').create(baseCOMP, 'zz_tmp_check')
try:
    k = _t.create(constantCHOP)
    _o.append('constant pars: ' + str([(p.name, p.label) for p in k.pars() if getattr(p.page, 'name', '') != 'Common'][:20]))
    s = _t.create(selectCHOP)
    p = s.par.channames
    _o.append('select channames style=%s isMenu=%s isString=%s' % (p.style, p.isMenu, p.isString))
    tr = _t.create(triggerCHOP)
    _o.append('trigger units: ' + str([(n, getattr(tr.par, n).eval(), list(getattr(tr.par, n).menuNames)) for n in ('minsustainunit', 'decayunit', 'attackunit')]))
    _o.append('trigger defaults: threshold=%s threshup=%s' % (tr.par.threshold.eval(), tr.par.threshup.eval()))
finally:
    _t.destroy()
open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/check_log.txt', 'w').write('\n'.join(_o))
print('check done')
