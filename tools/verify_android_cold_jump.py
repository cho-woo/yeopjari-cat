"""Measure the first launcher gesture after force-stopping the app, on the owned API 35 AVD."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time

PACKAGE = 'com.valuewander.yeopjaricat'
CONFIG = """
buffers { size_kb: 16384 fill_policy: RING_BUFFER }
data_sources { config { name: "android.surfaceflinger.frametimeline" } }
data_sources { config { name: "linux.ftrace" ftrace_config {
  atrace_apps: "com.valuewander.yeopjaricat"
  atrace_categories: "gfx" atrace_categories: "view" atrace_categories: "dalvik"
  atrace_categories: "wm" ftrace_events: "sched/sched_switch"
  compact_sched { enabled: true }
} } }
duration_ms: 90000
"""

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--adb', required=True)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--version', required=True)
    parser.add_argument('--rounds', type=int, default=5)
    parser.add_argument('--coats', nargs='+', type=int, default=[0, 8, 10], choices=[0, 8, 10])
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    prefix = [args.adb, '-s', 'emulator-5580']
    def adb(*parts, check=True):
        return subprocess.run(prefix+list(parts), check=check, capture_output=True, text=True,
                              encoding='utf-8', errors='replace', timeout=120)
    assert 'YeopjariCat_API35' in adb('emu', 'avd', 'name').stdout
    assert 'versionName='+args.version in adb('shell', 'dumpsys', 'package', PACKAGE).stdout
    config = args.out/'first-jump.pbtxt'
    config.write_text(CONFIG, encoding='utf-8')
    adb('push', str(config), '/data/misc/perfetto-configs/yeopjari-first.pbtxt')
    adb('shell', 'appops', 'set', PACKAGE, 'GET_USAGE_STATS', 'allow')
    adb('shell', 'appops', 'set', PACKAGE, 'SYSTEM_ALERT_WINDOW', 'allow')
    adb('shell', 'input', 'keyevent', 'KEYCODE_WAKEUP')
    adb('shell', 'wm', 'dismiss-keyguard')
    for trial in range(args.rounds):
        order = args.coats[trial%len(args.coats):]+args.coats[:trial%len(args.coats)]
        for coat in order:
            name = f'c{coat}-r{trial+1}'
            adb('shell', 'am', 'force-stop', PACKAGE)
            assert adb('shell', 'pidof', PACKAGE, check=False).returncode == 1, 'Previous process still alive'
            capture = '/data/misc/perfetto-traces/yeopjari-cold-'+name
            result = adb('shell','perfetto','--background-wait','--txt','-c','/data/misc/perfetto-configs/yeopjari-first.pbtxt','-o',capture)
            pid = re.fullmatch(r'\s*(\d+)\s*', result.stdout)
            assert pid, (result.stdout, result.stderr)
            try:
                run = adb('shell','am','instrument','-w','-e','mode','cold-jump','-e','coat',str(coat),
                          PACKAGE+'.test/'+PACKAGE+'.MotionSmoke')
                (args.out/(name+'.log')).write_text(run.stdout, encoding='utf-8')
                assert 'COLD_JUMP_PASSED' in run.stdout, run.stdout
            finally:
                adb('shell','kill','-TERM',pid[1],check=False)
                for _ in range(60):
                    if pid[1] not in adb('shell','pidof','perfetto',check=False).stdout.split():
                        break
                    time.sleep(.1)
                adb('pull',capture,str(args.out/(name+'.perfetto-trace')))
                assert (args.out/(name+'.perfetto-trace')).stat().st_size>65536, 'Incomplete system trace'
                adb('shell','rm','-f',capture)
            dest=args.out/(name+'.json')
            adb('pull','/sdcard/Android/data/'+PACKAGE+'/files/mobile-review/cold-jump.json',str(dest))
            data=json.loads(dest.read_text())
            assert data['version']==args.version and data['coat']==coat and data['ordered_landed_and_recovered']
            print(name, data['pid'], len(data['draws']), flush=True)

if __name__=='__main__':
    main()
