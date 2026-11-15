// The world editor, built into the game (EditorLauncher.h). Its sources are
// the standalone editor's (src/worldeditor), compiled here as one unit inside
// the namespace worldeditor:: so that its names don't clash with the game's or the
// other editor's. SMW_EMBEDDED_EDITOR leaves out what the game has itself (the
// window, the screen surfaces, the stand-ins for gameplay code).
#include <new>    // (placement new: a session starts with new menus)

#define SMW_EMBEDDED_EDITOR
#include "../../worldeditor/WorldEditorState.h"

namespace worldeditor {
#include "../../worldeditor/WorldEditorDraw.cpp"
#include "../../worldeditor/WorldEditorEdit.cpp"
#include "../../worldeditor/WorldEditorFile.cpp"
#include "../../worldeditor/WorldEditorGlobals.cpp"
#include "../../worldeditor/WorldEditorStage.cpp"
#include "../../worldeditor/WorldEditorStageMode.cpp"
#include "../../worldeditor/WorldEditorTerrain.cpp"
#include "../../worldeditor/WorldEditorTileModes.cpp"
#include "../../worldeditor/WorldEditorVehiclesWarps.cpp"
#include "../../worldeditor/main.cpp"
} // namespace worldeditor
