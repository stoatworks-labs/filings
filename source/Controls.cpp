#include "Controls.h"

#include "Physics.h"
#include "Sheet.h"

#include <algorithm>
#include <cmath>

namespace filings
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

constexpr double kStrengthLow = 0.002, kStrengthHigh = 0.1;
constexpr double kDepthLow = 0.01, kDepthHigh = 0.25;
constexpr double kTapHigh = 2.0;
constexpr double kFrictionLow = 0.05, kFrictionHigh = 1.0;
constexpr double kDensityLow = 0.05, kDensityHigh = 0.6;
constexpr double kSizeLow = 1.0 / 200.0, kSizeHigh = 1.0 / 30.0;

const char* const kPoleNames[] = { "1", "2", "3", "4" };
const char* const kSignNames[] = { "N", "S" };
const char* const kTapOnNames[] = { "Onset", "Beat", "Half Beat", "2 Beats", "Bar", "Off" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0 };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count };
}
ControlInfo event( unsigned int id, const char* name, const char* group )
{
	return { id, name, group, Kind::Event, 0.0f, nullptr, 0 };
}

//---------------------------------------------------------------------------
// The defaults: a bar magnet (N left, S right) a third of the frame apart,
// 40 mT straight above each pole at 12% of the frame's height under the sheet;
// onsets tap at the reference dose; mu 0.2; filings a seventieth of the frame
// long covering 20% of the sheet, drawn a dark grey (Tone 0.15). AGENTS.md
// says how they were chosen (over Resolume's demo clips, with the harness's
// music).
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_POLES, "Poles", "Magnets", 1.0f, kPoleNames, 4 ),
	standard( PT_STRENGTH, "Strength", "Magnets", ParamFromStrength( 0.04 ) ),
	standard( PT_DEPTH, "Depth", "Magnets", ParamFromDepth( 0.12 ) ),
	standard( PT_POLE1_X, "Pole 1 X", "Magnets", 0.35f ),
	standard( PT_POLE1_Y, "Pole 1 Y", "Magnets", 0.5f ),
	option( PT_POLE1_SIGN, "Pole 1 Sign", "Magnets", 0.0f, kSignNames, 2 ),
	standard( PT_POLE2_X, "Pole 2 X", "Magnets", 0.65f ),
	standard( PT_POLE2_Y, "Pole 2 Y", "Magnets", 0.5f ),
	option( PT_POLE2_SIGN, "Pole 2 Sign", "Magnets", 1.0f, kSignNames, 2 ),
	standard( PT_POLE3_X, "Pole 3 X", "Magnets", 0.5f ),
	standard( PT_POLE3_Y, "Pole 3 Y", "Magnets", 0.82f ),
	option( PT_POLE3_SIGN, "Pole 3 Sign", "Magnets", 0.0f, kSignNames, 2 ),
	standard( PT_POLE4_X, "Pole 4 X", "Magnets", 0.5f ),
	standard( PT_POLE4_Y, "Pole 4 Y", "Magnets", 0.18f ),
	option( PT_POLE4_SIGN, "Pole 4 Sign", "Magnets", 1.0f, kSignNames, 2 ),

	{ PT_AUDIO, "Audio", "Tap", Kind::Buffer, 0.0f, nullptr, 0 },
	option( PT_TAP_ON, "Tap On", "Tap", 0.0f, kTapOnNames, static_cast< int >( TapOn::Count ) ),
	event( PT_TAP, "Tap", "Tap" ),
	standard( PT_SENSITIVITY, "Sensitivity", "Tap", 0.5f ),
	standard( PT_TAP_STRENGTH, "Tap Strength", "Tap", ParamFromTapStrength( 1.0 ) ),

	standard( PT_FRICTION, "Friction", "Filings", ParamFromFriction( 0.2 ) ),
	standard( PT_DENSITY, "Density", "Filings", ParamFromDensity( 0.2 ) ),
	standard( PT_SIZE, "Size", "Filings", ParamFromSize( 1.0 / 70.0 ) ),
	event( PT_SCATTER, "Scatter", "Filings" ),

	standard( PT_TONE, "Tone", "Look", 0.15f ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const long i = std::lround( static_cast< double >( value ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

unsigned int PoleX( int i )
{
	return PT_POLE1_X + 3u * static_cast< unsigned int >( std::clamp( i, 0, 3 ) );
}
unsigned int PoleY( int i )
{
	return PoleX( i ) + 1u;
}
unsigned int PoleSign( int i )
{
	return PoleX( i ) + 2u;
}

double StrengthFromParam( float v )
{
	return geometric( v, kStrengthLow, kStrengthHigh );
}
float ParamFromStrength( double t )
{
	return inverseGeometric( t, kStrengthLow, kStrengthHigh );
}
double DepthFromParam( float v )
{
	return geometric( v, kDepthLow, kDepthHigh );
}
float ParamFromDepth( double su )
{
	return inverseGeometric( su, kDepthLow, kDepthHigh );
}
double TapStrengthFromParam( float v )
{
	return linear( v, 0.0, kTapHigh );
}
float ParamFromTapStrength( double dose )
{
	return inverseLinear( dose, 0.0, kTapHigh );
}
double FrictionFromParam( float v )
{
	return geometric( v, kFrictionLow, kFrictionHigh );
}
float ParamFromFriction( double mu )
{
	return inverseGeometric( mu, kFrictionLow, kFrictionHigh );
}
double DensityFromParam( float v )
{
	return linear( v, kDensityLow, kDensityHigh );
}
float ParamFromDensity( double c )
{
	return inverseLinear( c, kDensityLow, kDensityHigh );
}
double SizeFromParam( float v )
{
	return geometric( v, kSizeLow, kSizeHigh );
}
float ParamFromSize( double su )
{
	return inverseGeometric( su, kSizeLow, kSizeHigh );
}

int FilingCount( double coverage, double length )
{
	const double area   = 4.0 * Sheet::kHalfWidth * Sheet::kHalfHeight;
	const double a      = 0.5 * length, b = a / kAspect;
	const double filing = kPi * a * b;
	const double count  = std::round( std::max( 0.0, coverage ) * area / std::max( filing, 1e-12 ) );
	return static_cast< int >( std::min( count, static_cast< double >( Sheet::kMaxFilings ) ) );
}

} // namespace filings
