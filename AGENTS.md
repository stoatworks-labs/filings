# AGENTS.md: Filings

Read this before changing the tap (`Sheet::Tap`, `Sheet::Pass`, `Turn`), the field, the
onset detector and its hit, the trigger, the clock or the composite. `CLAUDE.md` has the
commands; the spec is `~/Projects/resolume/specs/SPEC-filings.md`, and it is kept in step with
this repo (no As-built section: where the build departed from the first draft, the spec was
changed).

## The idea

**Each filing is a soft-iron needle the magnets magnetise along its length. Friction pins it
to the sheet; only a tap frees it, and a tap is an onset in the music.** During a tap a filing
turns towards the field in closed form against kinetic friction, slides up |B∥|² by the force
on its two induced poles, feels its neighbours' poles (chains) and is pushed out of them
(contact). Between taps nothing happens at all: the state is written on tap frames only.

Audio drives the mechanism (a tap), never a parameter wobble. Resolume's FFT buffer and the
host's beats are the only inputs; earshot (the shared capture library) is not used.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Filings`, `FG01` (`FI01` is filament's), an effect,
  `com.stoatworks.ffgl.filings`, harness `fgtest`. One class, `FilingsPlugin`; host indices
  are the ParamIds, the About block last.
- **All physics on the CPU in double**, in frame heights on a sheet x ∈ [−1, 1], y ∈ [−0.5,
  0.5] (any aspect to 2:1 is covered; at 16:9 about 11% of the filings are off the frame
  and still simulated). The physical scale comes from the filing: 1 mm long, Size long on the
  frame. The GPU draws what the CPU placed.
- **The field is point poles at depth d** (the Gilbert model of a magnet standing on end;
  its far pole left out). Strength is the field straight above a pole and stops at 100 mT,
  below where the torque formula's linear-magnetisation assumption fails (125 mT).
- **The filing is a prolate spheroid of aspect 5**, so Osborn's demagnetising factors and
  Huillery's torque A sin 2ψ apply; its induced poles sit at c ± L/3 with charge q from the
  axial magnetisation (derived in the spec).
- **The pole mean.** The turn uses the mean of the in-plane fields at the two poles (magnets
  plus neighbours); q uses the magnets' field there along the axis. Four field evaluations a
  neighbour pair instead of six, and the lone-needle claim is stated against exactly this.
- **A tap is a time, not a force.** The bullet says static friction pins every filing until a
  tap; the model makes that literal (no code path moves a filing between taps) and gives the
  tap one chosen constant, the dose: one reference tap turns a frictionless filing in 4 mT by
  tan ψ → tan ψ/e. The tap's time scales with Tap Strength × the onset's hit.
- **The onset's hit, not the detector's level, sets the dose.** The level is normalised to its
  own running peak, so a soft kick and a hard one came out within a few per cent. The hit is
  the onset's flux over a decaying peak of onset fluxes: the loudest onset yet is 1.
- **One tap a frame**, at the largest dose any source asks for (a press 1, an onset its hit,
  a beat crossing 1). The beat is the host's bar phase when it changes, else the plugin's own
  clock at the host's tempo.
- **Two Jacobi passes a tap**, each from a snapshot, on 8 threads by filing (none below 2048
  filings). A Jacobi pass makes the result independent of order and thread count, which is
  what lets `--step` compare it with a second coding exactly.
- **The slide and the contact push are each capped at b a pass**, an explicit step's
  stability; contact is a projection (half the overlap each), applied only to a filing that is
  loose this pass, so a crowded but pinned region stays pinned.
- **The clip is the light through the sheet** (the overhead-projector demonstration): in linear
  light out = clip × T + ink × (1 − T), T the needles' transmission. The clip never touches the
  physics, so nothing it does can move a filing. Luminance as density or as friction was
  rejected (the spec says why).
- **Output alpha** = α + (1 − α)(1 − T): the sheet is clear, the filings opaque. Over a
  transparent part of a clip the filings stand on their own over the layer below. Looked at
  over a checkerboard on two of Resolume's demo clips with real alpha (BattleWeapon_Tank_09
  and Ethnik2). Resolume's AV demo clips (Bass, Beat, Synth) carry an opaque alpha; the Shop74
  ones mostly carry a real one.
- **Each needle's coverage is an exact 1D box filter across it** in the fragment shader, so
  its ink is its area whatever the rasteriser does with edges; needles multiply into an
  RGBA32F buffer cleared to 1 (blend `GL_ZERO, GL_SRC_COLOR`).
