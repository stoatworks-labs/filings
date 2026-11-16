# Attributions

Filings is built on other people's work. This file lists what that work is, who did it, and
what it is doing here.

**A provisional hand copy (2026-11-16).** Filings is not yet registered in
`stoatworks-backend`, so this was written by hand in the generated file's shape. Once it is
registered, `scripts/sync-attributions.py` generates it from the master lists; edit it there
then, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness, the tooling and the analyser — Stoatworks fountain, and ferrofluid before it

<https://github.com/stoatworks-labs/ferrofluid>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its Perturb and --offline (tools/fgtest is fountain's fttest infrastructure, which is ferrofluid's fetest, under new checks), and the harness's music (fountain's: a bed, hi-hats, soft and hard kicks and a ghost note between); tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh, tools/sweep.py, the CI and release workflows and scripts/release-lib.sh; the clock's vote on seconds or milliseconds (ferrofluid's, by way of rosette and millpond) with fountain's offline fallback (source/Filings.cpp); and source/Audio.{h,cpp}, fountain's copy of ferrofluid's onset analyser (schlieren's, millpond's, rosette's, adapted from macroblock's) with its primed first frame and fountain's flux over the lowest quarter of the bins. Filings adds the hit (an onset's flux over a decaying peak of onset fluxes). All adapted. fountain lives at ~/dev/fountain and is not yet published.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's. source/GLState.h is radar's, which is vectrix's, from resolume-scopes; filings adds a multiplicative blend.

### The About block — Stoatworks zoetrope

<https://github.com/stoatworks-labs/zoetrope>  
Licence: MIT  
Copyright: Stoatworks Labs

source/StoatworksAbout*.h, a provisional hand copy of fountain's, itself from zoetrope's first header (no user guide yet).

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in fgtest and the picture in docs/hero.png.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl, pinned at b1afaf9.

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something this plugin calls directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### The torque on a soft ferromagnetic needle — P. Huillery et al., arXiv:1903.09699v3, eq. (1), citing J. J. Abbott et al., IEEE Trans. Robotics 23 (2007) 1247

T = A sin 2ψ with A = V (n_r − n_a) B² / (2 μ0 n_a n_r) for a prolate soft-iron body below saturation, and the field below which that holds (source/Physics.cpp). Read in the arXiv paper; Abbott's paper itself was not read. fgtest --law checks the plugin's torque against the formula and its turn against an RK4 of the torque.

### Demagnetising factors of a prolate spheroid — J. A. Osborn, Phys. Rev. 67 (1945) 351

The axial factor n_a(R) in closed form (source/Physics.cpp, AxialDemag), checked through two reproductions (Huillery et al., arXiv:1903.09699v3, below eq. (1); A. I. Nahlawi, Z. L. Ma, M. S. Conradi and B. Saam, arXiv:1904.11562v2, eq. (A.5)), not Osborn's paper itself. fgtest --law re-derives it from the ellipsoid's integral, and tools/constants.py from the closed form.

### The field of a magnetic point charge — J. D. Jackson, Classical Electrodynamics, 3rd ed., §6.11

∇·B = μ0 ρm, so a pole's field is radial and inverse-square (source/Physics.cpp, FieldAt), and an N–S pair tends to the point dipole far off. Checked through Wikipedia's "Magnetic monopole" (SI section, citing Jackson) and "Magnetic moment" (the magnetic-pole section), not Jackson itself.

### Iron's density, and where it saturates — Wikipedia, "Iron" and "Saturation (magnetic)"

7.874 g/cm³ at 20 °C (the infobox), and iron alloys saturating at 1.6–2.2 T (Laughton and Warne, Electrical Engineer's Reference Book, 16th ed., as quoted there), which caps Strength at 100 mT.

### Hash functions for GPU rendering — M. Jarzynski and M. Olano, Journal of Computer Graphics Techniques 9(3) (2020)

They recommend a PCG-style hash as the default one-input hash; the scatter's `pcg` (source/Sheet.cpp) is the fleet's copy of it. Its constants came from the fleet's code and were not checked against the paper's supplementary code.

## Standards and published specifications

What the implementation is measured against.

- **CODATA 2022 (NIST)** — μ0 = 1.25663706127e−6 N/A², in source/Physics.h.
- **3rd CGPM (1901), Declaration 2** — standard gravity 9.80665 m/s², the filings' weight, in source/Physics.h.
- **IEC 61966-2-1 (sRGB)** — toLinear() and toCode() in source/Shaders.cpp: the clip is linearised so the filings stop it as light, then encoded again.

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
