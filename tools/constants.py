#!/usr/bin/env python3
"""Every physical number filings uses, computed from its source.

Nothing here is typed in from memory. Each block names the published formula or
figure it starts from (checked against the source on 2026-11-16; see
ATTRIBUTIONS.md and the spec), computes what source/Physics.h and Physics.cpp
embed, and prints both so that a reader can diff them. The harness re-derives
the demagnetising factors in C++ from the ellipsoid's integral under
`fgtest --law`; this script is the second derivation, in a different language,
from the closed form.

Run: python3 tools/constants.py
"""
import math

# ---------------------------------------------------------------------------
# Constants.
# mu0: CODATA 2022 recommended value (NIST, physics.nist.gov/cgi-bin/cuu/Value?mu0).
MU0 = 1.25663706127e-6  # N/A^2
# Standard gravity: 3rd CGPM (1901), Declaration 2: 980.665 cm/s^2.
G_N = 9.80665  # m/s^2
# Iron at 20 degC: Wikipedia, "Iron", infobox: 7.874 g/cm^3.
RHO_FE = 7874.0  # kg/m^3

# ---------------------------------------------------------------------------
# The filing. Assumptions, not citations: 1 mm long, a prolate spheroid of
# aspect 5 (spec, "The filing").
L = 1e-3  # m
R = 5.0
a = L / 2.0
b = a / R
V = 4.0 / 3.0 * math.pi * a * b * b
m = RHO_FE * V

# Osborn, Phys. Rev. 67, 351 (1945), as reproduced in Huillery et al.,
# arXiv:1903.09699v3 below eq. (1), and Nahlawi et al., arXiv:1904.11562v2,
# eq. (A.5):
#   n_a = 1/(R^2 - 1) [ R / (2 sqrt(R^2 - 1)) ln((R + sqrt(R^2 - 1))/(R - sqrt(R^2 - 1))) - 1 ]
s = math.sqrt(R * R - 1.0)
n_a = (1.0 / (R * R - 1.0)) * (R / (2.0 * s) * math.log((R + s) / (R - s)) - 1.0)
n_r = 0.5 * (1.0 - n_a)

# Huillery et al. eq. (1): T = A sin 2 psi, A = V (n_r - n_a) B^2 / (2 mu0 n_a n_r).
A_PER_B2 = V * (n_r - n_a) / (2.0 * MU0 * n_a * n_r)  # N m / T^2


def friction_torque(mu):
    """Kinetic friction mu m g spread uniformly along the length: mu m g L / 4."""
    return mu * m * G_N * L / 4.0


def threshold_field(mu):
    """The field below which the largest torque, A at psi = 45 deg, never beats friction."""
    return math.sqrt(friction_torque(mu) / A_PER_B2)


def stop_angle(B, mu):
    """Where a filing stops turning: 1/2 asin(F/A)."""
    A = A_PER_B2 * B * B
    F = friction_torque(mu)
    return 0.5 * math.asin(F / A) if A > F else math.pi / 2.0


# The formula holds below B < mu0 m_s sqrt(2) n_a n_r / sqrt(n_a^2 + n_r^2)
# (Huillery et al.); iron alloys saturate at mu0 m_s = 1.6-2.2 T (Laughton &
# Warne via Wikipedia, "Saturation (magnetic)"): the lower end is the bound.
B_SAT_LIMIT = 1.6 * math.sqrt(2.0) * n_a * n_r / math.sqrt(n_a * n_a + n_r * n_r)

# The reference tap (chosen): one full hit at Tap Strength 1 turns a
# frictionless filing in a 4 mT field by tan psi -> tan psi / e: 2 A k t = 1.
B_REF = 4e-3
REF_DOSE = 1.0 / (2.0 * A_PER_B2 * B_REF * B_REF)  # k t, 1/(N m)

# ---------------------------------------------------------------------------
# A neighbour's pole. q = M pi b^2 with M = B_ax / (mu0 n_a); its field at r is
# mu0 q / (4 pi r^2), so against the field B_ax that made it:
#   ratio(r) = b^2 / (4 n_a r^2).
# The reach is cut at centres 1.5 L apart, where the nearest two poles (each
# L/3 from its centre) are 1.5 L - 2 L/3 apart.


def pole_ratio(r):
    return b * b / (4.0 * n_a * r * r)


