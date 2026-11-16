// The checks. Included into main.cpp's anonymous namespace; see main.cpp for
// the Rig, the pictures and the music.

//===========================================================================
// The harness's own physics, written from SPEC-filings.md and not from the
// plugin's source. Every constant is restated here with its source.
//===========================================================================
constexpr double hPi     = 3.14159265358979323846;
constexpr double hMu0    = 1.25663706127e-6;///< CODATA 2022 (NIST)
constexpr double hG      = 9.80665;         ///< standard gravity, 3rd CGPM
constexpr double hRho    = 7874.0;          ///< iron at 20 degC (Wikipedia, "Iron")
constexpr double hLength = 1e-3;            ///< a filing, m (the spec's assumption)
constexpr double hAspect = 5.0;             ///< (assumed)
constexpr double hRef    = 4e-3;            ///< the dose's reference field, T
constexpr double hReach  = 1.5;             ///< a neighbour's reach, filing lengths

double degrees( double radians )
{
	return radians * 180.0 / hPi;
}

/// An angle into ( -pi/2, pi/2 ]: the harness's own (fmod, not remainder).
double hWrap( double a )
{
	double w = std::fmod( a, hPi );
	if( w > 0.5 * hPi )
		w -= hPi;
	if( w <= -0.5 * hPi )
		w += hPi;
	return w;
}

/// Gauss-Legendre nodes and weights on [ -1, 1 ], by Newton on P_n.
void gaussLegendre( int n, std::vector< double >& x, std::vector< double >& w )
{
	x.assign( static_cast< size_t >( n ), 0.0 );
	w.assign( static_cast< size_t >( n ), 0.0 );
	for( int i = 0; i < n; ++i )
	{
		double z = std::cos( hPi * ( i + 0.75 ) / ( n + 0.5 ) );
		double dp = 0.0;
		for( int it = 0; it < 100; ++it )
		{
			double p0 = 1.0, p1 = z;
			for( int k = 2; k <= n; ++k )
			{
				const double p2 = ( ( 2.0 * k - 1.0 ) * z * p1 - ( k - 1.0 ) * p0 ) / k;
				p0              = p1;
				p1              = p2;
			}
			dp              = n * ( z * p1 - p0 ) / ( z * z - 1.0 );
			const double dz = p1 / dp;
			z -= dz;
			if( std::fabs( dz ) < 1e-16 )
				break;
		}
		x[ static_cast< size_t >( i ) ] = z;
		w[ static_cast< size_t >( i ) ] = 2.0 / ( ( 1.0 - z * z ) * dp * dp );
	}
}

/// int_lo^hi f by `panels` panels of 16-point Gauss-Legendre.
double integrate( const std::function< double( double ) >& f, double lo, double hi, int panels )
{
	static std::vector< double > gx, gw;
	if( gx.empty() )
		gaussLegendre( 16, gx, gw );
	double sum       = 0.0;
	const double len = ( hi - lo ) / panels;
	for( int p = 0; p < panels; ++p )
	{
		const double a = lo + p * len, mid = a + 0.5 * len;
		for( size_t i = 0; i < gx.size(); ++i )
			sum += gw[ i ] * 0.5 * len * f( mid + 0.5 * len * gx[ i ] );
	}
	return sum;
}

/// Osborn's demagnetising factors of a prolate spheroid, semi-axes a > b,
/// from the ellipsoid's integrals (not the closed form the plugin uses):
/// n_a = ( a b^2 / 2 ) int_0^inf ds / ( ( s + a^2 )^(3/2) ( s + b^2 ) ), and
/// n_r with ( s + a^2 )^(1/2) ( s + b^2 )^2, substituting w = ( s + a^2 )^(-1/2).
struct Demag
{
	double na, nr;
};
Demag hDemag( double aspect, int panels = 400 )
{
	const double a = aspect, b = 1.0, c2 = a * a - b * b;
	const double na = a * b * b * integrate( [ & ]( double w ) { return w * w / ( 1.0 - c2 * w * w ); }, 0.0, 1.0 / a, panels );
	const double nr = a * b * b * integrate( [ & ]( double w ) {
		const double q = 1.0 - c2 * w * w;
		return w * w / ( q * q );
	}, 0.0, 1.0 / a, panels );
	return { na, nr };
}

/// The filing, in SI, from the spec.
struct HNeedle
{
	double L, a, b, V, m, na, nr;
	double torquePerB2;///< A / B^2
	double chargePerB; ///< q / B_axial
	double refDose;    ///< t / zeta_r of a reference tap
	double F( double mu ) const
	{
		return mu * m * hG * L / 4.0;
	}
	double friction( double mu ) const
	{
		return mu * m * hG;
	}
};
HNeedle hNeedle()
{
	HNeedle n;
	n.L               = hLength;
	n.a               = 0.5 * n.L;
	n.b               = n.a / hAspect;
	n.V               = 4.0 / 3.0 * hPi * n.a * n.b * n.b;
	n.m               = hRho * n.V;
	const Demag d     = hDemag( hAspect );
	n.na              = d.na;
	n.nr              = d.nr;
	n.torquePerB2     = n.V * ( n.nr - n.na ) / ( 2.0 * hMu0 * n.na * n.nr );
	n.chargePerB      = hPi * n.b * n.b / ( hMu0 * n.na );
	n.refDose         = 1.0 / ( 2.0 * n.torquePerB2 * hRef * hRef );
	return n;
}
const HNeedle& HN()
{
	static const HNeedle n = hNeedle();
	return n;
}

struct HPole
{
	double x, y, s;
};
struct HMagnets
{
	std::vector< HPole > poles;
	double b0 = 0.04, d = 0.06;
};

/// The poles' field on the sheet: B = s B0 d^2 ( x - P, d ) / ( |x - P|^2 + d^2 )^(3/2).
void hField( const HMagnets& m, double x, double y, double out[ 3 ] )
{
	out[ 0 ] = out[ 1 ] = out[ 2 ] = 0.0;
	for( const HPole& p : m.poles )
	{
		const double rx = x - p.x, ry = y - p.y, rz = m.d;
		const double r  = std::sqrt( rx * rx + ry * ry + rz * rz );
		const double k  = p.s * m.b0 * m.d * m.d / ( r * r * r );
		out[ 0 ] += k * rx;
		out[ 1 ] += k * ry;
		out[ 2 ] += k * rz;
	}
}

/// The default magnets on a frame of aspect `aspect`, read from the controls'
/// defaults the way the spec places them (0..1 across and up the frame).
HMagnets hDefaultMagnets( double aspect = 16.0 / 9.0 )
{
	HMagnets m;
	const int count = OptionIndex( InfoOf( PT_POLES ).defaultValue, 4 ) + 1;
	for( int i = 0; i < count; ++i )
	{
		const double X = InfoOf( PoleX( i ) ).defaultValue, Y = InfoOf( PoleY( i ) ).defaultValue;
		m.poles.push_back( { ( X - 0.5 ) * aspect, Y - 0.5, OptionIndex( InfoOf( PoleSign( i ) ).defaultValue, 2 ) == 0 ? 1.0 : -1.0 } );
	}
	m.b0 = StrengthFromParam( InfoOf( PT_STRENGTH ).defaultValue );
	m.d  = DepthFromParam( InfoOf( PT_DEPTH ).defaultValue );
	return m;
}

/// The magnets a running rig's controls ask for.
HMagnets hMagnetsOf( const Rig& rig )
{
	HMagnets m;
	const FilingsPlugin& p = rig.plugin;
	const double aspect    = static_cast< double >( rig.width ) / rig.height;
	const int count        = OptionIndex( p.GetById( PT_POLES ), 4 ) + 1;
	for( int i = 0; i < count; ++i )
		m.poles.push_back( { ( std::clamp( static_cast< double >( p.GetById( PoleX( i ) ) ), 0.0, 1.0 ) - 0.5 ) * aspect,
		                     std::clamp( static_cast< double >( p.GetById( PoleY( i ) ) ), 0.0, 1.0 ) - 0.5,
		                     OptionIndex( p.GetById( PoleSign( i ) ), 2 ) == 0 ? 1.0 : -1.0 } );
	m.b0 = StrengthFromParam( p.GetById( PT_STRENGTH ) );
	m.d  = DepthFromParam( p.GetById( PT_DEPTH ) );
	return m;
}

/// The plugin's Magnets for the same poles.
Magnets toPlugin( const HMagnets& h )
{
	Magnets m;
	m.count = static_cast< int >( h.poles.size() );
	for( int i = 0; i < m.count; ++i )
		m.poles[ i ] = { h.poles[ static_cast< size_t >( i ) ].x, h.poles[ static_cast< size_t >( i ) ].y, h.poles[ static_cast< size_t >( i ) ].s };
	m.b0    = h.b0;
	m.depth = h.d;
	return m;
}

/// The textbook closed form of the turn (the spec's, in u = tan psi):
/// ( u - u- ) / ( u - u+ ) = C e^( -2 R tau ), here solved for u directly.
/// Returns psi untouched when A |sin 2 psi| <= F.
double hTurn( double psi, double A, double F, double tau )
{
	const double m = std::fabs( psi );
	if( !( A * std::sin( 2.0 * m ) > F ) || !( tau > 0.0 ) )
		return psi;
	double u;
	if( F <= 0.0 )
		u = std::tan( m ) * std::exp( -2.0 * A * tau );
	else
	{
		const double R  = std::sqrt( A * A - F * F );
		const double up = ( A + R ) / F;
		const double um = F / ( A + R );//( A - R ) / F, without the cancellation
		const double u0 = std::tan( m );
		const double C  = ( u0 - um ) / ( u0 - up );
		const double E  = std::exp( -2.0 * R * tau );
		u               = ( um - up * C * E ) / ( 1.0 - C * E );
	}
	const double turned = std::atan( u );
	return psi < 0.0 ? -turned : turned;
}

/// dpsi/dtau = -sign( psi )( A sin 2|psi| - F ) by RK4, `steps` steps,
/// gated as the spec says: a needle that cannot beat friction does not move.
double hTurnRK4( double psi, double A, double F, double tau, int steps )
{
	if( !( A * std::sin( 2.0 * std::fabs( psi ) ) > F ) || !( tau > 0.0 ) )
		return psi;
	const double sign = psi < 0.0 ? -1.0 : 1.0;
	double y          = std::fabs( psi );
	auto f            = [ & ]( double v ) { return -( A * std::sin( 2.0 * v ) - F ); };
	const double h    = tau / steps;
	for( int i = 0; i < steps; ++i )
	{
		const double k1 = f( y ), k2 = f( y + 0.5 * h * k1 ), k3 = f( y + 0.5 * h * k2 ), k4 = f( y + h * k3 );
		y += h / 6.0 * ( k1 + 2.0 * k2 + 2.0 * k3 + k4 );
	}
	return sign * y;
}

/// The external field a lone needle turns in: the mean of the magnets' fields
/// at its two poles, +-L/3 along its axis (the spec's).
void hPoleMean( const HMagnets& m, double x, double y, double theta, double L, double out[ 2 ] )
{
	const double ux = std::cos( theta ), uy = std::sin( theta ), o = L / 3.0;
	double bh[ 3 ], bt[ 3 ];
	hField( m, x + o * ux, y + o * uy, bh );
	hField( m, x - o * ux, y - o * uy, bt );
	out[ 0 ] = 0.5 * ( bh[ 0 ] + bt[ 0 ] );
	out[ 1 ] = 0.5 * ( bh[ 1 ] + bt[ 1 ] );
}

