"""Render every parameter at both ends of its range and fail if any made no
difference. (ferrofluid's script by way of fountain; the table is filings'.)

**This is the only thing in the repo that catches a dead control.** A GLSL
uniform whose name does not match the C++ is silently ignored --
`glGetUniformLocation` returns -1 and `glUniform` on -1 is a documented no-op
-- so a slider can be stone dead while everything compiles, links, loads and
renders.

Each control is swept where it can act. Every render is 180 frames (3 s at
60 fps) at 320x180 (CI's raster) with the harness's music in (`--kicks`: a
kick every half second, soft and hard by turns, over a bed and hi-hats, with
a ghost note between that only a high Sensitivity hears), so the default Tap
On Onset taps the sheet six times. That is the base, because nothing moves a
filing without a tap: without the music every magnet control is dead by
design. Poles 3 and 4 only exist with Poles at 4. Tap and Scatter are presses
(`--at`, half a second before the frame read); Tap is swept with Tap On=Off so
the press is the only tap. An option parameter reads back 0..1 whatever its
count (the fleet's trap), so options are set here by name. A control whose ends
differ by less than `--floor` (mean 8-bit difference per channel) is barely
alive: vectrix's lesson.

Usage::

    tools/sweep.py [--build BUILD_DIR] [--verbose] [--jobs N] [--bare]

`--bare` drops every per-control context and the music, the addendum's
honesty check: what goes dead there is the list of controls that only act
with another one set (or with taps), which AGENTS.md records.
"""

import argparse
import concurrent.futures
import pathlib
import re
import subprocess
import sys
import tempfile
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent

MUSIC = ["--kicks"]

BASE = [MUSIC]

POLES = ["Poles=4"]
NO_TAPS = ["Tap On=Off"]

# name -> (low settings, high settings, context settings)
SWEEP = {
    "Poles": (["Poles=1"], ["Poles=4"], []),
    "Strength": (["Strength=0.2"], ["Strength=0.8"], []),
    "Depth": (["Depth=0.2"], ["Depth=0.8"], []),
    "Pole 1 X": (["Pole 1 X=0.2"], ["Pole 1 X=0.5"], []),
    "Pole 1 Y": (["Pole 1 Y=0.3"], ["Pole 1 Y=0.7"], []),
    "Pole 1 Sign": (["Pole 1 Sign=N"], ["Pole 1 Sign=S"], []),
    "Pole 2 X": (["Pole 2 X=0.5"], ["Pole 2 X=0.8"], []),
    "Pole 2 Y": (["Pole 2 Y=0.3"], ["Pole 2 Y=0.7"], []),
    "Pole 2 Sign": (["Pole 2 Sign=N"], ["Pole 2 Sign=S"], []),
    "Pole 3 X": (["Pole 3 X=0.3"], ["Pole 3 X=0.7"], POLES),
    "Pole 3 Y": (["Pole 3 Y=0.6"], ["Pole 3 Y=0.9"], POLES),
    "Pole 3 Sign": (["Pole 3 Sign=N"], ["Pole 3 Sign=S"], POLES),
    "Pole 4 X": (["Pole 4 X=0.3"], ["Pole 4 X=0.7"], POLES),
    "Pole 4 Y": (["Pole 4 Y=0.1"], ["Pole 4 Y=0.4"], POLES),
    "Pole 4 Sign": (["Pole 4 Sign=N"], ["Pole 4 Sign=S"], POLES),
    "Tap On": (["Tap On=Onset"], ["Tap On=Off"], []),
    "Tap": ([], [["--at", "150", "Tap=1"]], NO_TAPS),
    "Sensitivity": (["Sensitivity=0"], ["Sensitivity=1"], []),
    "Tap Strength": (["Tap Strength=0.2"], ["Tap Strength=0.8"], []),
    "Friction": (["Friction=0.2"], ["Friction=0.8"], []),
    "Density": (["Density=0.2"], ["Density=0.8"], []),
    "Size": (["Size=0.2"], ["Size=0.8"], []),
    "Scatter": ([], [["--at", "150", "Scatter=1"]], []),
    "Tone": (["Tone=0"], ["Tone=1"], []),
    "Mix": (["Mix=0"], ["Mix=1"], []),
}

