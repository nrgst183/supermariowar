# Netplay

SMW has an *experimental* online mode with rollback netcode, for up to four players. The players connect directly to each other: there is no server, one player hosts a lobby and the others join it. Every player's game simulates the whole match and the peers only exchange their inputs. When a late input arrives, the game rolls back to the first wrong frame and re-simulates up to the present, so your own controls always respond immediately.

The old lobby server mode (`smw-server`) has been removed.

## Playing online

### Hosting

1. In the main menu, choose **Online** and then host a lobby. The default port is **UDP 7300**.
2. Set up the match as usual (game mode, map, settings). The host's selections are what everybody plays.
3. Give the other players your address. Wait until they appear in the lobby, then start the match once everyone is ready.

### Joining

1. Main menu, **Online**, join a lobby.
2. Type the host's address: `192.168.1.20` or `192.168.1.20:7300` (the port can be left out when it is 7300), and wait in the lobby for the host to start.

All players must run the **same version of the game**: the connection is refused if the builds (or the game data that matters for the simulation) differ.

### Port forwarding and firewalls

- **On a LAN** (same router or switch) nothing is needed: join the host's local address.
- **Over the internet**, the host must make the port reachable: forward **UDP 7300** (or the port the host chose) on the router to the host's computer, and allow it in the firewall. Joiners need no forwarding. Tell the joiners the host's public IP address.
- With several players, the joiners also connect to *each other* directly (a full mesh), so every joiner must be able to reach the other joiners. Ordinary home routers allow this, because the joiners connect outwards.
- There is no NAT traversal or relay yet. If joining times out, the port is most likely not reachable.

### Things to know

**Input delay.** An input is applied a few frames after it is pressed, so that most of the network latency is hidden without rolling back. The game measures the round trip times in the lobby and picks the delay by itself (1 to 6 frames of 16 ms; the host can set a fixed value in the lobby). A short delay means more rollbacks, a long one means less responsive controls.

**Maps.** If the host's map is not in your maps folder, the host sends it to you when the match starts (maps up to 1 MB). Downloaded maps are saved in `maps/net/` in the data directory, and the lobby shows the progress.

**Network stats.** The line at the top of the screen, under the scores, shows the round trip time of every other player, the input delay in frames, and how many rollbacks happen per second. It is off by default and only drawn in online matches. Toggle it with **F3** during a match, or with **Options, Graphics, Network Stats**.

**Waiting.** When another player's inputs are late by more than the game can predict (about eight frames), the match freezes for a moment. After a split second, a box shows **Waiting for <name>** and for how long. If a player is silent for **5 seconds** the connection is considered lost, and the match ends for everybody (see below). After 3 seconds of waiting, the box says so.

**Leaving.** There is no pause online (the others cannot wait for you). Pressing the exit key asks **Leave online match?**. Leaving ends the match for everybody, and you go back to the main menu. Closing the game has the same effect.

**Disconnects.** If a player leaves or loses the connection, the match is aborted at that point for everybody. The others return to the lobby and see why on the lobby screen ("Match aborted: Alice timed out"). If the host leaves or is lost, the lobby is closed for everybody. A player that is kicked or has left is out of the lobby and has to join again.

## Command line

The menu does the same as these options, which are also used for testing (`smw --help` lists them all):

```
smw --net-host 7300 --net-peers 2         # host (with --autotest: wait for 1 more player)
smw --net-join 192.168.1.20:7300          # join
```

| Option | Meaning |
| --- | --- |
| `--net-host PORT` | host a lobby on this UDP port |
| `--net-peers N` | `--autotest` host: wait for N peers (the host included) before starting |
| `--net-join IP:PORT` | join a lobby |
| `--net-name NAME` | nickname in the lobby |
| `--net-port PORT` | local UDP port of a joiner (default: any) |
| `--net-delay N` | fixed input delay in ticks (default: automatic) |
| `--net-checksum N` | compare state checksums with the peers every N frames (default 60) |
| `--net-stats N` | print `netstats:` lines every N frames (default 300, 0 is off) |
| `--net-timeout MS` | a peer that sends nothing for this long is gone (default 5000) |
| `--net-quit-at F` | leave the match at frame F (disconnect tests) |
| `--net-sim-latency MS` | simulated one-way delay added to every packet sent |
| `--net-sim-jitter MS` | simulated +- variation of that delay |
| `--net-sim-loss PCT` | simulated loss of the unreliable packets |
| `--at-shot-every N`, `--at-shot-dir DIR` | with rendering on, save a PNG of the screen every N drawn frames of an online match (to review the overlays) |

Builds configured with `-DNO_NETWORK=ON` (the web build) leave out the network code.

## For developers

### Architecture

The code is in `src/smw/rollback/`, bottom to top:

