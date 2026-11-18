// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
`;

// kNeedleVertex, source/Shaders.cpp
export const NEEDLE_VERTEX = `
layout( location = 0 ) in vec4 Filing;// x y (frame heights from the centre), cos, sin
uniform vec2 Centre;                  // the sheet's origin, pixels
uniform float Scale;                  // pixels a frame height
uniform vec2 Half;                    // the ellipse's semi-axes a, b, pixels
uniform vec2 Size;                    // the buffer, pixels
out vec2 vLocal;
const int kCorner[ 6 ] = int[ 6 ]( 0, 1, 2, 2, 1, 3 );
void main()
{
	vec2 c   = Centre + Filing.xy * Scale;
	vec2 u   = Filing.zw;
	vec2 n   = vec2( -u.y, u.x );
	int k    = kCorner[ gl_VertexID % 6 ];
	float s  = ( k == 1 || k == 3 ) ? Half.x + 1.0 : -Half.x - 1.0;
	float t  = k >= 2 ? Half.y + 1.0 : -Half.y - 1.0;
	vec2 p   = c + u * s + n * t;
	gl_Position = vec4( p / Size * 2.0 - 1.0, 0.0, 1.0 );
	vLocal   = vec2( s, t );
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
vec3 toLinear( vec3 c )
{
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
`;

// kNeedleFragment, source/Shaders.cpp
export const NEEDLE_FRAGMENT = `
uniform vec2 Half;
in vec2 vLocal;
out vec4 FragColor;
void main()
{
	float along = vLocal.x / Half.x;
	float h     = Half.y * sqrt( max( 1.0 - along * along, 0.0 ) );
	float cover = clamp( min( vLocal.y + 0.5, h ) - max( vLocal.y - 0.5, -h ), 0.0, 1.0 );
	FragColor   = vec4( 1.0 - cover );
}
`;

// kCompositeFragment, source/Shaders.cpp
export const COMPOSITE_FRAGMENT = `
uniform sampler2D Transmission;
uniform sampler2D InputTexture;
uniform ivec2 ViewOrigin;// the host viewport's corner
uniform float Ink;       // the filings' grey, linear light
uniform float MixAmount;
uniform int Model;       // 0 the plugin; 1 and 2 the negative controls
out vec4 FragColor;
void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	float T      = clamp( texelFetch( Transmission, at, 0 ).r, 0.0, 1.0 );
	float cover  = 1.0 - T;
	vec3 light   = toLinear( src.rgb );
	// The clip is the light through a clear sheet; the filings stop it and
	// show their own grey. Premultiplied, then back.
	vec3 shown   = light * src.a * T + vec3( Ink ) * cover;
	float alpha  = src.a + ( 1.0 - src.a ) * cover;
	if( Model == 1 )
		shown = light * src.a + vec3( Ink ) * cover;
	vec3 rgb     = alpha > 0.0 ? shown / alpha : light;
	float outA   = Model == 2 ? 1.0 : alpha;
	FragColor    = vec4( mix( src.rgb, toCode( rgb ), MixAmount ), mix( src.a, outA, MixAmount ) );
}
`;
