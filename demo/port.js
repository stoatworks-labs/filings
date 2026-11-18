/**
 * Filings' CPU half, ported to JavaScript by hand. ONLY A READER CHECKS IT.
 *
 * Nothing in the repository compares this file with the C++ it was written
 * from: demo/tools/check_shaders.py holds the shaders to the plugin's,
 * character for character, but this is a translation, and a translation that
 * is subtly wrong lays filings along a subtly wrong field, which nobody would
 * notice. It was compared with the C++ once, by hand, when the page was built
 * (AGENTS.md, "The browser demo", has the numbers); nothing repeats that. When
 * you change one of the files below, change its section here too.
 *
 *   Controls.cpp   the parameter table and every conversion to physical units
 *   Physics.cpp    the needle's constants, Osborn's factor, the closed-form
 *                  turn, the poles' field
 *   Sheet.cpp      the seeded scatter, the count, the tap and its two Jacobi
 *                  passes (chains, the slide, contact by capsule distance)
 *   Trigger.cpp    which frames tap, and how hard
 *   Filings.cpp    the CPU half of ProcessOpenGL: the frame's clock, the beat,
 *                  Params(), Scatter, the count, the trigger, the tap, the
 *                  instance upload and the draw's uniforms
 *
 * What is NOT here, because a page cannot have it or does not need it: the
 * audio analyser (Audio.cpp; no spectrum reaches a page, and with silence in
 * the buffer its flux is 0, under kFluxFloor, so it never fires and its Hit()
 * stays 0 -- that is what this hands the trigger), the host clock's
 * seconds-or-milliseconds vote (this host's unit is known: seconds), the
 * eight threads (a Jacobi pass reads only its snapshot, so one thread gives
 * the same sheet; Sheet.h says so), the log, and every negative-control hook
 * (all off in a shipped instance).
 *
 * Everything the plugin does in double is done here in double, in the same
 * order of operations, so the only differences are the ones a reader cannot
 * remove: clang fuses multiply-adds on arm64 (FMA, no -ffp-contract in
 * CMakeLists.txt) where JavaScript rounds twice, and V8's Math.cos, sin, tan,
 * atan, atan2, exp, log and pow are not Apple's libm. C++ `float` is mirrored
 * with Math.fround where the plugin stores or computes in float (its
 * parameters, the beat, what goes to the GPU). std::lround is half away from
 * zero, std::remainder rounds the quotient to even, and std::clamp, std::min
 * and std::max are comparisons; each is ported as such, not as the nearest
 * Math function.
 *
 * Pure: no DOM, no GL, so it runs under node as well as in the page.
 */

export const f32 = Math.fround;
/** std::clamp exactly: ( v < lo ) ? lo : ( hi < v ) ? hi : v. */
const clamp = (v, lo, hi) => (v < lo ? lo : hi < v ? hi : v);
/** std::min( a, b ) and std::max( a, b ): ( b < a ) ? b : a, ( a < b ) ? b : a. */
const min2 = (a, b) => (b < a ? b : a);
const max2 = (a, b) => (a < b ? b : a);
/** std::lround: half away from zero (Math.round alone rounds -2.5 to -2). */
export const lround = (v) => (v < 0 ? -Math.round(-v) : Math.round(v));
const now = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

//===========================================================================
// Physics.h / Physics.cpp
//===========================================================================

export const kPi = 3.14159265358979323846;
/** mu0, CODATA 2022 (NIST). */
export const kMu0 = 1.25663706127e-6;
/** Standard gravity, 3rd CGPM (1901). */
export const kGravity = 9.80665;
/** Iron at 20 degC, kg/m^3. */
export const kIronDensity = 7874.0;
/** A filing's length, m (assumed). */
export const kFilingLength = 1e-3;
/** Length over diameter (assumed). */
export const kAspect = 5.0;
/** The dose's reference field, T (chosen). */
export const kReferenceField = 4e-3;
/** A neighbour's reach, filing lengths, centre to centre. */
export const kReach = 1.5;
export const kMaxPoles = 4;

/** Osborn's axial demagnetising factor of a prolate spheroid of aspect R. */
export function AxialDemag(R) {
  const s = Math.sqrt(R * R - 1.0);
  return (1.0 / (R * R - 1.0)) * ((R / (2.0 * s)) * Math.log((R + s) / (R - s)) - 1.0);
}

export function MakeNeedle() {
  const n = {};
  n.length = kFilingLength;
  n.a = 0.5 * n.length;
  n.b = n.a / kAspect;
  n.volume = 4.0 / 3.0 * kPi * n.a * n.b * n.b;
  n.mass = kIronDensity * n.volume;
  n.na = AxialDemag(kAspect);
  n.nr = 0.5 * (1.0 - n.na);
  n.torquePerB2 = n.volume * (n.nr - n.na) / (2.0 * kMu0 * n.na * n.nr);
  n.chargePerB = kPi * n.b * n.b / (kMu0 * n.na);
  n.poleOffset = n.length / 3.0;
  // 2 A( Bref ) kt = 1: the reference tap divides tan psi by e.
  n.referenceDose = 1.0 / (2.0 * n.torquePerB2 * kReferenceField * kReferenceField);
  return n;
}

