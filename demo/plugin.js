/**
 * Filings — browser demo.
 *
 * Iron filings on a sheet over the clip, magnets (point poles) under it.
 * Friction pins every filing until a tap; each tap turns the filings part way
 * into the field in closed form, slides them up |B∥|² and chains them, so the
 * field lines come up over a few taps. Between taps nothing moves at all.
 *
 * Three files, and they are not equally faithful:
 *
 *   **render.js runs the plugin's own GLSL**, unedited: the needles (one
 *   instanced quad per filing, each pixel's coverage an exact box filter
 *   across the needle, multiplied into a transmission buffer) and the
 *   composite (out = clip × T + ink × (1 − T) in linear light). shaders.js is
 *   spliced from source/Shaders.cpp by demo/tools/check_shaders.py --write,
 *   and the same script fails tools/verify.sh if a character or the way the
 *   pieces are joined drifts.
 *
 *   **port.js is a hand port of the CPU half, and only a reader checks it**:
 *   Controls.cpp, Physics.cpp, Sheet.cpp, Trigger.cpp and the CPU half of
 *   Filings.cpp's ProcessOpenGL. It was compared with the C++ once, by hand,
 *   when the page was built (AGENTS.md has the numbers).
 *
 *   **This file is the page**: the parameter panel, the host's stand-ins
 *   (its tempo, and a pad for Tap and Scatter), the harness's card as a clip,
 *   the line under the picture and the page's presets. None of it is the
 *   plugin.
 *
 * No audio reaches a page. Audio and Sensitivity (the onset detector's) are
 * absent from the panel rather than present and dead (ferrofluid's rule, and
 * bassalt's, cadence's and vocoder's before it). With silence in the Audio
 * buffer the plugin's analyser never fires, so Tap On's default, Onset, never
 * taps here; the panel keeps that default, because it is the plugin's, and the
 * page says so under the picture and offers Tap and the beat modes instead.
 */

import { mountDemo } from './vendor/demo.js';
import { SOURCES } from './vendor/sources.js';
import { createPasses } from './render.js';
import {
  f32, PT, Kind, InfoOf, TapOn, kTapOnNames, kInkGrey, pcg, OptionIndex, FilingsPlugin, FilingCount,
  StrengthFromParam, ParamFromStrength, DepthFromParam, ParamFromDepth, TapStrengthFromParam, ParamFromTapStrength,
  FrictionFromParam, ParamFromFriction, DensityFromParam, ParamFromDensity, SizeFromParam, ParamFromSize,
} from './port.js';

//===========================================================================
// The harness's card: tools/fgtest/main.cpp's buildCard, ported in double,
// as the plugin's own README picture and every fgtest render see it. The
// harness hands the plugin its card as 32-bit floats (RGBA32F, rows bottom
// first), and so does this page, so the clip under the filings is the
// harness's to the bit. It is a still: the filings never read the clip.
//===========================================================================
const hash01 = (a, b = 0) => pcg((Math.imul(a, 2654435761) ^ pcg((b + 0x9e3779b9) >>> 0)) >>> 0) * (1.0 / 4294967296.0);
const BARS = [[1, 1, 1], [1, 1, 0], [0, 1, 1], [0, 1, 0], [1, 0, 1], [1, 0, 0], [0, 0, 1]];

