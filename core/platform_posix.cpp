// core/platform_posix.cpp -- Linux/POSIX implementation of core/platform.h.
#ifndef _WIN32

#include "platform.h"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

namespace plat {

// ---------------------------------------------------------------- case-insensitive lookup
// Normalise separators, then walk the path one component at a time. A component that exists
// exactly is kept; otherwise the directory is scanned for a case-insensitive match (Windows
// installs mix "DINPUT8.dll", "DSfix.ini", "Font\\English" ...). The first component that matches
// nothing is kept as given, along with the rest, so callers can still create new files.
static std::string Resolve(const std::string& in) {
    std::string p = in;
    for (auto& c : p) if (c == '\\') c = '/';
    if (p.empty()) return p;

    std::string out;
    size_t i = 0;
    if (p[0] == '/') { out = "/"; i = 1; }
    bool missing = false;
    while (i <= p.size()) {
        size_t j = p.find('/', i);
        if (j == std::string::npos) j = p.size();
        std::string comp = p.substr(i, j - i);
        i = j + 1;
        if (comp.empty()) { if (j >= p.size()) break; continue; }
        std::string base = out.empty() ? "." : out;
        std::string cand = (out.empty() || out.back() == '/') ? out + comp : out + "/" + comp;
        if (!missing && access(cand.c_str(), F_OK) != 0) {
            bool found = false;
            if (DIR* d = opendir(base.c_str())) {
                while (dirent* e = readdir(d)) {
                    if (strcasecmp(e->d_name, comp.c_str()) == 0) {
                        cand = (out.empty() || out.back() == '/') ? out + e->d_name : out + "/" + e->d_name;
                        found = true;
                        break;
                    }
                }
                closedir(d);
            }
            if (!found) missing = true;
        }
        out = cand;
        if (j >= p.size()) break;
    }
    return out;
}

bool FileExists(const std::string& path) {
    struct stat st;
    return stat(Resolve(path).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool DirExists(const std::string& path) {
    struct stat st;
    return stat(Resolve(path).c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::vector<uint8_t> ReadAll(const std::string& path) {
    std::vector<uint8_t> v;
    int fd = open(Resolve(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return v;
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size > 0 && st.st_size < (off_t)200 * 1024 * 1024) {
        v.resize((size_t)st.st_size);
        size_t done = 0;
        while (done < v.size()) {
            ssize_t r = read(fd, v.data() + done, v.size() - done);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) { v.clear(); break; }
            done += (size_t)r;
        }
    }
    close(fd);
    return v;
}
std::string ReadText(const std::string& path) {
    std::vector<uint8_t> v = ReadAll(path);
    return std::string(v.begin(), v.end());
}

static bool WriteRaw(const std::string& p, const void* data, size_t size) {
    int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    const uint8_t* b = (const uint8_t*)data;
    size_t done = 0; bool ok = true;
    while (done < size) {
        ssize_t w = write(fd, b + done, size - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) { ok = false; break; }
        done += (size_t)w;
    }
    if (close(fd) != 0) ok = false;
    return ok;
}
bool WriteAtomic(const std::string& path, const void* data, size_t size) {
    std::string dst = Resolve(path), tmp = dst + ".tmp";
    if (!WriteRaw(tmp, data, size)) { unlink(tmp.c_str()); return false; }
    return rename(tmp.c_str(), dst.c_str()) == 0;
}
bool WriteText(const std::string& path, const std::string& data) {
    return WriteRaw(Resolve(path), data.data(), data.size());
}

bool Rename(const std::string& from, const std::string& to, bool replaceExisting) {
    std::string f = Resolve(from), t = Resolve(to);
    if (!replaceExisting && access(t.c_str(), F_OK) == 0) return false;
    return rename(f.c_str(), t.c_str()) == 0;
}
bool Remove(const std::string& path) { return unlink(Resolve(path).c_str()) == 0; }
bool MakeDir(const std::string& path) {
    std::string p = Resolve(path);
    while (!p.empty() && p.back() == '/') p.pop_back();
    return mkdir(p.c_str(), 0755) == 0 || errno == EEXIST;
}

bool StatForSignature(const std::string& path, uint32_t& sizeLow, uint32_t& sizeHigh, uint64_t& filetime) {
    struct stat st;
    if (stat(Resolve(path).c_str(), &st) != 0) return false;
    uint64_t sz = (uint64_t)st.st_size;
    sizeLow = (uint32_t)sz; sizeHigh = (uint32_t)(sz >> 32);
    // Same mapping Wine uses: FILETIME = unix seconds * 1e7 + 1601->1970 offset, + ns / 100.
    filetime = (uint64_t)st.st_mtim.tv_sec * 10000000ull + 116444736000000000ull
             + (uint64_t)st.st_mtim.tv_nsec / 100ull;
    return true;
}

// ---------------------------------------------------------------- profile strings
static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
static std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    size_t i = 0;
    while (i < text.size()) {
        size_t j = text.find('\n', i);
        if (j == std::string::npos) j = text.size();
        std::string l = text.substr(i, j - i);
        if (!l.empty() && l.back() == '\r') l.pop_back();
        lines.push_back(l);
        i = j + 1;
    }
    return lines;
}
// "[name]" -> name (trimmed); returns false if the line is not a section header.
static bool SectionName(const std::string& line, std::string& name) {
    std::string t = Trim(line);
    if (t.size() < 2 || t[0] != '[') return false;
    size_t e = t.find(']');
    if (e == std::string::npos) return false;
    name = Trim(t.substr(1, e - 1));
    return true;
}
// "key = value" -> key/value (trimmed). Comment lines (';') and lines without '=' don't count.
static bool KeyValue(const std::string& line, std::string& key, std::string& val) {
    std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '[') return false;
    size_t eq = t.find('=');
    if (eq == std::string::npos) return false;
    key = Trim(t.substr(0, eq));
    val = Trim(t.substr(eq + 1));
    return !key.empty();
}

std::string IniGet(const std::string& path, const char* section, const char* key, const char* def) {
    std::vector<std::string> lines = SplitLines(ReadText(path));
    bool in = false;
    for (const std::string& l : lines) {
        std::string name, k, v;
        if (SectionName(l, name)) { in = strcasecmp(name.c_str(), section) == 0; continue; }
        if (!in || !KeyValue(l, k, v)) continue;
        if (strcasecmp(k.c_str(), key) != 0) continue;
        if (v.size() >= 2 && (v[0] == '"' || v[0] == '\'') && v.back() == v[0]) v = v.substr(1, v.size() - 2);
        return v;
    }
    return def ? def : "";
}

// GetPrivateProfileInt: missing key -> def; otherwise RtlUnicodeStringToInteger(base 0) rules:
// optional sign, then "0x"/"0o"/"0b" picks hex/octal/binary (a bare leading 0 is still decimal),
// digits read until the first invalid one; no digits -> 0. So NetworkVersion=0x4D reads as 77.
int IniGetInt(const std::string& path, const char* section, const char* key, int def) {
    const char* kMissing = "\x01missing\x01";
    std::string v = IniGet(path, section, key, kMissing);
    if (v == kMissing) return def;
    size_t i = 0;
    bool neg = false;
    if (i < v.size() && (v[i] == '+' || v[i] == '-')) neg = v[i++] == '-';
    int base = 10;
    if (i + 1 < v.size() && v[i] == '0') {
        char p = (char)tolower((unsigned char)v[i + 1]);
        if (p == 'x') base = 16; else if (p == 'o') base = 8; else if (p == 'b') base = 2;
        if (base != 10) i += 2;
    }
    unsigned long acc = 0;
    for (; i < v.size(); ++i) {
        char c = (char)tolower((unsigned char)v[i]);
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'z') ? c - 'a' + 10 : 99;
        if (d >= base) break;
        acc = acc * (unsigned long)base + (unsigned long)d;
    }
    long r = neg ? -(long)acc : (long)acc;
    return (int)r;
}

bool IniSet(const std::string& path, const char* section, const char* key, const std::string& value) {
    std::string text = ReadText(path);
    const std::string nl = (text.empty() || text.find("\r\n") != std::string::npos) ? "\r\n" : "\n";
    std::vector<std::string> lines = SplitLines(text);
    const std::string entry = std::string(key) + "=" + value;

    int secLine = -1, lastEntry = -1;
    bool done = false, in = false;
    for (size_t i = 0; i < lines.size() && !done; ++i) {
        std::string name, k, v;
        if (SectionName(lines[i], name)) {
            if (in) break;                                // left our section without finding the key
            in = strcasecmp(name.c_str(), section) == 0;
            if (in) { secLine = (int)i; lastEntry = (int)i; }
            continue;
        }
        if (!in) continue;
        if (!Trim(lines[i]).empty()) lastEntry = (int)i;
        // Like Windows, an updated key keeps the spelling it already has in the file.
        if (KeyValue(lines[i], k, v) && strcasecmp(k.c_str(), key) == 0) { lines[i] = k + "=" + value; done = true; }
    }
    if (!done) {
        if (secLine >= 0) {
            lines.insert(lines.begin() + lastEntry + 1, entry);
        } else {
            lines.push_back("[" + std::string(section) + "]");
            lines.push_back(entry);
        }
    }
    std::string out;
    for (const std::string& l : lines) out += l + nl;
    return WriteText(path, out);
}

std::string Join(const std::string& dir, const std::string& name) {
    if (dir.empty()) return name;
    char last = dir.back();
    return (last == '\\' || last == '/') ? dir + name : dir + "/" + name;
}

} // namespace plat

#endif // !_WIN32