/** Needle::Torque (not linear: that is a negative control). */
export const Torque = (needle, field) => needle.torquePerB2 * field * field;
/** Needle::FrictionTorque, mu m g L / 4, and FrictionForce, mu m g. */
export const FrictionTorque = (needle, mu) => mu * needle.mass * kGravity * needle.length / 4.0;
export const FrictionForce = (needle, mu) => mu * needle.mass * kGravity;

/**
 * std::remainder( x, y ) for y > 0: x - n y, n the integer nearest x / y, ties
 * to even. Exact for |x| < 4y, which covers every call here (an angle less an
 * angle, each within ( -pi, pi ]): then n is -2..2, n y is exact, and x - n y
 * is exact by Sterbenz's lemma, so the comparisons below are exact too.
 */
export function remainder(x, y) {
  if (!Number.isFinite(x)) return NaN;
  let n = Math.round(x / y);
  let r = x - n * y;
  const half = 0.5 * y;
  if (r > half) { n += 1; r = x - n * y; } else if (r < -half) { n -= 1; r = x - n * y; }
  if ((r === half || r === -half) && n % 2 !== 0) {
    n += r > 0 ? 1 : -1;
    r = x - n * y;
  }
  return r;
}

/** psi into ( -pi/2, pi/2 ]: a needle has no north end. */
export function WrapHalf(angle) {
  let w = remainder(angle, kPi);
  if (w <= -0.5 * kPi) w += kPi;
  return w;
}

/** The angle a filing stops at, 1/2 asin( F / A ); pi/2 if it never turns. */
export function StopAngle(A, F) {
  if (A <= F) return 0.5 * kPi;
  return 0.5 * Math.asin(max2(0.0, F) / A);
}

/** The closed-form turn (Physics.cpp, Turn): see the C++ for the derivation. */
export function Turn(psi, A, F, kt) {
  const magnitude = Math.abs(psi);
  if (!(A > F) || !(kt > 0.0) || !(A * Math.sin(2.0 * magnitude) > F)) return psi;
  const f = max2(0.0, F);
  const R = Math.sqrt((A - f) * (A + f));
  const S = A + R;
  const uMinus = f / S;
  const u0 = Math.tan(magnitude);
  const E = Math.exp(-2.0 * R * kt);
  const num = uMinus * (f * u0 - S) - S * (u0 - uMinus) * E;
  const den = f * u0 - S - f * (u0 - uMinus) * E;
  const turned = Math.atan(num / den);
  return psi < 0.0 ? -turned : turned;
}

/** Magnets: up to four poles, Strength b0 (T) and Depth (su). */
export function MakeMagnets() {
  return {
    poles: Array.from({ length: kMaxPoles }, () => ({ x: 0.0, y: 0.0, sign: 1.0 })),
    count: 2,
    b0: 0.04,
    depth: 0.12,
  };
}

/** The magnets' field on the sheet at ( x, y ), su; written into `out`. */
export function FieldAt(magnets, x, y, out) {
  let fx = 0.0, fy = 0.0, fz = 0.0;
  const d = magnets.depth;
  const count = clamp(magnets.count, 0, kMaxPoles);
  for (let i = 0; i < count; ++i) {
    const p = magnets.poles[i];
    const dx = x - p.x, dy = y - p.y;
    const r2 = dx * dx + dy * dy + d * d;
    const inv = 1.0 / (r2 * Math.sqrt(r2));
    const s = p.sign * magnets.b0 * d * d * inv;
    fx += s * dx;
    fy += s * dy;
    fz += s * d;
  }
  out.x = fx;
  out.y = fy;
  out.z = fz;
  return out;
}

//===========================================================================
// Sheet.h / Sheet.cpp
//===========================================================================

/** PCG's output hash, in uint32 arithmetic. */
export function pcg(v) {
  const state = (Math.imul(v, 747796405) + 2891336453) >>> 0;
  const word = Math.imul(((state >>> ((state >>> 28) + 4)) ^ state) >>> 0, 277803737) >>> 0;
  return ((word >>> 22) ^ word) >>> 0;
}

/**
 * A double in [ 0, 1 ) from two hashes, 53 bits. ( hi << 21 ) ^ ( lo >> 11 )
 * in uint64: the two parts do not overlap, so it is hi x 2^21 + ( lo >> 11 ),
 * under 2^53 and exact in a double; the 53-bit mask changes nothing.
 */
export function unit(seed, k, channel) {
  const hi = pcg((Math.imul(seed, 0x9E3779B9) ^ pcg((Math.imul(k, 3) + channel) >>> 0)) >>> 0);
  const lo = pcg((hi ^ 0x85EBCA6B) >>> 0);
  return (hi * 2097152 + (lo >>> 11)) * (1.0 / 9007199254740992.0);
}

