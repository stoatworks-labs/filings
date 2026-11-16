#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the physics, would pass every mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run at
# CI's raster, 320x180, under a time limit (a mutant can hang: honeydew's
# lesson; a timeout counts as caught). The state checks take no raster and
# ignore --size. (ferrofluid's script by way of fountain; filings' mutants.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap).
MUTANTS=(
	"source/Physics.cpp|		f.z += s * d;|		f.z -= s * d;|--field|C++: the pole field's vertical part with the wrong sign, + -> -"
	"source/Physics.cpp|		f.x += s * dx;|		f.x -= s * dx;|--field|C++: the pole field's x part pointing the wrong way, + -> -"
	"source/Physics.cpp|R / ( 2.0 * s ) * std::log|R / ( 3.0 * s ) * std::log|--law|C++: Osborn's axial demagnetising factor with 3 for 2, 2 -> 3"
	"source/Physics.cpp|const double E      = std::exp( -2.0 * R * kt );|const double E      = std::exp( -3.0 * R * kt );|--law|C++: the closed-form turn's exponent 3 R k t, 2 -> 3"
	"source/Physics.cpp|return mu * mass * kGravity * length / 4.0;|return mu * mass * kGravity * length / 5.0;|--law|C++: the friction torque mu m g L / 5, 4 -> 5"
	"source/Sheet.cpp|const double axial = 0.5 * ( ( bh.x + bt.x ) * s.ux|const double axial = 0.5 * ( ( bh.x - bt.x ) * s.ux|--step|C++: the induced charge from the difference of the poles' fields, + -> -"
	"source/Sheet.cpp|add( s.hx - o.tx, s.hy - o.ty, -kq, bhx, bhy );|add( s.hx - o.tx, s.hy - o.ty, +kq, bhx, bhy );|--step|C++: a neighbour's tail charged like its head, - -> +"
	"source/Sheet.cpp|pushX += 0.5 * ( touch - dist ) * nxv;|pushX -= 0.5 * ( touch - dist ) * nxv;|--step|C++: contact pulls overlapping filings together in x, + -> -"
	"source/Sheet.cpp|const double ltOver = kt * needle.length * needle.length / 12.0;|const double ltOver = kt * needle.length * needle.length / 13.0;|--step|C++: translational drag 13 zeta_r / L^2, 2 -> 3"
	"source/Sheet.cpp|			for( int ox = -1; ox <= 1; ++ox )|			for( int ox = -1; ox <= 0; ++ox )|--step|C++: the cell search misses the cells to the right, 1 -> 0"
	"source/Sheet.cpp|const double fx = s.q * ( bhx - btx ), fy|const double fx = s.q * ( bhx + btx ), fy|--align|C++: the slide's x force from the sum of the poles' fields, - -> +"
	"source/Audio.cpp|			flux = 0.0f;|			flux = 1.0f;|--taps|C++: the cold start primed with a flux of 1, not 0, 0 -> 1"
	"source/Audio.cpp|static_cast< int >( std::lround( n / 4.0 ) ), std::min( n, 1 )|static_cast< int >( std::lround( n / 1.0 ) ), std::min( n, 1 )|--taps|C++: onset flux over every bin, not the lowest quarter, 4 -> 1"
	"source/Audio.cpp|hit     = clamp01( flux / hitPeak );|hit     = clamp01( flux * hitPeak );|--taps|C++: the hit's strength flux x peak, / -> *"
	"source/Trigger.cpp|case TapOn::Beat: step = 0.25; break;|case TapOn::Beat: step = 0.35; break;|--taps|C++: Tap On Beat steps 0.35 of a bar, 2 -> 3"
	"source/Filings.cpp|clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;|clockScale  = hostDelta > 1.0 ? 1.0 : 0.001;|--clock|C++: the clock-unit fallback upside down, < -> >"
	"source/Filings.cpp|const double halfA  = 0.5 * length * height;|const double halfA  = 0.6 * length * height;|--draw|C++: needles drawn 1.2 x their length, 5 -> 6"
	"source/Shaders.cpp|float cover = clamp( min( vLocal.y + 0.5, h )|float cover = clamp( min( vLocal.y + 0.6, h )|--draw|GLSL: the box filter a pixel and a tenth wide on one side, 5 -> 6"
	"source/Shaders.cpp|vec2 n   = vec2( -u.y, u.x );|vec2 n   = vec2( +u.y, u.x );|--draw|GLSL: the needle's quad sheared off its axis, - -> +"
	"source/Shaders.cpp|vec3 shown   = light * src.a * T + vec3( Ink ) * cover;|vec3 shown   = light * src.a * T - vec3( Ink ) * cover;|--draw|GLSL: the filings' grey subtracted, + -> -"
	"source/Shaders.cpp|float alpha  = src.a + ( 1.0 - src.a ) * cover;|float alpha  = src.a - ( 1.0 - src.a ) * cover;|--alpha|GLSL: the filings take alpha away over a clear clip, + -> -"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && "$ONLY" != "$index" ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target fgtest -j8 >/dev/null 2>&1
	status=0
	limit 300 "$tree/build/fgtest" "$check" --size 320x180 >"$WORK/log" 2>&1 || status=$?
	if [[ "$status" -eq 0 ]]; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant (exit %d):\n' "$check" "$status"
		# A mutant that timed out printed no FAIL: grep finding none must not end the run.
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
