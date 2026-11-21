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
- Settings and controls are saved to `T:\` (the title's save folder, `E:\TDATA\534D0001`).
  Each saved film (replay) is a game save of its own, shown in the dashboard's memory screen.
- Cached map previews go on the utility drive `Z:` (one of the hard disk's cache partitions).
- Player 1-4 use the controller in port 1-4. Default buttons: A jump/select, B turbo/back,
  X power-up, Start pause, Back cancel. The d-pad works like the left stick.
- System link: the main menu's Start opens the lobby; set Open to System link and other consoles
  and PCs on the network find the game. Join a Game lists the games hosted on the local network
  (other consoles, and PCs running the same build), where A joins one; Join by address joins a game
  at an address (an on-screen keyboard types it). It is the
  game's netplay (ENet, UDP port 7300; the search uses UDP port 7301), over the Xbox's Winsock
  started without XNet's security, so consoles and PCs talk plain UDP. Under xemu, use a
  bridged (pcap) network or a UDP tunnel between instances: the default NAT mode has no LAN.
- The map and world editors are in the game (main menu, Editors), worked with the controllers: see
  docs/HistoryReplaysEditors.md. Maps and worlds are saved in the data folder when it can be written
  to (the game started from a folder on the hard disk); started from a disc, they're saved on `T:\`
  (in `maps\` and `worlds\`) and the game's lists read them there too, a saved copy in place of
  the disc's.
- Text (a nickname, an address, a map's name) is typed on an on-screen keyboard: the d-pad moves,
  A types, B is done. Options > Online has the nickname (else the console's), the run-ahead, the port,
  ping bars and network stats, as on a PC.
- To keep within 64 MB of RAM, tileset images load per size as they're drawn and stay loaded
  up to 20 MB (the current map's always stay).
- Images read from the disc are decoded once and kept decoded on the cache drive (`img\` there, up
  to 400 MB): later loads skip the DVD read and the PNG decoding (`src/sdl_image.cpp`). Sounds and
  music are copied there as they are (`snd\`, up to 200 MB, `src/sdl_mixer.cpp`). An editor's graphics also stay loaded after it's left, while the
  game is in its menus, so opening it again is quick; they're freed when a match starts or the other
  editor opens.
- Press both sticks in (L3 + R3) to show or hide a profiler: frame time split into blitting,
  filling, the copy to the screen, the swap (vertical blank wait), the rest of the game, and the
  audio thread's share of the CPU.
