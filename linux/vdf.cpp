// linux/vdf.cpp -- see vdf.h.
#include "vdf.h"

#include <fstream>
#include <sstream>
#include <strings.h>

namespace vdf {

const Node* Node::Child(const std::string& key) const {
    for (const auto& c : children)
        if (strcasecmp(c.first.c_str(), key.c_str()) == 0) return c.second.get();
    return nullptr;
}
const Node* Node::Find(std::initializer_list<const char*> path) const {
    const Node* n = this;
    for (const char* k : path) { n = n->Child(k); if (!n) return nullptr; }
    return n;
}
std::string Node::Get(std::initializer_list<const char*> path, const std::string& def) const {
    const Node* n = Find(path);
    return (n && n->IsLeaf()) ? n->value : def;
}

namespace {
struct Lexer {
    const std::string& s; size_t i = 0;
    explicit Lexer(const std::string& t) : s(t) {}
    void SkipWs() {
        for (;;) {
            while (i < s.size() && (unsigned char)s[i] <= ' ') ++i;
            if (i + 1 < s.size() && s[i] == '/' && s[i + 1] == '/') {        // // comment
                while (i < s.size() && s[i] != '\n') ++i;
                continue;
            }
            break;
        }
    }
    // Next token: a string (quoted or bare), "{" or "}". Returns false at end of input.
    bool Next(std::string& tok, bool& isBrace) {
        SkipWs();
        if (i >= s.size()) return false;
        if (s[i] == '{' || s[i] == '}') { tok = std::string(1, s[i++]); isBrace = true; return true; }
        isBrace = false; tok.clear();
        if (s[i] == '"') {
            ++i;
            while (i < s.size() && s[i] != '"') {
                if (s[i] == '\\' && i + 1 < s.size()) {
                    char e = s[i + 1];
                    tok += (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
                    i += 2;
                } else tok += s[i++];
            }
            if (i < s.size()) ++i;
        } else {
            while (i < s.size() && (unsigned char)s[i] > ' ' && s[i] != '{' && s[i] != '}' && s[i] != '"') tok += s[i++];
        }
        // Skip a trailing conditional like [$WIN32].
        SkipWs();
        if (i < s.size() && s[i] == '[') { while (i < s.size() && s[i] != ']') ++i; if (i < s.size()) ++i; }
        return true;
    }
};

void ParseInto(Lexer& lx, Node& parent, int depth) {
    std::string key, tok; bool brace = false;
    while (lx.Next(key, brace)) {
        if (brace) { if (key == "}") return; continue; }          // stray '{' -- ignore
        if (!lx.Next(tok, brace)) return;
        auto child = std::make_unique<Node>();
        if (brace && tok == "{") { if (depth < 64) ParseInto(lx, *child, depth + 1); }
        else if (brace) { parent.children.emplace_back(key, std::move(child)); return; }  // "key" }
        else child->value = tok;
        parent.children.emplace_back(key, std::move(child));
    }
}
} // namespace

std::unique_ptr<Node> Parse(const std::string& text) {
    auto root = std::make_unique<Node>();
    Lexer lx(text);
    ParseInto(lx, *root, 0);
    return root;
}

std::unique_ptr<Node> ParseFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::make_unique<Node>();
    std::ostringstream ss; ss << f.rdbuf();
    return Parse(ss.str());
}

} // namespace vdf
