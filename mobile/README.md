# Android preview — 옆자리 고양이

User scope confirmed 2026-11-17: **home screen only by default**. Showing the pet
above other apps is a separate, explicit opt-in setting. No
accessibility permission, screen capture, automated interaction with other apps,
or boot receiver is used. Network access is used only for public GitHub update
metadata and user-requested APK downloads. Optional Home touch controls compare
the latest foreground package to the selected launcher locally, with explicit
Usage Access permission; no usage history or UI contents are stored/transmitted.

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
  after boot/process death until the user opens the app again. The overlay hides
  on screen-off/lock and while the app launches installation permission/confirmation.
- The small pet window receives touches inside its rectangle. Other portions
  of the display remain outside that window. The app does not observe or inject
  interaction with other apps. Launcher touch forwarding is device-dependent.
- **홈 화면 터치 조작** provides reliable long press and 2D drag through small
  native pet windows. It requires overlay permission (for the touch windows) and
  Usage Access (to hide them outside the selected Home app, including transparent
  foreground apps). This does **not** turn on the separate cross-app switch.
  Permission explanations can be cancelled without starting the service. Loss of
  usage access fails closed. Basic wallpaper remains available without these
  optional permissions; launcher-forwarded long press/drag alone is not reliable.
- Hold either cat for 550ms to open **종료하기 / 취소**. Quit disables both cats,
  wallpaper animation and touch service. App settings can turn cats back on.
  Dragged positions are stored as normalized coordinates, shared between the
  Home wallpaper and touch/cross-app windows, and retained across viewport changes.
- Mobile automatic rest is 0.4–1.0 seconds **after full recovery** (startup
  0.6–1.2 seconds). Stroll, grooming, kneading and paw play remain active; automatic
  sleep/settling is removed. Source frame clocks and authored recoveries are not
  shortened. Holding/dragging pauses the owned actor, with no timeline rewind.

## Updates inside the app (0.3+)

- Opening the app, or showing the enabled Home wallpaper, checks the public
  Android channel at most once per six hours. There is no polling timer, boot
  receiver, account, API key or user-data upload. Manual **업데이트 확인** can
  retry immediately. Notification permission is respected; when allowed, a new
  version produces one **고양이: 업데이트해줘!** notification per version.
- The app update card and new-version prompt download an APK only after the
  user chooses to update. Progress is shown in the app. Android may require a
  one-time **이 출처 허용** grant for this app, followed by its native Update
  confirmation. Declining permission or cancelling leaves the installed app and
  staged download available for retry. This is not a promise of silent installs.
- Update discovery uses the public releases list and **only**
  `vX.Y.Z-android-preview` tags and the canonical APK filename. It does not use
  the Windows `/releases/latest` endpoint. Drafts, desktop tags, foreign URLs,
  missing digests, old/equal versions and oversized assets are rejected.
- Transfers use HTTPS on the GitHub/CDN allowlist. Expected length and SHA256,
  application ID, newer Android versionCode, versionName, minimum SDK and the
  installed APK signer are checked before opening the system installer. APKs
  are staged in private cache, outside Android backup; the read-only provider
  exposes one URI through a temporary read grant, never arbitrary files.
- Settings, coats, companion selection and dragged positions survive an
  in-place update with the existing preview signing key. User-selected touch
  windows resume when the updated app is opened. No update can retrofit this
  feature into 0.1/0.2: users install 0.3 once from the release link first.
- This updater is for the website-distributed preview. A future Play-distributed
  build must use the store's update route and its applicable policy instead.

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
4. Output: `build/android-0.3.0-preview/YeopjariCat-Android-0.3.0-preview.apk`.

The preview uses a local Android debug signing key; it is not the future store
release key. Do not upload this test key as the production signing identity.
No APK is placed in the Windows release or its update feed.

## Verification

- `mobile/tests/MotionClockSpec.java` checks full timeline/recovery continuity.
- `mobile/tests/PointerGestureSpec.java` checks tap/drag/long-press ownership,
  hold thresholds, release endpoints and cancellation across 367 cases.
- `mobile/tests/UpdatePolicySpec.java` checks the separate Android channel,
  numeric version ordering, URL/size constraints and exact transfer integrity.
