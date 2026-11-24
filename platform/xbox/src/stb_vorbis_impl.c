// stb_vorbis (Ogg Vorbis decoding) for the Xbox SDL3 compatibility layer, in
// its own C translation unit (no Xbox headers), as RXDK's music sample does.
// Decodes from a file (the music streams: sdl_mixer.cpp opens it) or memory.

#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