/** Sheet.cpp's segmentDistance: the closest points of two segments. */
function segmentDistance(px, py, d1x, d1y, qx, qy, d2x, d2y, h, out) {
  const ax = px - h * d1x, ay = py - h * d1y;
  const bx = qx - h * d2x, by = qy - h * d2y;
  const e1x = 2.0 * h * d1x, e1y = 2.0 * h * d1y;
  const e2x = 2.0 * h * d2x, e2y = 2.0 * h * d2y;
  const rx = ax - bx, ry = ay - by;
  const a = e1x * e1x + e1y * e1y;
  const e = e2x * e2x + e2y * e2y;
  const f = e2x * rx + e2y * ry;
  let s = 0.0, t = 0.0;
  if (a <= 1e-30 && e <= 1e-30) {
    s = t = 0.0;
  } else if (a <= 1e-30) {
    s = 0.0;
    t = clamp(f / e, 0.0, 1.0);
  } else {
    const c = e1x * rx + e1y * ry;
    if (e <= 1e-30) {
      t = 0.0;
      s = clamp(-c / a, 0.0, 1.0);
    } else {
      const b = e1x * e2x + e1y * e2y;
      const denom = a * e - b * b;
      s = denom > 1e-30 * a * e ? clamp((b * f - c * e) / denom, 0.0, 1.0) : 0.5;
      t = (b * s + f) / e;
      if (t < 0.0) {
        t = 0.0;
        s = clamp(-c / a, 0.0, 1.0);
      } else if (t > 1.0) {
        t = 1.0;
        s = clamp((b - c) / a, 0.0, 1.0);
      }
    }
  }
  out.cpx = ax + e1x * s;
  out.cpy = ay + e1y * s;
  out.cqx = bx + e2x * t;
  out.cqy = by + e2y * t;
  const dx = out.cpx - out.cqx, dy = out.cpy - out.cqy;
  return dx * dx + dy * dy;
}

/**
 * The sheet: struct-of-arrays where the C++ has a vector of Filing, so a pass
 * over 60,000 filings is typed-array work. Same fields, same order, same
 * double precision.
 */
export class Sheet {
  static kHalfWidth = 1.0;
  static kHalfHeight = 0.5;
  static kPassesPerTap = 2;
  static kMaxFilings = 60000;

  constructor() {
    this.needle = MakeNeedle();
    const N = Sheet.kMaxFilings;
    this.x = new Float64Array(N);
    this.y = new Float64Array(N);
    this.theta = new Float64Array(N);
    this.nx = new Float64Array(N);
    this.ny = new Float64Array(N);
    this.ntheta = new Float64Array(N);
    this.n = 0;
    // The snapshot, rebuilt each pass.
    this.s = {
      hx: new Float64Array(N), hy: new Float64Array(N), tx: new Float64Array(N), ty: new Float64Array(N),
      ux: new Float64Array(N), uy: new Float64Array(N), q: new Float64Array(N),
      bhx: new Float64Array(N), bhy: new Float64Array(N), btx: new Float64Array(N), bty: new Float64Array(N),
    };
    this.cellStart = new Int32Array(1);
    this.cellItems = new Int32Array(N);
    this.cellsX = 0;
    this.cellsY = 0;
    this.cellSize = 1.0;
    this.seed = 1;
    this.taps = 0;
    this.generation = 0;
    this.turned = 0;
    this.moved = 0;
    this.Scatter(1, 0);
  }

  /** Where filing k of the seeded sequence starts. */
  static Scattered(seed, k, out) {
    const index = k >>> 0;
    out.x = (2.0 * unit(seed, index, 0) - 1.0) * Sheet.kHalfWidth;
    out.y = (2.0 * unit(seed, index, 1) - 1.0) * Sheet.kHalfHeight;
    out.theta = WrapHalf((unit(seed, index, 2) - 0.5) * kPi);
    return out;
  }

  static MetresPerSu(length) {
    return kFilingLength / length;
  }

  Scatter(newSeed, count) {
    this.seed = newSeed >>> 0;
    this.n = 0;
    this.SetCount(count);
    ++this.generation;
  }

  SetCount(count) {
    count = clamp(count, 0, Sheet.kMaxFilings);
    const have = this.n;
    if (count === have) return;
    if (count < have) {
      this.n = count;
    } else {
      const f = { x: 0, y: 0, theta: 0 };
      for (let k = have; k < count; ++k) {
        Sheet.Scattered(this.seed, k, f);
        this.x[k] = f.x;
        this.y[k] = f.y;
        this.theta[k] = f.theta;
      }
      this.n = count;
    }
    ++this.generation;
  }

  /** One tap, `dose` times the reference: two passes. */
  Tap(dose, params) {
    if (!(dose > 0.0)) return;
    ++this.taps;
    const kt = dose * this.needle.referenceDose / Sheet.kPassesPerTap;
    for (let p = 0; p < Sheet.kPassesPerTap; ++p) this.Pass(kt, params);
  }