function buildCard(width, height) {
  const card = new Float32Array(width * height * 4);
  const s = Math.min(width, height);
  for (let y = 0; y < height; y += 1) {
    for (let x = 0; x < width; x += 1) {
      const u = (x + 0.5 - 0.5 * width) / s, v = (y + 0.5 - 0.5 * height) / s;
      const k = Math.min(Math.max(0.5 + v, 0.0), 1.0);
      let r = 0.95 - 0.8 * k, g = 0.45 - 0.3 * k, b = 0.25 + 0.5 * k;
      const d = Math.sqrt((u + 0.25) * (u + 0.25) + (v + 0.02) * (v + 0.02));
      if (d < 0.2) {
        r = 1.0;
        g = 0.85 - 0.5 * d;
        b = 0.35;
      }
      const block = Math.trunc(Math.floor((u + 2.0) * 11.0));
      const top = -0.12 + 0.16 * hash01(block >>> 0, 7);
      if (v < top) {
        r = g = b = 0.05;
        const wx = Math.trunc(Math.floor((u + 2.0) * 70.0)), wy = Math.trunc(Math.floor(v * 60.0 + 100.0));
        if (wx % 3 !== 0 && wy % 2 === 0 && hash01((Math.imul(wx, 131) + wy) >>> 0, 9) > 0.55) {
          r = 1.0;
          g = 0.9;
          b = 0.55;
        }
      }
      if (v < -0.36) {
        const bar = Math.min(Math.max(Math.trunc(Math.floor((x + 0.5) / width * 7.0)), 0), 6);
        r = 0.75 * BARS[bar][0];
        g = 0.75 * BARS[bar][1];
        b = 0.75 * BARS[bar][2];
      }
      if (Math.abs(u - 0.42) < 0.28 && Math.abs(v - 0.3) < 0.06) r = g = b = 0.95;
      const o = (y * width + x) * 4;
      card[o] = r;
      card[o + 1] = g;
      card[o + 2] = b;
      card[o + 3] = 1.0;
    }
  }
  return card;
}

if (!SOURCES.some((s) => s.id === 'card')) {
  SOURCES.unshift({
    id: 'card',
    name: 'The harness’s card (fgtest)',
    hint: 'The plugin harness’s own clip (buildCard in fgtest), the one under the README’s picture: a sunset sky, a sun, a skyline, a stripe of bars and a white block. Built in the page in double and handed over as 32-bit floats, as the harness does. A still.',
    // The kit renders this every frame and the page draws the card instead
    // (createRenderer): the card is built on the CPU, as the harness's is.
    fragment: 'in vec2 uv;\nout vec4 fragColor;\nvoid main()\n{\n\tfragColor = vec4( 0.0, 0.0, 0.0, 1.0 );\n}\n',
  });
}

function cardTexture(gl, cache, width, height) {
  if (cache.texture && cache.width === width && cache.height === height) return cache;
  if (cache.texture) gl.deleteTexture(cache.texture);
  cache.texture = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, cache.texture);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, buildCard(width, height));
  // NEAREST: a float texture is incomplete under LINEAR without
  // OES_texture_float_linear, and the composite reads it with texelFetch.
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  cache.width = width;
  cache.height = height;
  return cache;
}

//===========================================================================
// The host's tempo. Resolume sends its BPM and bar phase every frame; here the
// page is the host, and its bar phase runs on the page's clock at the tempo
// in the host row, continuous when the tempo changes, in 4/4. Paused, it
// stands still.
//===========================================================================
const TEMPOS = [60, 90, 100, 120, 128, 140, 150, 174];
const host = { bpm: 120, phase: 0, lastTime: null };

//===========================================================================
// The renderer the kit drives.
//===========================================================================
let current = null; // { cpu, frame, passes, taps }
let mounted = null;

/** Tap and Scatter are FF_TYPE_EVENT: a press here is a toggle let go of after one frame. */
const EVENTS = ['tap', 'scatter'];

