#pragma once

#include <string>

// What the editor overlay (EditorInput.cpp) shows of the world editor: its mode
// and the world, read from the editor's own variables. Implemented beside the
// editor's code, in EmbeddedWorldEditor.cpp.
namespace worldeditor::shell {

// The editor shows the world (not one of its full-screen pickers or dialogs)
bool inMapView();
// The mode's name ("Background", "Path"...), and whether tiles join up as they're painted
std::string modeName();
bool autoPaint();
// The world's name (its file's, without the folder and the extension)
std::string worldName();

} // namespace worldeditor::shell
