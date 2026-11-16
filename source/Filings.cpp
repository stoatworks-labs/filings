#include "Filings.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

using namespace ffglex;

namespace filings
{
namespace
{
/// Frames that must agree before the host's clock unit is settled.
constexpr int kClockVotes = 4;
/// Frames of advancing host time after which an unsettled vote is decided by
/// the size of the host's step alone (UpdateClock).
constexpr int kClockPatience = 30;
/// Host seconds a single frame may advance by.
constexpr double kMaxFrameDelta = 0.1;
/// --still's negative control: the pass a sheet that never pins takes every
/// frame, x the reference dose.
constexpr double kCreep = 0.05;

double wallSeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

bool sameMagnets( const Magnets& a, const Magnets& b )
{
	if( a.count != b.count || a.b0 != b.b0 || a.depth != b.depth )
		return false;
	for( int i = 0; i < a.count; ++i )
		if( a.poles[ i ].x != b.poles[ i ].x || a.poles[ i ].y != b.poles[ i ].y || a.poles[ i ].sign != b.poles[ i ].sign )
			return false;
	return true;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
FilingsPlugin::FilingsPlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	//The beat clock and the onset detector's time constants are in seconds.
	SetTimeSupported( true );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Event: SetParamInfo( id, info.name, FF_TYPE_EVENT, false ); break;
		case Kind::Buffer:
			//An FFT buffer: Resolume shows it as an audio-source picker and
			//writes the spectrum into it every frame. With nothing routed the
			//flux is 0 and Tap On Onset never taps.
			SetBufferParamInfo( id, info.name, audio::kBins, FF_USAGE_FFT );
			for( int i = 0; i < audio::kBins; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), "", 0.0f );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint FilingsPlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult FilingsPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &needleShader, shaders::Pass::Needles, "needles" },
		{ &compositeShader, shaders::Pass::Composite, "composite" },
	};
	for( const Stage& stage : stages )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Filings: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//The needles: one vertex array, its buffer stepping once an instance; and
	//an empty one for the composite's full-screen triangle (a core profile
	//draws nothing without a vertex array bound).
	SavedGLState saved;
	saved.Capture();
	glGenVertexArrays( 2, vaos );
	glGenBuffers( 1, &vbo );
	glBindVertexArray( vaos[ 0 ] );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glEnableVertexAttribArray( 0 );
	glVertexAttribPointer( 0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof( float ), nullptr );
	glVertexAttribDivisor( 0, 1 );
	saved.Restore();
	uploadedGeneration = ~0ull;

	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
SheetParams FilingsPlugin::Params( int width, int height ) const
{
	SheetParams p;
	p.length   = SizeFromParam( params[ PT_SIZE ] );
	p.friction = FrictionFromParam( params[ PT_FRICTION ] );
	Magnets& m = p.magnets;
	m.count    = OptionIndex( params[ PT_POLES ], kMaxPoles ) + 1;
	m.b0       = StrengthFromParam( params[ PT_STRENGTH ] );
	m.depth    = DepthFromParam( params[ PT_DEPTH ] );
	//The poles are placed on the frame (0..1 across and up it), the sheet is
	//measured in frame heights from the centre.
	const double aspect = height > 0 ? static_cast< double >( width ) / height : 16.0 / 9.0;
	for( int i = 0; i < kMaxPoles; ++i )
	{
		m.poles[ i ].x    = ( std::clamp( static_cast< double >( params[ PoleX( i ) ] ), 0.0, 1.0 ) - 0.5 ) * aspect;
		m.poles[ i ].y    = std::clamp( static_cast< double >( params[ PoleY( i ) ] ), 0.0, 1.0 ) - 0.5;
		m.poles[ i ].sign = OptionIndex( params[ PoleSign( i ) ], 2 ) == 0 ? 1.0 : -1.0;
	}
	m.cubeLaw     = fieldCube;
	m.signIgnored = fieldSign;
	m.onePole     = fieldOne;
	return p;
}

//---------------------------------------------------------------------------
void FilingsPlugin::upload()
{
	if( sheet.Generation() == uploadedGeneration )
		return;
	const std::vector< Filing >& filings = sheet.Filings();
	instances.resize( filings.size() * 4 );
	for( size_t i = 0; i < filings.size(); ++i )
	{
		instances[ 4 * i + 0 ] = static_cast< float >( filings[ i ].x );
		instances[ 4 * i + 1 ] = static_cast< float >( filings[ i ].y );
		instances[ 4 * i + 2 ] = static_cast< float >( std::cos( filings[ i ].theta ) );
		instances[ 4 * i + 3 ] = static_cast< float >( std::sin( filings[ i ].theta ) );
	}
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glBufferData( GL_ARRAY_BUFFER, static_cast< GLsizeiptr >( instances.size() * sizeof( float ) ), instances.data(), GL_DYNAMIC_DRAW );
	uploadedGeneration = sheet.Generation();
}

