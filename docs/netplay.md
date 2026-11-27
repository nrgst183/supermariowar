# Netplay

SMW has an *experimental* online mode with rollback netcode, for up to four players. The players connect directly to each other: there is no server, one player hosts a lobby and the others join it. Every player's game simulates the whole match and the peers only exchange their inputs. When a late input arrives, the game rolls back to the first wrong frame and re-simulates up to the present, so your own controls always respond immediately.

The old lobby server mode (`smw-server`) has been removed.

## Playing online

### Hosting

Every game starts in the same lobby, online or not:

1. In the main menu, choose **Start**. The lobby opens as a game on this computer (**Open to: No one**), with the players and CPUs of the last game. Who plays is set there (**My players**, **CPU players**), and the main menu's **Go!** plays with the same ones.
2. Set **Open to** to **LAN** (players on your network see the game in their **Join a Game** list) or **Internet** (the game also asks your router to open the port, see below). The status line shows the address the others join at. The port is **UDP 7300**, or the one in **Options, Online**.
3. Wait until the others appear in the lobby, then **Continue** to set up the match as usual (match type, game mode, map, settings). The host's selections are what everybody plays.

**Join a Game** has a **Host a game** button too: the same lobby, already open to the LAN.

### Joining

1. Main menu, **Join a Game**. Games hosted on your network show up in the list by themselves: pick one.
2. A game on the internet: **Join by address**, then type the host's address: `192.168.1.20` or `192.168.1.20:7300` (the port can be left out when it is 7300), and wait in the lobby for the host to start.
3. **A match already being played** can be joined too (a single match: a tournament, tour or world can only be joined between them). The lobby says so: pick your character and skin as usual, then **Join the match**. Your game catches up on the match in a moment (the host sends everything that has happened so far, and your game plays it through without showing it), then you take over one of the CPU players: "Alice joined - takes over player 3" shows on every screen. If there's no CPU player to take over, you watch the match and play from the next one.

All players must run the **same version of the game**: the connection is refused if the builds (or the game data that matters for the simulation) differ.

### Port forwarding and firewalls