/// A lone needle's pass: turn in the pole-mean field, nothing else.
double hLonePass( const HMagnets& m, const Filing& f, double L, double mu, double tau, bool rk4 = false )
{
	double b[ 2 ];
	hPoleMean( m, f.x, f.y, f.theta, L, b );
	const double bm = std::sqrt( b[ 0 ] * b[ 0 ] + b[ 1 ] * b[ 1 ] );
	if( bm <= 0.0 )
		return f.theta;
	const double A   = HN().torquePerB2 * bm * bm;
	const double phi = std::atan2( b[ 1 ], b[ 0 ] );
	const double psi = hWrap( f.theta - phi );
	const double out = rk4 ? hTurnRK4( psi, A, HN().F( mu ), tau, 20000 ) : hTurn( psi, A, HN().F( mu ), tau );
	return out == psi ? f.theta : hWrap( phi + out );
}

/// The closest points of two segments, a different algorithm from the
/// plugin's (Ericson's): in 2D two segments that do not cross are closest at
/// one of the four endpoint-to-segment pairs. Segment i: c +- h u.
double pointSegment( double px, double py, double cx, double cy, double ux, double uy, double h, double& qx, double& qy )
{
	const double t = std::clamp( ( px - cx ) * ux + ( py - cy ) * uy, -h, h );
	qx             = cx + t * ux;
	qy             = cy + t * uy;
	return ( px - qx ) * ( px - qx ) + ( py - qy ) * ( py - qy );
}
bool segmentsCross( double ax, double ay, double bx, double by, double cx, double cy, double dx, double dy )
{
	auto cross = []( double ox, double oy, double px, double py, double qx, double qy ) { return ( px - ox ) * ( qy - oy ) - ( py - oy ) * ( qx - ox ); };
	const double d1 = cross( cx, cy, dx, dy, ax, ay ), d2 = cross( cx, cy, dx, dy, bx, by );
	const double d3 = cross( ax, ay, bx, by, cx, cy ), d4 = cross( ax, ay, bx, by, dx, dy );
	return ( ( d1 > 0 && d2 < 0 ) || ( d1 < 0 && d2 > 0 ) ) && ( ( d3 > 0 && d4 < 0 ) || ( d3 < 0 && d4 > 0 ) );
}
/// Squared distance; p on segment 1, q on segment 2.
double hSegments( const Filing& f, const Filing& g, double h, double& px, double& py, double& qx, double& qy )
{
	const double ux = std::cos( f.theta ), uy = std::sin( f.theta ), vx = std::cos( g.theta ), vy = std::sin( g.theta );
	const double a0x = f.x - h * ux, a0y = f.y - h * uy, a1x = f.x + h * ux, a1y = f.y + h * uy;
	const double b0x = g.x - h * vx, b0y = g.y - h * vy, b1x = g.x + h * vx, b1y = g.y + h * vy;
	if( segmentsCross( a0x, a0y, a1x, a1y, b0x, b0y, b1x, b1y ) )
	{
		px = qx = 0.0;
		py = qy = 0.0;
		return 0.0;
	}
	double best = 1e300, tx, ty;
	auto consider = [ & ]( double d2, double ppx, double ppy, double qqx, double qqy ) {
		if( d2 < best )
		{
			best = d2;
			px   = ppx;
			py   = ppy;
			qx   = qqx;
			qy   = qqy;
		}
	};
	consider( pointSegment( a0x, a0y, g.x, g.y, vx, vy, h, tx, ty ), a0x, a0y, tx, ty );
	consider( pointSegment( a1x, a1y, g.x, g.y, vx, vy, h, tx, ty ), a1x, a1y, tx, ty );
	consider( pointSegment( b0x, b0y, f.x, f.y, ux, uy, h, tx, ty ), tx, ty, b0x, b0y );
	consider( pointSegment( b1x, b1y, f.x, f.y, ux, uy, h, tx, ty ), tx, ty, b1x, b1y );
	return best;
}

/// Where the harness's pass leaves a filing. When a decision sits on the
/// friction threshold to within rounding (an ill-posed case: a needle exactly
/// at its stop angle), both outcomes are kept and either is accepted.
struct HOutcome
{
	double theta;
	double x[ 2 ], y[ 2 ];///< without and with the contact push
	bool pushSure;        ///< whether the push is certainly (1) or certainly not (0) applied
	bool pushOn;
	int pushes;
};

/// One pass of the whole sheet, from the spec, O(N^2): the snapshot's poles
/// and the magnets' fields, every neighbour within 1.5 L, the turn, the slide,
/// the contact push.
std::vector< HOutcome > hPass( const std::vector< Filing >& in, const HMagnets& mag, double L, double mu, double tau )
{
	const HNeedle& N    = HN();
	const size_t n      = in.size();
	const double metres = hLength / L;
	const double o      = L / 3.0;
	struct S
	{
		double hx, hy, tx, ty, q, bh[ 3 ], bt[ 3 ];
	};
	std::vector< S > s( n );
	for( size_t i = 0; i < n; ++i )
	{
		const Filing& f = in[ i ];
		const double ux = std::cos( f.theta ), uy = std::sin( f.theta );
		s[ i ].hx = f.x + o * ux;
		s[ i ].hy = f.y + o * uy;
		s[ i ].tx = f.x - o * ux;
		s[ i ].ty = f.y - o * uy;
		hField( mag, s[ i ].hx, s[ i ].hy, s[ i ].bh );
		hField( mag, s[ i ].tx, s[ i ].ty, s[ i ].bt );
		s[ i ].q = N.chargePerB * 0.5 * ( ( s[ i ].bh[ 0 ] + s[ i ].bt[ 0 ] ) * ux + ( s[ i ].bh[ 1 ] + s[ i ].bt[ 1 ] ) * uy );
	}
	const double R     = hReach * L;
	const double bSu   = N.b / metres;
	const double touch = 2.0 * bSu;
	const double core  = ( N.a - N.b ) / metres;
	const double F     = N.F( mu );
	const double f0    = N.friction( mu );
	std::vector< HOutcome > out( n );
	for( size_t i = 0; i < n; ++i )
	{
		const Filing& f = in[ i ];
		double bh[ 2 ] = { s[ i ].bh[ 0 ], s[ i ].bh[ 1 ] }, bt[ 2 ] = { s[ i ].bt[ 0 ], s[ i ].bt[ 1 ] };
		double push[ 2 ] = {};
		int pushes       = 0;
		const double ux = std::cos( f.theta ), uy = std::sin( f.theta );
		for( size_t j = 0; j < n; ++j )
		{
			if( j == i )
				continue;
			const Filing& g = in[ j ];
			const double D2 = ( g.x - f.x ) * ( g.x - f.x ) + ( g.y - f.y ) * ( g.y - f.y );
			if( !( D2 < R * R ) )
				continue;
			const double taper = ( 1.0 - D2 / ( R * R ) ) * ( 1.0 - D2 / ( R * R ) );
			const double Q     = s[ j ].q * taper;
			//mu0 Q ( r - r_p ) / ( 4 pi ( |r - r_p|^2 + b^2 )^(3/2) ), metres.
			auto pole = [ & ]( double px, double py, double ppx, double ppy, double charge, double* b ) {
				const double rx = ( px - ppx ) * metres, ry = ( py - ppy ) * metres;
				const double den = std::pow( rx * rx + ry * ry + N.b * N.b, 1.5 );
				b[ 0 ] += hMu0 * charge * rx / ( 4.0 * hPi * den );
				b[ 1 ] += hMu0 * charge * ry / ( 4.0 * hPi * den );
			};
			pole( s[ i ].hx, s[ i ].hy, s[ j ].hx, s[ j ].hy, Q, bh );
			pole( s[ i ].hx, s[ i ].hy, s[ j ].tx, s[ j ].ty, -Q, bh );
			pole( s[ i ].tx, s[ i ].ty, s[ j ].hx, s[ j ].hy, Q, bt );
			pole( s[ i ].tx, s[ i ].ty, s[ j ].tx, s[ j ].ty, -Q, bt );
			double px, py, qx, qy;
			const double d2 = hSegments( f, g, core, px, py, qx, qy );
			if( d2 < touch * touch )
			{
				const double dist = std::sqrt( d2 );
				double nx, ny;
				if( dist > 1e-15 )
				{
					nx = ( px - qx ) / dist;
					ny = ( py - qy ) / dist;
				}
				else
				{
					nx = i < j ? -uy : uy;
					ny = i < j ? ux : -ux;
				}
				push[ 0 ] += 0.5 * ( touch - dist ) * nx;
				push[ 1 ] += 0.5 * ( touch - dist ) * ny;
				++pushes;
			}
		}
		HOutcome& r = out[ i ];
		r.pushes    = pushes;
		//The turn.
		const double bx = 0.5 * ( bh[ 0 ] + bt[ 0 ] ), by = 0.5 * ( bh[ 1 ] + bt[ 1 ] );
		const double bm = std::sqrt( bx * bx + by * by );
		r.theta         = f.theta;
		int turn        = 0;//0 no, 1 yes, 2 on the threshold
		if( bm > 0.0 )
		{
			const double A   = N.torquePerB2 * bm * bm;
			const double phi = std::atan2( by, bx );
			const double psi = hWrap( f.theta - phi );
			const double margin = A * std::sin( 2.0 * std::fabs( psi ) ) - F;
			turn                = std::fabs( margin ) <= 1e-9 * A ? 2 : ( margin > 0.0 ? 1 : 0 );
			const double after  = hTurn( psi, A, F, tau );
			if( after != psi )
				r.theta = hWrap( phi + after );
		}
		//The slide.
		const double fx = s[ i ].q * ( bh[ 0 ] - bt[ 0 ] ), fy = s[ i ].q * ( bh[ 1 ] - bt[ 1 ] );
		const double fm = std::sqrt( fx * fx + fy * fy );
		double mx = 0.0, my = 0.0;
		int slide = 0;
		if( fm > 0.0 )
		{
			const double margin = fm - f0;
			slide               = std::fabs( margin ) <= 1e-9 * f0 ? 2 : ( margin > 0.0 ? 1 : 0 );
			if( margin > 0.0 )
			{
				const double step = std::min( tau * hLength * hLength / 12.0 * margin / metres, bSu );
				mx                = step * fx / fm;
				my                = step * fy / fm;
			}
		}
		const double pm = std::sqrt( push[ 0 ] * push[ 0 ] + push[ 1 ] * push[ 1 ] );
		const double k  = pm > bSu ? bSu / pm : 1.0;
		auto place      = [ & ]( bool withPush, double& x, double& y ) {
			x = f.x + mx + ( withPush ? push[ 0 ] * k : 0.0 );
			y = f.y + my + ( withPush ? push[ 1 ] * k : 0.0 );
			if( x != f.x || y != f.y )
			{
				x = std::clamp( x, -1.0, 1.0 );
				y = std::clamp( y, -0.5, 0.5 );
			}
		};
		place( false, r.x[ 0 ], r.y[ 0 ] );
		place( true, r.x[ 1 ], r.y[ 1 ] );
		const bool looseSure = turn == 1 || slide == 1;
		const bool tightSure = turn == 0 && slide == 0;
		r.pushSure           = pm == 0.0 || looseSure || tightSure;
		r.pushOn             = looseSure && pm > 0.0;
	}
	return out;
}

/// How far a filing's state is from a harness outcome (rad, su).
void compareOutcome( const Filing& got, const HOutcome& want, double& dTheta, double& dCentre )
{
	dTheta = std::fabs( hWrap( got.theta - want.theta ) );
	auto d = [ & ]( int k ) { return std::max( std::fabs( got.x - want.x[ k ] ), std::fabs( got.y - want.y[ k ] ) ); };
	if( want.pushSure )
		dCentre = d( want.pushOn ? 1 : 0 );
	else
		dCentre = std::min( d( 0 ), d( 1 ) );
}

