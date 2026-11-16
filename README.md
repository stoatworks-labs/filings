# filings

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume.** What's established is established by an offline harness, `fgtest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context on a synthetic clock, on this Mac's GPU at two rasters. The magnets' field is the
> closed form of point poles to **1e-15**; a filing's turn under a tap matches an RK4
> integration of its torque against friction to **1e-13 rad**; a whole pass of the sheet, with
> chains, contact and the slide, matches a second, brute-force coding of the same law to
> **1e-13**. Over 40 kicks, lone filings settle within **0.4°** of where the closed form says
> they stop, and the full sheet ends **14°** from the magnets' field on average, after
> **27°** at the first kick: the lines come up over the music, not at once. Over 600 frames of
> music with no kick in it, while every magnet moves, **no filing moves by a single bit**.
> **18** deliberately wrong models and **21** one-character mutants of the shipped code are all
> caught. How hard a tap shakes the sheet, and the friction, are **chosen**, not measured: see
> [Status](#status).

Iron filings on a sheet of acetate over your clip, magnets under the sheet, and the music
tapping it, for [Resolume](https://resolume.com) Arena and Avenue: an FFGL effect, **SW
Filings**.

![A night skyline under a purple sky with a low sun, the whole frame covered in thousands of
short grey needles. Around the sun the needles point straight out from it like the spokes of a
wheel; to its right they curve over in loops towards a second centre, the textbook pattern of
a bar magnet, and far from the two centres they lie every which way](docs/hero.png)

<sub>One frame rendered by `fgtest`, not captured from Resolume: the defaults over the
harness's card, 10 s of its music in (`--kicks --frames 600`, 21 taps). The north pole is
under the sun, the south pole a third of the frame to its right.</sub>

Each filing is a tiny soft-iron needle. The magnets magnetise it along its length, so it would
turn into their field, slide up towards the poles and stick end to end with its neighbours,
but friction pins it to the sheet. **Only a tap frees it, and a tap is a kick in the music.**
Each one turns the filings part way into the field, so the field lines come up over a few
kicks; move a magnet and nothing happens until the next kick, which starts turning them
towards the new field; a quiet passage freezes everything mid-turn. Far from the magnets the
field never beats friction and the filings stay where they fell.

The clip is the light coming up through the sheet, as on an overhead projector in a physics
lesson: the filings are opaque needles in it, the clip is untouched between them, and over a
transparent part of a clip they stand on their own.

## Controls

| Group | |
| --- | --- |
| **Magnets** | Poles (1–4), Strength (the field straight above a pole, 2–100 mT), Depth (how far under the sheet, 1–25% of the frame's height), and for each pole X, Y and Sign (N or S). |
| **Tap** | Audio (Resolume's audio input), Tap On (Onset, Beat, Half Beat, 2 Beats, Bar, Off), Tap (one tap now), Sensitivity (the onset detector), Tap Strength (0–2× the reference tap). |
| **Filings** | Friction (μ, 0.05–1), Density (how much of the sheet they cover, 5–60%), Size (a filing's length, 1/200–1/30 of the frame's height), Scatter (throw a fresh sheet). |
| **Look** | Tone (the filings' grey, from black), Mix. |

The defaults are a bar magnet, N at the left and S at the right a third of the frame apart,
40 mT, 12% of the frame's height under the sheet, tapped on every onset at the reference tap;
filings a seventieth of the frame long covering 20% of the sheet (12,478 of them), friction
0.2, drawn a dark grey (Tone 0.15). **Density and Size set the cost**: a tap's work goes as the
number of filings, up to 60,000.

Nothing but a tap moves a filing: not the magnets, not Strength, not Friction. That is the
point, and it means that without music (or Tap On a beat, or a press) the magnet controls
change nothing you can see.

## Status

**v0.1.0, local only, 16 November 2026. Never loaded into Resolume.**

### Measured offline, on macOS

`tools/verify.sh` passes on this machine (Apple Silicon, macOS 26) against a fresh universal
Release build (16 November 2026). The picture checks run at **two rasters**, 1280×720 and
320×180, and six of them again on **Apple's software renderer** at 320×180; the state checks
need no raster.

| check | result |
| --- | --- |
| `--field` | four poles against the closed form of point poles at 400 points, worst **1.4e-15** relative; B∥ peaks at **0.707106781 d** at **0.384900179 B0** (d/√2 and 2/(3√3)); the flux through the sheet is 2π d² B0 to **9e-15**; an N–S pair far off is the point dipole to **0.83** of the octupole's allowance |
| `--law` | 2000 random taps: the closed-form turn against RK4, worst **6.5e-14 rad**; none past its stop angle; below the friction threshold, bit for bit; Osborn's factors equal the ellipsoid's integrals to 12 digits; the torque at 10 mT **1.316348e-8 N·m**, ×4 at 20 mT |
| `--step` | one pass of 6112 filings, tapped 12 times, against the harness's own brute-force pass: worst **7.4e-14 rad** and **1.2e-14** frame heights |
| `--align` | lone filings: every angle after each of 40 taps on the closed form to **2e-13 rad**; the mean angle to the field **45°** scattered, **26.8°** after one tap, **6.5°** after 40, within **0.4°** of the asymptote. The full model: **30.8°** after one tap, **14.0°** after 40. The filings near each pole rise tap by tap (291 → 304 → 322 and 297 → 308 → 325) |
| `--dipole` | the needles' orientation read off the picture: lone filings within **1.54°** (4.68° at 320×180) of the prediction, against an instrument bias of 1.73° (4.74°) on the same patches; the full model **5.3°** (7.6°) from the dipole's direction |
| `--still` | 600 frames of music with no kick while every magnet circles and the physics controls ramp: the sheet **0 frames** different, the picture **0 frames** different. With kicks, exactly the tap frames change the sheet |
| `--taps` | taps on exactly the kick frames (and the first ghost note, before the detector's floor has built); every ghost too at Sensitivity 1; hard kicks at the reference tap to **1e-7**, soft ones at most 0.886 of it, the ghost 0.057; on the beat at 128 BPM on the right frames; a press taps once |
| `--draw` | each needle's ink is its ellipse's area to **0.09%**, centred to **0.03 px**, along its angle to **0.02°**; the clip shows through as clip × T to **0 codes** |
| `--alpha` | alpha = α + (1 − α)(1 − T) to **0 codes**; over a clear clip the filings reach alpha **1.0** (0.91 at 320×180, where a needle is under a pixel wide); Mix 0 the clip bit for bit |
| `--resize` | 1280×720 → 320×180 → 1920×1080 mid-run: the sheet bit for bit a run never resized; the picture **0** values from a fresh instance's |
| `--clock` | a host clock at 4.99e8 ms taps on the same frames and doses as one from 0; the unit settles offline |
| `--glstate` | the host's GL state comes back, **0** things changed |
| `--pipe` | 2.5 frames in, exactly 2 out; bad cues and settings refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **18** deliberately wrong models, **18** caught |
| mutation | **21** one-character mutants of the shipped GLSL and C++, **21** caught |
| `tools/sweep.py` | all **25** controls measurably change the picture (with music in) |
| shaders | all 4 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Filings` / `FG01` / `effect` and renders through it |

Render cost, from verify.sh's run on 16 November 2026 with other builds sharing the machine,
at the defaults (12,478 filings) with the harness's kicks: GPU time **0.05 ms** at 720p,
**0.26 ms** at 1080p and **1.33 ms** at 4K (one run); a whole frame without a tap **0.28**,
**0.49** and **2.06 ms**. A frame with a tap adds the tap's CPU work: **2.4**, **3.4** and
**5.3 ms** median (worst **3.1**, **7.5** and **28.7 ms**, the worst being the first tap
measured). At 60,000 filings a tap frame takes **8.6 ms** at 1080p. macOS only.

### Not established

- **No host has loaded it.** Not Arena, not Avenue, on any platform; no sound has reached it.
  `oxbow` (the fleet's test host) reads the bundle and renders through it, which is all.
- **Windows** has never been built. The CI workflow that would is in the repo and has never run.
- **Chosen, not measured:** the friction (no checked figure for iron on paper was found), how
  long a tap frees the sheet (one reference tap turns a filing in 4 mT by tan ψ → tan ψ/e),
  the filing's shape (a 1 mm needle of aspect 5), the softened neighbour field and its reach.
- **The physics stops at first order:** filings lie flat (they never stand up over a pole),
  never saturate, and the magnets' far poles are left out.
- **Where Resolume's FFT bins sit** has not been measured; the onset detector assumes kicks are
  in the lowest quarter.
- A tap's work lands on one frame: at the defaults 2–3 ms of CPU on the kick.
- The defaults were chosen over five of Resolume's bundled clips through `--pipe`, looked at as
  stills, not in a show.
- No user guide, no presets, no OpenFX port and no browser demo.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Filings.bundle` into `~/Documents/Resolume Arena/Extra Effects`. Restart
Arena and it's under Effects as **SW Filings**.

Windows needs Visual Studio 2022 and vcpkg (for GLEW); see `.github/workflows/ci.yml`.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and six on the software renderer, the pipe, the
negative controls, the mutants, the sweep for dead controls and the render cost. `CLAUDE.md`
lists the checks one by one; `./build/fgtest --all` runs them all.

Render a frame of your own:

```bash
./build/fgtest --out /tmp/filings.png --kicks --set "Poles=4" --set "Strength=0.9"
```

Run a clip through it:

```bash
ffmpeg -i in.mov -f rawvideo -pix_fmt rgba - | ./build/fgtest --pipe --kicks --fps 30 --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mov
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
