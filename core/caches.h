// core/caches.h
//
// Overlay caches baked from the player's OWN game files on first launch (nothing copyrighted
// ships): the DSFont24 glyph blob, the covenant/role icon set, and the HP-gauge frame. Each is a
// cheap no-op when the cached blob's signature still matches its source files. Paths may use '\\'
// or '/'; the Linux platform layer resolves them case-insensitively.
#pragma once

#include <string>

namespace caches {

void GenerateFontCacheIfNeeded(const std::string& dataDir, const std::string& blobPath);
void GenerateIconCacheIfNeeded(const std::string& dataDir, const std::string& blobPath);
void GenerateGaugeCacheIfNeeded(const std::string& dataDir, const std::string& blobPath);

} // namespace caches
