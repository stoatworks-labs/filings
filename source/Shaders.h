#pragma once

#include <string>

/**
    The passes. The CPU has placed every filing; the GPU only draws them.

    1. **needles** -- one instanced quad per filing into an RGBA32F buffer the
       size of the output, cleared to 1 and blended multiplicatively (dst x src),
       so the buffer ends as T = prod( 1 - c ), the light the needles let
       through. A filing seen from above is an ellipse, semi-axes a = L/2 and
       b = a / 5; its coverage c of a pixel is the exact 1D box filter across
       it (the overlap of the pixel's width with [ -h(s), h(s) ], h the
       ellipse's half-width at the pixel's place s along it), so it sums to
       the ellipse's area whatever the rasteriser does with edges.
    2. **composite** -- to the host. The clip is the light through the sheet:
       in linear light, out = clip x T + ink x ( 1 - T ), ink = Tone x a grey;
       alpha = clip alpha + ( 1 - clip alpha )( 1 - T ) (the sheet is clear and
       the filings are opaque); Mix to the clip.

    Nothing but positions, angles and sizes reaches a shader: no time, no
    field. tools/glslc.sh's ASSEMBLED table mirrors Assemble().
*/
namespace filings::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kNeedleVertex;
extern const char* const kCommon;
extern const char* const kNeedleFragment;
extern const char* const kCompositeFragment;

enum class Pass
{
	Needles,
	Composite
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

} // namespace filings::shaders
