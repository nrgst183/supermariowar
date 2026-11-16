// stb_vorbis (Ogg Vorbis decoding) for the Xbox SDL3 compatibility layer, in
// its own C translation unit (no Xbox headers), as RXDK's music sample does.
// Decodes from memory only; the file access is in sdl_mixer.cpp.

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"
