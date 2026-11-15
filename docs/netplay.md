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

### Tournaments, tours and worlds

Every match type can be played online: single games, minigames, and the multi-match modes **tournament**, **tour** and **world**. The host picks the match type (and the number of wins, the tour or the world) under **Settings** in the lobby, and starts it from the lobby like a single game. Then:

- The **host leads the menus** between the matches: it continues from the scoreboard and the tour stop screen, and in a tournament it chooses every game (mode, goal, map). The other players see the same screens ("Waiting for the host...").
- The **bonus wheel** is the same for everybody, and only the winning team can stop it (the host stops it after a while if they don't, or at once for CPU players).
- On the **world map**, whoever has the turn moves, enters stages and uses items, as offline; every player can open their team's items with the random key. The map waits for the slowest player's input, so it reacts a little later for the players far from the host.
- When it is over (or the host leaves it), everybody returns to the lobby.
- A player who leaves or loses the connection between matches is replaced by a CPU player for the rest of the tournament, tour or world, so the standings stay meaningful. If the host is lost, the lobby is closed for everybody (as always).
- Every player needs the tour or world file (the same version) in their data directory; maps are downloaded as usual. On the world map, the exit key does nothing online: leave the lobby to quit.

### Things to know

**Input delay.** An input is applied a few frames after it is pressed, so that most of the network latency is hidden without rolling back. The game measures the round trip times in the lobby and picks the delay by itself (1 to 6 frames of 16 ms; the host can set a fixed value in the lobby). A short delay means more rollbacks, a long one means less responsive controls.

**Maps.** If the host's map is not in your maps folder, the host sends it to you when the match starts (maps up to 1 MB). Downloaded maps are saved in `maps/net/` in the data directory, and the lobby shows the progress.

**Skins.** Every player is shown (in the lobby and in the match) with the skin their own machine has chosen. A skin that you don't have, or have under the same name but with other contents, is sent by its owner when you join (skins are small images, at most 128 KB, sent in 1 KB chunks), checked against its hash and saved in `skins/net/` in the data directory, under a safe name that never replaces an existing file. Skins are cosmetic and never delay or change a match: until a skin has arrived (or if it can't be used) the player is drawn with the default skin in the color of the slot.

**Ping bars.** Every other player's score panel shows small signal bars for their connection: four green bars under 60 ms, three under 120 ms, two (yellow) under 200 ms, one (red) above. The lobby's player cards show the same bars. Online replays keep each player's ping, averaged per second, and the theater shows the bars as they were. **Options, Graphics, Ping Bars** turns them off.

**Network stats.** The bar at the top of the screen, under the scores, shows every other player's connection (signal bars and round trip time), the input delay in frames, and how many rollbacks happen per second. It is off by default and only drawn in online matches. Toggle it with **F3** during a match, or with **Options, Graphics, Network Stats**.

**Waiting.** When another player's inputs are late by more than the game can predict (about eight frames), the match freezes for a moment. After a split second, a box shows **Waiting for <name>**, for how long, and a line that runs down to the moment the CPU takes over. If a player is silent for **5 seconds** the connection is considered lost, and the CPU takes over that player (see below). After 3 seconds of waiting, the box also counts down the seconds. A player that is only lagging is never dropped before that.

**Leaving.** There is no pause online (the others cannot wait for you). Pressing the exit key asks **Leave online match?**. If you leave, you go back to the main menu and the others play on without you. Closing the game has the same effect.