function createRenderer(gl) {
  const passes = createPasses(gl);
  const cpu = new FilingsPlugin();
  const card = { texture: null, width: 0, height: 0 };
  current = { cpu, frame: null, passes, taps: 0, seed: cpu.sheet.seed, lastTapMs: null };

  return {
    render({ input, params, width, height, time }) {
      // The host restates every parameter each frame.
      for (const p of PARAMS) cpu.SetFloatParameter(p.slot, params.get(p.id));
      for (const id of EVENTS) if (params.get(id) > 0.5) queueMicrotask(() => params.set(id, 0));

      if (host.lastTime !== null && time > host.lastTime) host.phase = (host.phase + (time - host.lastTime) * host.bpm / 240) % 1;
      host.lastTime = time;
      cpu.SetBeatInfo(host.bpm, host.phase);

      const frame = cpu.ProcessFrame(time, width, height);
      current.frame = frame;
      // Taps on this sheet: Scatter throws a fresh one, with a fresh seed.
      if (cpu.sheet.seed !== current.seed) {
        current.seed = cpu.sheet.seed;
        current.taps = 0;
      }
      if (frame.tapped) {
        current.taps += 1;
        current.lastTapMs = cpu.lastTapMs;
      }
      const clip = mounted?.state.sourceId === 'card' ? cardTexture(gl, card, width, height) : input;
      passes.draw(frame, clip, width, height);
    },
  };
}

//===========================================================================
// The parameters: the constructor's, in kTable's order and groups, with the
// plugin's names, element lists and defaults. Audio and Sensitivity are
// absent: see the header.
//===========================================================================
const sig = (n) => (n >= 100 ? n.toFixed(0) : n >= 10 ? n.toFixed(1) : n >= 1 ? n.toFixed(2) : n.toFixed(3));
const across = (v) => `${(100 * f32(v)).toFixed(0)}% across`;
const up = (v) => `${(100 * f32(v)).toFixed(0)}% up`;

/** id, ParamId, display, hint. */
const SPEC = [
  ['poles', PT.POLES, null, 'How many magnets stand under the sheet, each a point pole a Depth below it. 2, the default, is a bar magnet: N on the left, S on the right.'],
  ['strength', PT.STRENGTH, (v) => `${sig(1000 * StrengthFromParam(f32(v)))} mT`, 'The field straight above a pole, 2 to 100 mT, geometrically. Stronger turns filings further from the poles and slides them in harder; it acts only through a tap.'],
  ['depth', PT.DEPTH, (v) => `${sig(100 * DepthFromParam(f32(v)))}% deep`, 'How far under the sheet the poles are, 1% to 25% of the frame’s height, geometrically. Shallow poles make a tight, strong pattern; deep ones spread it across the frame.'],
  ['pole1X', PT.POLE1_X, across, 'Pole 1’s place across the frame’s width. Moving a magnet moves nothing until the next tap.'],
  ['pole1Y', PT.POLE1_Y, up, 'Pole 1’s place up the frame.'],
  ['pole1Sign', PT.POLE1_SIGN, null, 'N or S. Two unlike poles join their lines; two like ones push them apart.'],
  ['pole2X', PT.POLE2_X, across, ''],
  ['pole2Y', PT.POLE2_Y, up, ''],
  ['pole2Sign', PT.POLE2_SIGN, null, ''],
  ['pole3X', PT.POLE3_X, across, 'Pole 3 counts when Poles is 3 or 4, Pole 4 only at 4.'],
  ['pole3Y', PT.POLE3_Y, up, ''],
  ['pole3Sign', PT.POLE3_SIGN, null, ''],
  ['pole4X', PT.POLE4_X, across, ''],
  ['pole4Y', PT.POLE4_Y, up, ''],
  ['pole4Sign', PT.POLE4_SIGN, null, ''],

  ['tapOn', PT.TAP_ON, null, 'What taps the sheet. Onset, the default, taps on every onset in Resolume’s audio, as hard as it hit; no audio reaches this page, so here Onset never taps and only Tap does. Beat, Half Beat, 2 Beats and Bar tap on the host’s beat (here the page’s, at the tempo in the host row under the picture). Off: only Tap.'],
  ['tap', PT.TAP, null, 'One tap now, at the reference dose times Tap Strength, in any Tap On mode. FF_TYPE_EVENT in the plugin; a toggle the page lets go of after one frame here. The host row under the picture has the same button.'],
  ['tapStrength', PT.TAP_STRENGTH, (v) => `${TapStrengthFromParam(f32(v)).toFixed(2)}× ref`, 'How long a tap frees the sheet, 0 to 2 times the reference tap (one reference tap turns a frictionless filing in 4 mT by tan ψ → tan ψ/e). A beat and a press tap at exactly this; an onset at this times how hard it hit.'],

  ['friction', PT.FRICTION, (v) => `μ ${FrictionFromParam(f32(v)).toFixed(3)}`, 'The friction coefficient between filing and sheet, 0.05 to 1, geometrically. Far from the poles the field never beats it and the filings stay where they fell.'],
  ['density', PT.DENSITY, (v) => `${(100 * DensityFromParam(f32(v))).toFixed(1)}% covered`, 'How much of the sheet the filings cover, 5% to 60%. With Size it sets how many there are (up to 60,000) and so what a tap costs; the line under the picture gives the count. Changing it adds or drops filings at the end of the seeded sequence and moves none.'],
  ['size', PT.SIZE, (v) => `1/${(1 / SizeFromParam(f32(v))).toFixed(0)} height`, 'A filing’s length, 1/200 to 1/30 of the frame’s height, geometrically. The physics takes a filing to be 1 mm long, so Size also sets the scale of everything else.'],
  ['scatter', PT.SCATTER, null, 'Throw a fresh sheet: new places and angles from the next seed. FF_TYPE_EVENT in the plugin; a toggle let go of after one frame here.'],

  ['tone', PT.TONE, (v) => `${(Math.min(Math.max(f32(v), 0), 1) * kInkGrey).toFixed(3)} linear`, 'The filings’ grey, from black: Tone × 0.45 in linear light.'],
  ['mix', PT.MIX, (v) => `${(100 * f32(v)).toFixed(0)}%`, 'The output against the clip.'],
];