  Pass(kt, params) {
    const n = this.n;
    this.turned = this.moved = 0;
    if (n === 0) return;
    const { x: X, y: Y, theta: TH, s, needle } = this;
    const L = max2(params.length, 1e-6);
    const offset = L / 3.0;
    const bh = { x: 0, y: 0, z: 0 }, bt = { x: 0, y: 0, z: 0 };
    for (let i = 0; i < n; ++i) {
      const ux = Math.cos(TH[i]);
      const uy = Math.sin(TH[i]);
      s.ux[i] = ux;
      s.uy[i] = uy;
      const hx = X[i] + offset * ux, hy = Y[i] + offset * uy;
      const tx = X[i] - offset * ux, ty = Y[i] - offset * uy;
      s.hx[i] = hx; s.hy[i] = hy; s.tx[i] = tx; s.ty[i] = ty;
      FieldAt(params.magnets, hx, hy, bh);
      FieldAt(params.magnets, tx, ty, bt);
      s.bhx[i] = bh.x; s.bhy[i] = bh.y; s.btx[i] = bt.x; s.bty[i] = bt.y;
      const axial = 0.5 * ((bh.x + bt.x) * ux + (bh.y + bt.y) * uy);
      s.q[i] = needle.chargePerB * axial;
    }

    // The cells, by counting sort on the snapshot's centres.
    const hw = Sheet.kHalfWidth, hh = Sheet.kHalfHeight;
    const cellSize = kReach * L;
    const cellsX = max2(1, Math.trunc(Math.ceil(2.0 * hw / cellSize)));
    const cellsY = max2(1, Math.trunc(Math.ceil(2.0 * hh / cellSize)));
    this.cellSize = cellSize;
    this.cellsX = cellsX;
    this.cellsY = cellsY;
    const cellCount = cellsX * cellsY;
    if (this.cellStart.length < cellCount + 1) this.cellStart = new Int32Array(cellCount + 1);
    const cellStart = this.cellStart;
    cellStart.fill(0, 0, cellCount + 1);
    const cellItems = this.cellItems;
    const cellOf = (i) => {
      const cx = clamp(Math.trunc(Math.floor((X[i] + hw) / cellSize)), 0, cellsX - 1);
      const cy = clamp(Math.trunc(Math.floor((Y[i] + hh) / cellSize)), 0, cellsY - 1);
      return cy * cellsX + cx;
    };
    for (let i = 0; i < n; ++i) ++cellStart[cellOf(i) + 1];
    for (let c = 0; c < cellCount; ++c) cellStart[c + 1] += cellStart[c];
    {
      const fill = cellStart.slice(0, cellCount);
      for (let i = 0; i < n; ++i) cellItems[fill[cellOf(i)]++] = i;
    }

    // next = filings
    this.nx.set(X.subarray(0, n));
    this.ny.set(Y.subarray(0, n));
    this.ntheta.set(TH.subarray(0, n));
    this.passRange(0, n, kt, params);

    // filings.swap( next )
    [this.x, this.nx] = [this.nx, this.x];
    [this.y, this.ny] = [this.ny, this.y];
    [this.theta, this.ntheta] = [this.ntheta, this.theta];
    ++this.generation;
  }

