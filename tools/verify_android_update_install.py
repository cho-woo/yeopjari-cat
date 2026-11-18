"""Exercise user approval and an actual in-place package upgrade on our disposable AVD.

The future fixture differs only in version metadata; its runtime DEX must match
the v0.3 release candidate. The test APK injects its local transport, not an app
debug endpoint. Nothing is installed on a physical phone by this script.
"""
import argparse,json,re,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path

PACKAGE='com.valuewander.yeopjaricat'

def main():
    p=argparse.ArgumentParser();p.add_argument('--adb',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    prefix=[a.adb,'-s','emulator-5580'];results=[]
    def adb(*parts):return subprocess.check_output(prefix+list(parts),text=True,encoding='utf-8',errors='replace',timeout=45)
    assert 'YeopjariCat_API35' in adb('emu','avd','name'),'Only the owned AVD may be changed'
    def version():
        raw=adb('shell','dumpsys','package',PACKAGE);return re.search(r'versionName=(\S+)',raw)[1]
    assert version()=='0.3.0-preview','Install the final release candidate first'
    def nodes():
        adb('shell','uiautomator','dump','/sdcard/cat-update-ui.xml');xml=adb('shell','cat','/sdcard/cat-update-ui.xml')
        assert "isn't responding" not in xml,'System ANR obscures UI';return list(ET.fromstring(xml).iter('node'))
    def visible(text):return any(n.get('text')==text for n in nodes())
    def tap_node(node):
        b=list(map(int,re.findall(r'\d+',node.get('bounds',''))));assert len(b)==4 and b[3]>b[1];adb('shell','input','tap',str((b[0]+b[2])//2),str((b[1]+b[3])//2));time.sleep(.3)
    def tap(text,scroll=False):
        if scroll:
            for _ in range(3):adb('shell','input','swipe','40','450','40','2200','150')
            time.sleep(.3)
        for _ in range(8 if scroll else 1):
            for node in nodes():
                if node.get('text')==text:tap_node(node);return
            if scroll:adb('shell','input','swipe','40','2100','40','600','250');time.sleep(.2)
        raise AssertionError('Missing control: '+text)
    def wait(condition,message,timeout=30):
        until=time.monotonic()+timeout
        while time.monotonic()<until:
            if condition():return
            time.sleep(.25)
        raise AssertionError(message)
    def shot(name):adb('shell','screencap','-p','/sdcard/cat-update.png');adb('pull','/sdcard/cat-update.png',str(a.out/(name+'.png')))
    def layer_hidden():return 'displaying=true' not in adb('shell','dumpsys','activity','service',PACKAGE+'/.CatOverlayService')
    adb('shell','appops','set',PACKAGE,'SYSTEM_ALERT_WINDOW','allow');adb('shell','pm','grant',PACKAGE,'android.permission.POST_NOTIFICATIONS');adb('shell','appops','set',PACKAGE,'REQUEST_INSTALL_PACKAGES','default')
    log=a.out/'instrumentation.log'
    with log.open('w',encoding='utf-8') as output:
        process=subprocess.Popen(prefix+['shell','am','instrument','-w','-e','mode','update-install',PACKAGE+'.test/'+PACKAGE+'.MotionSmoke'],stdout=output,stderr=subprocess.STDOUT)
        wait(lambda:'READY_FOR_UPDATE_FLOW' in log.read_text(encoding='utf-8'),'Injected test offer did not appear',45)
        assert layer_hidden(),'Native cats obscure the app update controls'
        shot('01-update-offer');tap('업데이트하기')
        wait(lambda:visible('권한 설정 열기'),'Download did not lead to permission explanation');shot('02-download-verified')
        assert version()=='0.3.0-preview';results.append({'verified_download_does_not_silently_install':True})
        adb('shell','input','keyevent','KEYCODE_BACK');tap('업데이트 설치',scroll=True)
        wait(lambda:visible('권한 설정 열기'),'Dismissed explanation left update button disabled');results.append({'dismissed_explanation_is_retryable':True})
        tap('권한 설정 열기');wait(layer_hidden,'Pet windows obscured installation permission');shot('03-install-permission-off')
        # Decline once: the APK stays ready and Android must not upgrade anything.
        adb('shell','input','keyevent','KEYCODE_BACK');time.sleep(.7);assert version()=='0.3.0-preview';results.append({'permission_denial_keeps_current_version':True})
        tap('업데이트 설치',scroll=True);wait(lambda:visible('권한 설정 열기'),'Permission retry missing');tap('권한 설정 열기')
        current=nodes();switches=[n for n in current if n.get('checkable')=='true' and n.get('checked')=='false']
        assert len(switches)==1,'Inspect the actual permission screen before changing it';tap_node(switches[0]);shot('04-install-permission-on');adb('shell','input','keyevent','KEYCODE_BACK')
        wait(lambda:visible('Update'),'Android update confirmation missing');assert layer_hidden();assert version()=='0.3.0-preview';shot('05-android-confirmation')
        results.append({'system_confirmation_required':True,'pet_windows_hidden_during_installer':True})
        tap('Cancel');time.sleep(.7);assert version()=='0.3.0-preview';shot('06-cancel-preserves-version');results.append({'install_cancel_preserves_current_version':True})
        tap('업데이트 설치',scroll=True);wait(lambda:visible('Update'),'Update retry missing');tap('Update')
        wait(lambda:version()=='0.3.1-preview','Android did not replace the package',60);shot('07-package-replaced')
        # This process exits when its target package is replaced; that is expected.
        try:process.wait(timeout=15)
        except subprocess.TimeoutExpired:process.terminate()
    if visible('Open'):tap('Open')
    else:adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity')
    time.sleep(1);shot('08-settings-retained')
    raw=adb('shell','run-as',PACKAGE,'cat','shared_prefs/pet_settings.xml');prefs={n.get('name'):n.get('value',n.text) for n in ET.fromstring(raw)}
    assert prefs.get('upgrade_marker')=='keep-v03-settings' and prefs.get('coat')=='8' and prefs.get('companion_coat')=='10' and prefs.get('companion')=='true',prefs
    assert abs(float(prefs['position.home.0.y'])-.45)<.002 and abs(float(prefs['position.home.1.y'])-.53)<.002,prefs
    results.append({'in_place_upgrade':True,'user_settings_and_both_cats_retained':True,'dragged_heights_retained':True})
    (a.out/'install-report.json').write_text(json.dumps({'passed':True,'scope':'Android15 AVD real OS installer; final v0.3 updater installs a same-DEX higher-version fixture, not a public v0.3.1 release','from_version':'0.3.0-preview','fixture_version':'0.3.1-preview','results':results},indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'checks':len(results)}))

if __name__=='__main__':main()