const PARAMS = SPEC.map(([id, slot, display, hint]) => {
  const info = InfoOf(slot);
  if (info.id !== slot) throw new Error(`${id}: not a declared parameter`);
  const p = { id, slot, name: info.name, group: info.group, hint };
  switch (info.kind) {
    case Kind.Option: return { ...p, type: 'option', elements: info.options.slice(), default: info.defaultValue };
    case Kind.Event: return { ...p, type: 'boolean', default: 0 };
    default: return { ...p, type: 'standard', display, default: info.defaultValue };
  }
});
{
  const slots = PARAMS.map((p) => p.slot);
  const want = Array.from({ length: PT.ABOUT_TEXT }, (_, i) => i).filter((i) => i !== PT.AUDIO && i !== PT.SENSITIVITY);
  if (slots.join() !== want.join()) throw new Error('the panel must carry every control but Audio and Sensitivity, in the plugin’s order');
}

// Presets: whole states on top of the constructor's defaults, the page's own.
// Each but the first also presses Scatter, so it starts from a fresh sheet.
const fresh = { scatter: 1 };
const PRESETS = {
  'The plugin’s defaults (Onset: only Tap taps here)': {},
  'A bar magnet, tapped on the beat': { ...fresh, tapOn: TapOn.Beat },
  'Tapped softly on the half beat': { ...fresh, tapOn: TapOn.HalfBeat, tapStrength: ParamFromTapStrength(0.5) },
  'Two like poles (N and N)': { ...fresh, tapOn: TapOn.Beat, pole2Sign: 0 },
  'Four poles, N S N S': { ...fresh, tapOn: TapOn.Beat, poles: 3 },
  'One pole: spokes': { ...fresh, tapOn: TapOn.Beat, poles: 0, pole1X: 0.5 },
  'Strong and shallow: chains and piles': { ...fresh, tapOn: TapOn.Beat, strength: ParamFromStrength(0.09), depth: ParamFromDepth(0.05) },
  'A slippery sheet (μ 0.07)': { ...fresh, tapOn: TapOn.Beat, friction: ParamFromFriction(0.07) },
  'Fine filings, thick (about 32,000)': { ...fresh, tapOn: TapOn.Beat, size: ParamFromSize(1 / 100), density: ParamFromDensity(0.25) },
  'Pale filings over the clip': { ...fresh, tapOn: TapOn.Beat, tone: 0.9 },
};

