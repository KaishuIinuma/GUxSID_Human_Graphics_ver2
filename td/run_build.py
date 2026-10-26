# exec(open('/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td/run_build.py').read())
import traceback
_d = '/Users/kaishu/Documents/OpenFrameWork/GUxSID_Human_Graphics_ver2/td'
try:
    exec(open(_d + '/build_fx_player.py').read())
except Exception:
    open(_d + '/build_error.txt', 'w').write(traceback.format_exc())
    print('BUILD ERROR -> build_error.txt')
