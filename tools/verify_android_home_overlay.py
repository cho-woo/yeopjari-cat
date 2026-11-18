"""Exercise the public Home-cat entry point on the project's disposable AVD."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as ET

PACKAGE = "com.valuewander.yeopjaricat"
START = "홈 화면에 고양이 켜기"
STOP = "고양이 종료하기"
CROSS = "다른 앱 위에 고양이 표시"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--adb", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--live-fixture", action="store_true",
                        help="Select the test APK's live wallpaper before taking the baseline")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    device = [args.adb, "-s", "emulator-5580"]

    def adb(*args):
        return subprocess.check_output(device + list(args), timeout=55, text=True,
                                       encoding="utf-8", errors="replace")

    assert "YeopjariCat_API35" in adb("emu", "avd", "name")
    def shell(*args):
        return adb("shell", *args)

    def layer():
        return shell("dumpsys", "activity", "service", PACKAGE + "/.CatOverlayService")

    def wait(predicate, message):
        end = time.monotonic() + 25
        while time.monotonic() < end:
            if predicate():
                return
            time.sleep(.2)
        raise AssertionError(message + "\n" + layer())

    def app():
        shell("am", "start", "-W", "-f", "0x10008000", "-n", PACKAGE + "/.MainActivity")

    def tap(label):
        last = []
        for _ in range(7):
            dumped = shell("uiautomator", "dump", "/sdcard/pet-home-ui.xml")
            if "dumped to:" not in dumped:
                continue
            nodes = list(ET.fromstring(shell("cat", "/sdcard/pet-home-ui.xml")).iter("node"))
            last = [n.get("text") for n in nodes if n.get("text")]
            for n in nodes:
                if n.get("text") == label and n.get("enabled") == "true":
                    left, top, right, bottom = map(int, re.findall(r"\d+", n.get("bounds")))
                    shell("input", "tap", str((left + right)//2), str((top + bottom)//2))
                    time.sleep(.45)
                    return
        raise AssertionError(f"Missing {label}: {last}")

    def identity():
        return [(i, w, c) for i, w, c in re.findall(
            r"User 0: id=(\d+): mWhich=(\d+):[^\n]*\n(?:(?! User 0:)[\s\S])*?mWallpaperComponent=ComponentInfo\{([^}]+)\}",
            shell("dumpsys", "wallpaper"))]

    def snapshot(name):
        with (args.out / (name + ".png")).open("wb") as out:
            subprocess.run(device + ["exec-out", "screencap", "-p"], check=True, stdout=out, timeout=20)

    def home():
        shell("input", "keyevent", "KEYCODE_HOME")

    if args.live_fixture:
        shell("am", "start", "-W", "-a", "android.service.wallpaper.CHANGE_LIVE_WALLPAPER",
              "--ecn", "android.service.wallpaper.extra.LIVE_WALLPAPER_COMPONENT",
              PACKAGE + ".test/" + PACKAGE + ".FixtureWallpaperService")
        tap("Set wallpaper")
        if not any("FixtureWallpaperService" in c for _, _, c in identity()):
            tap("Home screen")
        wait(lambda: any("FixtureWallpaperService" in c for _, _, c in identity()),
             "The independent live wallpaper was not selected")
    original = identity()
    assert original and all("CatWallpaperService" not in c for _, _, c in original)
    checks = []
    def passed(name):
        assert identity() == original, name + " changed the user's wallpaper"
        checks.append(name)
        print("PASS " + name, flush=True)

    shell("pm", "clear", PACKAGE)
    shell("wm", "dismiss-keyguard")
    shell("appops", "set", PACKAGE, "SYSTEM_ALERT_WINDOW", "default")
    shell("appops", "set", PACKAGE, "GET_USAGE_STATS", "default")
    shell("pm", "grant", PACKAGE, "android.permission.POST_NOTIFICATIONS")
    app()
    tap(START)
    tap("취소")
    assert "cat_layer_running=true" not in layer()
    passed("cancel overlay permission")
    shell("appops", "set", PACKAGE, "SYSTEM_ALERT_WINDOW", "allow")
    tap(START)
    tap("취소")
    assert "cat_layer_running=true" not in layer()
    passed("cancel usage access")
    shell("appops", "set", PACKAGE, "GET_USAGE_STATS", "allow")
    tap(START)
    wait(lambda: "displaying=true cross_apps=false" in layer(), "No Home-only cats")
    first = int(re.search(r"frames=(\d+)", layer())[1])
    wait(lambda: int(re.search(r"frames=(\d+)", layer())[1]) > first+5, "Home frames not advancing")
    snapshot("home")
    passed("start from public button and animate over original Home")
    shell("am", "start", "-W", "-a", "android.settings.SETTINGS")
    wait(lambda: "displaying=false" in layer(), "Visible over Settings")
    passed("hide over opaque app")
    home()
    wait(lambda: "displaying=true" in layer(), "No return Home")
    shell("am", "start", "-W", "-n", PACKAGE + ".test/" + PACKAGE + ".TranslucentReviewActivity")
    wait(lambda: "displaying=false" in layer(), "Visible over translucent Activity")
    passed("hide over translucent app")
    app()
    tap(CROSS)
    shell("am", "start", "-W", "-a", "android.settings.SETTINGS")
    wait(lambda: "displaying=true cross_apps=true" in layer(), "Cross-app opt-in failed")
    passed("explicit cross-app opt-in")
    app()
    tap(CROSS)
    shell("am", "start", "-W", "-a", "android.settings.SETTINGS")
    wait(lambda: "displaying=false cross_apps=false" in layer(), "Cross-app opt-out failed")
    home()
    wait(lambda: "displaying=true cross_apps=false" in layer(), "Home mode lost")
    passed("cross-app opt-out retains Home cats")
    shell("appops", "set", PACKAGE, "GET_USAGE_STATS", "ignore")
    wait(lambda: "displaying=false" in layer(), "Revoked Home detection did not hide cats")
    shell("appops", "set", PACKAGE, "GET_USAGE_STATS", "allow")
    wait(lambda: "displaying=true" in layer(), "Restored Home detection failed")
    passed("usage access revocation and regrant")
    app()
    tap(STOP)
    home()
    wait(lambda: "cat_layer_running=true" not in layer(), "Service remained after quitting")
    snapshot("quit")
    passed("quit leaves original Home without cats")
    app()
    tap(START)
    wait(lambda: "displaying=true cross_apps=false" in layer(), "Restart did not stay Home only")
    passed("restart in Home-only mode")
    density = int(re.findall(r"\d+", shell("wm", "density"))[-1]) / 160
    def cat_position():
        actor = re.search(r"actor=0 x=([\d.E-]+) y=([\d.E-]+)", layer())
        assert actor, "No cat window"
        return round(float(actor[1])*density), round((float(actor[2])-37)*density)
    x, y = cat_position()
    shell("input", "swipe", str(x), str(y), str(x), str(y-100), "450")
    wait(lambda: abs(cat_position()[1]-(y-100)) < 12, "Home cat drag failed")
    x, y = cat_position()
    shell("input", "swipe", str(x), str(y), str(x), str(y), "800")
    tap("종료하기")
    wait(lambda: "cat_layer_running=true" not in layer(), "Long-press quit failed")
    passed("drag and long-press quit")
    (args.out / "report.json").write_text(json.dumps({"passed":checks,"wallpapers":original}, indent=2))


if __name__ == "__main__":
    main()
