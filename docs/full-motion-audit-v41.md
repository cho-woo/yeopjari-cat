# Full motion audit — implementation and verification protocol

Requested scope: Cream (0), Tuxedo (8), Snow (10), both primary and companion;
all behavior states, authored actions, pointer/keyboard control, toys, furniture,
two-cat events and their complete transitions. The fixture uses isolated disposable
profiles. Existing Dropshot originals and canonical identity remain intact.

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
resource reuse, skipped abandoned work, and weak release.

## Evidence and remaining work

Two additional issues were reproduced during real-time review:

- The requested 16 ms WM_TIMER cadence was frequently delivered at 31 ms.
  Tuxedo ball-play trace: 2,486 active frames, mean interval 25.7381 ms
  (about 38.9 Hz), mean Tick work 7.3922 ms. A high-resolution waitable timer
  now posts at most one pending active-animation frame. Slow frames complete
  before another is queued, preserving input responsiveness; rest/hide retains
  the inexpensive timer and unsupported systems retain the old timer fallback.
  Same wall-command / Tuxedo trace window (10–25 seconds) measures 25.015 ms
  before and 16.611 ms after (about 40.0→60.2 Hz). Native timer checks delivered
  49 frames in 800 ms and kept one queued frame after a 180 ms UI stall.
- Companion climbing retained its approach owner. The approach timeout could
  overwrite the climb end with a 45–100 second quiet interval while leaving
  the final climb frame active. Its physical landing also entered the separate
  stationary-jump recovery, freezing the already-authored climb recovery clock.
  The approach owner is now released at grip and the climb keeps its own landing
  clock. Six coat/pause regressions pass. In the real-time Tuxedo comparison,
  the old companion remains on Climb at 25 seconds; the corrected one is Idle.
- Random fly hunts and furniture could bypass the neutral rest deadline. A due
  random event now waits at that boundary without resetting its cooldown; manual
  commands remain responsive. Three coat regressions cover the retained hunt and
  manual furniture path. Furniture tests cover an overdue event held across rest.
- One Tick now uses one timestamp for physical updates, source frame selection,
  rendering and frame-timer selection. The audit observes the exact painted pose
  without re-evaluating mutable transition state with an earlier timestamp.
- The real-time social group exposed a further host integration bug: a manual
  play request was accepted during a fresh quiet pose, then cancelled on the
  next Tick by the random-proposal rest gate. An accepted interaction now retains
  its safe neutral approach; other commands, keyboard control and care owners
  still preempt it. The native paired-play review now uses the actual host
  context on every frame instead of a simplified busy flag.
  Its previous fixed 12-second event budget also omitted distant approaches;
  the budget now includes travel and the planted entry. Three coat regressions
  verify a distant friend is reached and both paw bouts complete.

Local intermediate evidence is under `build/full-motion-audit`. The opt-in
`--live-motion-audit` fixture uses the real WM_TIMER, Tick, Box2D, renderer and
layered windows, plus a bounded background encoder. Input targets in scripted
controller cases are fixture samples, not OS mouse/key injection. Capture
timestamps preserve holds instead of shortening stalls. Covered capture regions
must be reported and reviewed; they cannot count as a visually verified action.

The optional `--framebuffer` mode runs off screen and copies the actual shipping
windows' painted BGRA surfaces in their real z-order. It uses real wall time and
the same timer/physics/renderer, not an accelerated synthetic clock. Its reports
and video labels explicitly distinguish app framebuffer capture from desktop
compositor recording. It does not read pixels from any other application.

`tools/run_live_motion_audit.py` pins the executable SHA-256 and records 21 real-time
runs: three cross-coat pairs (0+8, 8+10, 10+0), each covering 32 state probes three
times, eight special actions three times, wall climbing and social play three
times, 60-second ball/rod/pointer/keyboard cases, a young/150%-scale controller
group, and ten minutes of autonomy. Autonomy uses normal maintenance and periodic
saves inside the disposable profile. Other groups isolate controller/physics
behavior. Pointer input is shared between actors and drains its current recovery
before the test ends. Retired BoxSit and floor Hang states are pose probes; actual
wall grip/drop is case 44. DPI is an app coordinate simulation; physical monitor
coverage must be stated separately.

Harness corrections found during pilot runs are separate from product defects:
temporary toy surfaces are released before GDI+ shutdown, a stable autonomous
nap may satisfy the quiet endpoint after an already completed wall command, and
physical continuity uses the actual high-resolution physics clock rather than
the coarse GetTickCount64 sample grid. Interrupted superseded package runs are
retained as intermediate evidence, never counted as final verification.

The runner records process exits, exact source/CI/executable provenance, selected
source coverage, finite physics, physical continuity, capture drops and epoch
mismatches. Those checks do not substitute for visual playback and source/design
comparison. Final results and review media belong with the release evidence
bundle generated from the tested package. Any interrupted/failed attempts are
retained and must not be relabeled as passes.
