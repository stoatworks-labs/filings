#pragma once

#include "Physics.h"

#include <cstdint>
#include <vector>

/**
    The sheet of filings: the only state the plugin keeps, in frame heights
    ("su") and radians, double precision, and written ONLY by a tap (or by an
    operator's Scatter or a change of count, which appends or drops filings at
    the end and never moves one). Between taps nothing here is touched, so
    "no filing moves without a tap" is literal, bit for bit.

    **A tap** is kPassesPerTap Jacobi passes. A pass reads a snapshot -- every
    filing's poles, +-q at +-L/3 along its axis, q from the axial part of the
    mean of the magnets' fields at those two poles -- and then every filing,
    from that snapshot alone:

    1. feels the field at its two poles: the magnets' plus its neighbours'
       poles (softened over b, tapered to nothing at 1.5 L centre to centre);
    2. turns, in the mean of those two fields, by the closed form (Turn());
    3. slides by ( t / zeta_t )( |F| - mu m g )+ along the force on its poles,
       capped at b, with zeta_t = 12 zeta_r / L^2 (uniform drag along a rod);
    4. if it turned or slid (it is loose), is pushed out of any filing it
       overlaps, half the overlap each, capped at b.

    A filing whose torque and force both fail to beat friction keeps its
    angle and centre bit for bit.

    Neighbours are found in a grid of cells 1.5 L wide; the work is split over
    threads by filing, each reading only the snapshot, so the result does not
    depend on the thread count.
*/
namespace filings
{

struct Filing
{
	double x = 0.0, y = 0.0;///< su, from the frame's centre
	double theta = 0.0;     ///< radians, in ( -pi/2, pi/2 ]
};

struct SheetParams
{
	double length   = 1.0 / 70.0;///< a filing's length, su
	double friction = 0.2;       ///< mu
	Magnets magnets;
};

/// Test hooks for the negative controls. A shipped instance has every one off.
struct SheetHooks
{
	bool noChains     = false;///< neighbours' poles ignored
	bool noContact    = false;
	bool holdCentres  = false;///< nothing slides or is pushed (the lone-needle checks)
	bool snap         = false;///< a filing that turns at all lands on the field
	bool slideDown    = false;///< the slide down |B|^2
	bool compass      = false;///< a polar needle: aligns with +B, torque sin psi
	bool torqueLinear = false;///< torque in proportion to B, not B^2
	bool noFriction   = false;
	bool noDiagonals  = false;///< the grid misses the diagonal cells
};

class Sheet
{
public:
	/// The sheet: x in [ -kHalfWidth, kHalfWidth ], y in [ -kHalfHeight, kHalfHeight ].
	static constexpr double kHalfWidth  = 1.0;
	static constexpr double kHalfHeight = 0.5;
	static constexpr int kPassesPerTap  = 2;
	static constexpr int kMaxFilings    = 60000;

	Sheet();

	/// A fresh sheet: `count` filings at seeded random places and angles.
	void Scatter( uint32_t seed, int count );
	/// Keep the first `count` filings of the seeded sequence: grow by
	/// appending fresh ones, shrink by dropping the last. Nobody moves.
	void SetCount( int count );

	/// One tap, `dose` times the reference: kPassesPerTap passes.
	void Tap( double dose, const SheetParams& params );
	/// One pass, with t / zeta_r = kt (1 / ( N m )).
	void Pass( double kt, const SheetParams& params );

	const std::vector< Filing >& Filings() const
	{
		return filings;
	}
	/// Write access for the harness; counts as a change (the plugin re-uploads).
	std::vector< Filing >& FilingsForTest()
	{
		++generation;
		return filings;
	}
	const Needle& GetNeedle() const
	{
		return needle;
	}
	uint32_t Seed() const
	{
		return seed;
	}
	/// Taps since the plugin loaded.
	unsigned long long Taps() const
	{
		return taps;
	}
	/// Advances every time anything here is written.
	unsigned long long Generation() const
	{
		return generation;
	}
	/// Filings that turned and that slid or were pushed in the last pass.
	int TurnedLastPass() const
	{
		return turned;
	}
	int MovedLastPass() const
	{
		return moved;
	}

	/// Where the filing k of the seeded sequence starts.
	static Filing Scattered( uint32_t seed, int k );
	/// Metres a frame height, for a filing `length` su long.
	static double MetresPerSu( double length )
	{
		return kFilingLength / length;
	}

	SheetHooks hooks;
	/// Threads a pass may use (1 for a single-threaded reference).
	int threads = 8;

private:
	void passRange( int worker, int begin, int end, double kt, const SheetParams& params );

	Needle needle;
	std::vector< Filing > filings;
	std::vector< Filing > next;
	uint32_t seed = 1;
	unsigned long long taps = 0, generation = 0;
	int turned = 0, moved = 0;

	// The snapshot, rebuilt each pass.
	struct Snap
	{
		double hx, hy, tx, ty;///< head and tail poles, su
		double ux, uy;        ///< axis
		double q;             ///< A m: +q at the head, -q at the tail
		double bhx, bhy, btx, bty;///< the magnets' field at the head and the tail, T
	};
	std::vector< Snap > snap;
	std::vector< int > cellStart, cellItems;
	int cellsX = 0, cellsY = 0;
	double cellSize = 1.0;
	std::vector< int > turnedBy, movedBy;
};

} // namespace filings
