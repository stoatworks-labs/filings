#include "Physics.h"

#include <algorithm>
#include <cmath>

namespace filings
{

double AxialDemag( double R )
{
	const double s = std::sqrt( R * R - 1.0 );
	return ( 1.0 / ( R * R - 1.0 ) ) * ( R / ( 2.0 * s ) * std::log( ( R + s ) / ( R - s ) ) - 1.0 );
}

Needle MakeNeedle()
{
	Needle n;
	n.length     = kFilingLength;
	n.a          = 0.5 * n.length;
	n.b          = n.a / kAspect;
	n.volume     = 4.0 / 3.0 * kPi * n.a * n.b * n.b;
	n.mass       = kIronDensity * n.volume;
	n.na         = AxialDemag( kAspect );
	n.nr         = 0.5 * ( 1.0 - n.na );
	n.torquePerB2 = n.volume * ( n.nr - n.na ) / ( 2.0 * kMu0 * n.na * n.nr );
	n.chargePerB = kPi * n.b * n.b / ( kMu0 * n.na );
	n.poleOffset = n.length / 3.0;
	//2 A( Bref ) kt = 1: the reference tap divides tan psi by e.
	n.referenceDose = 1.0 / ( 2.0 * n.torquePerB2 * kReferenceField * kReferenceField );
	return n;
}

double Needle::Torque( double field, bool linear ) const
{
	return linear ? torquePerB2 * kReferenceField * std::fabs( field ) : torquePerB2 * field * field;
}

double Needle::FrictionTorque( double mu ) const
{
	return mu * mass * kGravity * length / 4.0;
}

double Needle::FrictionForce( double mu ) const
{
	return mu * mass * kGravity;
}

double WrapHalf( double angle )
{
	double w = std::remainder( angle, kPi );//into [ -pi/2, pi/2 ]
	if( w <= -0.5 * kPi )
		w += kPi;
	return w;
}

double StopAngle( double A, double F )
{
	if( A <= F )
		return 0.5 * kPi;
	return 0.5 * std::asin( std::max( 0.0, F ) / A );
}

double Turn( double psi, double A, double F, double kt )
{
	const double magnitude = std::fabs( psi );
	//Below the threshold, or at the unstable axis, nothing turns: returned as
	//it came, bit for bit.
	if( !( A > F ) || !( kt > 0.0 ) || !( A * std::sin( 2.0 * magnitude ) > F ) )
		return psi;

	//(u - u-)/(u - u+) = ((u0 - u-)/(u0 - u+)) e^(-2 R k t), with u = tan psi,
	//R = sqrt(A^2 - F^2), u+- = (A +- R)/F. Multiplied through by F so that
	//F -> 0 is the frictionless tan psi0 e^(-2Akt) with no infinity on the way:
	//with S = A + R and u- = F / S,
	//  u = [ u-( F u0 - S ) - S( u0 - u- ) E ] / [ F u0 - S - F( u0 - u- ) E ].
	const double f      = std::max( 0.0, F );
	const double R      = std::sqrt( ( A - f ) * ( A + f ) );
	const double S      = A + R;
	const double uMinus = f / S;
	const double u0     = std::tan( magnitude );
	const double E      = std::exp( -2.0 * R * kt );
	const double num    = uMinus * ( f * u0 - S ) - S * ( u0 - uMinus ) * E;
	const double den    = f * u0 - S - f * ( u0 - uMinus ) * E;
	const double turned = std::atan( num / den );
	return psi < 0.0 ? -turned : turned;
}

Field FieldAt( const Magnets& magnets, double x, double y )
{
	Field f;
	const double d  = magnets.depth;
	const int count = magnets.onePole ? std::min( magnets.count, 1 ) : std::clamp( magnets.count, 0, kMaxPoles );
	for( int i = 0; i < count; ++i )
	{
		const Pole& p   = magnets.poles[ i ];
		const double dx = x - p.x, dy = y - p.y;
		const double r2 = dx * dx + dy * dy + d * d;
		//r^-3 for the inverse square (the vector's own length is one power).
		const double inv = magnets.cubeLaw ? 1.0 / ( r2 * r2 ) * d : 1.0 / ( r2 * std::sqrt( r2 ) );
		const double s   = ( magnets.signIgnored ? 1.0 : p.sign ) * magnets.b0 * d * d * inv;
		f.x += s * dx;
		f.y += s * dy;
		f.z += s * d;
	}
	return f;
}

} // namespace filings
