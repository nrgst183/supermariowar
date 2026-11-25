#pragma once

struct MapHazard;

void LoadMapHazards(bool fPreview);
// A hazard's objects (a fire bar's fireballs, a cannon, a plant)
void SpawnMapHazard(const MapHazard& hazard, bool fPreview);
