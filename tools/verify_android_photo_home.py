"""Cold first finger gestures on the real live wallpaper of the disposable API 35 AVD.

The OS unsets a force-stopped live wallpaper, so every trial confirms the system
wallpaper picker again before its first swipe. A previously selected photo is required.
"""
import argparse
import json
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path
from verify_android_cold_jump import CONFIG, PACKAGE

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--coats', type=int, nargs='+', required=True, choices=[0,8,10])
    args=parser.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    prefix=[args.adb,'-s','emulator-5580']
    def adb(*cmd,check=True,stdin=None):
        return subprocess.run(prefix+[str(i) for i in cmd],input=stdin,capture_output=True,check=check,
                              timeout=90).stdout.decode('utf-8',errors='replace')
    def sh(*cmd):return adb('shell',*cmd)
    def wait(fn,msg,seconds=30):
        deadline=time.monotonic()+seconds
        while time.monotonic()<deadline:
            value=fn()
            if value:return value
            time.sleep(.18)
        raise AssertionError(msg)
    def matches(labels):
        sh('uiautomator','dump','--compressed','/sdcard/cat-photo-home.xml')
        try:tree=ET.fromstring(sh('cat','/sdcard/cat-photo-home.xml'))
        except ET.ParseError:return None
        return next((n.get('bounds') for n in tree.iter('node') if n.get('text') in labels),None)
    def tap(labels):
        bounds=wait(lambda:matches(labels),str(labels))
        x,y,u,v=map(int,re.findall(r'\d+',bounds))
        sh('input','tap',(x+u)//2,(y+v)//2)
    def status():return sh('dumpsys','activity','service',PACKAGE+'/.CatWallpaperService')
    def first_actor():
        raw=status()
        if 'cat_painted=true' not in raw or 'cat_home_visible=true' not in raw:return None
        return re.search(r'actor=0 coat=(\d+) action=(\S+) jumps=(\d+) owner=(\S*) hitX=([\d.]+) hitY=([\d.]+)',raw)
    assert 'YeopjariCat_API35' in adb('emu','avd','name')
    assert 'android.permission.PACKAGE_USAGE_STATS' not in sh('dumpsys','package',PACKAGE)
    assert 'allow' not in sh('appops','get',PACKAGE,'SYSTEM_ALERT_WINDOW').lower(),'Test the default without overlay access'
    config=args.out/'trace.pbtxt'
    config.write_text(CONFIG)
    adb('push',config,'/data/misc/perfetto-configs/yc-photo.pbtxt')
    def prefs_for(coat):
        file='shared_prefs/pet_settings.xml'
        prefs=ET.fromstring(sh('run-as',PACKAGE,'cat',file))
        assert any(k.get('name')=='photo' and k.text for k in prefs),'Choose a photo first'
        def change(kind,key,value):
            node=prefs.find(f"./{kind}[@name='{key}']")
            if node is None:node=ET.SubElement(prefs,kind,{'name':key})
            node.set('value',str(value))
        change('int','coat',coat);change('int','companion_coat',{8:10,0:8,10:0}[coat])
        for key,value in [('companion','true'),('overlay','false'),('pets_enabled','true')]:change('boolean',key,value)
        for key,value in [('0.x',.22),('0.y',.60),('1.x',.78),('1.y',.42)]:change('float','position.home.'+key,value)
        adb('shell','-T','run-as',PACKAGE,'tee',file,stdin=ET.tostring(prefs,encoding='utf-8'))
        assert f'name="coat" value="{coat}"' in sh('run-as',PACKAGE,'cat',file)
    for i,coat in enumerate(args.coats):
        name=f'{i+1:02}-c{coat}'
        sh('am','force-stop',PACKAGE)
        assert not adb('shell','pidof',PACKAGE,check=False).strip()
        prefs_for(coat)
        sh('am','start','-W','-n',PACKAGE+'/.MainActivity')
        pid=sh('pidof',PACKAGE).strip()
        tap({'내 사진으로 고양이 배경 적용'})
        tap({'Set wallpaper','배경화면 설정'})
        if 'CatWallpaperService' not in sh('dumpsys','wallpaper').split('mWallpaperComponent=',1)[-1].split('\n',1)[0]:
            tap({'Home screen','홈 화면'})
        sh('am','start','-W','-n',PACKAGE+'/.MainActivity')
        for scroll in range(6):
            play=matches({'홈에서 손가락 놀이'})
            if play:break
            sh('input','swipe',850,2060,850,710,320)
        assert play,'Missing Home finger-play command'
        x,y,u,v=map(int,re.findall(r'\d+',play));sh('input','tap',(x+u)//2,(y+v)//2)
        live=wait(first_actor,'No visible Home cat')
        assert int(live[1])==coat and int(live[3])==0 and sh('pidof',PACKAGE).strip()==pid,(name,live.groups(),pid,sh('pidof',PACKAGE))
        time.sleep(.55)  # The launcher must finish its input-window transition.
        live=wait(first_actor,'No touchable scene after returning to Home')
        density=int(re.findall(r'\d+',sh('wm','density'))[-1])/160
        layers=sh('dumpsys','SurfaceFlinger','--list')
        parent=re.findall(r'\{'+re.escape(PACKAGE)+r'\.CatWallpaperService#(\d+) parentId=',layers)
        layer=next((n for n,i in re.findall(r'\{(Wallpaper BBQ wrapper#\d+) parentId=(\d+)',layers) if i in parent),None)
        assert layer,'Wallpaper buffer layer not found'
        presents=set()
        def collect():
            for line in sh('dumpsys SurfaceFlinger --latency "'+layer+'"').splitlines():
                parts=line.split()
                if len(parts)==3 and parts[1].isdigit() and 0<int(parts[1])<2**62:presents.add(tuple(map(int,parts)))
        collect()
        remote='/data/misc/perfetto-traces/yc-first-wallpaper'
        trace_pid=sh('perfetto','--background-wait','--txt','-c','/data/misc/perfetto-configs/yc-photo.pbtxt','-o',remote).strip()
        assert trace_pid.isdigit()
        try:
            width,height=map(int,re.findall(r'(\d+)x(\d+)',sh('wm','size'))[-1])
            x=round(float(live[5])*density);y=round(float(live[6])*density)
            end_x=round(width*(.87 if x<width*.5 else .13))
            sh('input','swipe',x,y,end_x,max(100,y-round(height*.07)),320)
            states=[]
            def recovered():
                collect()
                a=first_actor()
                if a is None:return False
                state=[a[2],a[4],int(a[3])]
                if not states or state!=states[-1]:states.append(state)
                return int(a[3])>0 and a[4]!='finger'
            try:wait(recovered,'Finger pursuit did not finish '+name,32)
            finally:(args.out/(name+'-states.json')).write_text(json.dumps(states))
            assert any(s[1]=='finger' for s in states),states
        finally:
            adb('shell','kill','-TERM',trace_pid,check=False)
            wait(lambda:trace_pid not in adb('shell','pidof','perfetto',check=False).split(),'Trace did not stop')
            adb('pull',remote,args.out/(name+'.perfetto-trace'));sh('rm','-f',remote)
        record=dict(coat=coat,pid=pid,presentations=sorted(presents),layer=layer,force_stopped=True,states=states,default_wallpaper_mode=True)
        (args.out/(name+'.json')).write_text(json.dumps(record,indent=2))
        print(name,states,flush=True)
if __name__=='__main__':main()
