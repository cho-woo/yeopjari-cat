# Full motion audit — in progress

Requested scope: Cream (0), Tuxedo (8), Snow (10), both primary and companion;
all behavior states, authored actions, pointer/keyboard control, toys, furniture,
two-cat events and their complete transitions. No ordinary profile is loaded by
the audit fixture. Existing Dropshot originals and canonical identity remain intact.

## Baseline

- Shipping version 2.0.40, source `15b442a653974399242dcd0a8cc93bf9601250f4`.
- 105 synthetic-time native renderer/physics cases passed, with zero physical
  discontinuities. These are not desktop recordings. Largest consecutive painted
  centroid step: 12.6613 pixels, Tuxedo brake source frames 7→8; source review open.
- Real-time Tuxedo primary + companion: 32 state probes completed. Early pilot
  recordings were affected by moving/covering the test window and are private
  intermediate evidence, unsuitable for publication. Pilot timer-gap sampling
  outside the message pump could combine several timers; do not treat those
  reported multi-second intervals as proven application stalls.

## Confirmed defect and change

`movie::Loader::Reset` waited for a `std::async` future on the UI thread. Six
real Windows Media Foundation cancellation probes took 286.718–376.912 ms.
Replaced this with one shared decoder queue. Cancellation releases the request
immediately, queued abandoned work is skipped, and simultaneous requests for the
same resource share a pending decode and immutable movie. Completed unused movies
are held weakly. The decoder queue is joined only during process shutdown.

The same six cancellation probes now take 0–0.0073 ms locally. Portable queue
regression covers cancellation while the decoder is blocked, pending and completed
resource reuse, skipped abandoned work, and weak release. Full action/regression
validation and exact release package remain pending.

## Evidence and remaining work

Two additional issues were reproduced during real-time review:

- The requested 16 ms WM_TIMER cadence was frequently delivered at 31 ms.
  Tuxedo ball-play trace: 2,486 active frames, mean interval 25.7381 ms
  (about 38.9 Hz), mean Tick work 7.3922 ms. A high-resolution waitable timer
  now posts at most one pending active-animation frame. Slow frames complete
  before another is queued, preserving input responsiveness; rest/hide retains
  the inexpensive timer and unsupported systems retain the old timer fallback.
  Real-time after measurements remain pending.
- Companion climbing retained its approach owner. The approach timeout could
  overwrite the climb end with a 45–100 second quiet interval while leaving
  the final climb frame active. Its physical landing also entered the separate
  stationary-jump recovery, freezing the already-authored climb recovery clock.
  The approach owner is now released at grip and the climb keeps its own landing
  clock. Six coat/pause regressions pass; desktop re-verification is pending.

Local intermediate evidence is under `build/full-motion-audit`. The opt-in
`--live-motion-audit` fixture uses the real WM_TIMER, Tick, Box2D, renderer and
layered windows, plus a bounded background encoder. Input targets in scripted
controller cases are fixture samples, not OS mouse/key injection. Capture
timestamps preserve holds instead of shortening stalls. Covered capture regions
must be reported and reviewed; they cannot count as a visually verified action.

Remaining: complete all coats/roles/repetitions, review normal/slow playback and
source identity/contact registration, fix any further defects, run regressions,
verify and deploy one exact package, update the official website and verify its
download hash. No release has been published for this audit yet.
