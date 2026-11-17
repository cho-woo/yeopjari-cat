"""External lifecycle checks; attach no Instrumentation that restarts the wallpaper.

Run after applying the live wallpaper on the disposable YeopjariCat emulator.
"""
import argparse,json,re,subprocess,time
from pathlib import Path

def main():
    p=argparse.ArgumentParser();p.add_argument('--adb',required=True);p.add_argument('--out',type=Path,required=True);args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    prefix=[args.adb,'-s','emulator-5580']
    def adb(*parts):return subprocess.check_output(prefix+list(parts),text=True,encoding='utf-8',errors='replace',timeout=40)
    assert 'YeopjariCat_API35' in adb('emu','avd','name'),'Only the owned disposable AVD may be changed'
    def state():
        raw=adb('shell','dumpsys','activity','service','com.valuewander.yeopjaricat/.CatWallpaperService')
        match=re.search(r'cat_home_visible=(true|false) cat_frames=(\d+)',raw)
        if not match:return None
        return {'visible':match[1]=='true','frames':int(match[2]),'dump':raw}
    def wait(visible):
        until=time.monotonic()+15
        while time.monotonic()<until:
            value=state()
            if value and value['visible']==visible:return value
            time.sleep(.15)
        raise AssertionError('Wallpaper visibility did not reach '+str(visible))
    def shot(name):
        adb('shell','screencap','-p','/sdcard/yeopjari-qa.png');adb('pull','/sdcard/yeopjari-qa.png',str(args.out/(name+'.png')))
    results=[]
    adb('shell','input','keyevent','KEYCODE_HOME');before=wait(True);time.sleep(1.5);after=state();assert after['frames']>before['frames']
    shot('home');results.append({'home_animated':True,'frames_in_1_5s':after['frames']-before['frames']})
    adb('shell','am','start','-a','android.settings.SETTINGS');wait(False);time.sleep(.6);before=state();time.sleep(1.5);after=state();assert after['frames']==before['frames'],'Hidden wallpaper kept rendering'
    shot('other-app');results.append({'other_app_hidden':True,'hidden_frames':0})
    adb('shell','input','keyevent','KEYCODE_HOME');wait(True)
    try:
        for width,height in [(1968,2184),(1080,2520),(2520,1080)]:
            adb('shell','wm','size',f'{width}x{height}');time.sleep(1.5);s=wait(True)
            assert 'error=none' in s['dump'],s['dump'];shot(f'viewport-{width}x{height}')
            results.append({'viewport':[width,height],'visible':True,'render_error':False})
    finally:adb('shell','wm','size','reset')
    adb('shell','input','keyevent','KEYCODE_SLEEP');wait(False);time.sleep(.6);before=state();time.sleep(1.5);after=state();assert after['frames']==before['frames'],'Screen-off wallpaper kept rendering'
    results.append({'screen_off_frames':0})
    adb('shell','input','keyevent','KEYCODE_WAKEUP');time.sleep(.8);shot('lock-screen');adb('shell','wm','dismiss-keyguard');wait(True);time.sleep(1);shot('home-return')
    (args.out/'wallpaper-report.json').write_text(json.dumps({'passed':True,'scope':'Android15 AVD; three viewport sizes simulate fold/rotation, not physical Z Fold7 tests','results':results},indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'checks':len(results)}))

if __name__=='__main__':main()
