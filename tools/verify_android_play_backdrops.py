"""Exercise the shipped background picker and wallpaper on the disposable Android AVD."""
import argparse
import json
import io
import numpy as np
from PIL import Image
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

PACKAGE='com.valuewander.yeopjaricat'
SCENES={'sunroom':'햇살 마루','garden':'꽃담 정원','rooftop':'달빛 옥상'}


def main():
    p=argparse.ArgumentParser()
    p.add_argument('--adb',required=True)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args()
    args.out.mkdir(parents=True,exist_ok=True)
    def adb(*cmd):
        return subprocess.check_output([args.adb,'-s','emulator-5580',*cmd],encoding='utf-8',errors='replace',timeout=60)
    def shell(*cmd):return adb('shell',*cmd)
    assert 'YeopjariCat_API35' in adb('emu','avd','name')
    def tree():
        result=shell('uiautomator','dump','--compressed','/sdcard/preset-review.xml')
        if 'dumped' not in result:return []
        return list(ET.fromstring(shell('cat','/sdcard/preset-review.xml')).iter('node'))
    def control(*names):
        return next((n for n in tree() if n.get('text') in names or n.get('content-desc') in names),None)
    def until(fn,desc):
        limit=time.monotonic()+30
        while time.monotonic()<limit:
            value=fn()
            if value is not None and value is not False:return value
            time.sleep(.2)
        raise AssertionError(desc)
    def tap(*names):
        n=until(lambda:control(*names),str(names));x,y,u,v=map(int,re.findall(r'\d+',n.get('bounds')))
        w,h=map(int,re.findall(r'(\d+)x(\d+)',shell('wm','size'))[-1])
        if n.get('package')==PACKAGE and (y+v)/2>h*.84:
            shell('input','swipe',str(w//2),str(round(h*.73)),str(w//2),str(round(h*.48)),'340')
            n=until(lambda:control(*names),str(names))
            x,y,u,v=map(int,re.findall(r'\d+',n.get('bounds')))
        shell('input','tap',str((x+u)//2),str((y+v)//2))
    def open_app():shell('am','start','-W','-n',PACKAGE+'/.MainActivity')
    def shot(name):
        with (args.out/(name+'.png')).open('wb') as file:
            subprocess.run([args.adb,'-s','emulator-5580','exec-out','screencap','-p'],stdout=file,check=True)
    def selected(label):
        n=control(label)
        return n is not None and n.get('checked')=='true'
    def wallpaper():
        return shell('dumpsys','activity','service',PACKAGE+'/.CatWallpaperService')
    def visible():
        return 'preview=false visible=true ready=true' in wallpaper()
    art=Path(__file__).resolve().parents[1]/'mobile/android/app/src/main/res/drawable-nodpi'
    def capture(name):
        until(visible,'visible live wallpaper')
        launcher=shell('cmd','package','resolve-activity','--brief','-a','android.intent.action.MAIN','-c','android.intent.category.HOME').strip().splitlines()[-1].split('/')[0]
        until(lambda:any(n.get('package')==launcher for n in tree()),'launcher still transitioning')
        expected=name.split('-')[-1]
        prefs=ET.fromstring(shell('run-as',PACKAGE,'cat','shared_prefs/pet_settings.xml'))
        level=prefs.find("./int[@name='home_floor_percent']")
        lane=max(.30,min(.88,int(level.get('value'))/100)) if level is not None else .58
        # Launcher and wallpaper are separate surfaces. Android may report a
        # visible engine while its first home transition is still a black frame.
        def visible_art():
            png=subprocess.check_output([args.adb,'-s','emulator-5580','exec-out','screencap','-p'],timeout=20)
            with Image.open(io.BytesIO(png)) as screen:
                w,h=screen.size
                sample=np.asarray(screen.convert('RGB').resize((32,60)),dtype=float)
            errors={}
            for design,ground,focus in [('sunroom',.64,.51),('garden',.64,.47),('rooftop',.67,.62)]:
                portrait=w<h*.74
                if portrait:
                    ground={'sunroom':.62,'garden':.63,'rooftop':.65}[design]
                    focus=.5
                with Image.open(art/(('portrait_' if portrait else 'scene_')+design+'.webp')) as im:
                    scale=max(w/im.width,h*lane/(im.height*ground),h*(1-lane)/(im.height*(1-ground)))
                    x=max(w-im.width*scale,min(0,w/2-im.width*scale*focus))
                    y=h*lane-im.height*ground*scale
                    ref=im.convert('RGB').transform((32,60),Image.Transform.EXTENT,
                        (-x/scale,-y/scale,(w-x)/scale,(h-y)/scale),Image.Resampling.BILINEAR)
                    errors[design]=round(float(np.median(np.abs(sample-np.array(ref)).mean(axis=2))),2)
            if min(errors,key=errors.get)!=expected or errors[expected]>40:return None
            (args.out/(name+'.png')).write_bytes(png)
            return errors
        scores=until(visible_art,'Wallpaper pixels did not match '+expected)
        dump=wallpaper();assert 'error=none' in dump and 'cat_painted=true' in dump,dump
        return dict(view=name,visible=True,median_rgb_errors=scores,viewport=re.findall(r'viewport=([0-9.x]+)',dump))
    original=shell('wm','size')
    override=re.search(r'Override size: (\d+x\d+)',original)
    results=[]
    try:
        open_app()
        for key,label in SCENES.items():
            tap(label)
            until(lambda:selected(label),label+' not selected')
            if key=='sunroom' and control('홈 화면에 적용') is not None:
                tap('홈 화면에 적용')
                tap('Set wallpaper','배경화면 설정')
                modes=control('Home screen','홈 화면')
                if modes is not None:tap('Home screen','홈 화면')
            shell('input','keyevent','KEYCODE_HOME')
            results.append(capture('home-'+key))
            open_app()
        shell('wm','size','1768x1840')
        time.sleep(2)
        open_app()
        until(lambda:control('놀이 배경'),'app did not return after width change')
        for key,label in SCENES.items():
            tap(label);until(lambda:selected(label),'square '+label)
            time.sleep(.7)
            shot('wide-preview-'+key)
        shell('input','keyevent','KEYCODE_HOME')
        results.append(capture('wide-home-rooftop'))
        shell('am','force-stop',PACKAGE)
        open_app()
        until(lambda:selected(SCENES['rooftop']),'Selection did not survive process death')
    finally:
        shell('wm','size',override[1] if override else 'reset')
    (args.out/'wallpaper.json').write_text(json.dumps(dict(views=results,persisted_after_restart=True),indent=2))
    print('WALLPAPER_BACKDROPS_PASSED',flush=True)


if __name__=='__main__':main()
