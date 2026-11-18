# filings

Iron filings on a sheet over the clip, magnets under it, and the music tapping the sheet, for
Resolume Arena/Avenue as one FFGL effect. Point poles give a closed-form field; each filing is
a soft-iron needle that turns into it, slides up |B|² and chains with its neighbours, but
friction pins it until a tap (an onset, a beat or a press) frees it. The clip is the light
through the sheet. C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `FG01` | `SW Filings` | `Filings.bundle`, `com.stoatworks.ffgl.filings` | effect |

Read `AGENTS.md` before touching the tap (`Sheet::Tap`, `Sheet::Pass`, `Turn`), the field
(`FieldAt`), the onset detector and its hit (`Audio.cpp`), the trigger, the clock, or the
composite.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects).
  Never from a shared session on this Mac: nothing here has been loaded into Arena.
- Render offline: `./build/fgtest --out /tmp/f.png` (the defaults over the harness's card,
  6 s in, no music, so no taps: add `--kicks` for the harness's music); the summary line gives
  the filings and the taps
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/fgtest --list`
- Set anything by name: `./build/fgtest --set "Poles=4" --set "Strength=0.8"`
  (a standard control takes the host's 0..1; an option takes its name)
- Press something later: `./build/fgtest --at 100 "Tap=1"`; music in the Audio buffer: `--kicks`
- Film: `./build/fgtest --film 600 --size 1280x720 --kicks --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/fgtest --pipe --kicks --fps 30 --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
- Constants: `python3 tools/constants.py` prints the filing's numbers that `source/Physics.*` carries.

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved words, `glslc`, the
  submodule pin, a fresh universal build with no warnings, lipo, plist, ad-hoc signature,
  `oxbow probe` and `selftest` (required: no oxbow is a failure); every check at 1280x720 and
  320x180; six again on Apple's software renderer; `--offline`; `--pipe`; the negative
  controls; 21 mutants; the sweep; the bench). `KEEP_BUILD=1` reuses `build-verify/`.
- Every check at once: `./build/fgtest --all`. One at a time (`--size WxH`: that raster alone):
  - **State, no GL**: `--field` (the poles' closed form, B∥'s peak, the flux, the far
    dipole, signs), `--law` (the closed-form turn against RK4; Osborn's factors against the
    integrals; the torque and friction), `--step` (one pass of a busy sheet against the
    harness's own brute-force pass), `--align` (40 taps: not at once, the lone filings' stop
    angles, the full model under 22.5°, filings piling at each pole).
  - **Picture**: `--dipole` (the needles' orientation off the picture against the dipole,
    through a calibrated structure tensor), `--draw` (each needle's ink, centroid and axis;
    clip × T), `--alpha` (α + (1 − α)(1 − T); Mix 0 bit-exact).
  - **Nothing moves without a tap**: `--still` (600 frames of hats while the magnets move: 0
    bits), `--taps` (on the kicks, the ghosts only at Sensitivity 1, hard kicks at the full
    dose, the beat, a press).
  - **The rest**: `--resize` (mid-run: the sheet bit for bit), `--clock` (a clock at 4.99e8 ms
    taps what one from 0 does; the unit settles offline), `--glstate`.
- The checks can fail: `./build/fgtest --negative` (18 wrong models), `tools/mutate.sh`
  (21 mutants, arm64, 320x180, perl alarm at 300 s; `ONLY=n` runs one).
- No dead controls: `python3 tools/sweep.py` (with the harness's music; `--bare` drops it).
- Render cost: `./build/fgtest --bench` (720p, 1080p, one 4K run).
- `FGTEST_RENDERER=software` runs any of it on Apple's software renderer (not `--still` or
  `--resize`: AGENTS.md says why).
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.
- Browser demo: `python3 -m http.server 8700 --bind 127.0.0.1 --directory demo`, then
  http://127.0.0.1:8700/ (`demo/vendor/` is the shared kit: never edit it). After changing a
  shader run `python3 demo/tools/check_shaders.py --write`; `verify.sh` runs it without `--write`
  and fails on any drift. `demo/port.js` is a hand port of the CPU half: change it with the C++.

## Notes
- **All the physics is on the CPU in double**, in frame heights ("su") from the frame's centre
  on a sheet 2 × 1 su; the physical scale is a 1 mm filing, Size long on the frame. The GPU only
  draws the needles the CPU has placed (an instanced quad each, multiplied into an RGBA32F
  transmission buffer) and composites. No time and no field reach a shader.
- **The state is written on tap frames only.** `Sheet::Tap` is the only thing that moves a
  filing, `Scatter` the only thing that replaces them; Density and Size append or drop at the
  end of a seeded sequence.
- A tap is two Jacobi passes from a snapshot on 8 threads by filing; the result does not depend
  on the thread count. Neighbours are found in cells 1.5 L square.
- All host parameters are 0..1 and mapped in `Controls.cpp`; options hold the element index;
  Tap and Scatter act on the rising edge.
- GLSL reserved words (`patch sample input output filter common active half layout flat
  packed`, `noise1`–`noise4`) must not be identifiers; `verify.sh` greps for them.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `filings_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Provisional hand copies:** `source/StoatworksAbout*.h` (guide = "", no user guide) and
  `ATTRIBUTIONS.md`. Once the project is registered in stoatworks-backend, `sync-about.py`
  and `sync-attributions.py` generate them; edit them there then.
- **Public:** `stoatworks-labs/filings`, checked out at `~/Projects/resolume/filings` (every
  edit there goes through a worktree). `ci.yml` runs on every push and pull request (macOS
  `fgtest --offline` and glslc, a Windows MSVC build); `release.yml` builds the universal
  bundle, the Windows DLL (checked by `tools/wincheck.ps1`), the zips, the dmg and the
  installer, and publishes on a `v*` tag. A `workflow_dispatch` run builds without
  publishing: that is how a Windows DLL gets to win-lab before a tag. CI cannot sign: the
  autosign agent on the release Mac signs and notarises the macOS assets after publication.
  GitHub Actions only because the repo is public (the fleet's rule).
- The spec is `~/Projects/resolume/specs/SPEC-filings.md`.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/filings/filings.YYYY-MM-DD.log
    %LOCALAPPDATA%\filings\logs\filings.YYYY-MM-DD.log

`FILINGS_LOG_DIR` overrides the folder.
