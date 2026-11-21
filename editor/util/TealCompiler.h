// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#pragma once

#include <filesystem>
#include <string>

namespace doriax::editor {

class Project;

// Compiles a .tl file to Lua. The compiler is Teal 0.24.8 in engine/libs/teal
// (tl.lua; tl.tl is its source). Play mode and the properties panel read only the result.
class TealCompiler {
public:
    // Empty path on failure. A non-null error receives the reason and nothing is logged.
    static std::filesystem::path build(const Project* project, const std::filesystem::path& source, std::string* error = nullptr);
    // Drops the generated Lua. A directory source drops that whole generated tree.
    static void remove(const Project* project, const std::filesystem::path& source, bool directory = false);
    // During play, require looks for a .tl in the project Lua directory before the .lua loader.
    static void installRequireSearcher(const Project* project);
};

}
