// linux/pe_version.h -- read a Windows DLL's file version without Windows APIs.
#pragma once

#include <string>

// "major.minor.build" from the PE's VS_FIXEDFILEINFO (same format the Windows launcher shows for
// d3d9.dll), or "" if the file is missing or has no version resource.
std::string PeFileVersion(const std::string& path);
