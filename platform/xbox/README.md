# Original Xbox port

Super Mario War builds for the original Xbox with [RXDK](https://github.com/Team-Resurgent/RXDK-Libs),
Team Resurgent's open, MSVC-free Xbox SDK (clang/lld, picolibc + libc++, and the Xbox
D3D8 / DirectSound / XAPI libraries).

The game is written against SDL3, which has no original Xbox port. This folder holds a
small compatibility layer that implements the part of SDL3, SDL3_image and SDL3_mixer the
game uses, on the Xbox system libraries:

| SDL part | Xbox implementation |
|---|---|
| Surfaces, blitting | software, 32-bit (as on PC) — `src/sdl_surface.cpp` |
| Window, renderer, texture | Direct3D 8: the 640x480 frame is copied into the back buffer — `src/sdl_video.cpp` |
| Events, joysticks, gamepads | XInput, the four controller ports — `src/sdl_events.cpp` |
| SDL3_mixer | a software mixer on one DirectSound stream — `src/sdl_mixer.cpp` |
| SDL3_image | stb_image / stb_image_write — `src/sdl_image.cpp` |
| Timers, paths, init | `src/sdl_core.cpp` |

Third-party code: [stb_image, stb_image_write and stb_vorbis](https://github.com/nothings/stb)
(public domain / MIT), in `vendor/`.

## Building

The GitHub Actions workflow `.github/workflows/build_xbox.yml` builds it on Linux. Locally,
with the RXDK SDK, LLVM toolchain and host tools installed (by the RXDK Visual Studio or
VS Code extension):

```
cmake -B build-xbox -G Ninja -DCMAKE_TOOLCHAIN_FILE=<RXDK>/tools/cmake/RxdkToolchain.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-xbox
```

This makes `build-xbox/smw.xbe`, and `build-xbox/src/smw/XISO/supermariowar.iso` with the game
data. `build-xbox/src/smw/Build/supermariowar/` is the same as a folder (`default.xbe` + `data/`)
to copy to the hard disk.

## On the console

- The game data is read from `D:\data` (the disc, or the folder the .xbe was started from).
- Settings, controls and replays are saved to `T:\` (the title's save folder, `E:\TDATA\534D0001`).
- Player 1-4 use the controller in port 1-4. Default buttons: A jump/select, B turbo/back,
  X power-up, Start pause, Back cancel. The d-pad works like the left stick.
- No online play, map editor or world editor on the Xbox.
- To keep within 64 MB of RAM, only the 4 most recently used tilesets stay loaded.