//===========================================================================
// What this page does not reproduce. The kit adds its four.
//===========================================================================
const DIFFERENCES = [
  'The CPU half is a PORT that only a reader checks. The plugin does all its physics on the CPU in double, and demo/port.js is a hand translation of it: Controls.cpp (every mapping, name, group and default), Physics.cpp (the needle’s constants, Osborn’s factor, the closed-form turn against friction, the poles’ field), Sheet.cpp (the seeded scatter, the count, and a tap’s two Jacobi passes: the field at each filing’s two poles from the magnets and its neighbours, the turn, the slide up |B∥|², contact by capsule distance), Trigger.cpp, and the CPU half of Filings.cpp (the frame, Scatter, the count, the trigger, the tap, the instances and the uniforms). JavaScript’s numbers are doubles too. Nothing in the repository runs it against the C++. It was compared once, by hand, when the page was built (AGENTS.md has the numbers): every conversion, default and trigger decision the plugin’s, a scattered sheet the plugin’s to the bit, a sheet tapped five times within 2e-11 of a frame height and 1e-9 rad of it, and the picture after those five taps within one 8-bit code of fgtest’s. The sheet is chaotic, though: a filing crossing two others, a contact grazed, magnifies the last bit of a rounding about ten times a tap, so after twenty taps single filings sit visibly differently from the plugin’s, while the sheet as a whole agrees (its mean angle to the field within 0.1° after 40 taps). The plugin’s own Intel build parts from its Apple Silicon build in the same way and by the same amount, because clang fuses multiply-adds on one and not the other; here it is because V8 rounds every multiply and add, and its sine and arc tangent are not Apple’s.',
  'The GPU half is not a port: the needles and composite passes are the plugin’s own GLSL, joined as the plugin joins them, into a transmission buffer cleared to 1 and blended multiplicatively, as there. demo/tools/check_shaders.py fails the repository’s verify script if a character of the shaders or the way they are joined drifts from the plugin’s; the GL state around them (the buffer, the blend, the instance layout) only a reader checks. The plugin’s buffer is RGBA32F. Blending into one needs EXT_float_blend in WebGL2, and a browser without it gets RGBA16F here instead, which the line under the picture says; measured with the extension hidden, that moved the picture by at most one 8-bit code, in under 1% of pixels.',
  'No audio. The plugin declares an Audio buffer (Resolume’s FFT) and Sensitivity, its onset detector’s, for Tap On Onset. A browser has no Resolume FFT, so both are absent from the panel rather than shown doing nothing. With silence in that buffer the plugin’s analyser never fires, so Tap On Onset, the plugin’s default and the panel’s, never taps here, exactly as in a layer with no audio routed: only Tap does. Tap On’s beat modes and Off are the plugin’s, and the presets use the beat.',
  'The tempo and bar phase are the page’s: Resolume sends its own every frame. The tempo is chosen in the host row under the picture (120 BPM at first, what the plugin assumes before a host sends any); the bar phase runs on the page’s clock, 4/4, continuous across a tempo change, and stands still when paused, so a paused page taps only on Tap. The clock is seconds from frame deltas, a frame capped at a tenth of a second (the plugin caps one at a tenth too); the plugin’s vote on whether its host sends seconds or milliseconds is not ported. Restart is a clock jump backwards, which the plugin takes as no time passing; it does not reset the sheet (Scatter does).',
  'Tap and Scatter are FF_TYPE_EVENT in the plugin, acted on at the press. The kit has no event control, so each is a toggle the page lets go of after one frame: one press each, which is what the plugin counts (two presses inside one frame count once here). The Tap and Scatter buttons in the host row press the same two controls, the way a MIDI pad mapped to them would. The About block is not on the panel.',
  'A tap is the plugin’s work on one frame, and the page’s too. The plugin spreads it over eight threads by filing; the page does it on its one thread, which gives the same sheet (a Jacobi pass reads only its snapshot) but takes longer, and the picture waits for it. The line under the picture gives how long the last tap took. In Chrome on an Apple M4 Max a tap took 18 ms at the defaults (12,478 filings), about one frame, and 62 to 68 ms at 60,000 filings, a visible hitch; the plugin takes 2.6 ms at the defaults. A phone will be slower, by an amount not measured.',
  'The harness’s card, the first clip, is fgtest’s buildCard, ported in double and handed over as 32-bit floats as the harness does, so it is the clip under the README’s picture. The kit’s other clips are 8-bit and premultiplied, the kit’s model of what Resolume hands an effect; the plugin’s composite takes its input’s colour as straight and multiplies by alpha itself, so at a half-transparent edge of the transparency clip the colour here is weighted by alpha twice. Which of the two Resolume hands this plugin has not been measured. The plugin writes straight colour and the page’s canvas composites what it gets as premultiplied, so a half-covered pixel over a transparent part of a clip shows a little lighter over Behind than straight colour would.',
  'The host row, the line under the picture and the Presets menu are the page’s, not the plugin’s. The presets are whole states on top of the plugin’s defaults; all but the first press Scatter, so each starts from a fresh sheet, and all but the first tap on the beat.',
  'The output is the page’s 8-bit canvas. The plugin’s negative-control hooks (its harness’s deliberately wrong models) are off here, as in a shipped instance.',
  'The plugin’s harness, fgtest, is what checks the filings: the poles’ field against its closed form to 1e-15, a filing’s turn against an RK4 integration of its torque against friction to 1e-13 rad, a whole pass of the sheet against a second, brute-force coding of the same law to 1e-13, and the needles read off the picture against the dipole’s direction; it shows that over 600 frames of music with no kick, while every magnet moves, no filing moves by a bit, and it catches 18 wrong models and 21 mutants. The plugin has never been loaded into Resolume; the README’s Status says so.',
];

