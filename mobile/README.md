# Android preview — 옆자리 고양이

User scope confirmed 2026-11-17: **home screen only by default**. Showing the pet
above other apps is a separate, explicit opt-in setting. No home-launcher
classification, foreground-app surveillance, accessibility permission, screen
capture, or boot receiver is used.

## Product behavior

- `WallpaperService` is the default. The pet is behind launcher icons, rather
  than an overlay. Opening another opaque app hides the wallpaper naturally.
  The renderer stops when the system reports the wallpaper hidden. Cats are
  suppressed while the keyguard is locked, even if the user chooses the same
  wallpaper for both Home and Lock screens in the system picker.
- The system wallpaper picker is opened by **홈 화면에 데려오기**. The user
  selects Home there. This replaces the previous wallpaper; a private photo
  can be chosen through the Android document picker. Existing wallpaper images
  are not read automatically.
- Optional `TYPE_APPLICATION_OVERLAY` windows require an explicit switch,
  Android's special permission, and a user-started `specialUse` foreground
  service. Notification and app settings both provide Stop. There is no restart
  after boot/process death. The overlay hides on screen-off/lock.
- The small pet window receives touches inside its rectangle. Other portions
  of the display remain outside that window. The app does not observe or inject
  interaction with other apps. Launcher touch forwarding is device-dependent.

## Preview scope

Android 8.0+ (min SDK 26), target/compile SDK 36. Cream, Tuxedo, Snow, optional
companion, private photo background, size, gentle stroll, grooming, kneading,
complete close-paw play, short authored sleep/wake, and settling are included.
The existing immutable Dropshot poses/timing/registration are repacked losslessly
into bounded WebP pages. No new generated footage or purchase is used. Full
Windows feature parity, ball physics, furniture, finger pursuit, PC save sync,
and mobile store publication are **not** claimed by this preview.

## Build

1. Existing authoring Python environment requires Pillow (`.venv`).
2. `tools/setup_android_dev.ps1` downloads checksum-verified Microsoft JDK17,
   Gradle8.13 and official Android command-line tools into ignored `build/`.
   Add `-WithEmulator` for the disposable test device image.
3. `tools/build_android.ps1 -Tests` repacks verified source atlases, builds the
   debug-signed preview and runs Android lint. Temporary ASCII drive mapping
   avoids native Android tools' Unicode-path limitations on Windows.
4. Output: `build/android-preview/YeopjariCat-Android-0.1.0-preview.apk`.

The preview uses a local Android debug signing key; it is not the future store
release key. Do not upload this test key as the production signing identity.
No APK is placed in the Windows release or its update feed.

## Verification

- `mobile/tests/MotionClockSpec.java` checks full timeline/recovery continuity.
- `MotionSmoke` runs all six complete actions in the normal elapsed-time app
  View for each coat, companion paw recovery, rapid input ownership, default
  permission/service absence and explicit overlay start/stop. It stores the
  report and actual device screenshots under external files `mobile-review`.
- Wallpaper home/app switching, lock and screen-off need actual system lifecycle
  checks in addition to the SceneView test. Actual OEM phones and launchers,
  battery use and Google Play service approval remain release gates.

## Official platform references

- [Wallpaper engine lifecycle and forwarded touch](https://developer.android.com/reference/android/service/wallpaper/WallpaperService.Engine)
- [System live wallpaper picker](https://developer.android.com/reference/android/app/WallpaperManager#ACTION_CHANGE_LIVE_WALLPAPER)
- [Optional application overlay](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#TYPE_APPLICATION_OVERLAY)
- [Foreground service types](https://developer.android.com/develop/background-work/services/fgs/service-types)
- [Play foreground-service declarations and demonstration](https://support.google.com/googleplay/android-developer/answer/13392821?hl=en)

Technical ability to show an overlay is not a guarantee of Google Play approval.