- `UpdateReview` exercises real public HTTPS metadata/APK transfer, corrupt data,
  wrong signer/package/version rejection, ready-state restoration and offline
  retry. `tools/verify_android_update_install.py` uses the real Android installer
  to decline permission, cancel installation, retry and perform an in-place
  upgrade while retaining both cats and saved heights. Its higher-version fixture
  has byte-identical runtime DEX; the fixture transport exists only in the test
  APK, not in the downloadable app.
- `TouchReview` sends real touchscreen events to the app View for all three
  coats and both actor roles, checks drag/menu/quit/resume, then samples 35 seconds
  of normal elapsed-time automatic play for each pair.
- `MotionSmoke` runs all six complete actions in the normal elapsed-time app
  View for each coat, companion paw recovery, rapid input ownership, default
  permission/service absence and explicit overlay start/stop. It stores the
  report and actual device screenshots under external files `mobile-review`.
- `tools/verify_android_wallpaper.py` checks Home/app switching, screen-off and
  three fold/rotation viewport sizes through the actual system wallpaper.
  It does not start Instrumentation, which restarts the target process and can
  invalidate an already selected wallpaper during testing. First frames are
  posted for surface redraw callbacks, and the launcher wallpaper camera offset
  is cancelled independently of each pet's physical root.
- `tools/verify_android_touch.py` operates the actual system picker, native Home
  windows and native exit dialog. It checks permission cancellation, both actors'
  drag, saved heights, transparent/opaque app hiding, cross-app opt-in independence,
  usage-access revocation, screen-off, viewport changes, quit and resume. Its
  translucent test Activity exists only in the separate test APK.
- Actual Galaxy Z Fold7 (user's phone), One UI launchers, battery use and Google
  Play service approval remain release gates. Emulator viewport simulations are
  not described as physical foldable-device verification.

## Official platform references

- [Wallpaper engine lifecycle and forwarded touch](https://developer.android.com/reference/android/service/wallpaper/WallpaperService.Engine)
- [System live wallpaper picker](https://developer.android.com/reference/android/app/WallpaperManager#ACTION_CHANGE_LIVE_WALLPAPER)
- [Optional application overlay](https://developer.android.com/reference/android/view/WindowManager.LayoutParams#TYPE_APPLICATION_OVERLAY)
- [Foreground service types](https://developer.android.com/develop/background-work/services/fgs/service-types)
- [Play foreground-service declarations and demonstration](https://support.google.com/googleplay/android-developer/answer/13392821?hl=en)

Technical ability to show an overlay is not a guarantee of Google Play approval.

## Interaction with other apps / launcher UI

Drawing a cat over an icon does not reveal that icon's bounds or activate it.
Wallpaper input is forwarded by the launcher; overlay permission grants neither
another app's UI tree nor global touch observation. Our own scene objects can
provide accurate contact geometry. General launcher icons, widgets and other-app
buttons cannot currently serve as automatic climbable platforms in this preview.

Accessibility services can obtain exposed UI nodes and perform supported actions,
with separate user approval. Custom-drawn and protected UI can be unavailable.
MediaProjection is a different user-approved screen-capture route and would still
need image interpretation; this app does not request it. Actual button automation
has additional policy constraints and is not added by this update. A pet must not
misrepresent itself as an accessibility tool. Any future narrow feature would
need its own clear purpose, disclosure, opt-in and Play review.

- [Accessibility service capabilities](https://developer.android.com/guide/topics/ui/accessibility/views/service)
- [Play AccessibilityService policy](https://support.google.com/googleplay/android-developer/answer/10964491?hl=en)
- [Usage Access is user-granted in Settings](https://developer.android.com/reference/android/Manifest.permission#PACKAGE_USAGE_STATS)
- [App installation permission](https://developer.android.com/reference/android/content/pm/PackageManager#canRequestPackageInstalls())
- [Installer user-action requirements](https://developer.android.com/reference/android/content/pm/PackageInstaller.SessionParams#setRequireUserAction(int))
- [Public releases API](https://docs.github.com/en/rest/releases/releases#list-releases)