//===========================================================================
// Mount.
//===========================================================================
mounted = mountDemo({
  name: 'Filings',
  pluginId: 'FG01',
  tagline:
    'Iron filings on a sheet over your clip, magnets under it, and the music tapping it. Friction pins every filing until a tap; each tap turns them part way into the field, slides them towards the poles and chains them, so the field lines come up over a few taps. Move a magnet and nothing happens until the next one.',
  repo: 'https://github.com/stoatworks-labs/filings',

  blurb:
    'It is Filings’ own GLSL, ported from the repository to WebGL2 unedited, drawing a sheet of filings laid out by a JavaScript port of the plugin’s CPU half (the magnets’ field, the tap and the trigger) that only a reader checks, on generated clips in this page — no install. No audio reaches it, so Tap On’s default, Onset, never taps here: press Tap, or pick a beat.',

  needFloat: true,
  showBackdrop: true,

  // The harness's card first: the README's picture is on it.
  sources: ['card', 'scene', 'grid', 'bars', 'spot', 'alpha'],

  params: PARAMS,
  presets: PRESETS,
  differences: DIFFERENCES,
  createRenderer,
});

if (mounted && current) {
  buildHostRow();
  buildStatusLine();
}

//===========================================================================
// The host row: the stand-ins for what Resolume would hand the plugin.
//===========================================================================
function buildHostRow() {
  const transport = document.querySelector('.transport');
  if (!transport) return;
  const row = document.createElement('div');
  row.className = 'transport fg-host';
  const tag = document.createElement('span');
  tag.className = 'fg-host__tag';
  tag.textContent = 'The host';
  tag.title = 'Stand-ins for what Resolume would hand the plugin. The buttons press the plugin’s own Tap and Scatter controls.';

  const label = document.createElement('label');
  label.className = 'transport__field';
  label.title = 'Resolume’s BPM. The bar phase runs at it, in 4/4, on the page’s clock; Tap On’s beat modes follow it.';
  const span = document.createElement('span');
  span.className = 'transport__label';
  span.textContent = 'Tempo';
  const tempo = document.createElement('select');
  tempo.className = 'transport__select';
  for (const bpm of TEMPOS) tempo.append(new Option(`${bpm} BPM`, String(bpm)));
  tempo.value = String(host.bpm);
  tempo.addEventListener('change', () => {
    host.bpm = Number(tempo.value);
    mounted.redraw();
  });
  label.append(span, tempo);

  const pad = (text, id, title) => {
    const button = document.createElement('button');
    button.type = 'button';
    button.className = `btn fg-host__pad fg-host__pad--${id}`;
    button.textContent = text;
    button.title = title;
    button.addEventListener('click', () => mounted.params.set(id, 1));
    return button;
  };

  row.append(
    tag,
    label,
    pad('Tap', 'tap', 'Presses the plugin’s Tap: one tap now, in any Tap On mode.'),
    pad('Scatter', 'scatter', 'Presses the plugin’s Scatter: a fresh sheet from the next seed.'),
  );
  transport.after(row);
}

