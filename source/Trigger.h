#pragma once

#include "Controls.h"

/**
    When the sheet is tapped, and how hard. Plain C++, no GL.

    A frame taps at most once: a tap is the sheet jolted loose for a moment,
    and two in one 16 ms frame are one jolt. The tap's dose (x the reference,
    before Tap Strength) is the largest of what asked for it that frame:

    - a press of Tap: 1;
    - an onset, with Tap On Onset: how hard it hit (the analyser's Hit(): its
      flux over the recent peak of onset fluxes), so a soft kick taps softly
      and a ghost note barely at all;
    - a beat boundary crossed during the frame, with Tap On Beat, Half Beat,
      2 Beats or Bar: 1. The phase is the host's bar phase once it has been
      seen to move, else a clock of the plugin's own at the host's BPM (120
      if none), so Tap On is never dead (ferrofluid's Drive).

    Nothing here reads a level between taps: the audio only ever taps.
*/
namespace filings
{

struct TriggerInputs
{
	TapOn on      = TapOn::Onset;
	int presses   = 0;
	bool onset    = false;
	double kick   = 0.0;  ///< how hard the onset hit, 0..1
	double bpm    = 120.0;
	double barPhase = 0.0;///< 0..1
	bool hostBeat = false;///< the host's bar phase has been seen to move
	double now    = 0.0;  ///< seconds, the plugin's clock
	double dt     = 0.0;  ///< this frame, seconds
};

class Trigger
{
public:
	/// This frame's dose, 0 for no tap.
	double Advance( const TriggerInputs& in );
	void Reset()
	{
		lastPhase = -1.0;
	}

private:
	double lastPhase = -1.0;
};

} // namespace filings
