#include "Sheet.h"

#include <algorithm>
#include <cmath>
#include <thread>

namespace filings
{
namespace
{
uint32_t pcg( uint32_t v )
{
	const uint32_t state = v * 747796405u + 2891336453u;
	const uint32_t word  = ( ( state >> ( ( state >> 28u ) + 4u ) ) ^ state ) * 277803737u;
	return ( word >> 22u ) ^ word;
}

/// A double in [ 0, 1 ) from two hashes: 53 bits, so a scattered sheet has no
/// visible lattice at any Size.
double unit( uint32_t seed, uint32_t k, uint32_t channel )
{
	const uint32_t hi = pcg( seed * 0x9E3779B9u ^ pcg( k * 3u + channel ) );
	const uint32_t lo = pcg( hi ^ 0x85EBCA6Bu );
	const uint64_t bits = ( static_cast< uint64_t >( hi ) << 21 ) ^ ( lo >> 11 );
	return static_cast< double >( bits & ( ( uint64_t( 1 ) << 53 ) - 1 ) ) * ( 1.0 / 9007199254740992.0 );
}

/// The closest points of two segments p0 + s d1, s in [ -h, h ], and
/// q0 + t d2, t in [ -h, h ] (unit d1, d2): Ericson, Real-Time Collision
/// Detection, 5.1.9, re-centred. Returns the squared distance; cp, cq the points.
double segmentDistance( double px, double py, double d1x, double d1y, double qx, double qy, double d2x, double d2y, double h,
                        double& cpx, double& cpy, double& cqx, double& cqy )
{
	//Ericson's form on [ 0, 1 ]: P = p0 - h d1, D1 = 2h d1.
	const double ax = px - h * d1x, ay = py - h * d1y;
	const double bx = qx - h * d2x, by = qy - h * d2y;
	const double e1x = 2.0 * h * d1x, e1y = 2.0 * h * d1y;
	const double e2x = 2.0 * h * d2x, e2y = 2.0 * h * d2y;
	const double rx = ax - bx, ry = ay - by;
	const double a = e1x * e1x + e1y * e1y;
	const double e = e2x * e2x + e2y * e2y;
	const double f = e2x * rx + e2y * ry;
	double s = 0.0, t = 0.0;
	if( a <= 1e-30 && e <= 1e-30 )
	{
		s = t = 0.0;
	}
	else if( a <= 1e-30 )
	{
		s = 0.0;
		t = std::clamp( f / e, 0.0, 1.0 );
	}
	else
	{
		const double c = e1x * rx + e1y * ry;
		if( e <= 1e-30 )
		{
			t = 0.0;
			s = std::clamp( -c / a, 0.0, 1.0 );
		}
		else
		{
			const double b     = e1x * e2x + e1y * e2y;
			const double denom = a * e - b * b;
			s                  = denom > 1e-30 * a * e ? std::clamp( ( b * f - c * e ) / denom, 0.0, 1.0 ) : 0.5;
			t                  = ( b * s + f ) / e;
			if( t < 0.0 )
			{
				t = 0.0;
				s = std::clamp( -c / a, 0.0, 1.0 );
			}
			else if( t > 1.0 )
			{
				t = 1.0;
				s = std::clamp( ( b - c ) / a, 0.0, 1.0 );
			}
		}
	}
	cpx            = ax + e1x * s;
	cpy            = ay + e1y * s;
	cqx            = bx + e2x * t;
	cqy            = by + e2y * t;
	const double dx = cpx - cqx, dy = cpy - cqy;
	return dx * dx + dy * dy;
}
} // namespace

Sheet::Sheet() :
	needle( MakeNeedle() )
{
	Scatter( 1, 0 );
}

Filing Sheet::Scattered( uint32_t seed, int k )
{
	Filing f;
	const uint32_t index = static_cast< uint32_t >( k );
	f.x                  = ( 2.0 * unit( seed, index, 0 ) - 1.0 ) * kHalfWidth;
	f.y                  = ( 2.0 * unit( seed, index, 1 ) - 1.0 ) * kHalfHeight;
	f.theta              = WrapHalf( ( unit( seed, index, 2 ) - 0.5 ) * kPi );
	return f;
}

void Sheet::Scatter( uint32_t newSeed, int count )
{
	seed = newSeed;
	filings.clear();
	SetCount( count );
	++generation;
}

void Sheet::SetCount( int count )
{
	count = std::clamp( count, 0, kMaxFilings );
	const int have = static_cast< int >( filings.size() );
	if( count == have )
		return;
	if( count < have )
		filings.resize( static_cast< size_t >( count ) );
	else
		for( int k = have; k < count; ++k )
			filings.push_back( Scattered( seed, k ) );
	++generation;
}

void Sheet::Tap( double dose, const SheetParams& params )
{
	if( !( dose > 0.0 ) )
		return;
	++taps;
	const double kt = dose * needle.referenceDose / kPassesPerTap;
	for( int p = 0; p < kPassesPerTap; ++p )
		Pass( kt, params );
}

void Sheet::Pass( double kt, const SheetParams& params )
{
	const int n = static_cast< int >( filings.size() );
	turned = moved = 0;
	if( n == 0 )
		return;
	const double L      = std::max( params.length, 1e-6 );
	const double offset = L / 3.0;//the poles, su
	snap.resize( static_cast< size_t >( n ) );
	for( int i = 0; i < n; ++i )
	{
		const Filing& f = filings[ static_cast< size_t >( i ) ];
		Snap& s         = snap[ static_cast< size_t >( i ) ];
		s.ux            = std::cos( f.theta );
		s.uy            = std::sin( f.theta );
		s.hx            = f.x + offset * s.ux;
		s.hy            = f.y + offset * s.uy;
		s.tx            = f.x - offset * s.ux;
		s.ty            = f.y - offset * s.uy;
		const Field bh  = FieldAt( params.magnets, s.hx, s.hy );
		const Field bt  = FieldAt( params.magnets, s.tx, s.ty );
		s.bhx           = bh.x;
		s.bhy           = bh.y;
		s.btx           = bt.x;
		s.bty           = bt.y;
		//The axial magnetisation from the magnets' field its poles feel.
		const double axial = 0.5 * ( ( bh.x + bt.x ) * s.ux + ( bh.y + bt.y ) * s.uy );
		s.q                = needle.chargePerB * axial;
	}

	//The cells, by counting sort on the snapshot's centres.
	cellSize = kReach * L;
	cellsX   = std::max( 1, static_cast< int >( std::ceil( 2.0 * kHalfWidth / cellSize ) ) );
	cellsY   = std::max( 1, static_cast< int >( std::ceil( 2.0 * kHalfHeight / cellSize ) ) );
	const size_t cellCount = static_cast< size_t >( cellsX ) * static_cast< size_t >( cellsY );
	cellStart.assign( cellCount + 1, 0 );
	cellItems.resize( static_cast< size_t >( n ) );
	auto cellOf = [ & ]( const Filing& f ) {
		const int cx = std::clamp( static_cast< int >( std::floor( ( f.x + kHalfWidth ) / cellSize ) ), 0, cellsX - 1 );
		const int cy = std::clamp( static_cast< int >( std::floor( ( f.y + kHalfHeight ) / cellSize ) ), 0, cellsY - 1 );
		return static_cast< size_t >( cy ) * static_cast< size_t >( cellsX ) + static_cast< size_t >( cx );
	};
	for( const Filing& f : filings )
		++cellStart[ cellOf( f ) + 1 ];
	for( size_t c = 0; c < cellCount; ++c )
		cellStart[ c + 1 ] += cellStart[ c ];
	{
		std::vector< int > fill( cellStart.begin(), cellStart.end() - 1 );
		for( int i = 0; i < n; ++i )
			cellItems[ static_cast< size_t >( fill[ cellOf( filings[ static_cast< size_t >( i ) ] ) ]++ ) ] = i;
	}

	next = filings;
	const int workers = std::clamp( threads, 1, 64 );
	turnedBy.assign( static_cast< size_t >( workers ), 0 );
	movedBy.assign( static_cast< size_t >( workers ), 0 );
	if( workers == 1 || n < 2048 )
	{
		turnedBy.assign( 1, 0 );
		movedBy.assign( 1, 0 );
		passRange( 0, 0, n, kt, params );
	}
	else
	{
		std::vector< std::thread > pool;
		const int chunk = ( n + workers - 1 ) / workers;
		for( int w = 0; w < workers; ++w )
		{
			const int begin = w * chunk, end = std::min( n, begin + chunk );
			if( begin >= end )
				break;
			pool.emplace_back( [ this, w, begin, end, kt, &params ]() { passRange( w, begin, end, kt, params ); } );
		}
		for( std::thread& t : pool )
			t.join();
	}
	for( int w = 0; w < static_cast< int >( turnedBy.size() ); ++w )
	{
		turned += turnedBy[ static_cast< size_t >( w ) ];
		moved += movedBy[ static_cast< size_t >( w ) ];
	}
	filings.swap( next );
	++generation;
}

void Sheet::passRange( int worker, int begin, int end, double kt, const SheetParams& params )
{
	const double L      = std::max( params.length, 1e-6 );
	const double metres = MetresPerSu( L );
	const double reach  = kReach * L;
	const double reach2 = reach * reach;
	const double b      = needle.b;//m: the poles' softening
	const double b2     = b * b;
	const double bSu    = b / metres;
	const double core   = ( needle.a - needle.b ) / metres;//capsule core half-length, su
	const double k      = kMu0 / ( 4.0 * kPi );
	const double F      = hooks.noFriction ? 0.0 : needle.FrictionTorque( params.friction );
	const double fForce = hooks.noFriction ? 0.0 : needle.FrictionForce( params.friction );
	const double ltOver = kt * needle.length * needle.length / 12.0;//t / zeta_t, m / N
	const bool chains   = !hooks.noChains;
	const bool slides   = !hooks.holdCentres;
	const bool contact  = slides && !hooks.noContact;
	int myTurned = 0, myMoved = 0;

	for( int i = begin; i < end; ++i )
	{
		const Filing& f = filings[ static_cast< size_t >( i ) ];
		const Snap& s   = snap[ static_cast< size_t >( i ) ];
		double bhx = s.bhx, bhy = s.bhy, btx = s.btx, bty = s.bty;
		double pushX = 0.0, pushY = 0.0;

		const int cx = std::clamp( static_cast< int >( std::floor( ( f.x + kHalfWidth ) / cellSize ) ), 0, cellsX - 1 );
		const int cy = std::clamp( static_cast< int >( std::floor( ( f.y + kHalfHeight ) / cellSize ) ), 0, cellsY - 1 );
		for( int oy = -1; oy <= 1; ++oy )
			for( int ox = -1; ox <= 1; ++ox )
			{
				if( hooks.noDiagonals && ox != 0 && oy != 0 )
					continue;
				const int nx = cx + ox, ny = cy + oy;
				if( nx < 0 || ny < 0 || nx >= cellsX || ny >= cellsY )
					continue;
				const size_t cell = static_cast< size_t >( ny ) * static_cast< size_t >( cellsX ) + static_cast< size_t >( nx );
				for( int at = cellStart[ cell ]; at < cellStart[ cell + 1 ]; ++at )
				{
					const int j = cellItems[ static_cast< size_t >( at ) ];
					if( j == i )
						continue;
					const Filing& g = filings[ static_cast< size_t >( j ) ];
					const double dx = g.x - f.x, dy = g.y - f.y;
					const double d2 = dx * dx + dy * dy;
					if( d2 >= reach2 )
						continue;
					const Snap& o = snap[ static_cast< size_t >( j ) ];
					if( chains )
					{
						const double w  = ( 1.0 - d2 / reach2 ) * ( 1.0 - d2 / reach2 );
						const double kq = k * w * o.q;
						//Its head (+q) and tail (-q) at my head and my tail.
						auto add = [ & ]( double px, double py, double charge, double& bx, double& by ) {
							const double rx = px * metres, ry = py * metres;
							const double r2 = rx * rx + ry * ry + b2;
							const double c  = charge / ( r2 * std::sqrt( r2 ) );
							bx += c * rx;
							by += c * ry;
						};
						add( s.hx - o.hx, s.hy - o.hy, kq, bhx, bhy );
						add( s.hx - o.tx, s.hy - o.ty, -kq, bhx, bhy );
						add( s.tx - o.hx, s.ty - o.hy, kq, btx, bty );
						add( s.tx - o.tx, s.ty - o.ty, -kq, btx, bty );
					}
					//Two capsules can only touch with their centres closer than
					//2 ( a - b ) + 2 b = L.
					if( contact && d2 < L * L )
					{
						double cpx, cpy, cqx, cqy;
						const double dist2 = segmentDistance( f.x, f.y, s.ux, s.uy, g.x, g.y, o.ux, o.uy, core, cpx, cpy, cqx, cqy );
						const double touch = 2.0 * bSu;
						if( dist2 < touch * touch )
						{
							const double dist = std::sqrt( dist2 );
							double nxv = cpx - cqx, nyv = cpy - cqy;
							if( dist > 1e-15 )
							{
								nxv /= dist;
								nyv /= dist;
							}
							else
							{
								//Crossing exactly: out sideways, the lower index to the left.
								nxv = i < j ? -s.uy : s.uy;
								nyv = i < j ? s.ux : -s.ux;
							}
							pushX += 0.5 * ( touch - dist ) * nxv;
							pushY += 0.5 * ( touch - dist ) * nyv;
						}
					}
				}
			}

		Filing& out = next[ static_cast< size_t >( i ) ];

		//The turn, in the mean of the fields at the two poles.
		const double bx = 0.5 * ( bhx + btx ), by = 0.5 * ( bhy + bty );
		const double bm = std::sqrt( bx * bx + by * by );
		bool loose      = false;
		if( bm > 0.0 )
		{
			const double A   = needle.Torque( bm, hooks.torqueLinear );
			const double phi = std::atan2( by, bx );
			if( hooks.compass )
			{
				//A polar needle (the negative control): torque A sin psi towards
				//+B, frictionless, tan( psi/2 ) -> tan( psi/2 ) e^( -A k t ).
				const double psi  = std::remainder( f.theta - phi, 2.0 * kPi );
				const double turnedTo = 2.0 * std::atan( std::tan( 0.5 * psi ) * std::exp( -A * kt ) );
				if( turnedTo != psi )
				{
					out.theta = WrapHalf( phi + turnedTo );
					loose     = true;
				}
			}
			else
			{
				const double psi  = WrapHalf( f.theta - phi );
				double after      = Turn( psi, A, F, kt );
				if( after != psi )
				{
					if( hooks.snap )
						after = 0.0;
					out.theta = WrapHalf( phi + after );
					loose     = true;
				}
			}
		}
		if( loose )
			++myTurned;

		//The slide: the force on the two poles.
		double moveX = 0.0, moveY = 0.0;
		bool slid    = false;
		if( slides )
		{
			const double fx = s.q * ( bhx - btx ), fy = s.q * ( bhy - bty );
			const double fm = std::sqrt( fx * fx + fy * fy );
			if( fm > fForce )
			{
				double step = ltOver * ( fm - fForce ) / metres;//su
				step        = std::min( step, bSu );
				const double sign = hooks.slideDown ? -1.0 : 1.0;
				moveX = sign * step * fx / fm;
				moveY = sign * step * fy / fm;
				slid  = true;
				loose = true;
			}
		}
		if( contact && loose && ( pushX != 0.0 || pushY != 0.0 ) )
		{
			const double pm = std::sqrt( pushX * pushX + pushY * pushY );
			const double scale = pm > bSu ? bSu / pm : 1.0;
			moveX += pushX * scale;
			moveY += pushY * scale;
			slid = true;
		}
		if( slid )
		{
			out.x = std::clamp( f.x + moveX, -kHalfWidth, kHalfWidth );
			out.y = std::clamp( f.y + moveY, -kHalfHeight, kHalfHeight );
			++myMoved;
		}
	}
	turnedBy[ static_cast< size_t >( worker ) ] = myTurned;
	movedBy[ static_cast< size_t >( worker ) ]  = myMoved;
}

} // namespace filings