/// The mean angle (degrees) between filings and the magnets' field at their
/// centres, over the filings where A >= `atLeast` x F there.
double meanError( const std::vector< Filing >& filings, const HMagnets& m, double mu, double atLeast, size_t* counted = nullptr )
{
	double sum   = 0.0;
	size_t count = 0;
	for( const Filing& f : filings )
	{
		double b[ 3 ];
		hField( m, f.x, f.y, b );
		const double A = HN().torquePerB2 * ( b[ 0 ] * b[ 0 ] + b[ 1 ] * b[ 1 ] );
		if( A < atLeast * HN().F( mu ) )
			continue;
		sum += std::fabs( hWrap( f.theta - std::atan2( b[ 1 ], b[ 0 ] ) ) );
		++count;
	}
	if( counted )
		*counted = count;
	return count ? degrees( sum / count ) : 0.0;
}

//===========================================================================
// Shared by the checks that drive the plugin.
//===========================================================================

/// The quiet sheet: every control at its default, then no audio taps (Tap On
/// Off), Tone 0 (the filings black), Mix 1. Every check then sets what it
/// measures (honeydew's lesson: a check sets every control it depends on).
void quiet( Rig& rig )
{
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		if( info.kind == Kind::Standard || info.kind == Kind::Option )
			rig.Set( id, info.defaultValue );
	}
	rig.Set( PT_TAP_ON, static_cast< int >( TapOn::Off ) );
	rig.Set( PT_TONE, 0.0f );
	rig.Set( PT_MIX, 1.0f );
}

/// Every control back at its shipped default.
void defaults( Rig& rig )
{
	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		if( info.kind == Kind::Standard || info.kind == Kind::Option )
			rig.Set( id, info.defaultValue );
	}
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, SheetHooks base = SheetHooks {}, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	if( perturb.sheet )
		perturb.sheet( base );
	rig.plugin.SheetForTest().hooks = base;
	rig.plugin.SetFieldHooksForTest( perturb.cubeLaw, perturb.signIgnored, perturb.onePole );
	rig.plugin.SetResizeScattersForTest( perturb.resizeScatters );
	rig.plugin.SetNeverPinsForTest( perturb.neverPins );
	rig.plugin.SetMagnetDragsForTest( perturb.magnetDrags );
	rig.plugin.SetFloatClockForTest( perturb.floatClock );
	rig.plugin.SetUnprimedForTest( perturb.unprimed );
	rig.plugin.AnalyserForTest().SetFullSpectrumForTest( perturb.fullSpectrum );
	rig.plugin.SetCompositeModelForTest( perturb.compositeModel );
	quiet( rig );
	return true;
}

/// The default sheet's physics, for the checks that drive a Sheet directly.
SheetParams sheetParams( const HMagnets& m, double length, double mu, const Perturb& perturb )
{
	SheetParams p;
	p.length            = length;
	p.friction          = mu;
	p.magnets           = toPlugin( m );
	p.magnets.cubeLaw   = perturb.cubeLaw;
	p.magnets.signIgnored = perturb.signIgnored;
	p.magnets.onePole   = perturb.onePole;
	return p;
}

size_t differing( const Floats& a, const Floats& b )
{
	size_t n = 0;
	for( size_t i = 0; i < a.size() && i < b.size(); ++i )
		n += a[ i ] != b[ i ];
	return n + ( a.size() > b.size() ? a.size() - b.size() : b.size() - a.size() );
}

//===========================================================================
// --field
//===========================================================================
int runField( const Perturb& perturb )
{
	std::printf( "\n=== field: the poles' field is the closed form of point charges under the sheet -- shape, peak, flux, the far dipole, signs\n" );
	auto plugin = [ & ]( const HMagnets& h, double x, double y ) {
		Magnets m     = toPlugin( h );
		m.cubeLaw     = perturb.cubeLaw;
		m.signIgnored = perturb.signIgnored;
		m.onePole     = perturb.onePole;
		return FieldAt( m, x, y );
	};

	//The closed form at 400 points, four poles of both signs.
	{
		double worst = 0.0;
		for( int k = 0; k < 400; ++k )
		{
			HMagnets h;
			h.b0 = 0.002 + 0.098 * hash01( static_cast< uint32_t >( k ), 1 );
			h.d  = 0.01 + 0.24 * hash01( static_cast< uint32_t >( k ), 2 );
			for( int p = 0; p < 4; ++p )
				h.poles.push_back( { 2.0 * hash01( static_cast< uint32_t >( k * 8 + p ), 3 ) - 1.0, hash01( static_cast< uint32_t >( k * 8 + p ), 4 ) - 0.5,
				                     hash01( static_cast< uint32_t >( k * 8 + p ), 5 ) < 0.5 ? 1.0 : -1.0 } );
			const double x = 2.0 * hash01( static_cast< uint32_t >( k ), 6 ) - 1.0, y = hash01( static_cast< uint32_t >( k ), 7 ) - 0.5;
			double want[ 3 ];
			hField( h, x, y, want );
			const Field got  = plugin( h, x, y );
			const double mag = std::sqrt( want[ 0 ] * want[ 0 ] + want[ 1 ] * want[ 1 ] + want[ 2 ] * want[ 2 ] );
			const double err = std::sqrt( ( got.x - want[ 0 ] ) * ( got.x - want[ 0 ] ) + ( got.y - want[ 1 ] ) * ( got.y - want[ 1 ] )
			                              + ( got.z - want[ 2 ] ) * ( got.z - want[ 2 ] ) );
			worst = std::max( worst, err / mag );
		}
		Check( worst <= 1e-12, fmt( "the field of four poles against the harness's closed form at 400 random points: worst relative error %.2e (tolerance 1e-12: double arithmetic)", worst ) );
	}

	//One N pole: B|| peaks at rho = d / sqrt 2 at 2 / ( 3 sqrt 3 ) B0.
	{
		HMagnets h;
		h.b0 = 0.04;
		h.d  = 0.06;
		h.poles.push_back( { 0.0, 0.0, 1.0 } );
		auto along = [ & ]( double rho ) { return std::fabs( plugin( h, rho, 0.0 ).x ); };
		double lo = 0.1 * h.d, hi = 3.0 * h.d;
		const double step = 1e-6 * h.d;
		for( int it = 0; it < 200; ++it )
		{
			const double mid = 0.5 * ( lo + hi );
			( along( mid + step ) > along( mid - step ) ? lo : hi ) = mid;
		}
		const double rho  = 0.5 * ( lo + hi );
		const double want = h.d / std::sqrt( 2.0 );
		const double peak = along( rho ) / h.b0, wantPeak = 2.0 / ( 3.0 * std::sqrt( 3.0 ) );
		Check( std::fabs( rho - want ) <= 1e-9 * h.d && std::fabs( peak - wantPeak ) <= 1e-12,
		       fmt( "one pole, d = %.3f: B|| peaks at rho = %.12f d (d/sqrt2 = %.12f d; tolerance 1e-9 d: the bisection's sign test at a 1e-6 d step) at %.12f B0 "
		            "(2/(3 sqrt3) = %.12f; tolerance 1e-12)",
		            h.d, rho / h.d, want / h.d, peak, wantPeak ) );

		//The flux of B_z through the sheet is half the pole's: 2 pi d^2 B0.
		auto flux = [ & ]( int panels ) {
			const double Rmax = 50.0 * h.d;
			const double inner = integrate( [ & ]( double r ) { return plugin( h, r, 0.0 ).z * 2.0 * hPi * r; }, 0.0, Rmax, panels );
			return inner + 2.0 * hPi * h.b0 * h.d * h.d * h.d / std::sqrt( Rmax * Rmax + h.d * h.d );
		};
		const double f1 = flux( 400 ), f2 = flux( 800 );
		const double wantFlux = 2.0 * hPi * h.d * h.d * h.b0;
		Check( std::fabs( f1 - f2 ) <= 1e-12 * wantFlux && std::fabs( f2 - wantFlux ) <= 1e-9 * wantFlux,
		       fmt( "the flux of B_z through the sheet: %.12e T su^2 against 2 pi d^2 B0 = %.12e (relative %.1e; tolerance 1e-9; the quadrature moved %.1e when "
		            "doubled)",
		            f2, wantFlux, std::fabs( f2 - wantFlux ) / wantFlux, std::fabs( f1 - f2 ) / wantFlux ) );
	}

	//An N-S pair, far away, is the point dipole. Its error is the octupole's:
	//at most 2 ( l/2r )^2 of the dipole's field in any direction, the next
	//term 3 ( l/2r )^4 (tools/constants.py); tolerance 1.2 x the first.
	{
		HMagnets h;
		h.b0          = 0.04;
		h.d           = 0.06;
		const double l = 0.01;
		h.poles.push_back( { -0.5 * l, 0.0, 1.0 } );
		h.poles.push_back( { 0.5 * l, 0.0, -1.0 } );
		double worst = 0.0;
		for( int k = 0; k < 400; ++k )
		{
			const double ang = 2.0 * hPi * hash01( static_cast< uint32_t >( k ), 21 );
			const double rho = l * ( 20.0 + 80.0 * hash01( static_cast< uint32_t >( k ), 22 ) );
			const double x = rho * std::cos( ang ), y = rho * std::sin( ang );
			const double rv[ 3 ] = { x, y, h.d };
			const double r       = std::sqrt( x * x + y * y + h.d * h.d );
			const double pv[ 3 ] = { -l, 0.0, 0.0 };//from the S charge to the N
			const double pr      = ( pv[ 0 ] * rv[ 0 ] + pv[ 1 ] * rv[ 1 ] + pv[ 2 ] * rv[ 2 ] ) / r;
			double dip[ 3 ];
			for( int c = 0; c < 3; ++c )
				dip[ c ] = h.b0 * h.d * h.d * ( 3.0 * pr * rv[ c ] / r - pv[ c ] ) / ( r * r * r );
			const Field got  = plugin( h, x, y );
			const double err = std::sqrt( ( got.x - dip[ 0 ] ) * ( got.x - dip[ 0 ] ) + ( got.y - dip[ 1 ] ) * ( got.y - dip[ 1 ] )
			                              + ( got.z - dip[ 2 ] ) * ( got.z - dip[ 2 ] ) );
			const double mag = std::sqrt( dip[ 0 ] * dip[ 0 ] + dip[ 1 ] * dip[ 1 ] + dip[ 2 ] * dip[ 2 ] );
			const double a   = 0.5 * l / r;
			worst            = std::max( worst, err / ( mag * 1.2 * 2.0 * a * a ) );
		}
		Check( worst <= 1.0, fmt( "an N-S pair l apart, at 20 l to 100 l: the field against the point dipole, worst %.3f of the octupole allowance (1.2 x 2 (l/2r)^2)", worst ) );
	}

	//Signs and superposition.
	{
		HMagnets n, s, both;
		n.poles    = { { -0.2, 0.1, 1.0 } };
		s.poles    = { { 0.3, -0.1, -1.0 } };
		both.poles = { n.poles[ 0 ], s.poles[ 0 ] };
		const Field aboveN = plugin( n, -0.2, 0.1 ), aboveS = plugin( s, 0.3, -0.1 );
		const Field outN = plugin( n, -0.15, 0.1 ), outS = plugin( s, 0.35, -0.1 );
		double worst = 0.0;
		for( int k = 0; k < 100; ++k )
		{
			const double x = 2.0 * hash01( static_cast< uint32_t >( k ), 31 ) - 1.0, y = hash01( static_cast< uint32_t >( k ), 32 ) - 0.5;
			const Field a = plugin( n, x, y ), b = plugin( s, x, y ), c = plugin( both, x, y );
			const double mag = std::sqrt( c.x * c.x + c.y * c.y + c.z * c.z ) + 1e-30;
			worst = std::max( worst, std::sqrt( ( a.x + b.x - c.x ) * ( a.x + b.x - c.x ) + ( a.y + b.y - c.y ) * ( a.y + b.y - c.y ) + ( a.z + b.z - c.z ) * ( a.z + b.z - c.z ) ) / mag );
		}
		Check( aboveN.z > 0.0 && aboveS.z < 0.0 && outN.x > 0.0 && outS.x < 0.0 && worst <= 1e-15,
		       fmt( "signs: B_z %+.4f T over an N pole, %+.4f over an S; beside them B|| points away from N (%+.4f) and towards S (%+.4f); two poles = the "
		            "sum of each alone (worst %.1e; tolerance 1e-15)",
		            aboveN.z, aboveS.z, outN.x, outS.x, worst ) );
	}
	return Verdict();
}

