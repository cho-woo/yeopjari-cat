"""Check the public 0.3 -> 0.3.1 update on the owned AVD, without fixture transport."""
import argparse, hashlib, json, re, subprocess, time, xml.etree.ElementTree as ET
from pathlib import Path

PACKAGE='com.valuewander.yeopjaricat'

def main():
    p=argparse.ArgumentParser();p.add_argument('--adb',required=True);p.add_argument('--old-apk',type=Path,required=True);p.add_argument('--new-apk',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    prefix=[a.adb,'-s','emulator-5580']
    def adb(*parts):return subprocess.check_output(prefix+list(parts),text=True,encoding='utf-8',errors='replace',timeout=90)
    assert 'YeopjariCat_API35' in adb('emu','avd','name'),'Only the owned disposable AVD may be changed'
    assert hashlib.sha256(a.old_apk.read_bytes()).hexdigest()=='47cf1d2f33a3b119ed0ee5dea3409412896a40db6db4c066217430ffe4d0879b','Use the public 0.3 APK'
    def version():return re.search(r'versionName=(\S+)',adb('shell','dumpsys','package',PACKAGE))[1]
    def retained():
        prefs=adb('shell','run-as',PACKAGE,'cat','shared_prefs/wallpaper_history.xml')
        data={n.get('name'):n.get('value',n.text) for n in ET.fromstring(prefs)}
        assert data.get('saved_kind')=='image' and re.fullmatch(r'snap-[a-f0-9-]{36}\.img',data.get('saved_file',''))
        result=subprocess.check_output(prefix+['exec-out','run-as',PACKAGE,'cat','no_backup/wallpaper-history/'+data['saved_file']],timeout=30)
        return {'preferences':data,'photo_sha256':hashlib.sha256(result).hexdigest(),'photo_bytes':len(result)}
    def nodes():
        for _ in range(5):
            raw=adb('shell','uiautomator','dump','/sdcard/public-update-ui.xml')
            if 'dumped to:' not in raw:time.sleep(.5);continue
            xml=adb('shell','cat','/sdcard/public-update-ui.xml');assert "isn't responding" not in xml,'System ANR obscures the actual screen';return list(ET.fromstring(xml).iter('node'))
        raise AssertionError('No fresh UI hierarchy')
    def find(text):return next((n for n in nodes() if n.get('text')==text),None)
    def tap_node(n):
        b=list(map(int,re.findall(r'\d+',n.get('bounds',''))));assert len(b)==4;adb('shell','input','tap',str((b[0]+b[2])//2),str((b[1]+b[3])//2));time.sleep(.4)
    def tap(text):
        n=find(text);assert n is not None,'Missing actual control '+text;tap_node(n)
    def wait(condition,message,seconds=90):
        end=time.monotonic()+seconds
        while time.monotonic()<end:
            result=condition()
            if result:return result
            time.sleep(.4)
        raise AssertionError(message)
    def shot(name):adb('shell','screencap','-p','/sdcard/public-update.png');adb('pull','/sdcard/public-update.png',str(a.out/(name+'.png')))
    before=retained();adb('shell','am','force-stop',PACKAGE)
    adb('install','-r','-d',str(a.old_apk));assert version()=='0.3.0-preview'
    # Remove only the owned AVD's old fixture/throttle metadata. The old updater
    # then checks the real HTTPS feed; wallpaper preferences and files are retained.
    adb('shell','run-as',PACKAGE,'rm','-f','shared_prefs/app_updates.xml')
    adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity')
    wait(lambda:find('새 버전이 나왔어요'),'Public update was not discovered');shot('01-public-update-offer');tap('업데이트하기')
    def destination():
        current=nodes()
        return next((n for n in current if n.get('text') in ('Update','권한 설정 열기')),None)
    n=wait(destination,'Public APK did not reach native install confirmation',150)
    if n.get('text')=='권한 설정 열기':
        tap_node(n);current=nodes();switches=[n for n in current if n.get('checkable')=='true' and n.get('checked')=='false'];assert len(switches)==1;tap_node(switches[0]);adb('shell','input','keyevent','KEYCODE_BACK');wait(lambda:find('Update'),'Native confirmation missing')
    assert version()=='0.3.0-preview';shot('02-real-android-confirmation');tap('Update')
    wait(lambda:version()=='0.3.1-preview','Public APK did not replace the old package');shot('03-installed')
    adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity');wait(lambda:find('이전 배경으로 돌아가기'),'Updated return control missing');shot('04-return-control-and-retained-backup')
    after=retained();assert before==after,'Private wallpaper backup changed during update'
    path=adb('shell','pm','path',PACKAGE).strip();assert path.startswith('package:') and '\n' not in path
    installed=a.out/'installed-public.apk';adb('pull',path[len('package:'):],str(installed));sha=hashlib.sha256(installed.read_bytes()).hexdigest();assert sha==hashlib.sha256(a.new_apk.read_bytes()).hexdigest()
    report={'passed':True,'scope':'Android15 owned AVD; unmodified public 0.3 updater, real HTTPS GitHub feed/download and native Android installation confirmation; no injected transport','from':'0.3.0-preview','to':'0.3.1-preview','installed_apk_sha256':sha,'private_wallpaper_backup_retained':before==after,'retained_photo_sha256':after['photo_sha256'],'system_confirmation_required':True}
    (a.out/'public-update-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8');print(json.dumps(report))

if __name__=='__main__':main()
