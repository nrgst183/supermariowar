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
    libxgraphics${_rxdk_d}
    libdsound${_rxdk_d}
    libxapi${_rxdk_d}
    libxnet${_rxdk_d}
    libc${_rxdk_d}
    libcpp${_rxdk_d}
    libkernel${_rxdk_d}
    libcompat${_rxdk_d}
)

# The title's entry point (RXDK calls main(void); it calls the game's SDL_main).
# Added to the executable directly, so the linker always sees it.
set(SDL3_XBOX_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/src/sdl_main_xbox.cpp" CACHE INTERNAL "")

# ENet (the netplay's UDP, over system link) on the Xbox's Winsock: ENet's own
# sources, with the platform part here (enet/include/enet/enet.h and
# src/enet_xbox.c); ENet's CMake build is for Windows and Unix. The same ENet
# commit as the other builds (cmake/BundledDeps.cmake).
if(NOT NO_NETWORK)
    include(FetchContent)
    FetchContent_Populate(enet_xbox
        QUIET
        GIT_REPOSITORY https://github.com/mmatyas/enet.git
        GIT_TAG 74d43e850f5abda0e5f565caa5d54f462bc210fd
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/enet_xbox-src"
        BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/enet_xbox-build"
        SUBBUILD_DIR "${CMAKE_BINARY_DIR}/_deps/enet_xbox-subbuild"
    )

    add_library(enet STATIC
        "${enet_xbox_SOURCE_DIR}/callbacks.c"
        "${enet_xbox_SOURCE_DIR}/compress.c"
        "${enet_xbox_SOURCE_DIR}/host.c"
        "${enet_xbox_SOURCE_DIR}/list.c"
        "${enet_xbox_SOURCE_DIR}/packet.c"
        "${enet_xbox_SOURCE_DIR}/peer.c"
        "${enet_xbox_SOURCE_DIR}/protocol.c"
        src/enet_xbox.c
    )
    # This folder first: its enet/enet.h supplies the platform part, then
    # includes ENet's
    target_include_directories(enet PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/enet/include"
        "${enet_xbox_SOURCE_DIR}/include"
    )
    target_compile_options(enet PRIVATE -w)
    target_link_libraries(enet PUBLIC sdl3_xbox)
endif()