//===========================================================================
// --law
//===========================================================================
int runLaw( const Perturb& perturb )
{
	std::printf( "\n=== law: the closed-form turn against the harness's RK4; Osborn's factors against the integrals; a lone needle in a real sheet\n" );
	//The closed form against RK4, over 2000 random taps.
	{
		double worst = 0.0, worstHalf = 0.0;
		int below = 0, belowExact = 0, turned = 0, past = 0;
		for( int k = 0; k < 2000; ++k )
		{
			const double psi = hWrap( ( hash01( static_cast< uint32_t >( k ), 41 ) - 0.5 ) * hPi );
			const double A   = 0.1 + 9.9 * hash01( static_cast< uint32_t >( k ), 42 );
			const double F   = hash01( static_cast< uint32_t >( k ), 43 ) < 0.1 ? 0.0 : 1.2 * A * hash01( static_cast< uint32_t >( k ), 44 );
			const double tau = 3.0 * hash01( static_cast< uint32_t >( k ), 45 );
			const double got  = Turn( psi, A, F, tau );
			const double want = hTurnRK4( psi, A, F, tau, 20000 );
			worst             = std::max( worst, std::fabs( got - want ) );
			worstHalf         = std::max( worstHalf, std::fabs( Turn( Turn( psi, A, F, 0.5 * tau ), A, F, 0.5 * tau ) - got ) );
			if( !( A * std::sin( 2.0 * std::fabs( psi ) ) > F ) )
			{
				++below;
				std::uint64_t a, b;
				std::memcpy( &a, &psi, 8 );
				std::memcpy( &b, &got, 8 );
				belowExact += a == b;
			}
			else
			{
				++turned;
				const double stop = F < A ? 0.5 * std::asin( F / A ) : 0.5 * hPi;
				if( std::fabs( got ) < stop - 1e-15 || ( got != 0.0 && ( got < 0.0 ) != ( psi < 0.0 ) ) )
					++past;
			}
		}
		Check( worst <= 1e-9, fmt( "2000 random taps: the closed form against RK4 (tau/20000 steps), worst %.2e rad (tolerance 1e-9 rad: RK4's error at that step is ~1e-13)", worst ) );
		Check( past == 0 && below > 0 && belowExact == below,
		       fmt( "of %d that turned, %d went past the stop angle 1/2 asin( F/A ) or the field's axis; %d of %d that could not beat friction came back bit for bit", turned,
		            past, belowExact, below ) );
		Check( worstHalf <= 1e-12, fmt( "two half taps against one tap: worst %.2e rad (tolerance 1e-12: the closed form composes)", worstHalf ) );
		double worstAxis = 0.0;
		for( int k = 0; k < 1000; ++k )
		{
			const double theta = ( hash01( static_cast< uint32_t >( k ), 46 ) - 0.5 ) * 2.0 * hPi, phi = ( hash01( static_cast< uint32_t >( k ), 47 ) - 0.5 ) * 2.0 * hPi;
			worstAxis = std::max( worstAxis, std::fabs( hWrap( WrapHalf( theta + hPi - phi ) - WrapHalf( theta - phi ) ) ) );
		}
		Check( worstAxis <= 1e-14, fmt( "headless: a needle at theta and at theta + pi is the same angle to the field's axis (worst %.1e rad; tolerance 1e-14)", worstAxis ) );
	}

	//Osborn's factors against the integrals; the torque and friction from the spec.
	{
		const Demag q = hDemag( kAspect ), q2 = hDemag( kAspect, 800 );
		const Needle n = MakeNeedle();
		const HNeedle& h = HN();
		const double quad = std::max( std::fabs( q.na - q2.na ), std::fabs( q.nr - q2.nr ) );
		Check( std::fabs( n.na - q2.na ) <= 1e-9 && std::fabs( n.nr - q2.nr ) <= 1e-9 && std::fabs( q2.na + 2.0 * q2.nr - 1.0 ) <= 1e-9,
		       fmt( "aspect 5: the plugin's n_a = %.12f, n_r = %.12f; the integrals give %.12f and %.12f (tolerance 1e-9; the quadrature moved %.1e when doubled), "
		            "and n_a + 2 n_r = %.12f",
		            n.na, n.nr, q2.na, q2.nr, quad, q2.na + 2.0 * q2.nr ) );
		const double A10 = n.Torque( 0.01 ), A20 = n.Torque( 0.02 );
		const double hA  = h.torquePerB2 * 0.01 * 0.01;
		Check( std::fabs( A20 / A10 - 4.0 ) <= 1e-12 && std::fabs( A10 - hA ) <= 1e-9 * hA && std::fabs( n.FrictionTorque( 0.3 ) - h.F( 0.3 ) ) <= 1e-12 * h.F( 0.3 ),
		       fmt( "torque at 10 mT: %.6e N m (the spec's V (n_r - n_a) B^2 / (2 mu0 n_a n_r) gives %.6e); at 20 mT %.4f x as much (B^2: 4); friction mu m g L/4 "
		            "at mu 0.3: %.6e N m (harness %.6e)",
		            A10, hA, A20 / A10, n.FrictionTorque( 0.3 ), h.F( 0.3 ) ) );
	}

	//A lone needle in a real sheet: 200 filings 0.1 frame heights apart (no
	//neighbour within 1.5 L), centres held, three taps, against RK4 in the
	//pole-mean field.
	{
		const double L  = 1.0 / 90.0, mu = 0.3;
		const HMagnets m = hDefaultMagnets();
		Sheet sheet;
		sheet.threads = 1;
		SheetHooks hooks;
		hooks.holdCentres = true;
		if( perturb.sheet )
			perturb.sheet( hooks );
		sheet.hooks = hooks;
		std::vector< Filing >& f = sheet.FilingsForTest();
		f.clear();
		for( int i = 0; i < 20; ++i )
			for( int j = 0; j < 10; ++j )
				f.push_back( { -0.95 + 0.1 * i, -0.45 + 0.1 * j, hWrap( ( hash01( static_cast< uint32_t >( i * 10 + j ), 48 ) - 0.5 ) * hPi ) } );
		std::vector< Filing > want = f;
		const std::vector< Filing > initial = f;
		const SheetParams sp = sheetParams( m, L, mu, perturb );
		const double tau     = HN().refDose / 2.0;
		double worst         = 0.0;
		int moved            = 0;
		for( int tap = 0; tap < 3; ++tap )
		{
			sheet.Tap( 1.0, sp );
			for( int pass = 0; pass < 2; ++pass )
				for( Filing& w : want )
					w.theta = hLonePass( m, w, L, mu, tau, true );
		}
		for( size_t i = 0; i < want.size(); ++i )
		{
			worst = std::max( worst, std::fabs( hWrap( sheet.Filings()[ i ].theta - want[ i ].theta ) ) );
			moved += sheet.Filings()[ i ].theta != initial[ i ].theta;
		}
		Check( worst <= 1e-9, fmt( "200 lone needles 0.1 frame heights apart, three full taps (the default dipole, L 1/90, mu 0.3): worst %.2e rad from the harness's RK4 in the pole-mean field (tolerance 1e-9 rad); %d "
		                           "turned",
		                           worst, moved ) );
	}
	return Verdict();
}

//===========================================================================
// --step
//===========================================================================
int runStep( const Perturb& perturb )
{
	std::printf( "\n=== step: one pass of the whole sheet -- chains, contact, slide, friction -- against the harness's own O(N^2) pass from the same snapshot\n" );
	const double L = 1.0 / 40.0, mu = 0.3;
	const HMagnets m = hDefaultMagnets();
	const SheetParams sp = sheetParams( m, L, mu, perturb );
	Sheet sheet;
	SheetHooks hooks;
	if( perturb.sheet )
		perturb.sheet( hooks );
	sheet.hooks = hooks;
	sheet.Scatter( 1, FilingCount( 0.3, L ) );
	for( int tap = 0; tap < 12; ++tap )
		sheet.Tap( 1.0, sp );
	const std::vector< Filing > before = sheet.Filings();
	const double tau = HN().refDose / Sheet::kPassesPerTap;
	sheet.Pass( sheet.GetNeedle().referenceDose / Sheet::kPassesPerTap, sp );
	const std::vector< HOutcome > want = hPass( before, m, L, mu, tau );
	double worstTheta = 0.0, worstCentre = 0.0;
	size_t turned = 0, moved = 0, pushed = 0, contacts = 0, ambiguous = 0;
	for( size_t i = 0; i < before.size(); ++i )
	{
		double dt, dc;
		compareOutcome( sheet.Filings()[ i ], want[ i ], dt, dc );
		worstTheta  = std::max( worstTheta, dt );
		worstCentre = std::max( worstCentre, dc );
		turned += sheet.Filings()[ i ].theta != before[ i ].theta;
		moved += sheet.Filings()[ i ].x != before[ i ].x || sheet.Filings()[ i ].y != before[ i ].y;
		pushed += want[ i ].pushSure && want[ i ].pushOn;
		contacts += want[ i ].pushes > 0;
		ambiguous += !want[ i ].pushSure;
	}
	Check( worstTheta <= 1e-9 && worstCentre <= 1e-9 && turned > 100 && pushed > 100,
	       fmt( "%zu filings %.4f frame heights long, tapped 12 times: worst %.2e rad and %.2e frame heights from the harness's pass (tolerance 1e-9: two codings of "
	            "one law); %zu turned, %zu moved, %zu loose ones pushed out of a neighbour (%zu in contact; %zu on the friction threshold, either outcome taken)",
	            before.size(), L, worstTheta, worstCentre, turned, moved, pushed, contacts, ambiguous ) );
	return Verdict();
}

