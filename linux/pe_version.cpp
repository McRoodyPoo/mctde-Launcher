// linux/pe_version.cpp -- see pe_version.h.
#include "pe_version.h"
#include "../core/platform.h"

#include <cstdint>
#include <cstdio>

std::string PeFileVersion(const std::string& path) {
    std::vector<uint8_t> d = plat::ReadAll(path);
    if (d.size() < 64 || d[0] != 'M' || d[1] != 'Z') return "";
    // VS_FIXEDFILEINFO starts with the signature 0xFEEF04BD, then struct version, then
    // dwFileVersionMS / dwFileVersionLS. It's DWORD-aligned inside the version resource.
    for (size_t i = 0; i + 16 <= d.size(); i += 4) {
        if (d[i] != 0xBD || d[i + 1] != 0x04 || d[i + 2] != 0xEF || d[i + 3] != 0xFE) continue;
        auto u32 = [&](size_t o) { return (uint32_t)d[o] | (uint32_t)d[o + 1] << 8 | (uint32_t)d[o + 2] << 16 | (uint32_t)d[o + 3] << 24; };
        uint32_t ms = u32(i + 8), ls = u32(i + 12);
        char v[48];
        snprintf(v, sizeof(v), "%u.%u.%u", ms >> 16, ms & 0xFFFF, ls >> 16);
        return v;
    }
    return "";
}
