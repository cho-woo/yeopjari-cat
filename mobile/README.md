# Android preview — 옆자리 고양이

The default Home mode uses the Android live wallpaper surface. The customer selects one photo and confirms the wallpaper in the Android preview. The app does not request Usage Access, read foreground-app history, or request accessibility access. Public GitHub releases provide the in-app preview updates.

## Product behavior

- **내 사진으로 고양이 배경 적용** opens the system photo picker on compatible devices, with the document picker as a fallback. Only the chosen image is accessible. Select **홈 화면** in the wallpaper preview when the launcher offers a destination.
- The cat moves behind launcher icons. **고양이 종료하기** hides the cats and keeps the selected photo. **고양이 다시 켜기** resumes them. **배경 사진 고르기** changes the photo. **이전 배경으로 돌아가기** uses the available restore options when the customer wants to replace the entire wallpaper.
- **홈에서 손가락 놀이** arms the first cat and returns to Home. The wallpaper consumes the touch events forwarded by the launcher. Icons and launcher gestures take priority. The app preview and optional floating mode provide their own touch surfaces.
- **다른 앱 위에 고양이 표시** is separately opt-in and initially off. Only that switch can start the foreground service and request Android's overlay permission. An upgrade preserves the system wallpaper until the customer explicitly applies the new one.
- With a companion enabled, the cats can meet, take turns swatting, race,
  follow, sniff, share a nap, compete for a snack or react to a sneeze. These
  events also run on request through **두 마리 함께 놀기**.

To measure first gestures on the current wallpaper, select a photo once on the owned API 35 AVD, then run `tools/verify_android_photo_home.py --adb <adb> --out <dir> --coats 8 0 10 8`. Each sample terminates the process, reapplies the wallpaper through the public Android picker and dispatches a real swipe. `tools/analyze_android_photo_home.py --processor <trace_processor_shell> --dir <dir>` reports the wallpaper surface's actual display intervals for the run, jump, strike and recovery.

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

Android 8.0+ (min SDK 26), target/compile SDK 36. Both actors share the same
47-command catalog and 42 registered clips per coat. The exporter includes
the 22 Windows video-action atlases, the treat sequence, three complete
furniture movies, the registered ball pounce and 15 procedural Windows
gestures per coat. Color and alpha from packed scene movies become WebP atlas
pages. The native gesture adapter samples the Windows pose evaluator and GDI+
renderer on a fixed 96-pixel reference at 24 FPS. The checked-in gesture
pages allow an Android build without a second Windows build.

Runs carry ground speed into the planted turn. Ball impulses use forward paw
motion through the collision radius during the registered stroke.
Finger pounces use the extended paw from the coat's own source at takeoff.
Mobile perch play uses a visible shelf belonging to the app. Home gestures
begin on a cat so launcher controls outside the cat retain their normal input.

For cold first-jump regressions, install the instrumentation APK and run
`tools/verify_android_cold_jump.py --adb <adb> --out <dir> --version <version> --rounds 5`.
It force-stops the app before each first gesture on the launcher and saves a Perfetto trace
and draw markers for each coat. `tools/analyze_android_cold_jump.py --processor <trace_processor_shell> --dir <dir>`
matches the cat's submitted buffers to actual display frames, including takeoff and landing transitions.

## Build

1. Existing authoring Python environment requires Pillow, NumPy and OpenCV (`.venv`).
2. `tools/setup_android_dev.ps1` downloads checksum-verified Microsoft JDK17,
   Gradle8.13 and official Android command-line tools into ignored `build/`.
   Add `-WithEmulator` for the disposable test device image.
3. `tools/build_android.ps1 -Tests` repacks verified source atlases, builds the
   debug-signed preview and runs Android lint. Temporary ASCII drive mapping
   avoids native Android tools' Unicode-path limitations on Windows.
4. Output: `build/android-0.5.1-preview/YeopjariCat-Android-0.5.1-preview.apk`.

The preview uses a local Android debug signing key; it is not the future store
release key. Do not upload this test key as the production signing identity.
No APK is placed in the Windows release or its update feed.

## Verification

- Install the target and instrumentation APKs on the owned API 35 AVD.
  Run `tools/verify_android_actions.py --adb <adb> --out <dir> --mode parity`
  for frame sweeps, and repeat with `--mode parity-live` for normal-time
  screen recordings. The first exercises all commands in both roles, geometry,
  registered ball contact, handoffs, pairing and rapid finger reversals. The
  second uses the activity's own renderer and Android frame scheduling.
- Use `--mode pacing-home` to inject finger gestures through the launcher and
  record both cats completing the jump, strike, landing and reciprocal paw
  exchange. Use `--mode pacing` for the same sequences in the activity. Each
  coat pair produces an MP4 and a JSON report of draw intervals and observed
  atlas waits. The same runner can measure the previous public APK.

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
- [System photo picker](https://developer.android.com/training/data-storage/shared/photo-picker)
- [App installation permission](https://developer.android.com/reference/android/content/pm/PackageManager#canRequestPackageInstalls())
- [Installer user-action requirements](https://developer.android.com/reference/android/content/pm/PackageInstaller.SessionParams#setRequireUserAction(int))
- [Public releases API](https://docs.github.com/en/rest/releases/releases#list-releases)