//===========================================================================
// --align
//===========================================================================
int runAlign( const Perturb& perturb )
{
	std::printf( "\n=== align: the default dipole, 40 taps -- field lines come up over the kicks, and filings pile at the poles\n" );
	const double L = SizeFromParam( InfoOf( PT_SIZE ).defaultValue ), mu = FrictionFromParam( InfoOf( PT_FRICTION ).defaultValue );
	const HMagnets m     = hDefaultMagnets();
	const SheetParams sp = sheetParams( m, L, mu, perturb );
	const int count      = FilingCount( DensityFromParam( InfoOf( PT_DENSITY ).defaultValue ), L );
	const double tau     = HN().refDose / Sheet::kPassesPerTap;

	//Lone needles: no chains, no contact, centres held. Every angle after every
	//tap is the harness's closed form iterated.
	{
		Sheet sheet;
		SheetHooks hooks;
		hooks.noChains    = true;
		hooks.noContact   = true;
		hooks.holdCentres = true;
		if( perturb.sheet )
			perturb.sheet( hooks );
		sheet.hooks = hooks;
		sheet.Scatter( 1, count );
		std::vector< Filing > want = sheet.Filings();
		const std::vector< Filing > start = want;
		double worst = 0.0;
		std::vector< double > means;
		for( int tap = 1; tap <= 40; ++tap )
		{
			sheet.Tap( 1.0, sp );
			for( int pass = 0; pass < Sheet::kPassesPerTap; ++pass )
				for( Filing& w : want )
					w.theta = hLonePass( m, w, L, mu, tau );
			for( size_t i = 0; i < want.size(); ++i )
				worst = std::max( worst, std::fabs( hWrap( sheet.Filings()[ i ].theta - want[ i ].theta ) ) );
			means.push_back( meanError( sheet.Filings(), m, mu, 2.0 ) );
		}
		size_t strong = 0;
		const double random = meanError( start, m, mu, 2.0, &strong );
		bool falls          = true;
		for( size_t k = 1; k < means.size(); ++k )
			falls = falls && means[ k ] <= means[ k - 1 ] + 1e-9;
		//The asymptote: a needle that ever turns ends at its stop angle in the
		//pole-mean field; one that cannot (past the upper stop) never moves.
		double asymptote = 0.0, reached = 0.0;
		size_t counted   = 0;
		for( size_t i = 0; i < start.size(); ++i )
		{
			double c[ 3 ];
			hField( m, start[ i ].x, start[ i ].y, c );
			if( HN().torquePerB2 * ( c[ 0 ] * c[ 0 ] + c[ 1 ] * c[ 1 ] ) < 2.0 * HN().F( mu ) )
				continue;
			const Filing& f = sheet.Filings()[ i ];
			double b[ 2 ];
			hPoleMean( m, f.x, f.y, f.theta, L, b );
			const double A   = HN().torquePerB2 * ( b[ 0 ] * b[ 0 ] + b[ 1 ] * b[ 1 ] );
			const double psi = std::fabs( hWrap( f.theta - std::atan2( b[ 1 ], b[ 0 ] ) ) );
			double b0[ 2 ];
			hPoleMean( m, start[ i ].x, start[ i ].y, start[ i ].theta, L, b0 );
			const double A0   = HN().torquePerB2 * ( b0[ 0 ] * b0[ 0 ] + b0[ 1 ] * b0[ 1 ] );
			const double psi0 = std::fabs( hWrap( start[ i ].theta - std::atan2( b0[ 1 ], b0[ 0 ] ) ) );
			const bool turns  = A0 * std::sin( 2.0 * psi0 ) > HN().F( mu );
			asymptote += turns ? 0.5 * std::asin( std::min( 1.0, HN().F( mu ) / A ) ) : psi0;
			reached += psi;
			++counted;
		}
		asymptote = degrees( asymptote / std::max< size_t >( counted, 1 ) );
		reached   = degrees( reached / std::max< size_t >( counted, 1 ) );
		Check( worst <= 1e-9, fmt( "lone needles: %d filings, every angle after each of 40 taps within %.2e rad of the harness's closed form (tolerance 1e-9 rad)", count, worst ) );
		Check( means[ 0 ] > 15.0 && falls && std::fabs( reached - asymptote ) <= 0.5,
		       fmt( "lone needles, over the %zu where A >= 2F: mean angle to the field %.1f deg scattered, %.1f after 1 tap (> 15: not at once), %.1f after 5, %.1f "
		            "after 40, never rising; against the pole-mean field %.2f deg, the asymptote (stop angles; past-the-stop needles where they lie) %.2f "
		            "(tolerance 0.5 deg)",
		            strong, random, means[ 0 ], means[ 4 ], means[ 39 ], reached, asymptote ) );
	}

	//The full model: chains, contact, slide.
	{
		Sheet sheet;
		SheetHooks hooks;
		if( perturb.sheet )
			perturb.sheet( hooks );
		sheet.hooks = hooks;
		sheet.Scatter( 1, count );
		//Each pole's own count: a slide that carried filings from one pole to
		//the other would raise the total too (a mutant did).
		auto near = [ & ]() {
			std::vector< size_t > n( m.poles.size(), 0 );
			for( const Filing& f : sheet.Filings() )
				for( size_t k = 0; k < m.poles.size(); ++k )
				{
					const HPole& p = m.poles[ k ];
					n[ k ] += ( f.x - p.x ) * ( f.x - p.x ) + ( f.y - p.y ) * ( f.y - p.y ) < m.d * m.d;
				}
			return n;
		};
		const std::vector< size_t > at0 = near();
		std::vector< size_t > at10;
		double after1 = 0.0;
		for( int tap = 1; tap <= 40; ++tap )
		{
			sheet.Tap( 1.0, sp );
			if( tap == 1 )
				after1 = meanError( sheet.Filings(), m, mu, 2.0 );
			if( tap == 10 )
				at10 = near();
		}
		const std::vector< size_t > at40 = near();
		size_t strong       = 0;
		const double after40 = meanError( sheet.Filings(), m, mu, 2.0, &strong );
		Check( after40 < after1 && after40 < 22.5,
		       fmt( "the full model, over the %zu where A >= 2F: mean angle to the magnets' field %.1f deg after 1 tap, %.1f after 40 (below half a scattered "
		            "sheet's 45: chosen)",
		            strong, after1, after40 ) );
		bool rising = at0.size() == at10.size() && at0.size() == at40.size();
		std::string counts;
		for( size_t k = 0; rising && k < at0.size(); ++k )
		{
			rising = rising && at10[ k ] > at0[ k ] && at40[ k ] > at10[ k ];
			counts += fmt( "%spole %zu %zu, %zu, %zu", k ? "; " : "", k + 1, at0[ k ], at10[ k ], at40[ k ] );
		}
		Check( rising, fmt( "filings within d of each pole, scattered, after 10 taps, after 40: %s (each rising: they creep up |B|^2)", counts.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --dipole
//===========================================================================

/// The orientation of the needles in square patches of the picture: the
/// structure tensor of the coverage 1 - T (Gaussian sigma 0.7 px, central
/// differences), its weak axis. sigma was chosen on the calibration sheet
/// (0.5, 0.7, 1, 1.5 px tried; 0.7 read it best at both rasters). Patches are `side` px, from the
/// bottom-left corner; partial ones at the edges are left out.
struct PatchReading
{
	int px, py;     ///< the patch's index
	double angle;   ///< the needles' axis, rad, in ( -pi/2, pi/2 ]
	double energy;  ///< trace of the tensor
};
std::vector< PatchReading > readPatches( const std::vector< double >& T, int w, int h, int side, double sigma = 0.7 )
{
	//The coverage 1 - T, not the optical density -ln T: a needle 1.2 px wide
	//covers some pixels almost fully, and -ln of almost nothing is noise.
	std::vector< double > D( T.size() ), tmp( T.size() ), S( T.size() );
	for( size_t i = 0; i < T.size(); ++i )
		D[ i ] = 1.0 - std::clamp( T[ i ], 0.0, 1.0 );
	const int radius = static_cast< int >( std::ceil( 3.0 * sigma ) );
	std::vector< double > kernel( static_cast< size_t >( 2 * radius + 1 ) );
	double ks = 0.0;
	for( int i = -radius; i <= radius; ++i )
		ks += kernel[ static_cast< size_t >( i + radius ) ] = std::exp( -0.5 * i * i / ( sigma * sigma ) );
	for( double& v : kernel )
		v /= ks;
	const double* k = kernel.data() + radius;
	auto at = [ & ]( const std::vector< double >& a, int x, int y ) { return a[ static_cast< size_t >( std::clamp( y, 0, h - 1 ) ) * w + std::clamp( x, 0, w - 1 ) ]; };
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			double s = 0.0;
			for( int i = -radius; i <= radius; ++i )
				s += k[ i ] * at( D, x + i, y );
			tmp[ static_cast< size_t >( y ) * w + x ] = s;
		}
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			double s = 0.0;
			for( int i = -radius; i <= radius; ++i )
				s += k[ i ] * at( tmp, x, y + i );
			S[ static_cast< size_t >( y ) * w + x ] = s;
		}
	std::vector< PatchReading > out;
	for( int py = 0; ( py + 1 ) * side <= h; ++py )
		for( int px = 0; ( px + 1 ) * side <= w; ++px )
		{
			double jxx = 0.0, jyy = 0.0, jxy = 0.0;
			for( int y = py * side; y < ( py + 1 ) * side; ++y )
				for( int x = px * side; x < ( px + 1 ) * side; ++x )
				{
					const double gx = 0.5 * ( at( S, x + 1, y ) - at( S, x - 1, y ) ), gy = 0.5 * ( at( S, x, y + 1 ) - at( S, x, y - 1 ) );
					jxx += gx * gx;
					jyy += gy * gy;
					jxy += gx * gy;
				}
			//The gradient's dominant axis is across the needles.
			const double across = 0.5 * std::atan2( 2.0 * jxy, jxx - jyy );
			out.push_back( { px, py, hWrap( across + 0.5 * hPi ), jxx + jyy } );
		}
	return out;
}

/// The doubled-angle mean of the angles of the filings whose centres fall in
/// each patch, and how coherent they are (the resultant's length over the
/// count, 1 when all agree).
struct PatchTruth
{
	double angle = 0.0, coherence = 0.0;
	int count    = 0;
};
std::map< std::pair< int, int >, PatchTruth > patchTruth( const std::vector< Filing >& filings, const std::vector< double >& angles, int w, int h, int side )
{
	std::map< std::pair< int, int >, std::pair< double, double > > sums;
	std::map< std::pair< int, int >, int > counts;
	for( size_t i = 0; i < filings.size(); ++i )
	{
		const double x = 0.5 * w + filings[ i ].x * h, y = 0.5 * h + filings[ i ].y * h;
		if( x < 0.0 || y < 0.0 )
			continue;
		const std::pair< int, int > key( static_cast< int >( x / side ), static_cast< int >( y / side ) );
		sums[ key ].first += std::cos( 2.0 * angles[ i ] );
		sums[ key ].second += std::sin( 2.0 * angles[ i ] );
		++counts[ key ];
	}
	std::map< std::pair< int, int >, PatchTruth > out;
	for( const auto& e : sums )
	{
		PatchTruth t;
		t.count     = counts[ e.first ];
		t.angle     = 0.5 * std::atan2( e.second.second, e.second.first );
		t.coherence = std::sqrt( e.second.first * e.second.first + e.second.second * e.second.second ) / t.count;
		out[ e.first ] = t;
	}
	return out;
}

/// The mean |difference| (degrees) between what the picture reads and the
/// truth, over the patches with at least 5 filings whose truth is coherent.
using PatchErrors = std::map< std::pair< int, int >, double >;
double patchError( const std::vector< PatchReading >& read, const std::map< std::pair< int, int >, PatchTruth >& truth, int* used = nullptr,
                   PatchErrors* each = nullptr )
{
	double sum = 0.0;
	int n      = 0;
	for( const PatchReading& r : read )
	{
		const auto it = truth.find( { r.px, r.py } );
		if( it == truth.end() || it->second.count < 5 || it->second.coherence < 0.8 )
			continue;
		const double e = std::fabs( hWrap( r.angle - it->second.angle ) );
		sum += e;
		++n;
		if( each )
			( *each )[ { r.px, r.py } ] = e;
	}
	if( used )
		*used = n;
	return n ? degrees( sum / n ) : 90.0;
}

int runDipole( const Perturb& perturb )
{
	std::printf( "\n=== dipole: one N-S pair over a white clip, 40 taps -- the picture's needle orientation against the closed form, through an instrument\n"
	             "    calibrated first on a sheet whose angles are known\n" );
	const double L = 1.0 / 30.0;
	for( const Raster& raster : kRasters )
	{
		const Floats white = flatCard( raster.w, raster.h, 1.0f, 1.0f, 1.0f );
		const int side     = static_cast< int >( std::lround( 4.0 * L * raster.h ) );
		auto setUp         = [ & ]( Rig& rig ) {
			rig.Set( PT_SIZE, ParamFromSize( L ) );
			rig.Set( PT_TONE, 0.0f );
		};
		auto transmissionOf = [ & ]( const Rig& rig ) {
			//White clip, Tone 0: the picture is T, in code values.
			const Floats out = rig.Output();
			std::vector< double > T( static_cast< size_t >( raster.w ) * raster.h );
			for( size_t i = 0; i < T.size(); ++i )
				T[ i ] = linearOf( out[ 4 * i ] );
			return T;
		};
		const HMagnets m = hDefaultMagnets( static_cast< double >( raster.w ) / raster.h );

		//The instrument: the same density, the angles set to the field's.
		PatchErrors known;
		int calibrated = 0;
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, Perturb {}, SheetHooks {}, &white ) )
				return 1;
			setUp( rig );
			rig.plugin.SetHoldCountForTest( true );
			const int count = FilingCount( DensityFromParam( InfoOf( PT_DENSITY ).defaultValue ), L );
			std::vector< Filing >& f = rig.plugin.SheetForTest().FilingsForTest();
			f.clear();
			std::vector< double > angles;
			for( int k = 0; k < count; ++k )
			{
				Filing g = Sheet::Scattered( 7, k );
				double b[ 3 ];
				hField( m, g.x, g.y, b );
				g.theta = hWrap( std::atan2( b[ 1 ], b[ 0 ] ) );
				f.push_back( g );
				angles.push_back( g.theta );
			}
			if( !rig.Render( 1 ) )
				return 1;
			patchError( readPatches( transmissionOf( rig ), raster.w, raster.h, side ), patchTruth( f, angles, raster.w, raster.h, side ), &calibrated, &known );
		}

		//Lone needles, tapped 40 times: the harness predicts every angle.
		double error = 0.0, predictedVsField = 0.0, bias = 0.0;
		int used = 0, biased = 0;
		{
			Rig rig;
			SheetHooks lone;
			lone.noChains    = true;
			lone.noContact   = true;
			lone.holdCentres = true;
			if( !startRig( rig, raster.w, raster.h, perturb, lone, &white ) )
				return 1;
			setUp( rig );
			if( !rig.Render( 1 ) )
				return 1;
			std::vector< Filing > want = rig.plugin.State().Filings();
			for( int tap = 0; tap < 40; ++tap )
				if( !rig.Press( PT_TAP ) )
					return 1;
			const double mu  = FrictionFromParam( rig.plugin.GetById( PT_FRICTION ) );
			const double tau = HN().refDose * TapStrengthFromParam( rig.plugin.GetById( PT_TAP_STRENGTH ) ) / Sheet::kPassesPerTap;
			for( int pass = 0; pass < 40 * Sheet::kPassesPerTap; ++pass )
				for( Filing& w : want )
					w.theta = hLonePass( m, w, L, mu, tau );
			std::vector< double > angles;
			for( const Filing& w : want )
				angles.push_back( w.theta );
			const auto truth = patchTruth( want, angles, raster.w, raster.h, side );
			PatchErrors each;
			error = patchError( readPatches( transmissionOf( rig ), raster.w, raster.h, side ), truth, &used, &each );
			//The instrument's bias over the same patches of the known sheet.
			for( const auto& e : each )
			{
				const auto k = known.find( e.first );
				if( k == known.end() )
					continue;
				bias += k->second;
				++biased;
			}
			bias = biased ? degrees( bias / biased ) : 90.0;
			double s = 0.0;
			int n    = 0;
			for( const auto& e : truth )
			{
				if( e.second.count < 5 || e.second.coherence < 0.8 )
					continue;
				const double x = ( ( e.first.first + 0.5 ) * side - 0.5 * raster.w ) / raster.h, y = ( ( e.first.second + 0.5 ) * side - 0.5 * raster.h ) / raster.h;
				double b[ 3 ];
				hField( m, x, y, b );
				s += std::fabs( hWrap( e.second.angle - std::atan2( b[ 1 ], b[ 0 ] ) ) );
				++n;
			}
			predictedVsField = n ? degrees( s / n ) : 90.0;
		}
		Check( calibrated > 10 && used > 10 && biased == used && error <= bias + 1.0,
		       fmt( "%4dx%-4d lone needles, 40 taps: the picture's orientation is %.2f deg from the harness's prediction over %d patches %d px square; the "
		            "instrument reads the same patches of a sheet laid along the field to %.2f deg (tolerance that + 1 deg). (The prediction is %.1f deg from "
		            "the dipole's direction at the patch centres.)",
		            raster.w, raster.h, error, used, side, bias, predictedVsField ) );

		//The full model, against the dipole's own direction.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb, SheetHooks {}, &white ) )
				return 1;
			setUp( rig );
			for( int tap = 0; tap < 40; ++tap )
				if( !rig.Press( PT_TAP ) )
					return 1;
			const std::vector< PatchReading > read = readPatches( transmissionOf( rig ), raster.w, raster.h, side );
			const double mu = FrictionFromParam( rig.plugin.GetById( PT_FRICTION ) );
			double s = 0.0;
			int n    = 0;
			for( const PatchReading& r : read )
			{
				const double x = ( ( r.px + 0.5 ) * side - 0.5 * raster.w ) / raster.h, y = ( ( r.py + 0.5 ) * side - 0.5 * raster.h ) / raster.h;
				double b[ 3 ];
				hField( m, x, y, b );
				if( HN().torquePerB2 * ( b[ 0 ] * b[ 0 ] + b[ 1 ] * b[ 1 ] ) < 4.0 * HN().F( mu ) || r.energy <= 0.0 )
					continue;
				s += std::fabs( hWrap( r.angle - std::atan2( b[ 1 ], b[ 0 ] ) ) );
				++n;
			}
			const double full = n ? degrees( s / n ) : 90.0;
			Check( n > 5 && full < 22.5, fmt( "%4dx%-4d the full model, 40 taps: the picture's orientation is %.1f deg from the dipole's direction over the %d patches where "
			                                  "A >= 4F at the centre (below half a scattered sheet's 45: chosen)",
			                                  raster.w, raster.h, full, n ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --still
//===========================================================================
int runStill( const Perturb& perturb )
{
	std::printf( "\n=== still: no filing moves without a tap -- not when the magnets move, not when any physics control moves, not over music that\n"
	             "    never makes an onset; and when it does tap, exactly the tap frames change the sheet\n" );
	for( const Raster& raster : kRasters )
	{
		//600 frames of the bed and the hats (no kicks, no ghosts), Tap On Onset,
		//every pole circling and every physics control ramping.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_TAP_ON, static_cast< int >( TapOn::Onset ) );
			rig.Set( PT_TONE, 0.3f );
			rig.beforeFrame = [ &rig ]( int f ) { feedMusic( rig.plugin, f, 0, 30, false, false ); };
			if( !rig.Render( 1 ) )
				return 1;
			const std::vector< Filing > state0 = rig.plugin.State().Filings();
			const Floats out0                  = rig.Output();
			size_t stateChanged = 0, framesChanged = 0;
			rig.beforeFrame = [ &rig ]( int f ) {
				feedMusic( rig.plugin, f, 0, 30, false, false );
				for( int i = 0; i < kMaxPoles; ++i )
				{
					rig.Set( PoleX( i ), static_cast< float >( 0.5 + 0.3 * std::sin( 2.0 * hPi * f / 240.0 + i ) ) );
					rig.Set( PoleY( i ), static_cast< float >( 0.5 + 0.3 * std::cos( 2.0 * hPi * f / 200.0 + i ) ) );
					rig.Set( PoleSign( i ), ( f / 100 + i ) % 2 );
				}
				const float ramp = static_cast< float >( 0.5 + 0.5 * std::sin( 2.0 * hPi * f / 300.0 ) );
				rig.Set( PT_POLES, ( f / 150 ) % 4 );
				rig.Set( PT_STRENGTH, ramp );
				rig.Set( PT_DEPTH, 1.0f - ramp );
				rig.Set( PT_FRICTION, ramp );
				rig.Set( PT_TAP_STRENGTH, ramp );
			};
			for( int f = 1; f <= 600; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				stateChanged += !sameState( rig.plugin.State().Filings(), state0 );
				framesChanged += differing( rig.Output(), out0 ) != 0;
			}
			Check( rig.plugin.Analyser().Onsets() == 0 && stateChanged == 0 && framesChanged == 0,
			       fmt( "%4dx%-4d 600 frames of bed and hats (%llu onsets) with every pole moving and Strength, Depth, Friction, Tap Strength and Poles ramping: "
			            "the sheet differed from frame 0's on %zu frames (memcmp), the picture on %zu (bit-exact, same context)",
			            raster.w, raster.h, static_cast< unsigned long long >( rig.plugin.Analyser().Onsets() ), stateChanged, framesChanged ) );
		}

		//With kicks: the sheet changes on exactly the tap frames.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_TAP_ON, static_cast< int >( TapOn::Onset ) );
			rig.beforeFrame = [ &rig ]( int f ) { feedKicks( rig.plugin, f, 6, 30, true ); };
			if( !rig.Render( 1 ) )
				return 1;
			std::vector< Filing > previous = rig.plugin.State().Filings();
			int taps = 0, mismatched = 0;
			for( int f = 1; f < 240; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				const bool tapped  = rig.plugin.LastDose() > 0.0;
				const bool changed = !sameState( rig.plugin.State().Filings(), previous );
				taps += tapped;
				mismatched += tapped != changed;
				previous = rig.plugin.State().Filings();
			}
			Check( taps >= 7 && mismatched == 0,
			       fmt( "%4dx%-4d 240 frames of kicks: %d taps; %d frames where the sheet changed without a tap or a tap left it as it was", raster.w, raster.h, taps,
			            mismatched ) );

			//A magnet moved between taps: nothing until the next tap, which
			//turns the filings towards the new field.
			rig.beforeFrame = nullptr;
			writeSpectrum( rig.plugin, []( int ) { return 0.0; } );
			rig.Set( PT_TAP_ON, static_cast< int >( TapOn::Off ) );
			const std::vector< Filing > held = rig.plugin.State().Filings();
			rig.Set( PoleX( 0 ), 0.2f );
			rig.Set( PoleY( 0 ), 0.65f );
			if( !rig.Render( 30 ) )
				return 1;
			const bool still   = sameState( rig.plugin.State().Filings(), held );
			const HMagnets m   = hMagnetsOf( rig );
			const double mu    = FrictionFromParam( rig.plugin.GetById( PT_FRICTION ) );
			const double before = meanError( rig.plugin.State().Filings(), m, mu, 2.0 );
			if( !rig.Press( PT_TAP ) )
				return 1;
			const double after = meanError( rig.plugin.State().Filings(), m, mu, 2.0 );
			Check( still && after < before,
			       fmt( "%4dx%-4d pole 1 moved: the sheet %s over the next 30 frames; the next tap took the mean angle to the new field from %.1f to %.1f deg",
			            raster.w, raster.h, still ? "stayed bit for bit" : "CHANGED", before, after ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --taps
//===========================================================================
int runTaps( const Perturb& perturb )
{
	std::printf( "\n=== taps: the music taps the sheet -- on the kicks, the ghosts only at high Sensitivity, softly on a soft kick, on the beat, on a press\n" );
	for( const Raster& raster : kRasters )
	{
		auto tapFrames = [ & ]( float sensitivity, std::vector< double >* doses ) {
			std::vector< int > frames;
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return frames;
			rig.Set( PT_TAP_ON, static_cast< int >( TapOn::Onset ) );
			rig.Set( PT_SENSITIVITY, sensitivity );
			rig.beforeFrame = [ &rig ]( int f ) { feedKicks( rig.plugin, f, 6, 30, true ); };
			for( int f = 0; f < 186; ++f )
			{
				if( !rig.Render( 1 ) )
					return frames;
				if( rig.plugin.LastDose() > 0.0 )
				{
					frames.push_back( f );
					if( doses )
						doses->push_back( rig.plugin.LastDose() );
				}
			}
			return frames;
		};
		auto list = []( const std::vector< int >& v ) {
			std::string s;
			for( int f : v )
				s += ( s.empty() ? "" : " " ) + std::to_string( f );
			return s.empty() ? std::string( "none" ) : s;
		};
		const std::vector< int > kicks = { 6, 36, 66, 96, 126, 156 };
		//At 0.5 the first ghost, before the second kick has built the
		//detector's floor, is heard too (and taps at a few % of a kick).
		std::vector< int > atHalfWant = kicks;
		atHalfWant.insert( atHalfWant.begin() + 1, 21 );
		std::vector< int > withGhosts  = kicks;
		for( int f : { 21, 51, 81, 111, 141, 171 } )
			withGhosts.push_back( f );
		std::sort( withGhosts.begin(), withGhosts.end() );
		std::vector< double > doses;
		const std::vector< int > atHalf = tapFrames( 0.5f, &doses );
		const std::vector< int > atOne  = tapFrames( 1.0f, nullptr );
		Check( atHalf == atHalfWant, fmt( "%4dx%-4d a cold start into the bed, kicks from frame 6, Sensitivity 0.5: taps on %s (the kicks and the first ghost, "
		                                  "before the floor has built: %s; nothing on frame 0)",
		                                  raster.w, raster.h, list( atHalf ).c_str(), list( atHalfWant ).c_str() ) );
		Check( atOne == withGhosts, fmt( "%4dx%-4d Sensitivity 1: taps on %s (kicks and ghosts: %s)", raster.w, raster.h, list( atOne ).c_str(), list( withGhosts ).c_str() ) );
		//Doses by frame: the kicks alternate soft (6, 66, 126) and hard (36, 96,
		//156). The first soft kick, at a cold start, has nothing to be soft
		//against, so it is left out.
		std::map< int, double > dose;
		for( size_t k = 0; k < atHalf.size() && k < doses.size(); ++k )
			dose[ atHalf[ k ] ] = doses[ k ];
		const double soft  = std::max( dose[ 66 ], dose[ 126 ] ), hard = std::min( { dose[ 36 ], dose[ 96 ], dose[ 156 ] } );
		const double ghost = dose[ 21 ];
		//The ghost's flux against a soft kick's, from the music's own numbers
		//(bins of 0.15 plus 0.02 or 0.6, square-rooted as the analyser does).
		const double ratio = ( std::sqrt( 0.17 ) - std::sqrt( 0.15 ) ) / ( std::sqrt( 0.75 ) - std::sqrt( 0.15 ) );
		//A hard kick is the loudest onset yet, so its hit is 1: the full
		//reference dose (Tap Strength 1), to float rounding.
		const double hardMax = std::max( { dose[ 36 ], dose[ 96 ], dose[ 156 ] } );
		Check( doses.size() == atHalfWant.size() && std::fabs( hard - 1.0 ) <= 1e-6 && std::fabs( hardMax - 1.0 ) <= 1e-6 && hard > soft && soft > 0.0 &&
		           ghost < 2.0 * ratio * soft,
		       fmt( "%4dx%-4d a hard kick taps harder: hard kicks at %.7f to %.7f x the reference (the loudest onset yet: 1, tolerance 1e-6, float), soft kicks at "
		            "most %.3f; the ghost's %.4f (its flux is %.1f%% of a soft kick's: tolerance twice that, of the soft kicks' dose)",
		            raster.w, raster.h, hard, hardMax, soft, ghost, 100.0 * ratio ) );

		//Tap On Beat at 128 BPM: once a beat, on the frame its boundary falls in.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.bpm = 128.0;
			rig.Set( PT_TAP_ON, static_cast< int >( TapOn::Beat ) );
			std::vector< int > got, want;
			for( int f = 0; f < 240; ++f )
			{
				if( !rig.Render( 1 ) )
					return 1;
				if( rig.plugin.LastDose() > 0.0 )
					got.push_back( f );
				if( f > 0 )
				{
					const double p0 = std::fmod( rig.Seconds( f - 1 ) * 128.0 / 240.0, 1.0 ) * 4.0, p1 = std::fmod( rig.Seconds( f ) * 128.0 / 240.0, 1.0 ) * 4.0;
					if( std::floor( p1 + ( p1 < p0 ? 4.0 : 0.0 ) ) > std::floor( p0 ) )
						want.push_back( f );
				}
			}
			Check( got == want && !got.empty(), fmt( "%4dx%-4d Tap On Beat at 128 BPM: taps on %s (the harness's beat boundaries: %s)", raster.w, raster.h,
			                                         list( got ).c_str(), list( want ).c_str() ) );
		}

		//A press taps once, at full strength x Tap Strength.
		{
			Rig rig;
			if( !startRig( rig, raster.w, raster.h, perturb ) )
				return 1;
			rig.Set( PT_TAP_STRENGTH, ParamFromTapStrength( 1.5 ) );
			std::vector< int > got;
			double dose = 0.0;
			for( int f = 0; f < 30; ++f )
			{
				if( f == 10 )
					rig.Set( PT_TAP, 1.0f );
				if( f == 14 )
					rig.Set( PT_TAP, 0.0f );
				if( !rig.Render( 1 ) )
					return 1;
				if( rig.plugin.LastDose() > 0.0 )
				{
					got.push_back( f );
					dose = rig.plugin.LastDose();
				}
			}
			Check( got == std::vector< int > { 10 } && std::fabs( dose - 1.5 ) < 1e-6,
			       fmt( "%4dx%-4d Tap held from frame 10 to 13: taps on %s, dose %.4f (Tap Strength 1.5)", raster.w, raster.h, list( got ).c_str(), dose ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --draw
//===========================================================================
int runDraw( const Perturb& perturb )
{
	std::printf( "\n=== draw: each needle's ink is its ellipse's area, centred on it and along it; the clip shows through as clip x T\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		const Floats card = buildCard( raster.w, raster.h );
		if( !startRig( rig, raster.w, raster.h, perturb, SheetHooks {}, &card ) )
			return 1;
		//Sparse needles an eighth of the frame long, far apart, at known places
		//and angles, none on a pixel's grid.
		const double L = 1.0 / 8.0;
		rig.plugin.SetDrawLengthForTest( L );
		rig.plugin.SetHoldCountForTest( true );
		std::vector< Filing >& f = rig.plugin.SheetForTest().FilingsForTest();
		f.clear();
		for( int i = 0; i < 6; ++i )
			for( int j = 0; j < 3; ++j )
				f.push_back( { -0.75 + 0.3 * i + 0.013 * j, -0.3 + 0.3 * j + 0.007 * i, hWrap( ( 7.0 + 23.0 * ( i * 3 + j ) ) * hPi / 180.0 ) } );
		const std::vector< Filing > needles = f;
		rig.Set( PT_TONE, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const std::vector< double > T = rig.Transmission();
		const double a = 0.5 * L * raster.h, b = a / kAspect;
		const double area = hPi * a * b;
		//What a lattice of pixel centres can miss: the share of the ellipse
		//within a pixel of its two tips, (8/3) b sqrt( 2 / a ) px^2 of pi a b.
		const double tipShare = ( 8.0 / 3.0 ) * b * std::sqrt( 2.0 / a ) / area;
		double worstArea = 0.0, worstCentre = 0.0, worstAngle = 0.0;
		for( const Filing& n : needles )
		{
			const double cx = 0.5 * raster.w + n.x * raster.h, cy = 0.5 * raster.h + n.y * raster.h;
			double s = 0.0, sx = 0.0, sy = 0.0;
			const int r = static_cast< int >( std::ceil( a + 3.0 ) );
			for( int y = static_cast< int >( cy ) - r; y <= static_cast< int >( cy ) + r; ++y )
				for( int x = static_cast< int >( cx ) - r; x <= static_cast< int >( cx ) + r; ++x )
				{
					if( x < 0 || y < 0 || x >= raster.w || y >= raster.h )
						continue;
					const double c = 1.0 - T[ static_cast< size_t >( y ) * raster.w + x ];
					s += c;
					sx += c * ( x + 0.5 );
					sy += c * ( y + 0.5 );
				}
			const double mx = sx / s, my = sy / s;
			double cxx = 0.0, cyy = 0.0, cxy = 0.0;
			for( int y = static_cast< int >( cy ) - r; y <= static_cast< int >( cy ) + r; ++y )
				for( int x = static_cast< int >( cx ) - r; x <= static_cast< int >( cx ) + r; ++x )
				{
					if( x < 0 || y < 0 || x >= raster.w || y >= raster.h )
						continue;
					const double c = 1.0 - T[ static_cast< size_t >( y ) * raster.w + x ];
					cxx += c * ( x + 0.5 - mx ) * ( x + 0.5 - mx );
					cyy += c * ( y + 0.5 - my ) * ( y + 0.5 - my );
					cxy += c * ( x + 0.5 - mx ) * ( y + 0.5 - my );
				}
			worstArea   = std::max( worstArea, std::fabs( s - area ) / area );
			worstCentre = std::max( worstCentre, std::hypot( mx - cx, my - cy ) );
			worstAngle  = std::max( worstAngle, std::fabs( hWrap( 0.5 * std::atan2( 2.0 * cxy, cxx - cyy ) - n.theta ) ) );
		}
		Check( worstArea <= tipShare && worstCentre <= tipShare * a && degrees( worstAngle ) <= 0.5,
		       fmt( "%4dx%-4d %zu needles %.1f x %.1f px: ink worst %.3f%% from pi a b (tolerance %.2f%%: the ellipse within a pixel of its tips), centroid worst "
		            "%.3f px from the centre (tolerance %.2f px: that share x a), axis worst %.3f deg (tolerance 0.5 deg)",
		            raster.w, raster.h, needles.size(), 2.0 * a, 2.0 * b, 100.0 * worstArea, 100.0 * tipShare, worstCentre, tipShare * a, degrees( worstAngle ) ) );

		//Tone 0: out = clip x T in linear light, whatever the clip's colour; Tone
		//0.5: clip x T + Tone x grey x ( 1 - T ).
		double worst[ 2 ] = {};
		const float tones[ 2 ] = { 0.0f, 0.5f };
		for( int k = 0; k < 2; ++k )
		{
			rig.Set( PT_TONE, tones[ k ] );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats out = rig.Output();
			const double ink = tones[ k ] * kInkGrey;
			for( size_t p = 0; p < T.size(); ++p )
				for( int c = 0; c < 3; ++c )
				{
					const double want = codeOf( linearOf( card[ 4 * p + c ] ) * T[ p ] + ink * ( 1.0 - T[ p ] ) );
					worst[ k ]        = std::max( worst[ k ], std::fabs( out[ 4 * p + c ] - want ) * 255.0 );
				}
		}
		Check( worst[ 0 ] <= 2.0 && worst[ 1 ] <= 2.0,
		       fmt( "%4dx%-4d on the look card: Tone 0 is clip x T to %.4f codes, Tone 0.5 is clip x T + grey x (1 - T) to %.4f codes (tolerance 2 codes: two roundings)",
		            raster.w, raster.h, worst[ 0 ], worst[ 1 ] ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: the sheet is clear and the filings opaque -- alpha = a + (1 - a)(1 - T); Mix to the clip; Mix 0 hands the clip through\n" );
	for( const Raster& raster : kRasters )
	{
		Floats card = buildCard( raster.w, raster.h );
		for( int y = 0; y < raster.h; ++y )
			for( int x = 0; x < raster.w; ++x )
				card[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 + 3 ] = x < raster.w / 2 ? 0.0f : std::round( 255.0f * ( 2.0f * x / raster.w - 1.0f ) ) / 255.0f;
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb, SheetHooks {}, &card ) )
			return 1;
		rig.Set( PT_TONE, 0.3f );
		double worst[ 2 ] = {};
		double overClear  = 0.0;
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		for( int k = 0; k < 2; ++k )
		{
			rig.Set( PT_MIX, mixes[ k ] );
			if( !rig.Press( PT_TAP ) )
				return 1;
			const Floats out              = rig.Output();
			const std::vector< double > T = rig.Transmission();
			for( size_t p = 0; p < T.size(); ++p )
			{
				const double a    = card[ 4 * p + 3 ];
				const double full = a + ( 1.0 - a ) * ( 1.0 - T[ p ] );
				worst[ k ]        = std::max( worst[ k ], std::fabs( out[ 4 * p + 3 ] - ( a + ( full - a ) * mixes[ k ] ) ) * 255.0 );
				if( k == 0 && a == 0.0 )
					overClear = std::max( overClear, static_cast< double >( out[ 4 * p + 3 ] ) );
			}
		}
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const size_t through = differing( rig.Output(), card );
		Check( worst[ 0 ] <= 2.0 && worst[ 1 ] <= 2.0 && through == 0 && overClear >= 0.9,
		       fmt( "%4dx%-4d a clip transparent on the left and ramped on the right: alpha at Mix 1 %.4f codes from a + (1 - a)(1 - T), at Mix 0.5 %.4f (tolerance "
		            "2 codes); over alpha 0 the filings reach alpha %.3f; Mix 0: %zu values differ from the clip (bit-exact)",
		            raster.w, raster.h, worst[ 0 ], worst[ 1 ], overClear, through ) );
	}
	return Verdict();
}

//===========================================================================
// --resize
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: the sheet lives in frame heights -- resized mid-run (1280x720 -> 320x180 -> 1920x1080) under the music, it is the sheet of a run\n"
	             "    never resized, and the picture is what a fresh instance draws from it\n" );
	Rig moved, always;
	if( !startRig( moved, 1280, 720, perturb ) || !startRig( always, 1280, 720, perturb ) )
		return 1;
	for( Rig* r : { &moved, &always } )
	{
		r->Set( PT_TAP_ON, static_cast< int >( TapOn::Onset ) );
		r->Set( PT_TONE, 0.3f );
		Rig* rr        = r;
		r->beforeFrame = [ rr ]( int f ) { feedKicks( rr->plugin, f, 6, 30, true ); };
	}
	if( !moved.Render( 60 ) || !moved.Resize( 320, 180 ) || !moved.Render( 60 ) || !moved.Resize( 1920, 1080 ) || !moved.Render( 30 ) || !always.Render( 150 ) )
		return 1;
	const bool same = sameState( moved.plugin.State().Filings(), always.plugin.State().Filings() );
	const unsigned long long taps = moved.plugin.State().Taps();

	Rig fresh;
	if( !startRig( fresh, 1920, 1080, Perturb {} ) )
		return 1;
	fresh.Set( PT_TONE, 0.3f );
	fresh.plugin.SetHoldCountForTest( true );
	fresh.plugin.SheetForTest().FilingsForTest() = moved.plugin.State().Filings();
	if( !fresh.Render( 1 ) )
		return 1;
	const Floats a = moved.Output(), b = fresh.Output();
	const size_t differ = differing( a, b );
	Check( same && taps >= 4 && differ == 0,
	       fmt( "after %llu taps across the two resizes, the sheet %s a run kept at 1280x720 (memcmp); at 1920x1080 %zu of %zu values differ from a fresh instance "
	            "drawing the same sheet (bit-exact, same context)",
	            taps, same ? "equals" : "DIFFERS FROM", differ, a.size() ) );
	return Verdict();
}

//===========================================================================
// --clock
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: a host clock at 4.99e8 ms (Resolume's has been seen there) taps the sheet on the frames and at the doses a clock from 0 s does,\n"
	             "    and a clock left to settle its own unit settles on the right one\n" );
	for( const Raster& raster : kRasters )
	{
		Rig zero, late;
		if( !startRig( zero, raster.w, raster.h, perturb ) || !startRig( late, raster.w, raster.h, perturb ) )
			return 1;
		late.plugin.SetClockScaleForTest( 0.001 );
		late.timeOffset = 4.99e8;
		late.timeScale  = 1000.0;
		for( Rig* r : { &zero, &late } )
		{
			r->Set( PT_TAP_ON, static_cast< int >( TapOn::Onset ) );
			Rig* rr        = r;
			r->beforeFrame = [ rr ]( int f ) { feedKicks( rr->plugin, f, 6, 30, true ); };
		}
		int differ = 0, taps = 0;
		bool same  = true;
		for( int f = 0; f < 180; ++f )
		{
			if( !zero.Render( 1 ) || !late.Render( 1 ) )
				return 1;
			differ += zero.plugin.LastDose() != late.plugin.LastDose();
			taps += zero.plugin.LastDose() > 0.0;
			if( f % 60 == 59 )
				same = same && sameState( zero.plugin.State().Filings(), late.plugin.State().Filings() );
		}
		Check( differ == 0 && same && taps >= 5,
		       fmt( "%4dx%-4d 180 frames of kicks: %d taps; %d frames whose dose differs; the sheets at frames 60, 120, 180 %s (memcmp)", raster.w, raster.h, taps,
		            differ, same ? "equal" : "DIFFER" ) );

		//The unit, unforced: this harness renders far faster than real time,
		//as an offline host or oxbow's selftest does, so the wall-clock vote
		//cannot settle it and the step's size has to.
		for( const double unit : { 1.0, 1000.0 } )
		{
			Rig r;
			if( !startRig( r, raster.w, raster.h, perturb ) )
				return 1;
			r.plugin.SetClockScaleForTest( 0.0 );
			r.timeOffset = unit == 1.0 ? 12.5 : 4.99e8;
			r.timeScale  = unit;
			if( !r.Render( 60 ) )
				return 1;
			const double want = 1.0 / unit;
			const double got  = r.plugin.ClockScale();
			const double host = ( r.timeOffset + r.timeScale * r.Seconds( r.frame - 1 ) ) * want;
			Check( got == want && std::fabs( r.plugin.HostNow() - host ) < 1e-6,
			       fmt( "%4dx%-4d a host clock in %s at 60 fps, rendered faster than real time: settled on %s by frame 60, and reads %.6f s (host %.6f s)", raster.w,
			            raster.h, unit == 1.0 ? "seconds" : "milliseconds", got == 1.0 ? "seconds" : ( got == 0.001 ? "milliseconds" : "nothing" ), r.plugin.HostNow(),
			            host ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		GLuint vao = 0, buffer = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		int wrong = 0;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glActiveTexture( GL_TEXTURE3 );
			rig.plugin.SetTime( rig.Seconds( frame ) );
			rig.Set( PT_TAP, frame == 1 ? 1.0f : 0.0f );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += glGetError() != GL_NO_ERROR;
		}
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		Check( wrong == 0, fmt( "%4dx%-4d three frames (one a tap) with a host viewport, blend, clear colour, vertex array, buffer and texture unit of its own: %d "
		                        "things not put back, GL errors included",
		                        raster.w, raster.h, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "field", runField, true },
		{ "law", runLaw, true },
		{ "step", runStep, true },
		{ "align", runAlign, true },
		{ "dipole", runDipole, false },
		{ "still", runStill, false },
		{ "taps", runTaps, false },
		{ "draw", runDraw, false },
		{ "alpha", runAlpha, false },
		{ "resize", runResize, false },
		{ "clock", runClock, false },
		{ "glstate", runGLState, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	auto sheet = []( std::function< void( SheetHooks& ) > f ) { return [ f ]( Perturb& p ) { p.sheet = f; }; };
	add( "field", runField, "the poles' field falling as 1/r^3", []( Perturb& p ) { p.cubeLaw = true; } );
	add( "field", runField, "every pole read as N: the dipole's sign lost", []( Perturb& p ) { p.signIgnored = true; } );
	add( "law", runLaw, "torque in proportion to B, not B^2", sheet( []( SheetHooks& h ) { h.torqueLinear = true; } ) );
	add( "law", runLaw, "friction ignored", sheet( []( SheetHooks& h ) { h.noFriction = true; } ) );
	add( "law", runLaw, "a compass needle: polar, not headless", sheet( []( SheetHooks& h ) { h.compass = true; } ) );
	add( "step", runStep, "the cell grid misses the diagonal cells", sheet( []( SheetHooks& h ) { h.noDiagonals = true; } ) );
	add( "step", runStep, "no contact between filings", sheet( []( SheetHooks& h ) { h.noContact = true; } ) );
	add( "align", runAlign, "a tap snaps every filing that turns onto the field", sheet( []( SheetHooks& h ) { h.snap = true; } ) );
	add( "align", runAlign, "the slide down |B|^2", sheet( []( SheetHooks& h ) { h.slideDown = true; } ) );
	add( "dipole", runDipole, "the magnets' field from the first pole only", []( Perturb& p ) { p.onePole = true; } );
	add( "still", runStill, "the sheet never pins: a small pass every frame", []( Perturb& p ) { p.neverPins = true; } );
	add( "still", runStill, "a moved magnet drags the filings at once", []( Perturb& p ) { p.magnetDrags = true; } );
	add( "taps", runTaps, "the onset detector never primed", []( Perturb& p ) { p.unprimed = true; } );
	add( "taps", runTaps, "onset flux over every bin, hi-hats included", []( Perturb& p ) { p.fullSpectrum = true; } );
	add( "draw", runDraw, "the filings' grey added to the clip, which is not stopped", []( Perturb& p ) { p.compositeModel = 1; } );
	add( "alpha", runAlpha, "alpha mix( a, 1, Mix ): the sheet opaque", []( Perturb& p ) { p.compositeModel = 2; } );
	add( "resize", runResize, "a new raster throws a fresh sheet", []( Perturb& p ) { p.resizeScatters = true; } );
	add( "clock", runClock, "the host's clock read in single precision", []( Perturb& p ) { p.floatClock = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED, and the wall clock of a
// whole frame (a tap's passes run on the CPU, on the tap frame).
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		const char* what;
		std::function< void( Rig& ) > set;
		bool all;///< every raster (else 1080p only)
	};
	const Case cases[] = {
		{ "defaults, kicks every 0.5 s", []( Rig& ) {}, true },
		{ "Density 0.6, Size 1/200 (60000 filings)", []( Rig& r ) {
			 r.Set( PT_DENSITY, 1.0f );
			 r.Set( PT_SIZE, 0.0f );
		 },
		  false },
	};
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) and the frame's wall clock, music in (Tap On Onset); 240 frames of warm-up, 120 timed\n"
	             "    (4K once: 60 of warm-up and 120 timed; the first timed tap frame at each raster has been seen to cost up to 20 ms more than the rest, so read the median)\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			if( !c.all && size.w != 1920 )
				continue;
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			defaults( rig );
			c.set( rig );
			rig.beforeFrame = [ &rig ]( int f ) { feedKicks( rig.plugin, f, 0, 30, true ); };
			const bool big  = size.w == 3840;
			if( !rig.Render( big ? 60 : 240 ) )
				return 1;
			const int timed = 120;
			std::vector< double > gpu, quiet, tapped;
			for( int f = 0; f < timed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				glBeginQuery( GL_TIME_ELAPSED, query );
				if( !rig.Render( 1 ) )
					return 1;
				glEndQuery( GL_TIME_ELAPSED );
				GLuint64 ns = 0;
				glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
				gpu.push_back( static_cast< double >( ns ) * 1e-6 );
				const double wall = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count();
				( rig.plugin.LastDose() > 0.0 ? tapped : quiet ).push_back( wall );
			}
			std::sort( gpu.begin(), gpu.end() );
			std::sort( quiet.begin(), quiet.end() );
			std::sort( tapped.begin(), tapped.end() );
			std::printf( "  %-42s %-6s %6zu filings  GPU median %6.2f ms, worst %6.2f (%4.1f%% of a 60 fps frame); wall %5.2f ms, tap frames %5.2f ms median "
			             "%5.2f worst (%zu)\n",
			             c.what, size.name, rig.plugin.State().Filings().size(), gpu[ gpu.size() / 2 ], gpu.back(), 100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ),
			             quiet.empty() ? 0.0 : quiet[ quiet.size() / 2 ], tapped.empty() ? 0.0 : tapped[ tapped.size() / 2 ], tapped.empty() ? 0.0 : tapped.back(),
			             tapped.size() );
		}
	glDeleteQueries( 1, &query );
	return 0;
}
