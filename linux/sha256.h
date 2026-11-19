// linux/sha256.h -- SHA-256 (FIPS 180-4), used to verify downloaded launcher updates.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Lowercase hex digest of the data / of a file ("" if the file can't be read).
std::string Sha256Hex(const void* data, size_t size);
std::string Sha256FileHex(const std::string& path);
