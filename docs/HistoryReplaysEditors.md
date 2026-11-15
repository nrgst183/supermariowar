# Match history, replays and the built-in editors

## Match history (main menu > History)

Every match is added to the history when it ends or is left, local or online:

- `match_history.txt` in the settings folder: a log to read (date, mode and goal, map,
  length, the teams with their players, skins and final scores, the winner)
- `match_history.jsonl`: the same, one JSON object per line (for tools; the History
  screen reads it)
- `replays/*.smwr`: the replay of each match (the newest 200 are kept)

The History screen lists the matches, newest first (up/down, left/right a page), with the
selected match's details below. Select plays its replay.

## Replays

The simulation is deterministic (rollback netplay depends on it), so a replay is the
match config (settings, players, map, RNG seed) plus every tick's input, a few KB per
minute (`src/smw/rollback/Replay.h`). The map file is stored too: a replay still plays
after the map was changed or deleted. So are the players' skin files (a skin that isn't
on this computer, or is a different file here, goes to the downloaded skins) and the
music: the song the match started with and every change of song, by tick, since a match
picks its songs at random; the theater plays the same songs at the same moments, also
after a jump. An online replay also keeps every other player's ping, averaged over each
second as the recording computer saw it: the theater shows it as signal bars on their
score panels, like the match did (Graphics > Ping Bars turns them off, in matches and replays). Every 60 ticks a state checksum is stored; if a replay plays differently
(another game version) the theater says so. Replays from before the skins and songs were
stored still play, with the skins found by name and random songs.

CPU players are recorded like the humans: their AI runs outside the simulation and its
buttons are the CPU's input in every tick (`src/smw/CpuDriver.h`). So a replay plays the
CPUs exactly as they played, also after the AI was changed (the AI no longer decides
anything inside the replay). Each player's input is stored as runs of equal bytes, so four
CPUs add about 5 KB per minute. Replays from before this (version 3 and older) still run the
AI inside the simulation, so they play as recorded only with the AI of their time.

`smw --replay FILE` plays one at startup (with `--autotest`: then quits).

## The theater

Replays play in the theater (`src/smw/GameplayTheater.cpp`), modelled on Halo 3's. Its dock
along the bottom is a riveted panel (Graphics > Overlay Colors: Night, the default, dark blue
in SMB3's white status bar frame; Pipe, the game's green dialog; Castle; SMB3, the status
bar's own blue; the editors' panels use the same
colors, see `src/smw/ui/SmwTheme.h`): the timeline is a line (white as far as the
replay has played, a gold knob for the playhead, flags in the scoring team's color, a checkered
flag at the end of the match) with the buttons under it. Each button shows the controller
button or the key that does the same, for the device used last; any connected controller
works.

| | Controller | Keyboard | Mouse |
|---|---|---|---|
| play / pause | A (or Start) | Space | the play button |
| back / forward 5 s (paused: a frame) | LB / RB (d-pad left / right) | Left / Right | the arrow buttons |
| rewind / fast forward (held) | LT / RT | Shift+Left / Shift+Right | |
| slower / faster (1/4x to 8x) | d-pad down / up | Down / Up | the clock button (right click: slower) |
| follow a player (their name, the buttons they hold) | X | F | the camera button |
| hide the bar | Y | H | the eye button |
| the controls | Back | F1 | the ? block |
| leave | B | Escape | the warp pipe |
| jump | | Home / End | click or drag on the meter |

When a replay starts it is simulated once without sound or effects (a few ms), keeping a
snapshot every 4 seconds: jumping anywhere is instant.

## Button prompts

The theater and the editors draw controller buttons in two styles
(`src/smw/ui/InputPrompts.h`): the Xbox one (A B X Y, LB RB, LT RT, View and Menu) and the
original Xbox's (the Duke and the Controller S: the colored gem buttons, White and Black,
the L and R triggers, START and BACK). Controls > Button Prompts picks one; Auto (the
default) uses the style of the controller pressed last. An original Xbox controller is
recognised by its USB ids (Microsoft's and the licensed ones) or its name; through an
adapter that reports itself as an Xbox 360 pad, pick Original Xbox by hand.

## The editors (main menu > Editors)

The map (level) editor and the world editor run inside the game
(`src/smw/editors/EditorLauncher.h`). They are the standalone editors' code, compiled
into the game in their own namespaces; `smw-leveledit` and `smw-worldedit` still build
as before. The game's graphics, map lists and settings are set aside for a session and
come back afterwards; maps and worlds saved in an editor show up in the game's lists.

Game controllers work in them (`src/smw/editors/EditorInput.h`):

| Controller | |
|---|---|
| Left stick | pointer (faster the further it's pushed) |
| D-pad | pointer, one tile per press |
| A / B | place / remove (left / right click; hold A to paint) |
| X / Y (held) | Shift / Ctrl |
| Right stick | arrow keys (scrolls the world) |
| RB | Enter |
| Back | Escape (in the main mode: asks to quit; A on Yes) |
| Start | the action menu: every mode and command of the editor |
| LB | hide or show the buttons' bar |

On a keyboard, Tab opens the action menu too; it lists each command's key, and the mouse
can click its entries. The bar along the bottom shows the controller's buttons (or, for a
few seconds, Tab, F1 and Esc), and every cursor marks the tile it edits.

When an editor asks for a name, an on-screen keyboard opens (A types, B deletes,
X shift, Y space, Start done, Back cancel; with the mouse: click the keys).

## Editing together

**On one computer:** every connected controller gets its own cursor (P1, P2, ...); each
paints and clicks where its cursor is. The editor's mode and selected tile are shared.

**Over the network** (Editors > Host Map / Host World, or Join with the host's address;
port 7301): everyone edits the same map or world in their own game, and sees the others'
cursors with their names (the nickname from the Online menu), who is there (bottom left),
and who joins or leaves.

How it works (`src/smw/editors/CoEdit.h`): every game edits its own copy. Once per frame,
each compares its copy with the last synced state; every changed map cell, world tile or
section (items, hazards, platforms, backgrounds and music; stages, vehicles, warps, start
items) is sent as an operation. The host puts all operations in one order and sends them
to everyone, so all copies end up the same: when two people change the same cell at once,
the later one (in the host's order) wins everywhere. Because changes are found by comparing,
every tool of the editors works together without changes to the tools.

The host opens the map or world (the others get it when they join) and is the only one who
saves to its file, or switches to another map or world (that switches it for everyone).
The others may Save As a copy of their own.

## Testing

- `--autotest --replay FILE --at-replay-seeks N`: plays a replay with a random jump every
  N frames; the checksums after each jump must still match
- `--autotest --at-match shots --at-role history|theater|editors|controls --at-shots DIR`:
  screenshots of the history screen, the theater, both editors driven by a scripted
  controller (followed by a match, to check that the game came back intact), and the
  controls menu; `--at-prompts 1|2` draws the Xbox or the original Xbox buttons, `--at-colors 0-3` picks the
  overlay colors
- `--at-role cohost` and `--at-role cojoin --at-addr 127.0.0.1` (`--at-shotmap 1`: the
  world editor): two or more games edit together with scripted controllers, at the same
  time and on the same cells; each prints a hash of its document (`cotest:`), which must
  be the same everywhere. `--at-role colocal`: two controllers in one editor
