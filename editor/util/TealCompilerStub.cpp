// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TealCompiler.h"

namespace doriax::editor {

// Teal support is compiled out. These stubs keep existing callers linkable
// without bringing in the Teal compiler or its Lua state.
std::filesystem::path TealCompiler::build(const Project* project, const std::filesystem::path& source, std::string* error) {
    (void)project;
    (void)source;
    if (error) *error = "Teal support is disabled in this build";
    return {};
}

void TealCompiler::remove(const Project* project, const std::filesystem::path& source, bool directory) {
    (void)project;
    (void)source;
    (void)directory;
}

void TealCompiler::installRequireSearcher(const Project* project) {
    (void)project;
}

}
