// stb_image (PNG/BMP/TGA/JPEG decoding) and stb_image_write (PNG encoding) for
// the Xbox SDL3 compatibility layer, in their own C translation unit (no Xbox
// headers). Memory only; the file access is in sdl_image.cpp.

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_SIMD            // the Pentium III has no SSE2
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_ONLY_JPEG
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "stb_image_write.h"