**Disconnects.** If a player leaves or loses the connection during a match, the match goes on for the others: after a moment of waiting, **"Alice disconnected - CPU takes over"** shows, and from then on the CPU plays that player (same AI and difficulty as the other CPU players, the same on every machine). The player is out of the lobby and has to join again. If the host leaves or is lost, the match goes on just the same, and when it ends, the lobby is closed ("Lost the connection to the host (the match was played to its end)"): there is no host migration. If everybody else is gone, the last player plays the match to its end against the CPU. A player that drops out while the match is still being set up (before it starts) cancels it, as before: everybody returns to the lobby.

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
| `--net-skin NAME[,NAME2]` | skin of the local player(s) (testing: normally chosen in the menus) |
| `--net-slow-map N` | the host sends one map chunk per N frames (to catch the transfer in screenshots) |
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
- **CPU players** (`CpuDriver.h`) are input like the humans: the host's machine runs their AI before each tick, outside the simulation (its own AI objects off the arena, its own random numbers), and sends their buttons with its own players' input (the session gives the CPU slots to peer 0). The other peers predict and roll back on them like on any player. So the AI never has to be deterministic or identical between builds, and a replay keeps the CPUs' play. Only when the host is lost do its CPU slots, like any lost peer's, become CPUs thinking inside the simulation on every survivor (`takeOverByCpu`).
- **Snapshots** (`Snapshot.h`, `common/SimArena.h`). All simulation state lives in one memory arena that never moves, plus a short list of other regions (`g_sim`, the runtime part of `game_values` and `g_map`, the game mode object). A snapshot is a flat copy of those bytes, so saving and restoring are a few memcpys. `StateChecksum.h` hashes the state in a way that is comparable between machines, for desync detection.
- **RollbackSession** (`RollbackSession.h`, after GGPO). Runs one match: exchanges inputs, predicts missing ones (repeats the last input), rolls back and re-simulates on a wrong prediction, stalls when it is too far ahead of the confirmed inputs, runs time sync, compares checksums, and ends the match at a *barrier*: an irreversible event (the end of the match) only happens at a frame that is confirmed on every peer. It talks to the game through `ISessionCallbacks` and to the network through a `Transport`, and is unit-tested with fakes of both (`tests/rollback/`).
- **NetMesh** (`NetMesh.h`, `EnetTransport`). The connections: a full mesh over UDP (ENet), with a handshake that checks the version. The host numbers the joiners and tells each of them about the others; during a match all peers are equal. `NetConditionSim` adds latency, jitter and loss for testing.
- **NetLobby** (`NetLobby.h`). A persistent session: peers, names, slots, ready flags, the input delay, the map transfer, and the messages that set up and abort a match. The header starts with the *menu contract*: what the menus and overlays may call.
- **NetGame** (`NetGame.h`). Owns the lobby and the running session and connects them to the game: it captures the host's match setup (`MatchConfig`), starts the match on every peer at the same frame, saves and loads snapshots for the session, and returns to the lobby afterwards. `GameplayState::updateNetplay` calls `NetGame::advance` once per frame.
- **Series** (`NetSeries.h`, `NetGameSeries.cpp`, `SeriesState.h`, and the menus' part `MenuOnlineSeries.cpp`). Tournament, tour and world: the matches are ordinary online matches, and between them the host is the authority: it sends the start of the series, which screen it shows, the bonus wheel's stop; the world map is a lockstep of the players' menu keys, relayed by the host. The next match's config carries the host's series state, which every peer compares with its own (`modesum:` lines).
- **Overlays** (`NetOverlay.cpp`) draw the waiting box, the network stats and the "disconnected - CPU takes over" line from `NetGame`'s read-only queries (`waitInfo`, `peerPingMs`, `inputDelay`, `matchStats`, `dropNotice`).

### Lost peers

A peer that disconnects, leaves (BYE) or is silent for the timeout is *lost*; the others go on and the CPU takes over its slots. All remaining peers must make that switch at the same frame, with the same inputs of the lost peer before it, or they desync. That agreement is in `RollbackSession` ("LOST PEERS" in `RollbackSession.h`; message `PEER_DROP` in `NetProtocol.h`, reliable):

- Every peer keeps a *lost set*: the peers it noticed itself, united with the sets of the reports it receives. From a lost peer nothing more is accepted, so each survivor's count of its inputs (`known`) is frozen.
- On every change, a peer sends the others a report: its lost set and, per lost peer, `known`, its last inputs (up to 64 frames) and the decided drop frame, if any. A survivor with fewer of the lost peer's inputs takes them from the report (rolling back if it predicted them wrongly).
- The *coordinator* is the lowest peer id that isn't lost. Once the latest report of every survivor shows its own lost set, it decides the *drop frame* D: the highest `known` of all. Nobody has to forget an input it already used, and nobody rolls back further than for an ordinary late input (at most the 8 predicted frames that the snapshots cover). A decision is final: every peer adopts the first one it sees, and a new coordinator (the old one was lost too) adopts any decision a survivor reports.
- From frame D on, the lost peer's inputs are zero and known, and before every tick from D on the session calls `takeOverSlots()`: `GameplayState::takeOverByCpu` gives those players a `CPlayerAI` (in the simulation arena) and marks them in `g_sim.cpuTakeover`, at that tick inside the simulation. A rollback to before D undoes it, the re-simulation does it again. A survivor that already ran past D rolls back to D.
- A peer that was only cut off (it still runs) receives a report with itself in the set: its session fails, it leaves the match and the lobby.
- The host removes a lost peer from the lobby right away. After the host is lost the joiners' lobby is closed; the match still runs over the mesh (the closed lobby passes the match messages on), and ends normally.

Why the CPU and not an idle player or removing the player: an idle player is a free kill and distorts the scores, and removing a player in the middle of a match changes every game mode's bookkeeping (teams, goals, the scoreboard). A CPU player is something every mode already handles, and it keeps the match fair for the others. It is not a host setting.

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

**Screenshots of the lobby.** `scripts/menu-screenshots.sh OUTDIR build` walks through the online menus and the lobby with a host and a joiner (the joiner lacks the host's map and skin, so both are downloaded) and saves PNGs.

**Netplay test.** `scripts/netplay-test.sh` starts real game processes on `127.0.0.1` that play online matches against each other with seeded random input (`--autotest --at-humans`), under simulated network conditions, and checks that every peer finished every match and that the checksums of all confirmed frames (`netsum:` lines) are identical. It prints rollbacks, stalls and CPU use per run.

```
scripts/netplay-test.sh --smoke                              # 2 peers, 60 ms latency, 1 match: for CI
scripts/netplay-test.sh --peers "2 3 4" --conditions "a b c d"
scripts/netplay-test.sh --disconnect                         # 3 and 4 peers, conditions a and c: a joiner killed or leaving, the host killed, two joiners killed
scripts/netplay-test.sh --map-transfer                       # the map is downloaded
scripts/netplay-test.sh --match tour --peers "2 3" --conditions "a c"   # a whole tour (also tournament, world)
scripts/netplay-test.sh --builds "build build-clang"         # peers alternate between builds
```

The disconnect tests check that the survivors play the match to its end, agree on the drop frame, and have identical `netsum:` lines. The conditions are `a` (no added lag), `b` (60 ms latency, 15 ms jitter), `c` (100 ms, 5% loss) and `d` (150 ms, 20 ms jitter, 10% loss), each way. The exit status is 0 if every run passed. The logs go to `/tmp/smw-netplay-test` (`--out`).

**Screenshots of the overlays.** Run two processes with rendering on (no `--at-norender`) and add `--at-shot-every 30 --at-shot-dir DIR` to the one to look at. Stopping the other process for a few seconds (`kill -STOP` / `kill -CONT`) shows the waiting box, `--net-sim-latency` makes rollbacks that the stats line shows.

**Unit tests.** `smw-tests` covers the session, lobby, mesh, snapshots and config.


## Lobby screens and who may change what

The lobby follows the offline flow. Its home is the **Team and Character Selection** screen (`MI_OnlineTeamSelect`): each player picks the skin (up/down) and team (left/right) of its own character and presses select to be ready; remote players and CPU players are shown, locked. The host's Continue opens the match type and game settings screens (the game's own `UI_GameSettingsMenu`, whose Start begins the match); the others' Settings button shows the same screen live, read-only. The Options page has the local players, CPU players, input delay, "Match settings" (Host only / Everyone can change) and kick.

Only the host changes the lobby unless it sets "Everyone can change" (`LOBBY_EXTRA` carries the flag). Then a joiner's mode, goal or map edit is sent as `SETTINGS_REQUEST` (`NetLobby::requestSetting`); the host ignores it while the flag is off, applies it to its own menus otherwise (`NetGame::popSettingRequest`, `MenuState::ApplyOnlineSettingRequests`) and the lobby broadcast shows the result (last write wins). Series setups, delay and CPU players stay host-only. `HOST_LACKS_FROM=L scripts/menu-screenshots.sh` tests skin transfer to the host.
