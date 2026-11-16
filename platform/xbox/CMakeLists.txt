# SDL3 / SDL3_image / SDL3_mixer compatibility layer for the original Xbox,
# on the RXDK system libraries (Direct3D 8, XInput, DirectSound).
# Included from cmake/BundledDeps.cmake when building with RxdkToolchain.cmake.

add_library(sdl3_xbox STATIC
    include/SDL3/SDL.h
    include/SDL3/SDL_main.h
    include/SDL3_image/SDL_image.h
    include/SDL3_mixer/SDL_mixer.h
    src/sdl_internal.h
    src/sdl_core.cpp
    src/sdl_events.cpp
    src/sdl_image.cpp
    src/sdl_mixer.cpp
    src/sdl_surface.cpp
    src/sdl_video.cpp
    src/stb_image_impl.c
    src/stb_vorbis_impl.c
)

target_include_directories(sdl3_xbox
    PUBLIC include
    PRIVATE src vendor
)
target_compile_features(sdl3_xbox PRIVATE cxx_std_20)

# The game code talks to these as if they were the real SDL libraries
add_library(SDL3::SDL3 ALIAS sdl3_xbox)
add_library(SDL3_image::SDL3_image ALIAS sdl3_xbox)
add_library(SDL3_mixer::SDL3_mixer ALIAS sdl3_xbox)

# The Xbox system libraries the layer runs on, and the runtime. SDK libraries
# are named by file; Debug links their debug builds ("d" suffix).
set(_rxdk_d "$<$<CONFIG:Debug>:d>")
target_link_libraries(sdl3_xbox INTERFACE
    libd3d8${_rxdk_d}
    libdsound${_rxdk_d}
    libxapi${_rxdk_d}
    libc${_rxdk_d}
    libcpp${_rxdk_d}
    libkernel${_rxdk_d}
    libcompat${_rxdk_d}
)

# The title's entry point (RXDK calls main(void); it calls the game's SDL_main).
# Added to the executable directly, so the linker always sees it.
set(SDL3_XBOX_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/sdl_main_xbox.cpp" CACHE INTERNAL "")
