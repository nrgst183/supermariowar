// Xbox SDL3 compatibility layer: big allocations in pages of their own.
//
// The C library's malloc (picolibc's) keeps one contiguous heap that grows (sbrk) and never gives
// memory back. In 64 MB, the game's big blocks (images, 1-4 MB each; a map's layers) came and went
// between small ones that stayed: after a few, the heap had 10 MB free in pieces and no piece for
// the next 3 MB image. So a block of 64 KB or more gets pages of its own from the system
// (VirtualAlloc), given back when it's freed: any free pages make the next one, wherever they are.
//
// C++'s new and delete go through here (the vectors, the strings, the game's objects), and so do
// the surfaces' pixels and stb_image's buffers (sdlx_malloc). Each block starts with a header
// saying which kind it is, so a free of either kind finds its way.

#include <xtl.h>

#include "sdl_internal.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {
constexpr size_t k_big = 64 * 1024;
constexpr Uint32 k_small_magic = 0x534D4C31;    // "SML1"
constexpr Uint32 k_big_magic = 0x42494731;      // "BIG1"

// 16 bytes, so the block after it stays 16-aligned
struct Header {
    Uint32 magic;
    Uint32 pad;
    size_t size;
    size_t pad2;
};
constexpr size_t k_header = 16;
static_assert(sizeof(Header) <= k_header, "the header fits");

Header* headerOf(void* block)
{
    return reinterpret_cast<Header*>(static_cast<Uint8*>(block) - k_header);
}

void* allocate(size_t size)
{
    if (size >= k_big) {
        void* pages = VirtualAlloc(NULL, size + k_header, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!pages)
            return nullptr;
        auto* header = static_cast<Header*>(pages);
        header->magic = k_big_magic;
        header->size = size;
        return static_cast<Uint8*>(pages) + k_header;
    }
    void* block = malloc(size + k_header);
    if (!block)
        return nullptr;
    auto* header = static_cast<Header*>(block);
    header->magic = k_small_magic;
    header->size = size;
    return static_cast<Uint8*>(block) + k_header;
}

void release(void* block)
{
    if (!block)
        return;
    Header* header = headerOf(block);
    if (header->magic == k_big_magic) {
        header->magic = 0;
        VirtualFree(header, 0, MEM_RELEASE);
    } else {
        header->magic = 0;
        free(header);
    }
}
} // namespace

extern "C" {

void* sdlx_malloc(size_t size)
{
    return allocate(size);
}

void* sdlx_calloc(size_t count, size_t size)
{
    const size_t bytes = count * size;
    if (size && bytes / size != count)
        return nullptr;
    void* block = allocate(bytes);
    //(new pages are zero already)
    if (block && bytes < k_big)
        memset(block, 0, bytes);
    return block;
}

void* sdlx_realloc(void* block, size_t size)
{
    if (!block)
        return allocate(size);
    if (!size) {
        release(block);
        return nullptr;
    }
    const size_t old = headerOf(block)->size;
    if (size <= old && (old < k_big || size >= old / 2))
        return block;
    void* bigger = allocate(size);
    if (!bigger)
        return nullptr;
    memcpy(bigger, block, old < size ? old : size);
    release(block);
    return bigger;
}

void sdlx_free(void* block)
{
    release(block);
}

} // extern "C"


/* ---- C++ ------------------------------------------------------------------ */

void* operator new(size_t size)
{
    if (void* block = allocate(size ? size : 1))
        return block;
    throw std::bad_alloc();
}

void* operator new[](size_t size)
{
    return operator new(size);
}

void* operator new(size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size ? size : 1);
}

void* operator new[](size_t size, const std::nothrow_t&) noexcept
{
    return allocate(size ? size : 1);
}

void operator delete(void* block) noexcept { release(block); }
void operator delete[](void* block) noexcept { release(block); }
void operator delete(void* block, size_t) noexcept { release(block); }
void operator delete[](void* block, size_t) noexcept { release(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { release(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { release(block); }

// Over-aligned (alignas above 16): rare; the block is over-allocated and aligned inside, with its
// start kept just before it
void* operator new(size_t size, std::align_val_t align)
{
    const size_t a = static_cast<size_t>(align);
    void* raw = allocate(size + a + sizeof(void*));
    if (!raw)
        throw std::bad_alloc();
    const uintptr_t start = reinterpret_cast<uintptr_t>(raw) + sizeof(void*);
    void* aligned = reinterpret_cast<void*>((start + a - 1) & ~(a - 1));
    reinterpret_cast<void**>(aligned)[-1] = raw;
    return aligned;
}

void* operator new[](size_t size, std::align_val_t align)
{
    return operator new(size, align);
}

void operator delete(void* block, std::align_val_t) noexcept
{
    if (block)
        release(reinterpret_cast<void**>(block)[-1]);
}

void operator delete[](void* block, std::align_val_t align) noexcept
{
    operator delete(block, align);
}

void operator delete(void* block, size_t, std::align_val_t align) noexcept
{
    operator delete(block, align);
}

void operator delete[](void* block, size_t, std::align_val_t align) noexcept
{
    operator delete(block, align);
}
