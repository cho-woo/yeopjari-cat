"""Real system-window QA on the owned disposable AVD, after View instrumentation.

No debug control endpoint or test Activity is packaged in the downloadable APK.
"""
import argparse,json,re,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path

PACKAGE='com.valuewander.yeopjaricat'

def main():
    p=argparse.ArgumentParser();p.add_argument('--adb',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    a.out.mkdir(parents=True,exist_ok=True);prefix=[a.adb,'-s','emulator-5580'];results=[]
    def adb(*parts):return subprocess.check_output(prefix+list(parts),text=True,encoding='utf-8',errors='replace',timeout=45)
    assert 'YeopjariCat_API35' in adb('emu','avd','name'),'Only the owned AVD may be changed'
    def layer():
        raw=adb('shell','dumpsys','activity','service',PACKAGE+'/.CatOverlayService')
        m=re.search(r'cat_layer_running=(\w+) displaying=(\w+) cross_apps=(\w+) home_touch=(\w+) enabled=(\w+) frames=(\d+)',raw)
        if not m:return {'running':False,'displaying':False,'raw':raw}
        s={k:v=='true' for k,v in zip(['running','displaying','cross_apps','home_touch','enabled'],m.groups()[:5])};s['frames']=int(m[6]);s['raw']=raw
        s['actors']=[{'id':int(i),'x':float(x),'y':float(y),'action':action} for i,x,y,action in re.findall(r'actor=(\d+) x=([\d.E-]+) y=([\d.E-]+) state=(\w+)',raw)]
        return s
    def wallpaper():return adb('shell','dumpsys','activity','service',PACKAGE+'/.CatWallpaperService')
    def wait(condition,message,timeout=18):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            if condition():return
            time.sleep(.15)
        raise AssertionError(message+'\n'+layer()['raw']+'\n'+wallpaper())
    def nodes():
        adb('shell','uiautomator','dump','/sdcard/cat-touch-ui.xml')
        text=adb('shell','cat','/sdcard/cat-touch-ui.xml');root=ET.fromstring(text)
        assert "isn't responding" not in text,'System ANR obscures app'
        return root.iter('node')
    def tap_text(text,scroll=False):
        for attempt in range(9 if scroll else 1):
            for node in nodes():
                if node.get('text')==text:
                    b=list(map(int,re.findall(r'\d+',node.get('bounds',''))))
                    if len(b)==4 and b[3]>b[1]:adb('shell','input','tap',str((b[0]+b[2])//2),str((b[1]+b[3])//2));time.sleep(.4);return
            if scroll:adb('shell','input','swipe','900','1960','900','700','350');time.sleep(.3)
        raise AssertionError('Missing visible control: '+text)
    def shot(name):
        adb('shell','screencap','-p','/sdcard/cat-touch.png');adb('pull','/sdcard/cat-touch.png',str(a.out/(name+'.png')))
    def main_app():
        adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity');time.sleep(.5)
        # Android may reuse the Activity with its previous scroll position.
        for _ in range(3):adb('shell','input','swipe','40','450','40','2200','150')
        time.sleep(.4)
    def home():adb('shell','input','keyevent','KEYCODE_HOME')
    def prefs():return ET.fromstring(adb('shell','run-as',PACKAGE,'cat','shared_prefs/pet_settings.xml'))
    def preference(name):
        for entry in prefs():
            if entry.get('name')==name:return entry.get('value',entry.text)
        return None
    density=int(re.findall(r'\d+',adb('shell','wm','density'))[-1])/160
    main_app()
    if preference('pets_enabled')=='false':tap_text('고양이 다시 켜기',scroll=True)
    if preference('touch_home')=='true':tap_text('홈 화면 터치 조작')
    if preference('overlay')=='true':tap_text('다른 앱 위에 고양이 표시',scroll=True)
    # Apply through the real Android picker. No shell-only wallpaper override.
    adb('shell','am','force-stop','com.android.wallpaper.livepicker')
    adb('shell','am','start','-W','-a','android.service.wallpaper.CHANGE_LIVE_WALLPAPER','--ecn','android.service.wallpaper.extra.LIVE_WALLPAPER_COMPONENT',PACKAGE+'/.CatWallpaperService')
    time.sleep(.6);tap_text('Set wallpaper')
    try:tap_text('Home screen')
    except AssertionError:
        # Reapplying the already selected live wallpaper returns directly Home.
        assert PACKAGE in adb('shell','dumpsys','wallpaper'),'Picker did not apply our wallpaper'
    home()
    wait(lambda:'cat_home_visible=true' in wallpaper(),'Wallpaper not applied');shot('basic-home')
    # User can cancel the extra permission explanation without enabling the mode.
    adb('shell','appops','set',PACKAGE,'SYSTEM_ALERT_WINDOW','allow');adb('shell','pm','grant',PACKAGE,'android.permission.POST_NOTIFICATIONS')
    adb('shell','appops','set',PACKAGE,'GET_USAGE_STATS','default');main_app();tap_text('홈 화면 터치 조작');shot('usage-disclosure');tap_text('취소')
    assert not layer()['running'],'Cancelled permission flow started a layer'
    results.append({'cancelled_usage_permission_stays_off':True})
    # The test harness grants the OS app-op on this disposable device only.
    adb('shell','appops','set',PACKAGE,'GET_USAGE_STATS','allow');tap_text('홈 화면 터치 조작');home()
    wait(lambda:layer()['displaying'],'Home-only layer did not appear');s=layer();assert s['home_touch'] and not s['cross_apps'];shot('touch-home')
    before=s['frames'];time.sleep(1);assert layer()['frames']>before
    results.append({'home_touch_enabled':True,'cross_apps_stays_off':True})
    saved_y=[]
    for role in range(2):
        s=layer();actor=s['actors'][role];x=round(actor['x']*density);y=round((actor['y']-37)*density)
        dx=round((40 if role==0 else -40)*density);dy=round(-60*density)
        adb('shell','input','swipe',str(x),str(y),str(x+dx),str(y+dy),'420');time.sleep(.15);after=layer()['actors'][role]
        assert abs(after['y']-actor['y']+60)<3,f'Native-window drag lost vertical displacement: {actor} -> {after}'
        assert abs(after['x']-actor['x']-dx/density)<12,'Native-window drag lost horizontal displacement'
        stored=float(preference(f'position.home.{role}.y'));saved_y.append(stored)
        results.append({'role':role,'native_2d_drag':True,'vertical_delta_dp':after['y']-actor['y'],'saved_normalized_y':stored})
        shot(f'home-drag-{role}')
    # Home-only layer must hide behind opaque AND wallpaper-showing foreground apps.
    adb('shell','am','start','-W','-a','android.settings.SETTINGS');wait(lambda:not layer()['displaying'],'Cat remained above another app');time.sleep(.5)
    before=layer()['frames'];time.sleep(1);assert layer()['frames']==before;shot('opaque-app')
    results.append({'opaque_app_hidden':True,'hidden_layer_frames':0})
    home();wait(lambda:layer()['displaying'],'No Home return')
    adb('shell','am','start','-W','-n',PACKAGE+'.test/com.valuewander.yeopjaricat.TranslucentReviewActivity')
    wait(lambda:not layer()['displaying'],'Cat remained above translucent app');time.sleep(.7)
    raw=wallpaper();assert 'cat_home_visible=true' in raw and 'cat_painted=false' in raw,raw;shot('translucent-app')
    results.append({'translucent_app_hidden':True,'wallpaper_fallback_cat_hidden':True})
    home();wait(lambda:layer()['displaying'],'No return after translucent app')
    try:
        for width,height in [(1968,2184),(1080,2520),(2520,1080)]:
            adb('shell','wm','size',f'{width}x{height}');time.sleep(1.2);wait(lambda:layer()['displaying'],'Layer lost on viewport change');s=layer()
            assert all(abs(actor['y']/(height/density)-saved_y[actor['id']])<.025 for actor in s['actors']),'Drag height not preserved on resize'
            assert 'error=none' in wallpaper();shot(f'viewport-{width}x{height}')
            results.append({'viewport':[width,height],'dragged_height_preserved':True,'render_error':False})
    finally:adb('shell','wm','size','reset')
    time.sleep(.8);wait(lambda:layer()['displaying'],'Layer not restored after resize')
    # Explicit separate cross-app switch remains independent of Home touch mode.
    main_app();tap_text('다른 앱 위에 고양이 표시',scroll=True)
    adb('shell','am','start','-W','-a','android.settings.SETTINGS');wait(lambda:layer()['displaying'],'Explicit cross-app mode did not display');shot('cross-app-opt-in')
    main_app();tap_text('다른 앱 위에 고양이 표시',scroll=True);home();wait(lambda:layer()['displaying'] and not layer()['cross_apps'],'Turning off cross-app mode stopped Home touch mode')
    results.append({'cross_app_explicit_opt_in':True,'home_touch_independent':True})
    # Loss of usage access fails closed instead of leaving a stale Home window.
    adb('shell','appops','set',PACKAGE,'GET_USAGE_STATS','ignore');wait(lambda:not layer()['displaying'],'Usage access revocation did not hide layer');shot('usage-revoked')
    adb('shell','appops','set',PACKAGE,'GET_USAGE_STATS','allow');wait(lambda:layer()['displaying'],'Grant did not restore Home layer')
    results.append({'usage_revocation_fails_closed':True})
    adb('shell','input','keyevent','KEYCODE_SLEEP');wait(lambda:not layer()['displaying'],'Screen-off layer remains');before=layer()['frames'];time.sleep(1);assert layer()['frames']==before
    adb('shell','input','keyevent','KEYCODE_WAKEUP');adb('shell','wm','dismiss-keyguard');home();wait(lambda:layer()['displaying'],'Wake did not restore layer')
    results.append({'screen_off_layer_frames':0,'wake_home_resumes':True})
    # Real Service-owned overlay dialog, including cancellation and companion quit.
    for role,label in [(0,'취소'),(1,'종료하기')]:
        actor=layer()['actors'][role];x=round(actor['x']*density);y=round((actor['y']-37)*density)
        adb('shell','input','swipe',str(x),str(y),str(x),str(y),'800');time.sleep(.5);shot(f'home-long-press-{role}');tap_text(label)
        if label=='취소':wait(lambda:layer()['displaying'],'Cancel unexpectedly stopped cats')
    wait(lambda:not layer()['running'],'Quit left layer running');assert preference('pets_enabled')=='false';time.sleep(.5);shot('quit-all')
    raw=wallpaper();before=int(re.search(r'cat_frames=(\d+)',raw)[1]);time.sleep(1);assert int(re.search(r'cat_frames=(\d+)',wallpaper())[1])==before
    results.append({'native_long_press_cancel':True,'companion_menu_quits_all':True,'quit_wallpaper_frames':0})
    main_app();tap_text('고양이 다시 켜기',scroll=True);tap_text('홈 화면 터치 조작');home();wait(lambda:layer()['displaying'],'Resume failed')
    results.append({'resume_after_quit':True})
    (a.out/'touch-system-report.json').write_text(json.dumps({'passed':True,'scope':'Android15 AVD, real wallpaper and native windows; viewport simulations are not physical Fold7/OneUI/battery tests','results':results},ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'checks':len(results)}))

if __name__=='__main__':main()
