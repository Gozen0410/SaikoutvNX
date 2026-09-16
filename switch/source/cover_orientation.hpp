#pragma once

#include "anime.hpp"
#include <string>

// Returns a cached cover whose JPEG EXIF orientation, when present, has been
// baked into the pixel data so Borealis/NanoVG can render the image without
// needing to interpret EXIF metadata. Falls back to the raw cached file when
// the image has no EXIF orientation or normalization fails.
std::string ensureAnimeCoverOrientationNormalized(const AnimeSummary& anime);
