#pragma once

// THE MAP AND WORLD EDITORS, IN THE GAME (main menu > Editors)
//
// The level (map) editor and the world editor are the standalone programs'
// code (src/leveleditor, src/worldeditor), built into the game too
// (EmbeddedLevelEditor.cpp, EmbeddedWorldEditor.cpp: each in its own
// namespace). run() hands them the game's window for a session:
//   - what they replace with their own copies (the resource manager with the
//     editor graphics, the map, the map/world/filter lists, the world map)
//     is swapped in for the session and back afterwards, and the game's
//     settings (game_values) are saved and restored: the editors use them
//     as scratch space (the world editor turns the sound off, rebinds player
//     1's keys, selects worlds)
//   - the tilesets are the game's: tile type changes apply right away
//   - game controllers work in them (EditorInput.h)
// Maps and worlds saved in an editor show up in the game's lists afterwards.
namespace editors {

enum class Editor { Level, World };

// Runs an editor until it is closed (its own quit, or Escape out of it).
// False if the window was closed during the session: the game should quit.
bool run(Editor editor);

} // namespace editors
