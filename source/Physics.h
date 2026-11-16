#pragma once

/**
    The magnets' field and the filing it acts on. Plain C++ in double, no GL,
    so `fgtest --field` and `--law` check it with no context at all.

    **The field.** Each pole is a magnetic point charge (the Gilbert model of a
    magnet's end) at a depth d under the sheet; on the sheet, z = 0,

        B(x) = s B0 d^2 ( x - P, d ) / ( |x - P|^2 + d^2 )^(3/2)

    with B0 the field straight above the pole (Strength) and s = +1 for N,
    -1 for S. Lengths are frame heights ("su"); the shape is dimensionless, so
    B is in tesla. The field of several poles is the sum. SPEC-filings.md has
    the source (Gauss's law for magnetic charge).

    **The filing** is a soft-iron prolate spheroid, 1 mm long (assumed), aspect
    5 (assumed). Osborn's demagnetising factors give the torque a field puts on
    it, A sin 2psi with A = V ( n_r - n_a ) B^2 / ( 2 mu0 n_a n_r ) (Huillery et
    al., arXiv:1903.09699 eq. 1), headless: it aligns with +B or -B, whichever is
    nearer. Its axial magnetisation B_axial / ( mu0 n_a ) is a pair of poles
    +-q, q = M pi b^2, at +-L/3 from its centre (the centroid of each half's
    surface charge on a uniformly magnetised ellipsoid).

    **The turn** is the closed form of an overdamped needle against Coulomb
    friction, dpsi/dt = -k ( A sin 2psi - F ), for the time a tap leaves it
    loose: Turn() below. Derived in the spec; checked against RK4 by --law.
*/
namespace filings
{

constexpr double kPi = 3.14159265358979323846;

/// mu0, CODATA 2022 (NIST): 1.256 637 061 27e-6 N/A^2.
constexpr double kMu0 = 1.25663706127e-6;
/// Standard gravity, 3rd CGPM (1901).
constexpr double kGravity = 9.80665;
/// Iron at 20 degC, 7.874 g/cm^3 (Wikipedia, "Iron", infobox).
constexpr double kIronDensity = 7874.0;
/// A filing's length, m. Assumed: no checked figure for commercial filings.
constexpr double kFilingLength = 1e-3;
/// A filing's length over its diameter. Assumed.
constexpr double kAspect = 5.0;
/// The dose's reference (chosen): at Tap Strength 1x and a full hit, one tap
/// turns a frictionless filing in this field by tan psi -> tan psi / e.
constexpr double kReferenceField = 4e-3;
/// How far a neighbour's poles reach, in filing lengths (centre to centre).
constexpr double kReach = 1.5;
/// Most poles.
constexpr int kMaxPoles = 4;

/// The axial demagnetising factor of a prolate spheroid of aspect R > 1
/// (Osborn 1945, as Huillery et al. and Nahlawi et al. reproduce it).
double AxialDemag( double aspect );

/// The filing, in SI. Everything a tap needs, from the constants above.
struct Needle
{
	double length;    ///< L, m
	double a, b;      ///< semi-axes, m
	double volume;    ///< m^3
	double mass;      ///< kg
	double na, nr;    ///< demagnetising factors, axial and radial
	double torquePerB2;///< A / B^2, N m / T^2
	double chargePerB;///< q / B_axial, A m / T
	double poleOffset;///< L / 3, m
	double referenceDose;///< t / zeta_r for the reference tap, 1 / ( N m )

	/// The torque amplitude A in a field of magnitude B (tesla). `linear` is
	/// the --law negative control: A in proportion to B, not B^2.
	double Torque( double field, bool linear = false ) const;
	/// Coulomb friction's torque about the centre, mu m g L / 4 (the load
	/// spread evenly along the filing), and its force, mu m g.
	double FrictionTorque( double mu ) const;
	double FrictionForce( double mu ) const;
};

Needle MakeNeedle();

/// The closed-form turn. psi is the angle from the field's axis, wrapped into
/// ( -pi/2, pi/2 ]; A the torque amplitude, F the friction torque, both N m;
/// kt = t / zeta_r, 1 / ( N m ). Returns the angle after the tap. A filing whose
/// torque does not beat friction, A |sin 2psi| <= F, is returned bit for bit.
double Turn( double psi, double A, double F, double kt );

/// The angle a filing stops at, 1/2 asin( F / A ); pi/2 if it never turns.
double StopAngle( double A, double F );

/// psi into ( -pi/2, pi/2 ]: a needle has no north end.
double WrapHalf( double angle );

//---------------------------------------------------------------------------
// The magnets.
//---------------------------------------------------------------------------
struct Pole
{
	double x = 0.0, y = 0.0;///< su
	double sign = 1.0;      ///< +1 N, -1 S
};

struct Magnets
{
	Pole poles[ kMaxPoles ];
	int count    = 2;
	double b0    = 0.04;///< T, the field straight above a pole
	double depth = 0.12;///< su

	/// --field's negative controls: the field falling as 1/r^3 (power 4, not 3,
	/// in the denominator's exponent x 2) and every pole read as N.
	bool cubeLaw     = false;
	bool signIgnored = false;
	/// --dipole's: only the first pole counts.
	bool onePole = false;
};

struct Field
{
	double x = 0.0, y = 0.0, z = 0.0;///< tesla
};

/// The magnets' field on the sheet at ( x, y ), su.
Field FieldAt( const Magnets& magnets, double x, double y );

} // namespace filings