REACH = 1.5
NEAREST_AT_REACH = (REACH - 2.0 / 3.0) * L

# ---------------------------------------------------------------------------
# The poles' field. A point pole s B0 at depth d: on the sheet,
#   B_par(rho) = B0 d^2 rho / (rho^2 + d^2)^(3/2), which peaks at rho = d/sqrt2
# at 2/(3 sqrt3) B0 (d/drho = 0 gives rho^2 + d^2 = 3 rho^2).
PEAK_RHO = 1.0 / math.sqrt(2.0)
PEAK_VALUE = 2.0 / (3.0 * math.sqrt(3.0))

# An N-S pair l apart against the point dipole. On the axis the pair gives
#   (q/r^2)[(1 - e)^-2 - (1 + e)^-2] = (q/r^2)(4e)(1 + 2e^2 + 3e^4 + ...),
# e = l / 2r, so the octupole's share there is 2 e^2 and the next term 3 e^4.
# Off the axis it is smaller: found below by sweeping the direction.


def pair_over_dipole(theta, e):
    """|B_pair - B_dipole| / |B_dipole| at angle theta to the axis, r = 1, l = 2e."""
    def pole(px, sign):
        dx, dy = math.cos(theta) - px, math.sin(theta)
        r3 = (dx * dx + dy * dy) ** 1.5
        return sign * dx / r3, sign * dy / r3
    hx, hy = pole(e, 1.0)
    tx, ty = pole(-e, -1.0)
    bx, by = hx + tx, hy + ty
    # The dipole of moment q l = 2e along x: 3 r (m.r) - m over r^3.
    mx = 2.0 * e
    dx = 3.0 * math.cos(theta) * (mx * math.cos(theta)) - mx
    dy = 3.0 * math.sin(theta) * (mx * math.cos(theta))
    return math.hypot(bx - dx, by - dy) / math.hypot(dx, dy)


def octupole_bound():
    e = 1e-3
    return max(pair_over_dipole(math.pi / 2.0 * k / 2000.0, e) for k in range(2001)) / (e * e)


def main():
    print("filing (assumed 1 mm, aspect 5)")
    print(f"  a = {a:.4e} m, b = {b:.4e} m, V = {V:.6e} m^3, m = {m:.6e} kg")
    print(f"  n_a = {n_a:.12f}   (Physics.cpp AxialDemag(5); fgtest --law prints 0.055820969802)")
    print(f"  n_r = {n_r:.12f}   (fgtest --law prints 0.472089515099)")
    print(f"  A / B^2 = {A_PER_B2:.6e} N m / T^2; A at 10 mT = {A_PER_B2 * 1e-4:.6e} N m (fgtest --law prints 1.316348e-08)")
    for mu in (0.2, 0.3):
        print(f"  mu {mu}: friction torque {friction_torque(mu):.6e} N m, threshold field {threshold_field(mu) * 1e3:.3f} mT")
    print("  stop angles at mu 0.2 (the default):")
    for B in (2e-3, 4e-3, 10e-3, 40e-3):
        print(f"    {B * 1e3:5.1f} mT: {math.degrees(stop_angle(B, 0.2)):.3f} deg")
    print(f"  the torque law's field limit at mu0 m_s = 1.6 T: {B_SAT_LIMIT * 1e3:.1f} mT (Strength stops at 100 mT)")
    print(f"  reference dose k t at 4 mT: {REF_DOSE:.6e} per N m")
    print()
    print("neighbours")
    print(f"  a pole's field over the field that made it, at the nearest poles across the reach "
          f"({NEAREST_AT_REACH / L:.4f} L): {pole_ratio(NEAREST_AT_REACH):.4f} (the spec: under 7%)")
    print()
    print("the poles' field")
    print(f"  B_par peaks at rho = {PEAK_RHO:.12f} d at {PEAK_VALUE:.12f} B0")
    print(f"  N-S pair against the dipole: octupole share at most {octupole_bound():.4f} (l/2r)^2 over every direction "
          f"(on the axis 2, next term 3 (l/2r)^4)")
    print(f"  so at r = 20 l the allowance 1.2 x 2 (l/2r)^2 = {1.2 * 2.0 / 1600.0:.6f}, the next term {3.0 / 40.0 ** 4:.2e}")


if __name__ == "__main__":
    main()
