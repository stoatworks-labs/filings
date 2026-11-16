#pragma once

#include <array>

/**
	The audio side: a spectrum in, an onset and its level out.

	fountain's, which is ferrofluid's, which is schlieren's, which is
	millpond's, which is rosette's, which adapted it from macroblock's
	analyser -- the one that has been driven with real music through
	Resolume's FFT buffer. Filings uses the onset detector (Tap On Onset: each
	onset taps the sheet) and the level it latches (a soft kick taps softly),
	including the primed first frame that keeps it from going deaf on a clip
	trigger. Its flux is taken over the lowest quarter of the bins only, so
	kicks and snares tap the sheet and hi-hats do not (fountain's finding: over
	the full spectrum a busy hat fired from the cold start).

	**Nobody has measured where Resolume's 64 bins sit in frequency**
	(fleet-wide). That the kick lives in the lowest quarter is an assumption;
	AGENTS.md says so.

	**Where the audio comes from.** FFGL has no audio path. What Resolume
	provides is a buffer parameter declared `FF_USAGE_FFT`, which the host
	fills with a 64-bin spectrum once per frame -- a *modulation* source at
	video rate, not a signal source, so the smallest interval this can
	resolve is a frame and a kick lands on the frame after the transient.

	**Normalised against its own recent peak**, so the same Sensitivity means
	the same thing on a quiet stem and a mastered track.
*/
namespace filings::audio
{

/// The spectrum Resolume delivers. Fixed by the host, not chosen here.
constexpr int kBins = 64;

struct Settings
{
	float attackSeconds  = 0.010f;
	float releaseSeconds = 0.250f;

	/// How easily an onset fires: the margin over the adaptive flux floor.
	float sensitivity = 0.6f;

	/// The time constant the latched kick decays with.
	float holdSeconds = 0.30f;
};

class Analyser
{
public:
	/// `bins` is what the host handed over; `count` may be less than kBins if
	/// it handed over fewer. `dt` is the frame in seconds.
	void Update( const float* bins, int count, float dt, const Settings& settings );

	/// The full-range level, normalised against its recent peak, 0..1.
	float Level() const;

	/// The latched kick: the level an onset arrived at, decaying since. 0..1.
	float Kick() const;

	/// How hard the last onset hit: its flux over the recent peak of onset
	/// fluxes (a slow decay, the level's), latched at the onset, 0..1. A
	/// soft kick after a hard one is below 1, a ghost note far below. This is
	/// what Tap On Onset scales a tap by. (Kick() latches the full-range
	/// level, which a peak normaliser holds near 1 on every kick, soft or hard.)
	float Hit() const
	{
		return hit;
	}

	/// A band's level, normalised against that band's own recent peak, 0..1.
	/// 0 Low, 1 Mid, 2 High, 3 Full.
	float BandLevel( int band ) const;

	/// Whether anything has been heard lately: the normalising peak is above
	/// its floor. With no audio routed it never is.
	bool HasSignal() const;

	/// True on the frame an onset was detected.
	bool Fired() const
	{
		return fired;
	}

	/// How many onsets have been detected since the plugin loaded.
	unsigned long long Onsets() const
	{
		return onsets;
	}

	/// Forget everything. Used when the host's clock jumps.
	void Reset();

	/// The --late negative control: the cold start left as it was, unprimed.
	void SetUnprimedForTest( bool on )
	{
		neverPrime = on;
	}

	/// The --taps negative control: onset flux over every bin, hi-hats included.
	void SetFullSpectrumForTest( bool on )
	{
		fullSpectrum = on;
	}

private:
	std::array< float, kBins > binLevel {};
	std::array< float, kBins > binPrevious {};

	static constexpr int kBands = 4;
	std::array< float, kBands > band {};    ///< enveloped mean per band
	std::array< float, kBands > bandPeak {};///< and its normalising peak

	float level      = 0.0f;///< enveloped, sqrt-compressed
	float peak       = 0.0f;///< slow decay, the normalising reference
	float flux       = 0.0f;///< positive spectral difference this frame
	float fluxMean   = 0.0f;///< running mean of the above, the adaptive floor
	float held       = 0.0f;///< the latched kick, decaying
	float hit        = 0.0f;///< the last onset's flux over hitPeak
	float hitPeak    = 0.0f;///< the onsets' flux, slow decay
	float refractory = 0.0f;///< seconds still to wait before another onset
	bool fired       = false;

	/// False until a frame has been seen. The first frame has no previous
	/// frame, so its flux is not small -- it is undefined, and computing it
	/// against a buffer of zeroes reports the entire spectrum as a rise. See
	/// Update().
	bool primed = false;
	bool neverPrime   = false;
	bool fullSpectrum = false;

	unsigned long long onsets = 0;
};

} // namespace filings::audio
