// The level (map) editor, built into the game (EditorLauncher.h). Its sources are
// the standalone editor's (src/leveleditor), compiled here as one unit inside
// the namespace leveleditor:: so that its names don't clash with the game's or the
// other editor's. SMW_EMBEDDED_EDITOR leaves out what the game has itself (the
// window, the screen surfaces, the stand-ins for gameplay code).
#define SMW_EMBEDDED_EDITOR
#include "../../leveleditor/LevelEditorState.h"

namespace leveleditor {
#include "../../leveleditor/EditorAnimation.cpp"
#include "../../leveleditor/EditorBackgrounds.cpp"
#include "../../leveleditor/EditorBlocks.cpp"
#include "../../leveleditor/EditorDialogs.cpp"
#include "../../leveleditor/EditorDraw.cpp"
#include "../../leveleditor/EditorFile.cpp"
#include "../../leveleditor/EditorGlobals.cpp"
#include "../../leveleditor/EditorMapEdit.cpp"
#include "../../leveleditor/EditorMapHazards.cpp"
#include "../../leveleditor/EditorModeItems.cpp"
#include "../../leveleditor/EditorPlatforms.cpp"
#include "../../leveleditor/EditorTiles.cpp"
#include "../../leveleditor/EditorWarpEyecandy.cpp"
#include "../../leveleditor/main.cpp"
} // namespace leveleditor