- **On a LAN** (same router or switch) nothing is needed: join the host's local address.
- **Over the internet**, the host's port has to be reachable. When you host, the game asks your router to forward it by itself (**UPnP**, which most home routers have on). Until a friend joins, the lobby's status line says how it went:
  - **"Friends join at 203.0.113.5:7300"**: done; give your friends that address.
  - **"Couldn't open port 7300: forward UDP 7300"**: no router answered (UPnP is off, or there is none), or it refused (the game's log says why, `upnp:` lines). Forward **UDP 7300** (or the port you chose) on the router to your computer by hand, and allow it in the firewall.
  - **"Port 7300 opened; your ISP's NAT may block it"**: the router's own internet address is private (carrier-grade NAT, common on mobile and some fibre connections), so the forward doesn't reach the internet. Then a forward by hand doesn't help either: use a VPN such as a virtual LAN (ZeroTier, Tailscale, Radmin VPN), or let a friend host.
  The forward is removed when the lobby closes. `--net-upnp 0` turns this off.
- Joiners need no forwarding. With several players, the joiners also connect to *each other* directly (a full mesh), so every joiner must be able to reach the other joiners. Ordinary home routers allow this, because the joiners connect outwards.
- There is no relay server: if joining times out, the host's port is most likely not reachable.

### Tournaments, tours and worlds

Every match type can be played online: single games, minigames, and the multi-match modes **tournament**, **tour** and **world**. The host picks the match type (and the number of wins, the tour or the world) with **Continue** in the lobby, and starts it like a single game. Then:

- The **host leads the menus** between the matches: it continues from the scoreboard and the tour stop screen, and in a tournament it chooses every game (mode, goal, map). The other players see the same screens ("Waiting for the host...").
- The **bonus wheel** is the same for everybody, and only the winning team can stop it (the host stops it after a while if they don't, or at once for CPU players).
- On the **world map**, whoever has the turn moves, enters stages and uses items, as offline; every player can open their team's items with the random key. The map waits for the slowest player's input, so it reacts a little later for the players far from the host.
- When it is over (or the host leaves it), everybody returns to the lobby.
- A player who leaves or loses the connection between matches is replaced by a CPU player for the rest of the tournament, tour or world, so the standings stay meaningful. If the host is lost, the lobby is closed for everybody (as always).
- Every player needs the tour or world file (the same version) in their data directory; maps are downloaded as usual. On the world map, the exit key does nothing online: leave the lobby to quit.

### Things to know

**Input delay.** An input is applied a few frames after it is pressed, so that most of the network latency is hidden without rolling back. The game measures the round trip times in the lobby and picks the delay by itself (0 to 6 frames of 16 ms: on a LAN none at all; the host can set a fixed value in the lobby). A short delay means more rollbacks, a long one means less responsive controls. The automatic delay leaves about four frames of the latency to rollback: a correction after a rollback is not a jump, the players glide to where they really are within about 100 ms, so rollbacks are hard to see.

**Maps.** If the host's map is not in your maps folder, the host sends it to you when the match starts (maps up to 1 MB). Downloaded maps are saved in `maps/net/` in the data directory, and the lobby shows the progress.

**Skins.** Every player is shown (in the lobby and in the match) with the skin their own machine has chosen. A skin that you don't have, or have under the same name but with other contents, is sent by its owner when you join (skins are small images, at most 128 KB, sent in 1 KB chunks), checked against its hash and saved in `skins/net/` in the data directory, under a safe name that never replaces an existing file. Skins are cosmetic and never delay or change a match: until a skin has arrived (or if it can't be used) the player is drawn with the default skin in the color of the slot.

**Ping bars.** Every other player's score panel shows small signal bars for their connection: four green bars under 60 ms, three under 120 ms, two (yellow) under 200 ms, one (red) above. The lobby's player cards show the same bars. Online replays keep each player's ping, averaged per second, and the theater shows the bars as they were. **Options, Online, Ping Bars** turns them off.

**Pausing.** The **host's** pause button pauses the match for everyone, as on one computer: every player's game stops on the same frame and shows "Paused by Alice" until the host goes on (the Map Maker's host gets its own pause menu, with the rights, Save and Clear map). The others' pause button opens the online panel, below.

**Run-ahead.** **Options, Online, Run-Ahead** (under your nickname; on the Xbox, in the Join a Game menu), or during a match the **pause button** (the others', not the host's): it opens a panel (the match goes on, your players stand still while it's up) with the ping, how many frames late the others' moves come, the input delay, and the run-ahead to change with left and right. **Auto** (the default) runs as far ahead as the input delay, which cancels it (with no input delay, nothing: rollback already shows your buttons at once); off and 1 to 10 frames are the other settings. Measured (`--at-lagtest`), your own jump shows on the screen *input delay minus run-ahead* frames after the press, never less than none: with Auto, on the same frame at any delay, even at 150 ms latency and 10% loss (a delay of 6). More run-ahead than the input delay doesn't make your buttons any faster; it only shows the others further ahead on a guess. Run-ahead shows the match that many frames ahead of where it really is, as emulators do: a button's effect appears that much sooner. Each frame the game runs its real tick, then a few more with the buttons held as they are, draws that, and goes back to the real state before the next frame. When you change your buttons, what was shown ahead is replaced a frame later, by a step of a pixel or two. It is only used online, to win back some of the input delay the network costs (the other players' last inputs are repeated, as rollback does anyway); offline matches are drawn as they are. A frame of it costs about what a tick does (around 10 microseconds on a PC), and it never changes the match itself: the checksums and replays are the same with it on or off.

**Music.** The host's game picks the songs (the match's first, the next one, the Map Maker's music) and everyone hears the same one: the others play the song the host's game tells them, if they have it (else their own). A host with the music turned off sends none; then each player's game picks its own after a few seconds.

**Network stats.** The bar at the top of the screen, under the scores, shows every other player's connection (signal bars and round trip time), the input delay in frames, and how many rollbacks happen per second. It is off by default and only drawn in online matches. Toggle it with **F3** during a match, or with **Options, Online, Net Stats**.

**Waiting.** When another player's inputs are late by more than the game can predict (about eight frames), the match freezes for a moment. After a split second, a box shows **Waiting for <name>**, for how long, and a line that runs down to the moment the CPU takes over. If a player is silent for **5 seconds** the connection is considered lost, and the CPU takes over that player (see below). After 3 seconds of waiting, the box also counts down the seconds. A player that is only lagging is never dropped before that.

**Leaving.** There is no pause online (the others cannot wait for you). Pressing the exit key asks **Leave online match?**. If you leave, you go back to the main menu and the others play on without you. Closing the game has the same effect.

**Disconnects.** If a player leaves or loses the connection during a match, the match goes on for the others: after a moment of waiting, **"Alice disconnected - CPU takes over"** shows, and from then on the CPU plays that player (same AI and difficulty as the other CPU players, the same on every machine). If everybody else is gone, the last player plays the match to its end against the CPU. A player that drops out while the match is still being set up (before it starts) cancels it, as before: everybody returns to the lobby.

**Getting back in.** A player whose connection drops doesn't have to do anything: their game tries to get back into the lobby for a minute ("Lost the connection to the host: getting back in..."). Back in, they have their name, players, team and color again, and if the match is still being played they catch up on it and get **their own player back** from the CPU ("Bob joined - takes over player 2"). Someone who closed the game can do the same by hand: join the lobby again with the same nickname and choose **Join the match**.

**When the host leaves.** The lobby doesn't end: the player who joined first among those still there hosts it next, at their own address, and everyone else's game moves over to it by itself ("The host left: Bob hosts the lobby now"). The new host's lobby has the same mode, goal, map, CPU players, input delay and settings rights. If the host leaves (or loses the connection) during a match, the match is played to its end first. Over the Internet the new host's address must be reachable the same way the old one's was, so this works best on a LAN or when every player could host. Only a host alone with one other player, or **--autotest**'s end, closes the lobby for good.

**Rematch or next map.** After a match, a **Play again?** panel takes the team box's place and asks everyone: **Rematch** (the same match again) or **Next map** (the next map in the list, with the filters). Each choice shows the numbers of the players who voted for it (the status line, their names), and a crown marks the winner once everyone has voted. Once everyone has voted, the host's game starts the winner after two seconds (a tie is a rematch). B closes the panel without voting; the host can then set up the next match as usual.

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
| `--net-cut-at F,MS` | at frame F of a match, the network goes dead for MS ms, as with a pulled cable (reconnect tests) |
| `--net-reconnect 0` | don't try to get back in after a lost connection, and don't move the lobby to a new host |
| `--net-upnp 0\|1` | ask the router to forward the host's port (default: yes, except with `--autotest`) |
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
- **Port mapping** (`PortMapping.h`). When hosting, a background thread asks the router to forward the port with UPnP (SSDP search, the router's description, `AddPortMapping`/`GetExternalIPAddress`/`DeletePortMapping` over SOAP), over ENet's sockets. `NetGame::routerNote()` turns its status into the lobby's line. Tested against a fake router; the parsing has unit tests (`tests/rollback/test_PortMapping.cpp`).
- **Pause and joining during a match** (`RollbackSession.h`, PAUSE and JOINING). Both ride in the host's input packets, so no peer can confirm the frame they happen at without knowing of them. The pause holds every peer at the next frame of the host's input (a peer that guessed past it rolls back to it). A player who joins during a match gets the match config and its confirmed inputs from the host's replay recording (`LateConfig`, `LateTicks`), plays them through silently, and asks for a slot at the frame it caught up to (`JumpIn`); the host hands it a CPU slot from a frame J far enough ahead for it to catch up (its lag and round trip): before J the host's inputs drive the slot, from J the new peer's, and the simulation plays that slot as a human's (`g_sim.humanTakeover`, kept in the replay's takeover bits). The new peer's session starts at the frame it caught up to (`SessionConfig::startFrame`), and the others add it as a peer and send it their inputs from there.
- **Feel** (all of it outside the simulation). After a rollback the players would jump to where the late input put them: `GameplayCorrection.h` draws them with the difference as an offset that shrinks to nothing in about 100 ms (big differences, like a respawn or a warp, are drawn at once). The host begins a match half its worst round trip after sending Start, as the farthest peer gets it, so everyone starts together (it used to run ahead and stall for a few frames at the start, waiting for the others' first inputs). Time sync doesn't skip ticks (a visible hitch) unless a peer is 3 frames ahead: below that, `NetGame` makes its ticks up to 10% longer (`FPSLimiter::setTickStretch`), so the others catch up smoothly; the advantage is exchanged in 1/64 frames for that. A sound of an event that only a rollback revealed (a stomp the late input caused) is played late, once, if it is at most 6 ticks old (`Resimulation.h`, `sfx.cpp`). Run-ahead (`GameplayTick.cpp`) draws the state a few ticks past the real one: after the frame's ticks the state is saved, run on with the newest input, and loaded back at the start of the next `update()`; the ticks run ahead aren't recorded in the replay, a tick shown ahead runs again as a re-simulation (its sounds were heard then, `sim::detail::aheadTick`), and if the match would end or change state ahead, nothing is shown ahead. ENet's packet throttle is off: it would drop input packets exactly when the connection jitters. The input packets are small: a 19 byte header (times and frames as 16 bit differences) and the inputs run-length coded when players hold buttons, about 4 kB/s per peer in a 4 player match. A snapshot copies about 15 kB: the object lists' unused capacity isn't saved (`Snapshot.h`).
- **Overlays** (`NetOverlay.cpp`) draw the waiting box, the network stats and the "disconnected - CPU takes over" line from `NetGame`'s read-only queries (`waitInfo`, `peerPingMs`, `inputDelay`, `matchStats`, `dropNotice`).

### Lost peers

A peer that disconnects, leaves (BYE) or is silent for the timeout is *lost*; the others go on and the CPU takes over its slots. All remaining peers must make that switch at the same frame, with the same inputs of the lost peer before it, or they desync. That agreement is in `RollbackSession` ("LOST PEERS" in `RollbackSession.h`; message `PEER_DROP` in `NetProtocol.h`, reliable):

- Every peer keeps a *lost set*: the peers it noticed itself, united with the sets of the reports it receives. From a lost peer nothing more is accepted, so each survivor's count of its inputs (`known`) is frozen.
- On every change, a peer sends the others a report: its lost set and, per lost peer, `known`, its last inputs (up to 64 frames) and the decided drop frame, if any. A survivor with fewer of the lost peer's inputs takes them from the report (rolling back if it predicted them wrongly).
- The *coordinator* is the lowest peer id that isn't lost. Once the latest report of every survivor shows its own lost set, it decides the *drop frame* D: the highest `known` of all. Nobody has to forget an input it already used, and nobody rolls back further than for an ordinary late input (at most the 8 predicted frames that the snapshots cover). A decision is final: every peer adopts the first one it sees, and a new coordinator (the old one was lost too) adopts any decision a survivor reports.
- From frame D on, the lost peer's inputs are zero and known, and before every tick from D on the session calls `takeOverSlots()`: `GameplayState::takeOverByCpu` gives those players a `CPlayerAI` (in the simulation arena) and marks them in `g_sim.cpuTakeover`, at that tick inside the simulation. A rollback to before D undoes it, the re-simulation does it again. A survivor that already ran past D rolls back to D.
- A peer that was only cut off (it still runs) receives a report with itself in the set: its session fails, it leaves the match and the lobby.
- The host removes a lost peer from the lobby right away. After the host is lost the joiners' lobby is closed; the match still runs over the mesh (the closed lobby passes the match messages on), and ends normally. Then the lobby moves to a new host (below).

### Rejoining and host migration

- **Peer ids.** There are 8 peer ids for at most 4 machines (`MAX_PEERS`, `MAX_MEMBERS` in `NetProtocol.h`). While a match runs, the host gives a new joiner an id that isn't one of the match's (`NetMesh::setReservedIds`): a lost peer's id stays lost for the rest of its match, so someone who comes back is a new peer.
- **Their slot back** ("REJOINING" in `RollbackSession.h`). The host hands the new peer the lost player's slot from a frame J after its drop frame D, as it hands over a CPU's slot to someone joining. The slot then has three spans: the old peer's inputs up to D, the CPU from D to J, the new peer's from J (and the CPU again from the new peer's own drop frame). `cpuAt()` says which. The simulation gives the player back at J (`GameplayState::handOverToHumans` clears `cpuTakeover`; the AI stays, idle). `hostJumpIn` gives a returning player their own slot first (the same nickname), then a CPU's, then any lost player's. The join reply lists the drops decided so far, which the new peer's session counts as final (`SessionConfig::dropFrames`). The late joiner counts as caught up once it has every tick the host had confirmed (`LATE_TICKS` says how many), and a reply of slot -2 means "not yet, catch up more".
- **Reconnecting** ("RECONNECTING" in `NetGame.h`). `NetLobby::closeCause()` says why the lobby closed. A joiner that lost the host (and no other joiner did), or was dropped by it, rejoins the host's address for a minute, ending its own match first (it was cut off from it). Back in, `restoreAfterReconnect` sets its name, players, teams and colors, and joins the match being played. Meanwhile `NetGame::active()` stays true and `reconnecting()` says so.
- **Host migration** ("HOST MIGRATION" in `NetLobby.h`). On `BYE HostLeave` every joiner knows the host is gone. On a lost connection, each joiner tells the others it reaches (`HOST_GONE`) and waits up to 6 seconds for each of them to say the same: those are the lobby without its host. The lowest of their ids is the successor. It opens the lobby again on its own port, with the old lobby's settings (the mode, goal and map go through its menus as setting requests), and the others join it at the address they reached it at. A joiner that nobody else says it to was cut off itself, and rejoins the old host instead. `BYE Close` (`endSession`) still ends the lobby for everyone.
- **Rematch vote** ("REMATCH VOTE" in `NetLobby.h`). Joiners send `VOTE` (the round, the choice) to the host, which shows everyone's in `LOBBY_EXTRA`. A vote makes a joiner ready. The votes are cleared when a match starts (`setRound`).

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

**CI.** `scripts/ci-checks.sh [BUILD_DIR]` runs the checks below that need the real game: the sync test in all 22 modes, replays with random jumps, and `scripts/netplay-test.sh` (2 and 3 peers, with and without lag and loss, then the disconnect tests), in about three minutes. GitHub Actions runs it on every push (`.github/workflows/checks.yml`, the "Checks" job), and a release waits for it.

**Sync test.** It runs without a network and finds violations of the rules above: every tick it rolls back up to N ticks, re-simulates them and checks that the state is identical.

```
smw --datadir data --autotest --at-synctest 8 --at-match single --at-frames 3000 --at-norender 1
```

`--at-peersum N` prints a cross-machine state checksum every N frames (`peersum:` lines), to compare runs of different builds or compilers. `--at-runahead N` turns on the run-ahead in online auto-play (which ignores the setting; -1 is Auto), `--at-online-panel 1` opens the pause button's panel in the matches (for screenshots). `--at-pause F` makes the host pause everyone at tick F for `--at-pause-for N` frames (`pausetest:` lines; each peer prints where it was held). `--at-late-join 1` makes a joiner that finds a match being played join it (start it a few seconds after the others: `netplay: ... joins match`, `playing match` lines, and its `netsum:` lines from where it started must be the host's). `--at-lagtest N` measures the input lag: the first local player stands still, then jumps, N times, and `lagtest:` lines say how many frames (and ms) each jump took to show on the screen, with the input delay and run-ahead (`--net-delay N --at-runahead N` to try them). `NETPLAY_EXTRA_ARGS="--at-runahead 2" scripts/netplay-test.sh ...` runs the netplay test with it for every peer, `NETPLAY_HOST_ARGS` only for the host: `scripts/ci-checks.sh` runs a host 3 ticks ahead with joiners that don't run ahead, and they must agree on every checksum.

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

The disconnect tests check that the survivors play the match to its end, agree on the drop frame, and have identical `netsum:` lines. For rejoining, run a joiner with `--net-cut-at 600,9000`: it must be back in the match (`netplay: playing match 1 as player N`) with `netsum:` lines equal to the host's from its handover frame on. For host migration, run the host with `--net-quit-at 600` or `--net-cut-at 600,60000` and the joiners with `--at-match single --at-frames 900 --at-migrated-matches 1`: after the first match the lowest joiner hosts a second one (`autotest: hosting after the host left`), and the others play it. The conditions are `a` (no added lag), `b` (60 ms latency, 15 ms jitter), `c` (100 ms, 5% loss) and `d` (150 ms, 20 ms jitter, 10% loss), each way. The exit status is 0 if every run passed. The logs go to `/tmp/smw-netplay-test` (`--out`).

**Lagged bots to play against.** `scripts/net-bots.sh [BOTS] [ROUND_TRIP_MS] [HOST[:PORT]]` starts 1 to 3 bots that join your lobby (default `127.0.0.1:7300`, 300 ms round trip): each is a copy of the game with `--net-bot 1`, whose players are CPU players its own machine drives, their AI's buttons sent over the network like a far-away player's (`netplay: match N slots:` says which peer drives each slot). Host a game from the menus, run the script, and start your game with `--net-sim-latency` half the round trip too. The bots get ready on their own, play every match you start, and quit when you close the lobby; their windows start minimized and muted, each with its own settings folder.

**Screenshots of the overlays.** Run two processes with rendering on (no `--at-norender`) and add `--at-shot-every 30 --at-shot-dir DIR` to the one to look at. Stopping the other process for a few seconds (`kill -STOP` / `kill -CONT`) shows the waiting box, `--net-sim-latency` makes rollbacks that the stats line shows.

**Unit tests.** `smw-tests` covers the session, lobby, mesh, snapshots and config.


## Lobby screens and who may change what

The lobby follows the offline flow. Its home is the **Team and Character Selection** screen (`MI_OnlineTeamSelect`): each player picks the skin (up/down) and team (left/right) of its own character and presses select to be ready; remote players and CPU players are shown, locked. The host's Continue opens the match type and game settings screens (the game's own `UI_GameSettingsMenu`, whose Start begins the match); the others' Settings button shows the same screen live, read-only. The Options page has the local players, CPU players, input delay, "Match settings" (Host only / Everyone can change) and kick.

Only the host changes the lobby unless it sets "Everyone can change" (`LOBBY_EXTRA` carries the flag). Then a joiner's mode, goal or map edit is sent as `SETTINGS_REQUEST` (`NetLobby::requestSetting`); the host ignores it while the flag is off, applies it to its own menus otherwise (`NetGame::popSettingRequest`, `MenuState::ApplyOnlineSettingRequests`) and the lobby broadcast shows the result (last write wins). Series setups, delay and CPU players stay host-only. `HOST_LACKS_FROM=L scripts/menu-screenshots.sh` tests skin transfer to the host.
