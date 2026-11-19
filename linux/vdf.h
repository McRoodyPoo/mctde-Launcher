// linux/vdf.h -- minimal reader for Valve's text KeyValues files (libraryfolders.vdf,
// config.vdf, registry.vdf, appmanifest_*.acf, toolmanifest.vdf, compatibilitytool.vdf).
#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vdf {

struct Node {
    std::string value;                                    // set for "key" "value" leaves
    std::vector<std::pair<std::string, std::unique_ptr<Node>>> children;  // in file order

    bool IsLeaf() const { return children.empty(); }
    // Case-insensitive child lookup; nullptr if absent.
    const Node* Child(const std::string& key) const;
    // Walk a path of keys, e.g. {"Registry","HKCU","Software"}; nullptr if any step is missing.
    const Node* Find(std::initializer_list<const char*> path) const;
    // Leaf value at a path, or def.
    std::string Get(std::initializer_list<const char*> path, const std::string& def = "") const;
};

// Parse text; returns a root Node whose children are the file's top-level keys. Never throws:
// malformed input just yields whatever parsed before the error.
std::unique_ptr<Node> Parse(const std::string& text);
std::unique_ptr<Node> ParseFile(const std::string& path);

} // namespace vdf
