#pragma once

#include <SDL3/SDL_surface.h>

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

// The whole world (the preview): its size in tiles, where the screen's top left is in
// it (in pixels, 32 a tile), and the world drawn whole into a new surface (32 pixels a
// tile; the caller frees it)
void worldSize(int* cols, int* rows);
void viewOrigin(int* x, int* y);
SDL_Surface* drawWhole();

} // namespace worldeditor::shell