  passRange(begin, end, kt, params) {
    const { x: X, y: Y, theta: TH, nx: OX, ny: OY, ntheta: OT, s, needle } = this;
    const { cellStart, cellItems, cellsX, cellsY, cellSize } = this;
    const hw = Sheet.kHalfWidth, hh = Sheet.kHalfHeight;
    const L = max2(params.length, 1e-6);
    const metres = Sheet.MetresPerSu(L);
    const reach = kReach * L;
    const reach2 = reach * reach;
    const b = needle.b;
    const b2 = b * b;
    const bSu = b / metres;
    const core = (needle.a - needle.b) / metres;
    const k = kMu0 / (4.0 * kPi);
    const F = FrictionTorque(needle, params.friction);
    const fForce = FrictionForce(needle, params.friction);
    const ltOver = kt * needle.length * needle.length / 12.0;
    const touch = 2.0 * bSu;
    const LL = L * L;
    const seg = { cpx: 0, cpy: 0, cqx: 0, cqy: 0 };
    let myTurned = 0, myMoved = 0;

    for (let i = begin; i < end; ++i) {
      const fx0 = X[i], fy0 = Y[i];
      const shx = s.hx[i], shy = s.hy[i], stx = s.tx[i], sty = s.ty[i];
      const sux = s.ux[i], suy = s.uy[i];
      let bhx = s.bhx[i], bhy = s.bhy[i], btx = s.btx[i], bty = s.bty[i];
      let pushX = 0.0, pushY = 0.0;

      const cx = clamp(Math.trunc(Math.floor((fx0 + hw) / cellSize)), 0, cellsX - 1);
      const cy = clamp(Math.trunc(Math.floor((fy0 + hh) / cellSize)), 0, cellsY - 1);
      for (let oy = -1; oy <= 1; ++oy) {
        for (let ox = -1; ox <= 1; ++ox) {
          const ncx = cx + ox, ncy = cy + oy;
          if (ncx < 0 || ncy < 0 || ncx >= cellsX || ncy >= cellsY) continue;
          const cell = ncy * cellsX + ncx;
          for (let at = cellStart[cell]; at < cellStart[cell + 1]; ++at) {
            const j = cellItems[at];
            if (j === i) continue;
            const gx = X[j], gy = Y[j];
            const dx = gx - fx0, dy = gy - fy0;
            const d2 = dx * dx + dy * dy;
            if (d2 >= reach2) continue;
            // chains
            {
              const w = (1.0 - d2 / reach2) * (1.0 - d2 / reach2);
              const kq = k * w * s.q[j];
              const ohx = s.hx[j], ohy = s.hy[j], otx = s.tx[j], oty = s.ty[j];
              // Its head (+q) and tail (-q) at my head and my tail: add() inlined.
              let rx, ry, r2, c;
              rx = (shx - ohx) * metres; ry = (shy - ohy) * metres;
              r2 = rx * rx + ry * ry + b2; c = kq / (r2 * Math.sqrt(r2));
              bhx += c * rx; bhy += c * ry;
              rx = (shx - otx) * metres; ry = (shy - oty) * metres;
              r2 = rx * rx + ry * ry + b2; c = -kq / (r2 * Math.sqrt(r2));
              bhx += c * rx; bhy += c * ry;
              rx = (stx - ohx) * metres; ry = (sty - ohy) * metres;
              r2 = rx * rx + ry * ry + b2; c = kq / (r2 * Math.sqrt(r2));
              btx += c * rx; bty += c * ry;
              rx = (stx - otx) * metres; ry = (sty - oty) * metres;
              r2 = rx * rx + ry * ry + b2; c = -kq / (r2 * Math.sqrt(r2));
              btx += c * rx; bty += c * ry;
            }
            // Two capsules can only touch with their centres closer than L.
            if (d2 < LL) {
              const dist2 = segmentDistance(fx0, fy0, sux, suy, gx, gy, s.ux[j], s.uy[j], core, seg);
              if (dist2 < touch * touch) {
                const dist = Math.sqrt(dist2);
                let nxv = seg.cpx - seg.cqx, nyv = seg.cpy - seg.cqy;
                if (dist > 1e-15) {
                  nxv /= dist;
                  nyv /= dist;
                } else {
                  // Crossing exactly: out sideways, the lower index to the left.
                  nxv = i < j ? -suy : suy;
                  nyv = i < j ? sux : -sux;
                }
                pushX += 0.5 * (touch - dist) * nxv;
                pushY += 0.5 * (touch - dist) * nyv;
              }
            }
          }
        }
      }

      // The turn, in the mean of the fields at the two poles.
      const bx = 0.5 * (bhx + btx), by = 0.5 * (bhy + bty);
      const bm = Math.sqrt(bx * bx + by * by);
      let loose = false;
      if (bm > 0.0) {
        const A = Torque(needle, bm);
        const phi = Math.atan2(by, bx);
        const psi = WrapHalf(TH[i] - phi);
        const after = Turn(psi, A, F, kt);
        if (after !== psi) {
          OT[i] = WrapHalf(phi + after);
          loose = true;
        }
      }
      if (loose) ++myTurned;

      // The slide: the force on the two poles.
      let moveX = 0.0, moveY = 0.0;
      let slid = false;
      {
        const qi = s.q[i];
        const fx = qi * (bhx - btx), fy = qi * (bhy - bty);
        const fm = Math.sqrt(fx * fx + fy * fy);
        if (fm > fForce) {
          let step = ltOver * (fm - fForce) / metres;
          step = min2(step, bSu);
          moveX = 1.0 * step * fx / fm;
          moveY = 1.0 * step * fy / fm;
          slid = true;
          loose = true;
        }
      }
      if (loose && (pushX !== 0.0 || pushY !== 0.0)) {
        const pm = Math.sqrt(pushX * pushX + pushY * pushY);
        const scale = pm > bSu ? bSu / pm : 1.0;
        moveX += pushX * scale;
        moveY += pushY * scale;
        slid = true;
      }
      if (slid) {
        OX[i] = clamp(fx0 + moveX, -hw, hw);
        OY[i] = clamp(fy0 + moveY, -hh, hh);
        ++myMoved;
      }
    }
    this.turned = myTurned;
    this.moved = myMoved;
  }
}

//===========================================================================
// Controls.h / Controls.cpp
//===========================================================================

/** Every control, by what it is (ParamId). The About block follows. */
export const PT = {
  POLES: 0, STRENGTH: 1, DEPTH: 2,
  POLE1_X: 3, POLE1_Y: 4, POLE1_SIGN: 5,
  POLE2_X: 6, POLE2_Y: 7, POLE2_SIGN: 8,
  POLE3_X: 9, POLE3_Y: 10, POLE3_SIGN: 11,
  POLE4_X: 12, POLE4_Y: 13, POLE4_SIGN: 14,
  AUDIO: 15, TAP_ON: 16, TAP: 17, SENSITIVITY: 18, TAP_STRENGTH: 19,
  FRICTION: 20, DENSITY: 21, SIZE: 22, SCATTER: 23,
  TONE: 24, MIX: 25,
  ABOUT_TEXT: 26,
};

export const Kind = { Standard: 'standard', Option: 'option', Event: 'event', Buffer: 'buffer' };

export const TapOn = { Onset: 0, Beat: 1, HalfBeat: 2, TwoBeats: 3, Bar: 4, Off: 5, Count: 6 };

