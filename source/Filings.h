#pragma once

#include "Audio.h"
#include "Controls.h"
#include "PassBuffer.h"
#include "Sheet.h"
#include "Trigger.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <utility>
#include <vector>

namespace filings
{

/**
    The plugin, `SW Filings`: the clip is the light coming up through a sheet
    of iron filings, magnets under the sheet, and the music taps it.

    Each frame: the clock, the audio, then at most one tap (Trigger), which is
    the only thing that moves a filing; then the filings are drawn (only
    re-uploaded when the sheet changed) and composited over the clip.

    Host indices are the ParamIds, About last.
*/
class FilingsPlugin : public CFFGLPlugin
{
public:
	FilingsPlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;
	void SetBeatInfo( float bpm, float barPhase ) override;

	/// Set a control by what it is.
	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// The physics the controls ask for, for a frame `width` x `height`: the
	/// poles are placed on the frame, so their place in frame heights depends
	/// on its aspect.
	SheetParams Params( int width, int height ) const;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	Sheet& SheetForTest()
	{
		return sheet;
	}
	const Sheet& State() const
	{
		return sheet;
	}
	const audio::Analyser& Analyser() const
	{
		return analyser;
	}
	audio::Analyser& AnalyserForTest()
	{
		return analyser;
	}
	/// The last frame's tap: its dose x the reference (Tap Strength
	/// included), 0 for none.
	double LastDose() const
	{
		return lastDose;
	}
	/// --resize's negative control: a new raster throws a fresh sheet.
	void SetResizeScattersForTest( bool on )
	{
		resizeScatters = on;
	}
	/// --still's: the sheet never pins (a small pass every frame without a tap).
	void SetNeverPinsForTest( bool on )
	{
		neverPins = on;
	}
	/// --still's: a moved magnet taps the sheet at once.
	void SetMagnetDragsForTest( bool on )
	{
		magnetDrags = on;
	}
	/// --clock's: the host's clock read in single precision.
	void SetFloatClockForTest( bool on )
	{
		floatClock = on;
	}
	void SetUnprimedForTest( bool on )
	{
		unprimed = on;
	}
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	/// --draw's and --alpha's: 1 adds the filings' grey without stopping the
	/// clip's light; 2 the alpha mix( alpha, 1, Mix ).
	void SetCompositeModelForTest( int model )
	{
		compositeModel = model;
	}
	/// --draw's and --resize's: the count stays whatever the harness wrote
	/// into the sheet, whatever Density and Size say.
	void SetHoldCountForTest( bool on )
	{
		holdCount = on;
	}
	/// --field's and --dipole's negative controls, applied to the magnets
	/// every frame: the field as 1/r^3, every pole read as N, one pole only.
	void SetFieldHooksForTest( bool cubeLaw, bool signIgnored, bool onePole )
	{
		fieldCube = cubeLaw;
		fieldSign = signIgnored;
		fieldOne  = onePole;
	}
	/// --draw's: draw the filings this long (su) without touching the
	/// physics' length.
	void SetDrawLengthForTest( double su )
	{
		drawLength = su;
	}
	GLuint TransmissionTextureID() const
	{
		return transmission.TextureID();
	}
	double HostNow() const
	{
		return now;
	}
	/// The host clock's unit as settled: 1 seconds, 0.001 milliseconds, 0 not yet.
	double ClockScale() const
	{
		return clockScale;
	}

private:
	void UpdateClock();
	void UpdateAudio( double dt );
	void upload();
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};

	Sheet sheet;
	SheetParams lastParams;
	bool haveLastParams = false;
	audio::Analyser analyser;
	Trigger trigger;
	double lastDose  = 0.0;
	uint32_t scatterSeed = 1;

	ffglex::FFGLShader needleShader, compositeShader;
	GLuint vaos[ 2 ] = {};///< the needles, the composite's (empty)
	GLuint vbo       = 0;
	std::vector< float > instances;
	unsigned long long uploadedGeneration = ~0ull;
	PassBuffer transmission;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	// The clock (ferrofluid's vote on seconds or milliseconds, fountain's
	// offline fallback).
	double hostTime     = -1.0;
	double clockScale   = 0.0;
	double lastRawTime  = -1.0;
	double lastWallTime = -1.0;
	double wallStart    = -1.0;
	int secondsVotes    = 0;
	int millisVotes     = 0;
	int clockSamples    = 0;
	int clockFrames     = 0;
	bool settledJump    = false;
	double now          = 0.0;
	double lastNow      = -1.0;

	float bpm          = 120.0f;
	float barPhase     = 0.0f;
	float lastBarPhase = -1.0f;
	bool hostBeatSeen  = false;

	int tapPresses     = 0;
	bool tapHeld       = false;
	int scatterPresses = 0;
	bool scatterHeld   = false;

	int lastWidth = 0, lastHeight = 0;
	bool resizeScatters = false;
	bool neverPins      = false;
	bool magnetDrags    = false;
	bool floatClock     = false;
	bool unprimed       = false;
	int compositeModel  = 0;
	double drawLength   = 0.0;
	bool holdCount      = false;
	bool fieldCube      = false;
	bool fieldSign      = false;
	bool fieldOne       = false;
};

} // namespace filings
