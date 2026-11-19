// core/platform.h
//
// The thin OS layer the shared launcher core sits on. Paths are UTF-8 std::strings and may use
// '/' or '\\' as separators on either platform.
//
//   * Windows (platform_win.cpp): Win32 file APIs, and the REAL GetPrivateProfileString /
//     WritePrivateProfileString for mctde-link.ini -- so the Windows launcher's ini behaviour is
//     byte-for-byte what it always was.
//   * Linux (platform_posix.cpp): POSIX file APIs, a reimplementation of the Windows profile-string
//     rules, and case-insensitive path lookup. Game installs come from Windows, so a file the core
//     asks for as "dinput8.dll" may really be "DINPUT8.dll" on a case-sensitive filesystem.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace plat {

// Existence (regular files only / directories only).
bool FileExists(const std::string& path);
bool DirExists(const std::string& path);

// Whole-file read. Returns an empty vector on failure (and for files over 200 MB).
std::vector<uint8_t> ReadAll(const std::string& path);
std::string ReadText(const std::string& path);

// Write via <path>.tmp + rename, so a crash never leaves a half-written file.
bool WriteAtomic(const std::string& path, const void* data, size_t size);
// Plain truncate-and-write (matches the old WriteFileUtf8 behaviour for ini text).
bool WriteText(const std::string& path, const std::string& data);

bool Rename(const std::string& from, const std::string& to, bool replaceExisting);
bool Remove(const std::string& path);
bool MakeDir(const std::string& path);

// File size (low 32 bits) and last-write time as a Windows FILETIME (100 ns ticks since 1601).
// Linux converts st_mtime the same way Wine does, so cache signatures match across platforms.
bool StatForSignature(const std::string& path, uint32_t& sizeLow, uint32_t& sizeHigh, uint64_t& filetime);

// Profile-string (INI) access with Windows semantics: case-insensitive section/key match, value
// trimmed, surrounding quotes stripped, first match wins, writes update in place or append.
std::string IniGet(const std::string& path, const char* section, const char* key, const char* def);
int  IniGetInt(const std::string& path, const char* section, const char* key, int def);
bool IniSet(const std::string& path, const char* section, const char* key, const std::string& value);

// Join a directory and a relative name with the platform separator.
std::string Join(const std::string& dir, const std::string& name);

} // namespace plat
