#pragma once
#include "pomposa_track.h"

namespace Tracks {
// Extend this catalog when adding circuits. Menu and timing share this list.
constexpr const Timing::TrackConfig* Catalog[] = {&PomposaLong};
constexpr unsigned int Count = sizeof(Catalog) / sizeof(Catalog[0]);
}