# The About block: a text line and browser buttons, with no pixel to sweep.
# Audio is the host's FFT buffer: --kicks writes it in every render, and
# Sensitivity is swept through it.
SKIP = {"About", "Project page", "Source on GitHub", "Support the work", "User guide", "Audio"}


def read_png(path):
    """Enough of PNG for fgtest's own writer: 8-bit RGBA, filter 0 rows."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, width, height, idat = 8, 0, 0, b""
    while pos < len(data):
        length = int.from_bytes(data[pos:pos + 4], "big")
        kind = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            width = int.from_bytes(body[0:4], "big")
            height = int.from_bytes(body[4:8], "big")
        elif kind == b"IDAT":
            idat += body
        pos += 12 + length
    raw = zlib.decompress(idat)
    stride = width * 4
    out = bytearray()
    for y in range(height):
        start = y * (stride + 1)
        out += raw[start + 1:start + 1 + stride]
    return bytes(out)


def difference(a, b):
    if len(a) != len(b):
        return 255.0
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def render(fgtest, out, settings):
    args = [str(fgtest), "--out", str(out), "--size", "320x180", "--frames", "180"]
    for setting in settings:
        args += list(setting) if isinstance(setting, list) else ["--set", setting]
    result = subprocess.run(args, capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fgtest failed: {' '.join(args)}\n{result.stderr.strip()}")
    return read_png(out)


def parameters(fgtest):
    result = subprocess.run([str(fgtest), "--list"], capture_output=True, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"fgtest --list failed: {result.stderr.strip()}")
    names = []
    for line in result.stdout.splitlines()[1:]:
        parts = re.split(r"\s{2,}", line.strip())
        if len(parts) >= 3 and parts[0].isdigit():
            names.append(parts[1].strip())
    return names


def sweep_one(fgtest, scratch, name, bare):
    low, high, context = SWEEP[name]
    context = [] if bare else BASE + context
    tag = f"p{abs(hash(name))}"
    before = render(fgtest, scratch / f"{tag}-a.png", context + low)
    after = render(fgtest, scratch / f"{tag}-b.png", context + high)
    return name, difference(before, after)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--bare", action="store_true")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--floor", type=float, default=0.05,
                        help="mean 8-bit difference below which a control is 'barely alive'")
    args = parser.parse_args()

    build = args.build if args.build.is_absolute() else REPO / args.build
    fgtest = build / "fgtest"
    if not fgtest.exists():
        print(f"{fgtest} not found", file=sys.stderr)
        return 1

    plugin = "SW Filings"
    declared = parameters(fgtest)
    unknown = [n for n in declared if n not in SWEEP and n not in SKIP]
    if unknown:
        # A new parameter with no sweep is a hole, not a pass.
        print(f"{plugin}: no sweep defined for: {', '.join(unknown)}", file=sys.stderr)
        return 1
    dead, weak = [], []
    with tempfile.TemporaryDirectory() as scratch:
        scratch = pathlib.Path(scratch)
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            names = [n for n in declared if n in SWEEP]
            results = list(pool.map(lambda n: sweep_one(fgtest, scratch, n, args.bare), names))
    for name, delta in results:
        if delta == 0.0:
            dead.append(name)
            print(f"  DEAD {plugin}: {name:16s} both ends identical")
        elif delta < args.floor:
            weak.append(name)
            print(f"  WEAK {plugin}: {name:16s} mean delta {delta:.4f}")
        elif args.verbose:
            print(f"  ok   {plugin}: {name:16s} mean delta {delta:.3f}")
    print(f"{plugin}: {len(results)} parameters swept{' (bare: no music, no context)' if args.bare else ''}, "
          f"{len(dead)} dead, {len(weak)} barely alive")
    if dead or weak:
        if not args.bare:
            print("\nA parameter that changes nothing is usually a uniform name that does not match\n"
                  "the C++, or a setting nothing reads. Both are silent everywhere else.", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
