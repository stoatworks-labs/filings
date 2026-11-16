#include "Trigger.h"

#include <algorithm>
#include <cmath>

namespace filings
{

double Trigger::Advance( const TriggerInputs& in )
{
	double dose = 0.0;
	if( in.presses > 0 )
		dose = 1.0;
	if( in.on == TapOn::Onset && in.onset )
		dose = std::max( dose, std::clamp( in.kick, 0.0, 1.0 ) );

	const double tempo    = in.bpm > 1.0 ? in.bpm : 120.0;
	const double phaseNow = in.hostBeat ? std::clamp( in.barPhase, 0.0, 1.0 ) : std::fmod( in.now * tempo / 240.0, 1.0 );
	double step           = 0.0;
	switch( in.on )
	{
	case TapOn::Beat: step = 0.25; break;
	case TapOn::HalfBeat: step = 0.125; break;
	case TapOn::TwoBeats: step = 0.5; break;
	case TapOn::Bar: step = 1.0; break;
	default: break;
	}
	if( step > 0.0 && lastPhase >= 0.0 && in.dt > 0.0 )
	{
		double p0 = lastPhase, p1 = phaseNow;
		if( p1 < p0 )
			p1 += 1.0;
		//A boundary in ( p0, p1 ]: the frame it falls in taps.
		if( p1 > p0 && ( std::floor( p0 / step + 1e-9 ) + 1.0 ) * step <= p1 + 1e-12 )
			dose = std::max( dose, 1.0 );
	}
	lastPhase = phaseNow;
	return dose;
}

} // namespace filings
