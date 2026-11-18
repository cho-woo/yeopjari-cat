"""Native rollback review on the owned AVD, with synthetic photos and an independent test wallpaper."""
import argparse,json,re,subprocess,time,xml.etree.ElementTree as ET
from pathlib import Path

PACKAGE='com.valuewander.yeopjaricat'

def main():
    p=argparse.ArgumentParser();p.add_argument('--adb',required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    prefix=[a.adb,'-s','emulator-5580'];results=[];sequence=0;cached=[]
    def adb(*args):return subprocess.check_output(prefix+list(args),text=True,encoding='utf-8',errors='replace',timeout=45)
    assert 'YeopjariCat_API35' in adb('emu','avd','name'),'Only the disposable review AVD is allowed'
    base='/sdcard/Android/data/'+PACKAGE+'/files/wallpaper-review/'
    def state():return json.loads(adb('shell','cat',base+'state.json'))
    def wait(condition,message,seconds=30):
        until=time.monotonic()+seconds
        while time.monotonic()<until:
            if condition():return
            time.sleep(.2)
        raise AssertionError(message+'\n'+json.dumps(state()))
    def nodes():
        for _ in range(5):
            output=adb('shell','uiautomator','dump','/sdcard/wallpaper-review-ui.xml')
            if 'dumped to:' not in output:time.sleep(.4);continue
            xml=adb('shell','cat','/sdcard/wallpaper-review-ui.xml');assert "isn't responding" not in xml,'System ANR obscures the app'
            return list(ET.fromstring(xml).iter('node'))
        raise AssertionError('No fresh UI hierarchy')
    def tap(text):
        nonlocal cached
        current=cached or nodes();cached=[]
        for n in current:
            if n.get('text')==text:
                b=list(map(int,re.findall(r'\d+',n.get('bounds',''))));assert len(b)==4
                adb('shell','input','tap',str((b[0]+b[2])//2),str((b[1]+b[3])//2));time.sleep(.3);return
        raise AssertionError('Missing visible control: '+text)
    def visible(text):
        nonlocal cached
        cached=nodes();return any(n.get('text')==text for n in cached)
    def back():
        nonlocal cached
        cached=[];adb('shell','input','keyevent','KEYCODE_BACK')
    def app():
        nonlocal cached
        cached=[]
        adb('shell','am','start','-W','-n',PACKAGE+'/.MainActivity','-f','0x24000000')
        # This flow only uses the top controls and never scrolls the Activity.
        # Avoid introducing launcher/back gestures into a wallpaper-return test.
        time.sleep(.2)
    def shot(name):adb('shell','screencap','-p','/sdcard/wallpaper-review.png');adb('pull','/sdcard/wallpaper-review.png',str(a.out/(name+'.png')))
    def set_wallpaper():
        wait(lambda:visible('Set wallpaper'),'Wallpaper preview not opened');previous=state()['id'];tap('Set wallpaper')
        # The destination dialog can be created after the first hierarchy dump.
        # Never dismiss it by relaunching our Activity or choose Home+Lock implicitly.
        # Android also applies directly when its current Home/Lock arrangement
        # has no destination choice. Require the actual manager ID to change.
        wait(lambda:state()['id']!=previous or visible('Home screen'),'Wallpaper neither applied nor offered a destination')
        if state()['id']==previous:tap('Home screen')
        time.sleep(.8);app()
    def command(action):
        nonlocal sequence
        sequence+=1
        local=a.out/'command.json';local.write_text(json.dumps({'id':sequence,'action':action}),encoding='utf-8')
        adb('push',str(local),base+'command.next');adb('shell','mv',base+'command.next',base+'command.json')
        wait(lambda:state()['command']==sequence,'Review command not acknowledged')
    def blue_like(original,current):
        return all(abs(((original>>shift)&255)-((current>>shift)&255))<=12 for shift in (0,8,16))
    def separate_lock():
        # The OS picker owns its Home/Lock destination. Seed an independent Lock
        # after that picker to isolate the app's FLAG_SYSTEM photo/default calls.
        command('lock');current=state()['lock_id'];assert current>0;return current
    log=a.out/'instrumentation.log'
    with log.open('w',encoding='utf-8') as output:
        process=subprocess.Popen(prefix+['shell','am','instrument','-w','-e','mode','wallpaper-return',PACKAGE+'.test/'+PACKAGE+'.MotionSmoke'],stdout=output,stderr=subprocess.STDOUT)
        wait(lambda:'READY_FOR_WALLPAPER_RETURN' in log.read_text(encoding='utf-8'),'Review did not start',90)
        app();baseline=state();lock=baseline['lock_id'];assert not baseline['cat'];shot('01-before-static')
        print('Reviewing photo backup and cancellation',flush=True)
        tap('홈 화면에 데려오기');tap('이전 사진 보관');wait(lambda:visible('Set wallpaper'),'Photo selection did not lead to preview')
        back();wait(lambda:state()['phase']=='','Cancelled picker did not reconcile')
        assert state()['id']==baseline['id'] and not state()['backup_present'];shot('02-cancel-keeps-original')
        results.append({'cancel_apply_preserves_original_id':True,'cancel_does_not_create_false_backup':True})
        print('Reviewing photo apply and return',flush=True)
        tap('홈 화면에 데려오기');tap('이전 사진 보관');set_wallpaper();wait(lambda:state()['cat'] and state()['saved_kind']=='image','Photo backup not committed')
        first=state();assert first['backup_bytes']>0;lock=separate_lock();shot('03-cat-with-photo-backup')
        tap('홈 화면에 데려오기');time.sleep(.3);assert state()['saved_file']==first['saved_file'] and state()['phase']==''
        tap('이전 배경으로 돌아가기');tap('보관한 이전 사진으로 되돌리기');wait(lambda:not state()['cat'] and not state()['busy'],'Photo restore failed')
        wait(lambda:blue_like(baseline['color'],state()['color']),'Restored photo color does not match');restored=state();assert not restored['enabled'] and restored['lock_id']==lock;shot('04-original-photo-restored')
        command('recreate');assert state()['recreated_backup'];results.append({'saved_photo_restored':True,'reapply_keeps_original_backup':True,'backup_survives_store_recreation':True,'cats_stop_on_restore':True,'lock_wallpaper_preserved':True})
        # A different package's live wallpaper is automatically remembered without reading images.
        print('Reviewing previous live wallpaper round trip',flush=True)
        adb('shell','am','start','-W','-a','android.service.wallpaper.CHANGE_LIVE_WALLPAPER','--ecn','android.service.wallpaper.extra.LIVE_WALLPAPER_COMPONENT',PACKAGE+'.test/'+PACKAGE+'.FixtureWallpaperService');set_wallpaper()
        foreign=state();assert 'FixtureWallpaperService' in foreign['component']
        tap('홈 화면에 데려오기');wait(lambda:visible('Set wallpaper'),'Foreign live wallpaper not detected');back();wait(lambda:state()['phase']=='','Live apply cancel stuck')
        assert state()['saved_file']==first['saved_file'] and 'FixtureWallpaperService' in state()['component'] and not state()['enabled']
        tap('홈 화면에 데려오기');set_wallpaper();wait(lambda:state()['cat'] and state()['saved_kind']=='live','Previous live component not saved');shot('05-live-component-backed-up')
        tap('이전 배경으로 돌아가기');tap('이전 움직이는 배경으로 되돌리기');set_wallpaper();wait(lambda:'FixtureWallpaperService' in state()['component'] and not state()['enabled'],'Previous live wallpaper not restored')
        shot('06-live-wallpaper-restored');results.append({'previous_live_restored_through_system_preview':True,'cancelled_live_apply_keeps_existing_photo_backup':True,'cancel_restores_previously_disabled_cats':True})
        # Simulate an older installation which never retained its original wallpaper.
        print('Reviewing legacy/default return',flush=True)
        tap('홈 화면에 데려오기');set_wallpaper();wait(lambda:state()['cat'],'Cat was not applied');lock=separate_lock();command('forget');assert not state()['backup_present']
        tap('이전 배경으로 돌아가기');assert not visible('보관한 이전 사진으로 되돌리기');tap('휴대폰 기본 배경으로 해제');wait(lambda:not state()['cat'] and not state()['busy'],'Legacy default exit failed')
        assert not state()['enabled'] and state()['lock_id']==lock;shot('07-legacy-default-exit');results.append({'unbacked_legacy_install_can_exit':True,'default_is_not_claimed_as_original_photo':True})
        # Known OEM default can be restored directly on the next application.
        tap('홈 화면에 데려오기');set_wallpaper();wait(lambda:state()['cat'] and state()['saved_kind']=='default','Known default was not recorded');lock=separate_lock()
        tap('이전 배경으로 돌아가기');tap('보관한 기본 배경으로 되돌리기');wait(lambda:not state()['cat'] and not state()['busy'],'Known default return failed');assert state()['lock_id']==lock;results.append({'known_default_round_trip':True})
        tap('홈 화면에 데려오기');set_wallpaper();wait(lambda:state()['cat'],'Legacy photo setup failed');lock=separate_lock();command('forget')
        tap('이전 배경으로 돌아가기');tap('이전 사진을 골라서 되돌리기');wait(lambda:not state()['cat'] and state()['saved_kind']=='image' and not state()['busy'],'Manual previous photo return failed')
        wait(lambda:blue_like(baseline['color'],state()['color']),'Selected photo color does not match');final=state();assert not final['enabled'] and final['lock_id']==lock;shot('08-manually-selected-photo-restored')
        results.append({'legacy_photo_selection_restores_home':True,'photo_and_default_returns_preserve_separately_selected_lock':True})
        command('finish');process.wait(timeout=20)
    (a.out/'wallpaper-return-report.json').write_text(json.dumps({'passed':True,'scope':'Android15 AVD real wallpaper manager and system picker; synthetic photo picker result supplied by a separate test APK; no broad image-reading permission','baseline':baseline,'final':final,'results':results},indent=2),encoding='utf-8')
    print(json.dumps({'passed':True,'scenarios':len(results)}))

if __name__=='__main__':main()