- **The onset detector** is ferrofluid's by way of fountain: flux over the lowest quarter of
  the bins only (over every bin the hi-hats tap the sheet constantly), primed on the first
  frame, 80 ms refractory.
- **The clock** is ferrofluid's vote on seconds or milliseconds, with fountain's fallback: after
  30 frames of host time without a verdict, the step's size decides (oxbow's selftest renders
  faster than real time and never wins the vote).
- **Defaults over the demo clips.** The first draft (Depth 0.06, Friction 0.3, Size 1/90,
  Density 0.3, Tone 0.3) showed a pattern only near the poles and a grey that swamped dark
  clips. Run through `--pipe` with the harness's music over Beat 001, Cyberspace_09,
  FogAndDust_3, NeonRoom2_32 and OrganicMotions_06 and looked at as stills: Depth 0.12 spreads
  the field across the frame, Friction 0.2 lets the weaker field turn filings, Size 1/70 makes
  them read at 720p, and Tone 0.15 with Density 0.2 keeps the clips readable.
- **The cost control is Density and Size**: the count is coverage × the sheet's area / πab, at
  most 60,000. There is no separate quality control; the GPU side is under 2% of a 60 fps frame
  at 1080p even at 60,000.
- **No OpenFX port.** The browser demo came with the release (the last section).
- **Arena on this Mac, not the gate, carries the tap check.** The fleet's gate measures a
  control by the clip thumbnail, and a magnet control changes nothing until a tap, so the
  gate's expectation holds Tap On at Beat for every magnet and friction row. The Mac probe
  read the composition monitor instead and saw the whole story: still with Tap On Off, the
  lines coming up on the beat.

## The traps actually hit

