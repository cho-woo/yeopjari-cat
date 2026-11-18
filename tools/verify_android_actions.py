"""Run both scene clocks on the isolated AVD and retain device reports and real-time video."""
import argparse
import re
import subprocess
import time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
PACKAGE='com.valuewander.yeopjaricat'

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--adb',required=True)
    parser.add_argument('--out',required=True,type=Path)
    parser.add_argument('--mode',choices=('parity','parity-live'),required=True)
    parser.add_argument('--coats',type=int,nargs='+',default=[0,8,10])
    a=parser.parse_args()
    a.out.mkdir(parents=True,exist_ok=True)
    prefix=[a.adb,'-s','emulator-5580']
    def adb(*parts,timeout=100):
        return subprocess.check_output(prefix+list(parts),encoding='utf-8',errors='replace',timeout=timeout)
    assert 'YeopjariCat_API35' in adb('emu','avd','name')
    for coat in a.coats:
        name=f'{a.mode}-{coat}'
        video=None
        if a.mode=='parity-live':
            video=subprocess.Popen(prefix+['shell','screenrecord','--size','540x1200','--bit-rate','2400000','--time-limit','180','/sdcard/'+name+'.mp4'])
        try:
            output=adb('shell','am','instrument','-w','-e','mode',a.mode,'-e','coat',str(coat),
                PACKAGE+'.test/'+PACKAGE+'.MotionSmoke',timeout=500)
            (a.out/(name+'.log')).write_text(output,encoding='utf-8')
            if 'PARITY_PASSED' not in output:
                raise RuntimeError(output)
            adb('pull','/sdcard/Android/data/'+PACKAGE+'/files/mobile-review/'+name+'.json',str(a.out/(name+'.json')))
            print(output.strip(),flush=True)
        finally:
            if video is not None:
                pid=adb('shell','pidof','screenrecord').strip()
                if re.fullmatch(r'\d+',pid):adb('shell','kill','-2',pid)
                video.wait(timeout=20)
                adb('pull','/sdcard/'+name+'.mp4',str(a.out/(name+'.mp4')))

if __name__=='__main__':
    main()