//---------------------------------------------------------------------------
void FilingsPlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height )
{
	if( !transmission.Ensure( width, height, GL_RGBA32F, PassBuffer::Sampling::Nearest ) )
	{
		diag::error( "could not allocate the transmission buffer" );
		return;
	}
	const double length = drawLength > 0.0 ? drawLength : SizeFromParam( params[ PT_SIZE ] );
	const double halfA  = 0.5 * length * height;//px
	const double halfB  = halfA / kAspect;

	//The needles: transmissions multiplied into a buffer cleared to 1.
	glBindFramebuffer( GL_FRAMEBUFFER, transmission.GetGLID() );
	glViewport( 0, 0, width, height );
	glClearColor( 1.0f, 1.0f, 1.0f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	const GLsizei count = static_cast< GLsizei >( sheet.Filings().size() );
	if( count > 0 )
	{
		setMultiplyBlend();
		glUseProgram( needleShader.GetGLID() );
		glUniform2f( where( needleShader, "Centre" ), 0.5f * static_cast< float >( width ), 0.5f * static_cast< float >( height ) );
		glUniform1f( where( needleShader, "Scale" ), static_cast< float >( height ) );
		glUniform2f( where( needleShader, "Half" ), static_cast< float >( halfA ), static_cast< float >( halfB ) );
		glUniform2f( where( needleShader, "Size" ), static_cast< float >( width ), static_cast< float >( height ) );
		glBindVertexArray( vaos[ 0 ] );
		upload();
		glDrawArraysInstanced( GL_TRIANGLES, 0, 6, count );
		glDisable( GL_BLEND );
	}

	//The picture, into the host's framebuffer and viewport.
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( compositeShader.GetGLID() );
	bindUnit( 0, transmission.TextureID() );
	bindUnit( 1, input->Handle );
	glUniform1i( where( compositeShader, "Transmission" ), 0 );
	glUniform1i( where( compositeShader, "InputTexture" ), 1 );
	glUniform2i( where( compositeShader, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform1f( where( compositeShader, "Ink" ), static_cast< float >( std::clamp( static_cast< double >( params[ PT_TONE ] ), 0.0, 1.0 ) * kInkGrey ) );
	glUniform1f( where( compositeShader, "MixAmount" ), std::clamp( params[ PT_MIX ], 0.0f, 1.0f ) );
	glUniform1i( where( compositeShader, "Model" ), compositeModel );
	glBindVertexArray( vaos[ 1 ] );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
	unbindTextureUnits( 2 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
void FilingsPlugin::UpdateClock()
{
	const double wallNow = wallSeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending both seconds and milliseconds through
	//SetTime, and nothing in the call says which. Vote on it against the wall
	//clock for a few frames, then stop asking (ferrofluid's, by way of
	//rosette and millpond).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;//wall time to the host's own: no dt across it
			}
		}
		//A host that renders much faster than real time -- an offline render,
		//oxbow's selftest, fgtest -- never wins the vote. After kClockPatience
		//frames the step's size decides: a frame is well under a second and
		//well over a millisecond (fountain's).
		if( hostDelta > 0.0 && clockScale == 0.0 && ++clockSamples >= kClockPatience )
		{
			clockScale  = hostDelta < 1.0 ? 1.0 : 0.001;
			settledJump = true;
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;

	//Double throughout: Resolume's clock has been measured at ~499 million ms,
	//where a float resolves 0.03 s. Only differences are used.
	now = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
	if( floatClock )
		now = static_cast< double >( static_cast< float >( now ) );
	if( ++clockFrames == 60 )
		diag::info( "host clock at frame 60: raw=" + std::to_string( raw ) + " scale=" + std::to_string( clockScale )
		            + " seconds=" + std::to_string( now ) );
}

void FilingsPlugin::UpdateAudio( double dt )
{
	float bins[ audio::kBins ] = {};
	int binCount               = 0;
	if( const ParamInfo* info = FindParamInfo( static_cast< FFUInt32 >( PT_AUDIO ) ) )
	{
		binCount = static_cast< int >( std::min< size_t >( info->elements.size(), audio::kBins ) );
		for( int i = 0; i < binCount; ++i )
			bins[ i ] = info->elements[ static_cast< size_t >( i ) ].value;
	}
	audio::Settings settings;
	settings.sensitivity = std::clamp( params[ PT_SENSITIVITY ], 0.0f, 1.0f );
	analyser.SetUnprimedForTest( unprimed );
	analyser.Update( bins, binCount, static_cast< float >( dt ), settings );
}

//---------------------------------------------------------------------------
FFResult FilingsPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 )
		return FF_FAIL;

	//Time, the audio and the beat.
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	const double dt = lastNow >= 0.0 ? std::clamp( now - lastNow, 0.0, kMaxFrameDelta ) : 0.0;
	lastNow         = now;
	UpdateAudio( dt );
	if( barPhase != lastBarPhase && lastBarPhase >= 0.0f )
		hostBeatSeen = true;
	lastBarPhase = barPhase;

	//The sheet. Scatter first (the operator's), then the count (appends or
	//drops at the end, never moves one), then the frame's tap.
	const SheetParams sp = Params( width, height );
	const int count      = FilingCount( DensityFromParam( params[ PT_DENSITY ] ), sp.length );
	if( scatterPresses > 0 )
	{
		scatterSeed += static_cast< uint32_t >( scatterPresses );
		sheet.Scatter( scatterSeed, count );
		scatterPresses = 0;
	}
	if( resizeScatters && lastWidth > 0 && ( width != lastWidth || height != lastHeight ) )
		sheet.Scatter( sheet.Seed(), count );
	lastWidth  = width;
	lastHeight = height;
	if( !holdCount )
		sheet.SetCount( count );

	TriggerInputs in;
	in.on       = static_cast< TapOn >( OptionIndex( params[ PT_TAP_ON ], static_cast< int >( TapOn::Count ) ) );
	in.presses  = tapPresses;
	in.onset    = analyser.Fired();
	in.kick     = analyser.Hit();
	in.bpm      = bpm;
	in.barPhase = barPhase;
	in.hostBeat = hostBeatSeen;
	in.now      = now;
	in.dt       = dt;
	tapPresses  = 0;
	lastDose    = trigger.Advance( in ) * TapStrengthFromParam( params[ PT_TAP_STRENGTH ] );

	const bool moved = haveLastParams && !sameMagnets( sp.magnets, lastParams.magnets );
	if( lastDose > 0.0 )
		sheet.Tap( lastDose, sp );
	else if( magnetDrags && moved )
		sheet.Tap( 1.0, sp );
	else if( neverPins )
		sheet.Pass( kCreep * sheet.GetNeedle().referenceDose, sp );
	lastParams     = sp;
	haveLastParams = true;

	render( input, hostViewport, pgl->HostFBO, width, height );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FilingsPlugin::DeInitGL()
{
	uniformCache.clear();
	needleShader.FreeGLResources();
	compositeShader.FreeGLResources();
	if( vaos[ 0 ] != 0 )
		glDeleteVertexArrays( 2, vaos );
	if( vbo != 0 )
		glDeleteBuffers( 1, &vbo );
	std::fill( std::begin( vaos ), std::end( vaos ), 0u );
	vbo                = 0;
	uploadedGeneration = ~0ull;
	transmission.Destroy();
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult FilingsPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

void FilingsPlugin::SetBeatInfo( float newBpm, float newBarPhase )
{
	CFFGLPlugin::SetBeatInfo( newBpm, newBarPhase );
	if( newBpm > 0.0f && std::isfinite( newBpm ) )
		bpm = newBpm;
	if( std::isfinite( newBarPhase ) )
		barPhase = newBarPhase - std::floor( newBarPhase );
}

char* FilingsPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult FilingsPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult FilingsPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	//Tap and Scatter act on the press, once: an event arrives as 1 on press
	//and 0 on release, and a host is free to restate either.
	if( index == PT_TAP || index == PT_SCATTER )
	{
		const bool down = value >= 0.5f;
		bool& held      = index == PT_TAP ? tapHeld : scatterHeld;
		int& presses    = index == PT_TAP ? tapPresses : scatterPresses;
		if( down && !held )
			++presses;
		held = down;
	}
	params[ index ] = value;
	return FF_SUCCESS;
}

float FilingsPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace filings