- **Simulation tick** (`GameplayState::advanceTick`, `simulateTick`). The game ticks at a fixed 62.5 Hz. A tick reads one `InputFrame` (one input byte per player slot, `InputFrame.h`) and nothing else from outside: the same state and the same input always give the same next state, on every machine and compiler. Rendering is separate and interpolates between ticks.
- **Snapshots** (`Snapshot.h`, `common/SimArena.h`). All simulation state lives in one memory arena that never moves, plus a short list of other regions (`g_sim`, the runtime part of `game_values` and `g_map`, the game mode object). A snapshot is a flat copy of those bytes, so saving and restoring are a few memcpys. `StateChecksum.h` hashes the state in a way that is comparable between machines, for desync detection.
- **RollbackSession** (`RollbackSession.h`, after GGPO). Runs one match: exchanges inputs, predicts missing ones (repeats the last input), rolls back and re-simulates on a wrong prediction, stalls when it is too far ahead of the confirmed inputs, runs time sync, compares checksums, and ends the match at a *barrier*: an irreversible event (the end of the match) only happens at a frame that is confirmed on every peer. It talks to the game through `ISessionCallbacks` and to the network through a `Transport`, and is unit-tested with fakes of both (`tests/rollback/`).
- **NetMesh** (`NetMesh.h`, `EnetTransport`). The connections: a full mesh over UDP (ENet), with a handshake that checks the version. The host numbers the joiners and tells each of them about the others; during a match all peers are equal. `NetConditionSim` adds latency, jitter and loss for testing.
- **NetLobby** (`NetLobby.h`). A persistent session: peers, names, slots, ready flags, the input delay, the map transfer, and the messages that set up and abort a match. The header starts with the *menu contract*: what the menus and overlays may call.
- **NetGame** (`NetGame.h`). Owns the lobby and the running session and connects them to the game: it captures the host's match setup (`MatchConfig`), starts the match on every peer at the same frame, saves and loads snapshots for the session, and returns to the lobby afterwards. `GameplayState::updateNetplay` calls `NetGame::advance` once per frame.
- **Overlays** (`NetOverlay.cpp`) draw the waiting box and the network stats from `NetGame`'s read-only queries (`waitInfo`, `peerPingMs`, `inputDelay`, `matchStats`).

### Rules for simulation code

(From `Snapshot.h` and `SimArena.h`. Break them and the peers drift apart.)

- Simulation state must live in the simulation arena (derive from `sim::ArenaAllocated`, or use the `sim::vector`, `sim::list`, `sim::map` containers), in `g_sim`, or in a region listed in `forEachStateRegion()` (`Snapshot.cpp`).
- No `static` variables or globals that the simulation writes. No `std::` containers, `std::string`, SDL surfaces or other heap memory owned by simulation objects.
- Simulation objects may point at each other, at loaded resources (`rm`), at `g_map`, `g_sim` and the game modes, but never at cosmetic things (eyecandy, spotlights, sounds): those are not rolled back.
- Cosmetic state that a tick advances (eyecandy, screen shake, tile animation, sounds) stays out of the snapshot and must not be advanced while `sim::isResimulating()` is set.
- **Drawing must never write into simulation objects**; cosmetic animation state that drawing advances lives elsewhere (e.g. `PlayerRenderState`). The overlays only read from `NetGame`.
- Use only the seeded simulation random number generator, never the clock, frame rate, floating point settings that differ per machine, or the order of pointers or hash tables.
- Debug keys and other local-only changes of the simulation are disabled in online matches.

### Testing

**Sync test.** It runs without a network and finds violations of the rules above: every tick it rolls back up to N ticks, re-simulates them and checks that the state is identical.

```
smw --datadir data --autotest --at-synctest 8 --at-match single --at-frames 3000 --at-norender 1
```

`--at-peersum N` prints a cross-machine state checksum every N frames (`peersum:` lines), to compare runs of different builds or compilers.

**Netplay test.** `scripts/netplay-test.sh` starts real game processes on `127.0.0.1` that play online matches against each other with seeded random input (`--autotest --at-humans`), under simulated network conditions, and checks that every peer finished every match and that the checksums of all confirmed frames (`netsum:` lines) are identical. It prints rollbacks, stalls and CPU use per run.

```
scripts/netplay-test.sh --smoke                              # 2 peers, 60 ms latency, 1 match: for CI
scripts/netplay-test.sh --peers "2 3 4" --conditions "a b c d"
scripts/netplay-test.sh --disconnect                         # a joiner or the host is killed or leaves
scripts/netplay-test.sh --map-transfer                       # the map is downloaded
scripts/netplay-test.sh --builds "build build-clang"         # peers alternate between builds
```

The conditions are `a` (no added lag), `b` (60 ms latency, 15 ms jitter), `c` (100 ms, 5% loss) and `d` (150 ms, 20 ms jitter, 10% loss), each way. The exit status is 0 if every run passed. The logs go to `/tmp/smw-netplay-test` (`--out`).

**Screenshots of the overlays.** Run two processes with rendering on (no `--at-norender`) and add `--at-shot-every 30 --at-shot-dir DIR` to the one to look at. Stopping the other process for a few seconds (`kill -STOP` / `kill -CONT`) shows the waiting box, `--net-sim-latency` makes rollbacks that the stats line shows.

**Unit tests.** `smw-tests` covers the session, lobby, mesh, snapshots and config.