- **A from-memory citation.** The spec's first draft cited arXiv:1904.11562 to "Shah et al.";
  read, it is Nahlawi, Ma, Conradi and Saam. Every figure was then re-checked against what was
  actually read (the spec says which, and that Osborn's 1945 paper itself was not).
  Wikipedia's "Demagnetizing field" lacks the prolate formula and "Saturation (magnetic)" has
  no table, so both came from other checked sources.
- **Standard gravity was uncited** until the final read of the spec: now 3rd CGPM (1901),
  Declaration 2, checked on bipm.org.
- **The lone-needle count compared the sheet with itself**: the harness held a reference to the
  sheet's vector as "before". Copied now.
- **−ln T could not read a needle 1.2 px wide.** `--dipole`'s structure tensor on −ln T at σ 1
  px read 10.1° at 320×180 against an 8.4° bias; a larger σ made it worse (the texture between
  needles). On the coverage 1 − T at σ 0.7 px, with the bias measured on the same patches of a
  sheet laid along the field, it reads 4.68° against 4.74°.
- **The first ghost note after a cold start is heard** at Sensitivity 0.5: the detector's floor
  has not built yet. `--taps` expects it exactly rather than pretending otherwise.
- **The detector's level is useless as a dose** (normalised to its own peak): soft and hard kicks
  tapped almost alike. Hence `Analyser::Hit()`.
- **Two mutants survived the first run of 21.** A slide pushing filings from one pole to the
  other still raised the count near "the poles" (now counted per pole), and a hit of flux × peak
  kept every dose in order (now a hard kick must tap at exactly the reference). Both caught now.
- **Apple's software renderer draws a sheet differently the first time**: its first frame of a
  sheet differs from every later frame of the same sheet by up to 2.2e-5 (all later frames are
  identical). `--still` and `--resize` compare pictures bit for bit and fail there, so they are
  off the software list; on the GPU both are bit-exact.
- **The first timed tap frame at 4K cost 25 ms** where the others cost 5: reproducible, the
  CPU work inside ProcessOpenGL measured 2.4 ms, and a glFinish inside the plugin made it vanish.
  Not explained. The bench now times 120 frames at 4K (four taps) and the median is reported.
- **zsh has no PIPESTATUS** and **macOS has no timeout**: the pipe checks run under bash, the
  mutants under perl's alarm.
- **The FFGL SDK warns** (FFGLUtilities.cpp, an implicit int → float); verify.sh ignores
  warnings under external/ffgl only, as fountain's does.

## Verified, and assumed

Verified, by `OXBOW=... tools/verify.sh` on 16 November 2026 (numbers in README's Status):
every claim in the table there, the picture checks at 1280×720 and 320×180 on this Mac's GPU
and six of them again on Apple's software renderer; the negative controls and mutants; the
sweep; the bundle universal, signed ad hoc, read and rendered by oxbow.

Assumed: a 1 mm filing of aspect 5; the friction μ (default 0.2; no checked figure for iron
on paper was found); the reference dose; the neighbour field softened over b and cut at 1.5 L;
that a filing lies flat and never saturates; that Resolume's 64 FFT bins put kicks in the
lowest quarter (nobody in the fleet has measured where they sit in frequency); that Resolume
sends SetTime and SetBeatInfo every frame as ferrofluid assumes.

In Resolume (18 November 2026), recorded in README's Status under "In Resolume": Arena 7.29.0
on this Mac registered `FG01` as an effect, showed all 31 controls at their declared defaults,
held the sheet still with Tap On Off while two poles moved, tapped it on Arena's beat and bar,
and took Tap and Scatter pressed over REST. Arena 7.27.1 on win-lab (llvmpipe), gating the v0.1.0
release's own zip after the tag (win-lab was held by other plugins' gates until then): 9
passed, 0 failed, 2 skipped; 23 controls live, 17 under Tap On Beat; Audio and Sensitivity
need sound, and Tap and Scatter are events the gate never presses.

Not done: real music (Onset on Resolume's FFT), a show, anything timed inside Resolume,
Avenue, presets, an OpenFX port.

**How the Mac probe was run** (for the next session): a copy of `build-verify/Filings.bundle`
in `~/Documents/Resolume Arena/Extra Effects`, Arena launched with `open -g`, REST on 8080
(server.xml enabled), one layer added on top of whatever composition opens, a white
1920×1080 still opened and connected there with Alpha at full opacity, the effect added by
`effect:///video/SW%20Filings`, and `/composition/monitors/1/snapshot.jpg` compared in the
middle three-fifths of the frame. The layer deleted, `quit app "Arena"`, then **Quit** (never
Save & Quit) in its "Quit!" window; the show's .avc mtime unchanged; the bundle removed.

## Would this hold on another rasteriser, at another raster?

One line per check.

- `--field`: no GL; double arithmetic only.
- `--law`: no GL; double, against an RK4 at t/20000.
- `--step`: no GL; two codings of one law in double; the cell grid against brute force.
- `--align`: no GL; the 22.5° bound is chosen, not raster-bound.
- `--dipole`: yes; patches are 4 L wide at any raster, Size is chosen so a needle is 6 px at
  320×180, and the instrument's bias is measured at the raster being checked. Passes on the
  software renderer.
- `--still`: the state half anywhere (memcmp); the picture half bit-exact on this GPU only. On
  Apple's software renderer the first draw of a sheet differs from later draws by 2.2e-5, so it
  fails there; on another renderer the claim is the same frame, not the same bits.
- `--taps`: no picture; the analyser and trigger in single and double precision, raster-free.
- `--draw`: yes; the box filter is in the fragment shader and the tolerance is the tips'
  approximation computed at the raster checked (0.40% at 1280×720, 3.2% at 320×180).
- `--alpha`: yes to 2 codes (two roundings); Mix 0 is a texelFetch and a copy.
- `--resize`: the state anywhere; the picture bit-exact on this GPU, not on the software
  renderer (as `--still`).
- `--clock`: state; the sheets compared by memcmp.
- `--glstate`: state the plugin puts back, on any driver that reports it.

## The negative controls and mutants

`fgtest --negative`, each wrong model required to fail (checks failed of those run):

| check | wrong model | failed |
| --- | --- | --- |
| field | the poles' field falling as 1/r³ | 4 |
| field | every pole read as N: the dipole's sign lost | 3 |
| law | torque in proportion to B, not B² | 1 |
| law | friction ignored | 1 |
| law | a compass needle: polar, not headless | 1 |
| step | the cell grid misses the diagonal cells | 1 |
| step | no contact between filings | 1 |
| align | a tap snaps every filing that turns onto the field | 2 |
| align | the slide down \|B\|² | 1 |
| dipole | the magnets' field from the first pole only | 4 |
| still | the sheet never pins: a small pass every frame | 6 |
| still | a moved magnet drags the filings at once | 4 |
| taps | the onset detector never primed | 6 |
| taps | onset flux over every bin, hi-hats included | 6 |
| draw | the filings' grey added to the clip, which is not stopped | 2 |
| alpha | alpha mix(α, 1, Mix): the sheet opaque | 2 |
| resize | a new raster throws a fresh sheet | 1 |
| clock | the host's clock read in single precision | 4 |

`--glstate` has no negative control.

`tools/mutate.sh`, 21 one-character mutants, each built arm64 and run at 320×180 under a 300 s
alarm, all caught: the pole field's vertical part and its x part reversed (`--field`, two);
Osborn's factor with 3 for 2, the turn's exponent 3Rkt, the friction torque over 5 (`--law`);
the induced charge from the difference of the poles' fields, a neighbour's tail charged like
its head, contact pulling together, translational drag 13ζ_r/L², the cell search missing the
cells to the right (`--step`); the slide's force from the sum of the poles' fields
(`--align`); the cold start primed with 1, flux over every bin, the hit as flux × peak, Tap On
Beat stepping 0.35 of a bar (`--taps`); the clock fallback upside down (`--clock`); needles
drawn 1.2× long, the box filter wide on one side, the quad sheared off its axis, the grey
subtracted (`--draw`); alpha taken away over a clear clip (`--alpha`).

## The sweep's honest list

`python3 tools/sweep.py --bare` (no music, no per-control context): **19 of 25 controls are
dead**, and that is the design, not a fault. Every magnet control (Poles, Strength, Depth, and
each pole's X, Y and Sign), Tap On, Sensitivity, Tap Strength and Friction act only through a
tap, and without music nothing taps. The six alive bare are Tap (a press taps), Density, Size,
Scatter, Tone and Mix. With the harness's music in (the sweep's normal base) all 25 change the
picture, Pole 3 and 4 with Poles at 4, Tap with Tap On Off.

## Open questions

- **Pole X is across the frame's width**, so on a 4:3 frame the default poles sit closer in
  frame heights than on 16:9 and the pattern changes with the aspect. Should X be in frame
  heights instead? It matters for the hub, which will send positions over OSC.
- **The hub's mapping**: one hand or person per pole, sign by what? The X/Y controls work
  without it today.
- **A tap's CPU work lands on one frame**: 2–3 ms at the defaults, about 8.5 ms at 60,000 filings,
  on the kick. Spreading a tap over two frames would halve the spike but delay the picture; is the
  spike acceptable in a busy Resolume composition?
- **The 4K first-tap spike** (25 ms once, 5 ms after) is unexplained.
- **The friction and the reference dose are chosen.** A real sheet of filings on paper, tapped,
  would give μ and how far one tap carries them.
- Should filings stand up over a pole (a pile is a ring at d/√2 now, with a bare centre)?
- Where do Resolume's 64 bins sit in frequency? The onset's "lowest quarter" assumes the kicks
  are in it.

## The browser demo (2026-11-18)

https://filings-demo.stoatworks-labs.com/, from `demo/`, served by the Worker in `wrangler.toml`
(a route: the zone is at Cloudflare's 100 custom domains, so `filings-demo` needs a proxied AAAA
`100::` record, made through the API).

- **The GPU half is the plugin's.** `demo/shaders.js` is spliced from `Shaders.cpp` by
  `demo/tools/check_shaders.py --write`; the same script (run by `tools/verify.sh`, after a grep
  for built-ins GLSL ES 3.00 lacks) fails if a character drifts, if Shaders.cpp grows a piece it
  does not list, if `AssembleVertex()`, `Assemble()` or InitGL join the pieces differently, or if
  `render.js` stops joining them the same way. The GL state around them (the transmission buffer
  cleared to 1, the multiplicative blend, the instance layout, NEAREST sampling) only a reader
  checks. WebGL2 needs EXT_color_buffer_float (the page stops without it) and EXT_float_blend for
  the RGBA32F buffer; without the second the page falls back to RGBA16F and says so under the
  picture. Measured with the extension hidden: at most one 8-bit code from the RGBA32F picture,
  99.4% of pixels identical (Chrome on Metal).
- **The CPU half is a port, and only a reader checks it.** `demo/port.js`: Controls.cpp,
  Physics.cpp, Sheet.cpp (the scatter, the count, the tap's two Jacobi passes, the cells,
  contact by capsule distance) and Trigger.cpp whole, and Filings.cpp's CPU half (the frame
  order, Scatter, the count, the trigger, the tap, the instances, the uniforms). Not ported: the
  onset analyser (Audio.cpp: silence never fires it, so it is a constant false), the clock vote
  (the page's clock is seconds), the threads (a Jacobi pass reads only its snapshot, so one
  thread gives the same sheet) and the negative-control hooks (`SheetHooks`, the field's and the
  torque's wrong models, the plugin's model switches: all off, as shipped). C++ floats are
  mirrored with `Math.fround`; `std::clamp`, `min` and `max` as the same comparisons; `lround`
  half away from zero; `std::remainder` as IEEE remainder. It was compared once with the C++
  when written, by a scratch driver linking the plugin's own Release objects (arm64, and again
  built for x86_64), dumping its state, and a script running port.js over the same inputs. That
  comparison is not in the repository and nothing repeats it:
  - every conversion at 1001 values, 5767 of 6006 bit-equal, worst 2.2e-16 relative (all 6006
    with `-ffp-contract=off`: the rest are clang's fused multiply-adds); FilingCount 1001/1001;
    the parameter table 26/26 rows (name, group, kind, default as a float, options); the needle's
    constants within 1.2e-16 relative; the trigger's dose on 10,800 of 10,800 frames (every mode,
    with and without a host beat, a jittery clock, presses; 353 taps);
  - `Params()` bit for bit, and the scattered sheet bit for bit, in four scenes (the defaults at
    960×540, four poles at 0.09 T and 1280×720, 60,000 filings at 1920×1080, one pole at
    640×480);
  - the tapped sheet at the defaults, worst centre and angle: 3.7e-15 su and 1.1e-13 rad after one
    tap, 1.4e-11 and 7.5e-10 after five, 5.8e-8 and 5.6e-6 after ten, 4.7e-3 su and 1.2 rad after
    twenty. The sheet is chaotic (a filing crossing two others, a grazed contact) and amplifies
    the last bit about tenfold a tap. **The plugin's own x86_64 build parts from its arm64 build
    by the same amount** (6.5e-8 su after ten taps, 6.4e-3 after twenty): clang fuses
    multiply-adds on arm64 and V8 does not, and V8's sin, cos and atan2 are not Apple's. The sheet
    as a whole agrees: the mean angle to the field, the share within 10° and the filings near
    each pole are identical to three decimals in all three through ten taps, and after 40 taps
    the mean angle is 20.23° (arm64), 20.32° (x86_64) and 20.33° (port.js);
  - the picture: the page's canvas read back after five presses of Tap on the card at 960×540
    (headless Chrome 157, ANGLE on this Mac's Metal), against `fgtest --out --size 960x540` with
    the same five presses: **worst one 8-bit code, 96.8% of pixels identical** (96.7% before any
    tap). Under SwiftShader the worst is 25 codes, 144 pixels off by more than 8, 98.2% within two.
- **A tap's cost in the page**, measured the same way (Chrome 157 headless, Apple M4 Max, the
  sheet's `Tap` timed in `ProcessFrame`, four runs on a machine other builds were sharing):
  median **18 ms** at the defaults (12,478 filings; 17.6–18.0 ms a run of 30 taps; the page's
  very first tap 28–30 ms), 47–50 ms at 24,828, **62–68 ms** at 60,000 (61–79 ms a tap). A frame without a tap costs the CPU half nothing measurable. The plugin takes 2.6 ms at the
  defaults on eight threads. So a tap at the defaults is about one frame at 60 Hz, and at 60,000
  filings a visible hitch; the line under the picture gives the last tap's time, and the page
  says it. It stays on the main thread: a worker would not change the model but would show
  each tap's result a frame or more after the beat that caused it, and the cost at the defaults
  did not justify that. Not measured on a phone.
- **What the page stands in for**: no audio, so Audio and Sensitivity are not on the panel
  (ferrofluid's rule); Tap On keeps the plugin's default, Onset, which never taps there, and the
  banner and the line under the picture say so and point at Tap and the beat modes. Resolume's BPM
  and bar phase are a tempo menu (120 at first) and the page's clock (4/4, still when paused).
  Tap and Scatter are toggles released after one frame, plus Tap and Scatter buttons in a host
  row (two presses inside one frame count once). The first clip is the harness's card
  (fgtest's `buildCard`, ported in double, uploaded as RGBA32F with NEAREST sampling, rows
  bottom first, as fgtest hands it over). The kit's other clips are premultiplied while the
  composite takes straight colour, and the canvas composites the plugin's straight output as
  premultiplied; both are listed on the page. The About block is not on the panel.
- **No photosensitivity gate**: a tap turns needles; filings does not flash (the release's
  measurement put the worst frame-to-frame step at 0.056 of white).
- **Traps**: the last pass's turned/moved counts differ from the C++ even where the sheet is
  equal, because a filing pushed by two others equally from both sides gets exactly 0 in V8 and
  an FMA residual under clang, which counts as moved: compare positions, not those counts.
  Linking the plugin's objects for x86_64 fails on duplicate FFGL symbols (the SDK is a unity
  build): keep only `ffgl_FFGLSDK.o`. Headless Chrome needs `SHOT_GL=1` (SwiftShader) or ANGLE
  on Metal for WebGL2; at a phone's size under SwiftShader only two of four presses of Tap
  0.35 s apart registered (on Metal, all four). The canvas has no preserved drawing buffer, so a
  read-back has to happen in the frame that drew it (the test wrapped `passes.draw`).
