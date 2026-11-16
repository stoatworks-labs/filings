#include "Filings.h"

/**
    The registration. `filings_core` is an OBJECT library so this file-scope
    constructor, which nothing references by name, is linked into the bundle
    (an archive would let the linker drop it, and the bundle would load and
    report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< filings::FilingsPlugin >,// Create method
	"FG01",                                 // Plugin unique ID of maximum length 4
	"SW Filings",                           // Plugin name
	2,                                      // API major version number
	1,                                      // API minor version number
	0,                                      // Plugin major version number
	1,                                      // Plugin minor version number
	FF_EFFECT,                              // Plugin type
	"Iron filings on a sheet over the clip, magnets under it, and the music tapping the sheet. Each onset frees the "
	"filings for a moment: they turn part way into the magnets' field, chain end to end and creep towards the poles, "
	"then friction pins them again. Field lines come up over a few kicks; a quiet passage freezes everything.",
	"Filings FFGL effect"// About
);

extern "C" const char* FilingsBuildStamp()
{
	return "filings " FILINGS_VERSION ", built " __DATE__ " " __TIME__;
}