//===========================================================================
// The line under the picture. It reports; it measures nothing but the page.
//===========================================================================
function buildStatusLine() {
  const stage = document.querySelector('.stage');
  if (!stage) return;
  const line = document.createElement('p');
  line.className = 'stage__status fg-status';
  line.setAttribute('aria-live', 'off');
  const note = document.createElement('p');
  note.className = 'stage__status fg-note';
  stage.append(line, note);

  const update = () => {
    if (!current.frame) return;
    const p = mounted.params;
    const mode = OptionIndex(p.get('tapOn'), TapOn.Count);
    const count = FilingCount(DensityFromParam(f32(p.get('density'))), SizeFromParam(f32(p.get('size'))));
    const beats = { [TapOn.Beat]: 1, [TapOn.HalfBeat]: 0.5, [TapOn.TwoBeats]: 2, [TapOn.Bar]: 4 }[mode];
    const taps = `${current.taps} tap${current.taps === 1 ? '' : 's'} on this sheet`;
    const cost = current.lastTapMs === null ? '' : ` · the last took ${current.lastTapMs.toFixed(1)} ms on this page’s one thread`;
    const buffer = current.passes.floatBlend ? '' : ' · transmission in RGBA16F (this browser cannot blend into 32-bit floats; the plugin’s is RGBA32F)';
    line.textContent = `${count.toLocaleString('en-GB')} filings · ${taps}${cost}${buffer}`;
    if (mode === TapOn.Onset) {
      note.textContent = 'Tap On is Onset, the plugin’s default: it taps on hits in Resolume’s audio, and no audio reaches this page, so nothing taps the sheet but Tap. Press Tap (in the host row, or on the panel), or set Tap On to Beat, or pick a preset.';
    } else if (mode === TapOn.Off) {
      note.textContent = 'Tap On is Off: only Tap taps the sheet.';
    } else {
      const perSecond = host.bpm / 60 / beats;
      note.textContent = `Tap On ${kTapOnNames[mode]} at ${host.bpm} BPM: a tap every ${beats === 1 ? 'beat' : beats < 1 ? 'half beat' : beats === 2 ? 'two beats' : 'bar'}, ${perSecond.toFixed(perSecond < 1 ? 2 : 1)} a second, at ${TapStrengthFromParam(f32(p.get('tapStrength'))).toFixed(2)}× the reference.${mounted.state.playing ? '' : ' Paused: the beat stands still, and only Tap taps.'}`;
    }
  };
  update();
  setInterval(update, 250);
}

export { current, mounted };