function clamp01(v) {
  // std::clamp( static_cast< double >( v ), 0.0, 1.0 ): a NaN passes through.
  return clamp(v, 0.0, 1.0);
}
function geometric(v, low, high) {
  return low * Math.pow(high / low, clamp01(v));
}
function inverseGeometric(value, low, high) {
  const lo = min2(low, high), hi = max2(low, high);
  return f32(Math.log(clamp(value, lo, hi) / low) / Math.log(high / low));
}
function linear(v, low, high) {
  return low + (high - low) * clamp01(v);
}
function inverseLinear(value, low, high) {
  return f32(clamp((value - low) / (high - low), 0.0, 1.0));
}

const kStrengthLow = 0.002, kStrengthHigh = 0.1;
const kDepthLow = 0.01, kDepthHigh = 0.25;
const kTapHigh = 2.0;
const kFrictionLow = 0.05, kFrictionHigh = 1.0;
const kDensityLow = 0.05, kDensityHigh = 0.6;
const kSizeLow = 1.0 / 200.0, kSizeHigh = 1.0 / 30.0;

export const kPoleNames = ['1', '2', '3', '4'];
export const kSignNames = ['N', 'S'];
export const kTapOnNames = ['Onset', 'Beat', 'Half Beat', '2 Beats', 'Bar', 'Off'];

export function StrengthFromParam(v) { return geometric(v, kStrengthLow, kStrengthHigh); }
export function ParamFromStrength(t) { return inverseGeometric(t, kStrengthLow, kStrengthHigh); }
export function DepthFromParam(v) { return geometric(v, kDepthLow, kDepthHigh); }
export function ParamFromDepth(su) { return inverseGeometric(su, kDepthLow, kDepthHigh); }
export function TapStrengthFromParam(v) { return linear(v, 0.0, kTapHigh); }
export function ParamFromTapStrength(dose) { return inverseLinear(dose, 0.0, kTapHigh); }
export function FrictionFromParam(v) { return geometric(v, kFrictionLow, kFrictionHigh); }
export function ParamFromFriction(mu) { return inverseGeometric(mu, kFrictionLow, kFrictionHigh); }
export function DensityFromParam(v) { return linear(v, kDensityLow, kDensityHigh); }
export function ParamFromDensity(c) { return inverseLinear(c, kDensityLow, kDensityHigh); }
export function SizeFromParam(v) { return geometric(v, kSizeLow, kSizeHigh); }
export function ParamFromSize(su) { return inverseGeometric(su, kSizeLow, kSizeHigh); }

/** How many filings cover `coverage` of the sheet, each `length` su long. */
export function FilingCount(coverage, length) {
  const area = 4.0 * Sheet.kHalfWidth * Sheet.kHalfHeight;
  const a = 0.5 * length, b = a / kAspect;
  const filing = kPi * a * b;
  // std::round: half away from zero, which Math.round is for a positive count.
  const count = Math.round(max2(0.0, coverage) * area / max2(filing, 1e-12));
  return Math.trunc(min2(count, Sheet.kMaxFilings));
}

/** The filings' grey (linear light) at Tone t: t x kInkGrey. */
export const kInkGrey = 0.45;

/** An option's index from the value the host stored. */
export function OptionIndex(value, count) {
  const i = lround(value);
  return clamp(i, 0, count - 1);
}

export function PoleX(i) { return PT.POLE1_X + 3 * clamp(i, 0, 3); }
export function PoleY(i) { return PoleX(i) + 1; }
export function PoleSign(i) { return PoleX(i) + 2; }

const standard = (id, name, group, value) => ({ id, name, group, kind: Kind.Standard, defaultValue: f32(value), options: null });
const option = (id, name, group, value, names) => ({ id, name, group, kind: Kind.Option, defaultValue: f32(value), options: names });
const event = (id, name, group) => ({ id, name, group, kind: Kind.Event, defaultValue: 0, options: null });

