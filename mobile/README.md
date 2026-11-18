# Android preview — 옆자리 고양이

User scope confirmed 2026-11-17: **home screen only by default**. Showing the pet
above other apps is a separate, explicit opt-in setting. No
accessibility permission, screen capture, automated interaction with other apps,
or boot receiver is used. Network access is used only for public GitHub update
metadata and user-requested APK downloads. Optional Home touch controls compare
the latest foreground package to the selected launcher locally, with explicit
Usage Access permission; no usage history or UI contents are stored/transmitted.

## Product behavior

- **홈 화면에 고양이 켜기** starts a user-controlled foreground service and
  opens Home. The cat lives in a small transparent window over the existing
  launcher. Starting, quitting, dragging, and changing display modes do not
  write the Home or Lock wallpaper or copy its contents.
- Android's overlay permission allows drawing the cat window. Usage Access
  identifies the resumed Home activity on-device. Cancel either setup prompt
  to leave the service off. While the screen is unlocked, the Home-only mode
  checks foreground activity changes, removes the windows when another app
  becomes active and resumes them when Home returns. This is a sampled
  observation, so removal can briefly follow a foreground transition.
- **다른 앱 위에 고양이 표시** is a separate switch, initially off. Switching
  it back off while Home mode is running returns the cat to Home-only display.
  Locked screens, screen-off and the app's settings/installer hide the windows.
- Hold either cat for 550 ms and choose **종료하기**, use **고양이 종료하기**
  in settings, or stop it from the notification. This removes both cats and
  stops the service. Starting again defaults to Home only. Dragged positions
  are retained across sessions and viewport changes.
- The former live wallpaper service remains installed to preserve backgrounds
  on upgrades. The previous backdrop stays selected until the user changes it.
  **이전 배경으로 돌아가기** appears when the old background or a retained backup
  is present. Only an explicit restore can replace the background.
- Mobile automatic rest is 0.4–1.0 seconds **after full recovery** (startup
  0.6–1.2 seconds). Stroll, grooming, kneading and paw play remain active; automatic
  sleep/settling is removed. Source frame clocks and authored recoveries are not
  shortened. Holding/dragging pauses the owned actor, with no timeline rewind.

## Previous wallpaper edition (0.3.1)

- **이전 배경으로 돌아가기** sits directly below the Apply button. A retained
  photo is restored to Home through `WallpaperManager.setStream(FLAG_SYSTEM)`;
  a visible previous live wallpaper component opens its native confirmation
  screen. Both paths stop the cats after a confirmed wallpaper change.
- Android 14+ does not let ordinary apps read the system wallpaper photo. Before
  replacing a static wallpaper, the user can select its original photo once.
  The selected bytes are copied into app-private, backup-excluded storage, with
  a 50MiB limit and image validation. No broad photo/file permission is requested.
  Live component visibility uses a narrow wallpaper-service package query.
- The previous snapshot is committed only after successful application. Cancelling
  the picker preserves the current wallpaper and the last confirmed backup.
  Reapplying an already selected cat wallpaper does not overwrite that backup.
  Open return/backup menus survive Activity recreation, including Android's
  wallpaper-color resource refresh, without repeating a selected restore action.
- Older installations did not save the previous image: the app cannot recover
  an overwritten photo retroactively. Return offers explicit photo selection,
  the phone's default wallpaper, and the system wallpaper chooser. The default
  is labelled as such and is never presented as the user's original photograph.
  A photo previously selected inside the cat app can also be restored.
- Direct photo/default returns preserve a separately selected Lock wallpaper.
  Live-wallpaper changes use the phone's native picker, whose Home/Lock destination
  remains under Android/OEM control. Exact original
  photo crop, launcher effects and OEM theme settings are not captured. A previous
  live wallpaper still needs to be installed. Removing app data/uninstalling also
  removes its private retained photo. **종료하기** stops cats; wallpaper return is
  a separate explicit action.

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
4. Output: `build/android-0.3.2-preview/YeopjariCat-Android-0.3.2-preview.apk`.

The preview uses a local Android debug signing key; it is not the future store
release key. Do not upload this test key as the production signing identity.
No APK is placed in the Windows release or its update feed.

## Verification

- Starting in 0.3.2, `tools/verify_android_home_overlay.py` checks Home-only
  operation over an independently selected wallpaper, permission cancellation,
  switching to opaque/translucent apps, explicit cross-app opt-in/out and
  quitting the service. It compares Home/Lock IDs and components at each step.
  Earlier wallpaper application and installer walkthroughs below refer to their
  respective versioned releases.

- `tools/verify_android_wallpaper_return.py` operates the actual system live
  wallpaper picker and WallpaperManager on the owned Android 15 AVD. It checks
  cancelled application, retained-photo restore/reapplication, an independent
  live-wallpaper round trip, default return, legacy photo selection, persistence,
  cat shutdown and preservation of a separate Lock wallpaper. Synthetic photo
  selection is supplied by the separate test APK; no user photo is used.
  The runner's `wallpaper-return-auto` mode performs the completed restore paths with
  one UiAutomation owner clicking the actual app/native accessibility controls.
  It avoids the review AVD's stalled gesture monitors, reports any unrelated
  system ANR dismissed, and never dismisses a cat-app ANR. Cancellation remains
  in the external review because the AVD's global Back monitor can stall. Neither review path
  installs test control endpoints in the downloadable app.
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
