#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in physical units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange`
    could widen it. The conversions live in Controls.cpp, one function per
    control, each with its inverse where a default or the harness needs it.
    Option parameters hold the element index (an option reads back 0..1
    whatever its count: map by index); Tap and Scatter are presses; Audio is
    Resolume's 64-bin FFT buffer.

    Units: tesla for Strength (shown in mT in the docs), frame heights ("su")
    for Depth and Size, the friction coefficient for Friction, sheet coverage
    for Density.
*/
namespace filings
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Magnets -----------------------------------------------------------------
	PT_POLES = 0,
	PT_STRENGTH,
	PT_DEPTH,
	PT_POLE1_X,
	PT_POLE1_Y,
	PT_POLE1_SIGN,
	PT_POLE2_X,
	PT_POLE2_Y,
	PT_POLE2_SIGN,
	PT_POLE3_X,
	PT_POLE3_Y,
	PT_POLE3_SIGN,
	PT_POLE4_X,
	PT_POLE4_Y,
	PT_POLE4_SIGN,

	// -- Tap ---------------------------------------------------------------------
	PT_AUDIO,
	PT_TAP_ON,
	PT_TAP,
	PT_SENSITIVITY,
	PT_TAP_STRENGTH,

	// -- Filings -----------------------------------------------------------------
	PT_FRICTION,
	PT_DENSITY,
	PT_SIZE,
	PT_SCATTER,

	// -- Look --------------------------------------------------------------------
	PT_TONE,
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Event,
	Buffer
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, or the element index
	const char* const* options;
	int optionCount;
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum class TapOn
{
	Onset = 0,///< every onset the audio analyser finds
	Beat,
	HalfBeat,
	TwoBeats,
	Bar,
	Off,///< only the Tap button
	Count
};

/// The X, Y and Sign ids of pole i (0..3).
unsigned int PoleX( int i );
unsigned int PoleY( int i );
unsigned int PoleSign( int i );

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double StrengthFromParam( float v );///< T, 2..100 mT, geometric
float ParamFromStrength( double tesla );
double DepthFromParam( float v );///< su, 0.01..0.25, geometric
float ParamFromDepth( double su );
double TapStrengthFromParam( float v );///< x the reference dose, 0..2, linear
float ParamFromTapStrength( double dose );
double FrictionFromParam( float v );///< mu, 0.05..1, geometric
float ParamFromFriction( double mu );
double DensityFromParam( float v );///< sheet coverage, 0.05..0.6, linear
float ParamFromDensity( double coverage );
double SizeFromParam( float v );///< a filing's length, su, 1/200..1/30, geometric
float ParamFromSize( double su );

/// How many filings cover `coverage` of the sheet when each is `length` su
/// long: coverage x the sheet's area / ( pi a b ), at most Sheet::kMaxFilings.
int FilingCount( double coverage, double length );

/// The filings' grey (linear light) at Tone t: t x kInkGrey.
constexpr double kInkGrey = 0.45;

} // namespace filings