// The defaults: a bar magnet (N left, S right) a third of the frame apart,
// 40 mT straight above each pole at 12% of the frame's height under the sheet;
// onsets tap at the reference dose; mu 0.2; filings a seventieth of the frame
// long covering 20% of the sheet, drawn a dark grey (Tone 0.15).
const kTable = [
  option(PT.POLES, 'Poles', 'Magnets', 1.0, kPoleNames),
  standard(PT.STRENGTH, 'Strength', 'Magnets', ParamFromStrength(0.04)),
  standard(PT.DEPTH, 'Depth', 'Magnets', ParamFromDepth(0.12)),
  standard(PT.POLE1_X, 'Pole 1 X', 'Magnets', 0.35),
  standard(PT.POLE1_Y, 'Pole 1 Y', 'Magnets', 0.5),
  option(PT.POLE1_SIGN, 'Pole 1 Sign', 'Magnets', 0.0, kSignNames),
  standard(PT.POLE2_X, 'Pole 2 X', 'Magnets', 0.65),
  standard(PT.POLE2_Y, 'Pole 2 Y', 'Magnets', 0.5),
  option(PT.POLE2_SIGN, 'Pole 2 Sign', 'Magnets', 1.0, kSignNames),
  standard(PT.POLE3_X, 'Pole 3 X', 'Magnets', 0.5),
  standard(PT.POLE3_Y, 'Pole 3 Y', 'Magnets', 0.82),
  option(PT.POLE3_SIGN, 'Pole 3 Sign', 'Magnets', 0.0, kSignNames),
  standard(PT.POLE4_X, 'Pole 4 X', 'Magnets', 0.5),
  standard(PT.POLE4_Y, 'Pole 4 Y', 'Magnets', 0.18),
  option(PT.POLE4_SIGN, 'Pole 4 Sign', 'Magnets', 1.0, kSignNames),

  { id: PT.AUDIO, name: 'Audio', group: 'Tap', kind: Kind.Buffer, defaultValue: 0, options: null },
  option(PT.TAP_ON, 'Tap On', 'Tap', 0.0, kTapOnNames),
  event(PT.TAP, 'Tap', 'Tap'),
  standard(PT.SENSITIVITY, 'Sensitivity', 'Tap', 0.5),
  standard(PT.TAP_STRENGTH, 'Tap Strength', 'Tap', ParamFromTapStrength(1.0)),

  standard(PT.FRICTION, 'Friction', 'Filings', ParamFromFriction(0.2)),
  standard(PT.DENSITY, 'Density', 'Filings', ParamFromDensity(0.2)),
  standard(PT.SIZE, 'Size', 'Filings', ParamFromSize(1.0 / 70.0)),
  event(PT.SCATTER, 'Scatter', 'Filings'),

  standard(PT.TONE, 'Tone', 'Look', 0.15),
  standard(PT.MIX, 'Mix', 'Look', 1.0),
];
if (kTable.length !== PT.ABOUT_TEXT || kTable.some((row, i) => row.id !== i)) {
  throw new Error('every control needs a row in the table, in ParamId order');
}

/** The table, indexed by ParamId, for every id below PT.ABOUT_TEXT. */
export function InfoOf(id) {
  return kTable[min2(id, PT.ABOUT_TEXT - 1)];
}

//===========================================================================
// Trigger.h / Trigger.cpp
//===========================================================================

export class Trigger {
  constructor() {
    this.lastPhase = -1.0;
  }

  Reset() {
    this.lastPhase = -1.0;
  }

  /** This frame's dose, 0 for no tap. */
  Advance(input) {
    let dose = 0.0;
    if (input.presses > 0) dose = 1.0;
    if (input.on === TapOn.Onset && input.onset) dose = max2(dose, clamp(input.kick, 0.0, 1.0));

    const tempo = input.bpm > 1.0 ? input.bpm : 120.0;
    // std::fmod, which JavaScript's % is for doubles (exact).
    const phaseNow = input.hostBeat ? clamp(input.barPhase, 0.0, 1.0) : (input.now * tempo / 240.0) % 1.0;
    let step = 0.0;
    switch (input.on) {
      case TapOn.Beat: step = 0.25; break;
      case TapOn.HalfBeat: step = 0.125; break;
      case TapOn.TwoBeats: step = 0.5; break;
      case TapOn.Bar: step = 1.0; break;
      default: break;
    }
    if (step > 0.0 && this.lastPhase >= 0.0 && input.dt > 0.0) {
      const p0 = this.lastPhase;
      let p1 = phaseNow;
      if (p1 < p0) p1 += 1.0;
      // A boundary in ( p0, p1 ]: the frame it falls in taps.
      if (p1 > p0 && (Math.floor(p0 / step + 1e-9) + 1.0) * step <= p1 + 1e-12) dose = max2(dose, 1.0);
    }
    this.lastPhase = phaseNow;
    return dose;
  }
}

//===========================================================================
// Filings.cpp: the CPU half of FilingsPlugin.
//===========================================================================

/** Host seconds a single frame may advance by. */
const kMaxFrameDelta = 0.1;

export class FilingsPlugin {
  constructor() {
    /** params[]: what the host stored, in float. */
    this.params = new Float64Array(PT.ABOUT_TEXT);
    for (let id = 0; id < PT.ABOUT_TEXT; ++id) this.params[id] = InfoOf(id).defaultValue;

    this.sheet = new Sheet();
    this.trigger = new Trigger();
    this.lastDose = 0.0;
    this.scatterSeed = 1;

    // The clock: seconds, as this host hands them (the vote is not ported).
    this.now = 0.0;
    this.lastNow = -1.0;

    this.bpm = f32(120.0);
    this.barPhase = f32(0.0);
    this.lastBarPhase = f32(-1.0);
    this.hostBeatSeen = false;

    this.tapPresses = 0;
    this.tapHeld = false;
    this.scatterPresses = 0;
    this.scatterHeld = false;

    this.lastWidth = 0;
    this.lastHeight = 0;

    /** The page's own measurement of the last tap, ms; not the plugin's. */
    this.lastTapMs = 0;
    this.instances = new Float32Array(0);
    this.uploadedGeneration = -1;
  }

  /** Tap and Scatter act on the press, once (SetFloatParameter). */
  SetFloatParameter(index, value) {
    if (!(index >= 0 && index < PT.ABOUT_TEXT)) return false;
    value = f32(value);
    if (index === PT.TAP || index === PT.SCATTER) {
      const down = value >= 0.5;
      if (index === PT.TAP) {
        if (down && !this.tapHeld) ++this.tapPresses;
        this.tapHeld = down;
      } else {
        if (down && !this.scatterHeld) ++this.scatterPresses;
        this.scatterHeld = down;
      }
    }
    this.params[index] = value;
    return true;
  }

