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
after the map was changed or deleted. Every 60 ticks a state checksum is stored; if a
replay plays differently (another game version) the theater says so.

`smw --replay FILE` plays one at startup (with `--autotest`: then quits).

## The theater

Replays play in the theater (`src/smw/GameplayTheater.cpp`), modelled on Halo 3's:

| Key (player 1's game keys) | |
|---|---|
| Jump or Start | play / pause |
| Left / Right | skip 5 s (paused: one frame; held: repeats) |
| Down | speed: 1/4, 1/2, 1, 2, 4, 8x |
| Run (held) | fast forward |
| Powerup | follow the next player: their name and the buttons they hold |
| H | hide the bar |
| Escape | leave |
| Mouse | click or drag on the timeline to jump |

The timeline shows the score changes (team colors) and the end of the match (gold). When
a replay starts it is simulated once without sound or effects (a few ms), keeping a
snapshot every 4 seconds: jumping anywhere is instant.

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

When an editor asks for a name, an on-screen keyboard opens (A types, B deletes,
X shift, Y space, Start done, Back cancel).

## Testing

- `--autotest --replay FILE --at-replay-seeks N`: plays a replay with a random jump every
  N frames; the checksums after each jump must still match
- `--autotest --at-match shots --at-role history|theater|editors --at-shots DIR`:
  screenshots of the history screen, the theater, and both editors driven by a scripted
  controller (followed by a match, to check that the game came back intact)