  GetFloatParameter(index) {
    return index >= 0 && index < PT.ABOUT_TEXT ? this.params[index] : 0.0;
  }

  SetBeatInfo(newBpm, newBarPhase) {
    newBpm = f32(newBpm);
    newBarPhase = f32(newBarPhase);
    if (newBpm > 0.0 && Number.isFinite(newBpm)) this.bpm = newBpm;
    if (Number.isFinite(newBarPhase)) this.barPhase = f32(newBarPhase - Math.floor(newBarPhase));
  }

  /** The physics the controls ask for, for a frame width x height. */
  Params(width, height) {
    const params = this.params;
    const p = { length: SizeFromParam(params[PT.SIZE]), friction: FrictionFromParam(params[PT.FRICTION]), magnets: MakeMagnets() };
    const m = p.magnets;
    m.count = OptionIndex(params[PT.POLES], kMaxPoles) + 1;
    m.b0 = StrengthFromParam(params[PT.STRENGTH]);
    m.depth = DepthFromParam(params[PT.DEPTH]);
    const aspect = height > 0 ? width / height : 16.0 / 9.0;
    for (let i = 0; i < kMaxPoles; ++i) {
      m.poles[i].x = (clamp(params[PoleX(i)], 0.0, 1.0) - 0.5) * aspect;
      m.poles[i].y = clamp(params[PoleY(i)], 0.0, 1.0) - 0.5;
      m.poles[i].sign = OptionIndex(params[PoleSign(i)], 2) === 0 ? 1.0 : -1.0;
    }
    return p;
  }

  /**
   * ProcessOpenGL's CPU half, for a host time `time` (seconds) and a frame
   * width x height. Returns what render.js needs to draw it.
   */
  ProcessFrame(time, width, height) {
    // Time and the beat. (UpdateAudio: silence; the analyser never fires.)
    this.now = time;
    const dt = this.lastNow >= 0.0 ? clamp(this.now - this.lastNow, 0.0, kMaxFrameDelta) : 0.0;
    this.lastNow = this.now;
    if (this.barPhase !== this.lastBarPhase && this.lastBarPhase >= 0.0) this.hostBeatSeen = true;
    this.lastBarPhase = this.barPhase;

    // The sheet. Scatter first, then the count, then the frame's tap.
    const sp = this.Params(width, height);
    const count = FilingCount(DensityFromParam(this.params[PT.DENSITY]), sp.length);
    if (this.scatterPresses > 0) {
      this.scatterSeed = (this.scatterSeed + this.scatterPresses) >>> 0;
      this.sheet.Scatter(this.scatterSeed, count);
      this.scatterPresses = 0;
    }
    this.lastWidth = width;
    this.lastHeight = height;
    this.sheet.SetCount(count);

    const input = {
      on: OptionIndex(this.params[PT.TAP_ON], TapOn.Count),
      presses: this.tapPresses,
      onset: false,
      kick: 0.0,
      bpm: this.bpm,
      barPhase: this.barPhase,
      hostBeat: this.hostBeatSeen,
      now: this.now,
      dt,
    };
    this.tapPresses = 0;
    this.lastDose = this.trigger.Advance(input) * TapStrengthFromParam(this.params[PT.TAP_STRENGTH]);

    let tapped = false;
    if (this.lastDose > 0.0) {
      const started = now();
      this.sheet.Tap(this.lastDose, sp);
      this.lastTapMs = now() - started;
      tapped = true;
    }

    // render(): the uniforms, and upload() when the sheet changed.
    const length = SizeFromParam(this.params[PT.SIZE]);
    const halfA = 0.5 * length * height;
    const halfB = halfA / kAspect;
    const rebuilt = this.upload();
    return {
      tapped,
      dose: this.lastDose,
      count: this.sheet.n,
      rebuilt,
      instances: this.instances,
      centre: [f32(0.5 * width), f32(0.5 * height)],
      scale: f32(height),
      half: [f32(halfA), f32(halfB)],
      size: [f32(width), f32(height)],
      ink: f32(clamp(this.params[PT.TONE], 0.0, 1.0) * kInkGrey),
      mix: f32(clamp(this.params[PT.MIX], 0.0, 1.0)),
    };
  }

  /** upload(): x, y, cos, sin as floats, only when the sheet changed. */
  upload() {
    const sheet = this.sheet;
    if (sheet.generation === this.uploadedGeneration) return false;
    const n = sheet.n;
    if (this.instances.length !== n * 4) this.instances = new Float32Array(n * 4);
    const out = this.instances;
    for (let i = 0; i < n; ++i) {
      out[4 * i + 0] = sheet.x[i];
      out[4 * i + 1] = sheet.y[i];
      out[4 * i + 2] = Math.cos(sheet.theta[i]);
      out[4 * i + 3] = Math.sin(sheet.theta[i]);
    }
    this.uploadedGeneration = sheet.generation;
    return true;
  }
}
