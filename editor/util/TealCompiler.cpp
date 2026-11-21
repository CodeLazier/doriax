// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#include "TealCompiler.h"

#include "Project.h"
#include "Out.h"
#include "util/FileUtils.h"
#include "util/Util.h"

#include "lua.hpp"
#include "script/LuaBinding.h"

#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <unordered_map>

using namespace doriax;

namespace {

// Entry used as tl.gen, on its own Lua state. Type errors do not block codegen.
// declaration is the engine API .d.tl, loaded as a teal-types global environment.
// Target is Teal 0.24.8's default 5.3. The Lua it emits runs on the engine's Lua 5.5.
const char* compileChunk = R"lua(
local tl = ...
local function newEnv()
    return tl.new_env({
        defaults = {
            feat_lax = "off",
            gen_compat = "optional",
            gen_target = "5.3",
            run_internal_compiler_checks = false,
        }
    })
end
return function(source, filename, declaration, modulePath)
    local env, envErr = newEnv()
    if not env then
        return nil, envErr or "could not start the Teal compiler"
    end
    if type(declaration) == "string" and declaration ~= "" then
        local declared = tl.check_string(declaration, env, "doriax.d.tl", "tl")
        local syntax = type(declared) == "table" and declared.syntax_errors
        if type(syntax) == "table" and #syntax > 0 then
            env = newEnv()
            if not env then
                return nil, envErr or "could not start the Teal compiler"
            end
        end
    end
    -- require("bar") looks for bar.tl in the project Lua directory.
    -- TL_PATH overrides tl.path, so hide it for this check only.
    local function checkSource()
        if type(modulePath) ~= "string" or modulePath == "" then
            return tl.check_string(source, env, filename)
        end
        local oldPath = tl.path
        local getenv = os.getenv
        tl.path = modulePath
        os.getenv = function(key)
            if key == "TL_PATH" then return nil end
            return getenv(key)
        end
        local ok, checked = pcall(function()
            return tl.check_string(source, env, filename)
        end)
        tl.path = oldPath
        os.getenv = getenv
        if not ok then return nil, checked end
        return checked
    end
    local result, checkErr = checkSource()
    local function dump(list)
        if type(list) ~= "table" then return "" end
        local lines = {}
        for _, e in ipairs(list) do
            local name = filename or ""
            local y = 0
            local x = 0
            local msg = tostring(e)
            if type(e) == "table" then
                if type(e.filename) == "string" and e.filename ~= "" then name = e.filename end
                y = e.y or 0
                x = e.x or 0
                msg = e.msg or msg
            end
            lines[#lines + 1] = tostring(name) .. ":" .. tostring(y) .. ":" .. tostring(x) .. ": " .. tostring(msg)
        end
        return table.concat(lines, "\n")
    end
    if type(result) ~= "table" or not result.ast or (type(result.syntax_errors) == "table" and #result.syntax_errors > 0) then
        local syntax = type(result) == "table" and dump(result.syntax_errors) or ""
        if syntax == "" and type(checkErr) == "string" and checkErr ~= "" then syntax = checkErr end
        if syntax == "" then syntax = "Teal syntax error" end
        return nil, syntax
    end
    local code, genErr = tl.generate(result.ast, "5.3")
    if type(code) ~= "string" then
        return nil, genErr or "Teal compiler produced no Lua"
    end
    return code
end
)lua";

const char* registryKey = "doriax.teal.compile";

std::mutex compilerMutex;
lua_State* compilerState = nullptr;

// Cache entry for one Teal source, keyed by its normalized absolute path.
struct CachedBuild {
    std::filesystem::file_time_type sourceTime{};
    bool checked = false;
    std::string error;
};

// Generated Lua cache. It avoids recompiling a script whose source time and
// sibling Lua state have not changed.
std::unordered_map<std::string, CachedBuild> builds;

// Reads a file as binary; returns false when it cannot be opened.
bool readFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::ostringstream text;
    text << file.rdbuf();
    out = text.str();
    return file.good() || file.eof();
}

// Maps a Teal source to <project>/.doriax/generated/teal/<relative>.lua.
// Returns an empty path when the source is outside the project's Lua root.
std::filesystem::path outputPath(const doriax::editor::Project* project, const std::filesystem::path& source) {
    std::filesystem::path script = project->normalizeToLuaRelative(project->resolveLuaPath(project->normalizeToLuaRelative(source)));
    if (script.empty() || script.is_absolute() || *script.begin() == "..")
        return {};
    script.replace_extension(".lua");
    return project->getProjectInternalPath() / "generated" / "teal" / script;
}

// Creates the private Lua state and loads tl.lua plus the compile helper on
// first use. The compiler state is reused for every later build.
bool ensureCompiler(std::string& error) {
    if (compilerState) return true;

    const std::filesystem::path compilerPath = doriax::editor::FileUtils::getEngineDir() / "libs" / "teal" / "tl.lua";
    std::string compilerText;
    if (!readFile(compilerPath, compilerText)) {
        error = "Teal compiler not found at " + doriax::editor::FileUtils::pathToGenericUtf8(compilerPath);
        return false;
    }

    lua_State* L = luaL_newstate();
    if (!L) {
        error = "could not create a Lua state for the Teal compiler";
        return false;
    }
    luaL_openlibs(L);

    const std::string chunkName = "@" + doriax::editor::FileUtils::pathToGenericUtf8(compilerPath);
    if (luaL_loadbuffer(L, compilerText.data(), compilerText.size(), chunkName.c_str()) != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "failed to load the Teal compiler";
        lua_close(L);
        return false;
    }

    if (luaL_loadbuffer(L, compileChunk, std::strlen(compileChunk), "teal-compile") != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "failed to prepare the Teal compiler";
        lua_close(L);
        return false;
    }
    // loadbuffer leaves [module, chunk]. lua_pcall calls the value under the
    // arguments, so the chunk has to sit under the module. Otherwise the call
    // invokes the module table: "attempt to call a table value".
    lua_rotate(L, -2, 1);
    if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "failed to prepare the Teal compiler";
        lua_close(L);
        return false;
    }
    if (!lua_isfunction(L, -1)) {
        error = "Teal compiler setup did not return a function";
        lua_close(L);
        return false;
    }

    lua_setfield(L, LUA_REGISTRYINDEX, registryKey);
    compilerState = L;
    return true;
}

// On success, code is the generated Lua. A syntax error is written to error.
// Engine API declaration produced by the build. Empty until engine-sdk has been copied.
std::string engineDeclaration() {
    const std::filesystem::path path = doriax::editor::FileUtils::getEngineDir() / "lua-api" / "doriax.d.tl";
    std::string text;
    readFile(path, text);
    return text;
}

// Runs tl.check_string and tl.generate in the private compiler state. Type errors
// are ignored; syntax errors are returned in error. On success code is the Lua.
bool compileSource(const std::string& source, const std::string& filename, const std::string& modulePath, std::string& code, std::string& error) {
    if (!ensureCompiler(error)) return false;

    const std::string declaration = engineDeclaration();
    lua_State* L = compilerState;
    lua_getfield(L, LUA_REGISTRYINDEX, registryKey);
    lua_pushlstring(L, source.data(), source.size());
    lua_pushlstring(L, filename.data(), filename.size());
    if (declaration.empty())
        lua_pushnil(L);
    else
        lua_pushlstring(L, declaration.data(), declaration.size());
    if (modulePath.empty())
        lua_pushnil(L);
    else
        lua_pushlstring(L, modulePath.data(), modulePath.size());
    // The compiler function is under these four arguments. Do not rotate this stack.
    if (lua_pcall(L, 4, 2, 0) != LUA_OK) {
        error = lua_tostring(L, -1) ? lua_tostring(L, -1) : "Teal compiler failed";
        lua_settop(L, 0);
        return false;
    }
    if (lua_isnil(L, -2) || !lua_isstring(L, -2)) {
        error = lua_isstring(L, -1) ? lua_tostring(L, -1) : "Teal syntax error";
        lua_settop(L, 0);
        return false;
    }
    size_t length = 0;
    const char* text = lua_tolstring(L, -2, &length);
    code.assign(text, length);
    lua_settop(L, 0);
    return true;
}

// Project used by the Lua require searcher during play.
const doriax::editor::Project* playProject = nullptr;
// Registry key that marks the searcher as already installed on this Lua state.
const char* searcherKey = "doriax.teal.require";

// A module segment may not contain a path separator or a character that Windows
// rejects. '%' is rejected too because Lua's path replacement treats it as an
// escape when Teal searches for modules.
bool isModuleChar(unsigned char c) {
    if (c < 0x20 || c == 0x7f) return false;
    switch (c) {
        // Lua's package.path replacement treats a '%' as an escape; a module
        // name containing one would make the Teal search error out.
        case '%': case '<': case '>': case ':': case '"': case '|': case '?': case '*':
            return false;
        default:
            return true;
    }
}

// require("lib.bar") is lib/bar.tl under the Lua directory. Anything outside
// that directory, including traversal out of it, is treated as a miss.
std::filesystem::path tealModuleFile(const doriax::editor::Project* project, const std::string& moduleName) {
    if (!project || moduleName.empty()) return {};
    if (moduleName.front() == '.' || moduleName.front() == '/' || moduleName.front() == '\\') return {};

    std::string relative;
    for (unsigned char c : moduleName) {
        if (c == '.' || c == '/' || c == '\\') {
            if (relative.empty() || relative.back() == '/') return {};
            relative.push_back('/');
        } else if (isModuleChar(c)) {
            relative.push_back(static_cast<char>(c));
        } else {
            return {};
        }
    }
    if (relative.empty() || relative.back() == '/') return {};

    const std::filesystem::path luaRoot = project->getLuaPath();
    if (luaRoot.empty()) return {};
    const std::filesystem::path full = (luaRoot / doriax::editor::FileUtils::pathFromUtf8(relative + ".tl")).lexically_normal();
    std::error_code ec;
    if (!std::filesystem::is_regular_file(full, ec)) return {};
    const std::filesystem::path within = project->normalizeToLuaRelative(full);
    if (within.empty() || within.is_absolute() || *within.begin() == "..") return {};
    return full;
}

// Lua package searcher for Teal modules used by the editor's play mode.
// It compiles the .tl on demand and returns the generated Lua loader.
int tealRequireSearcher(lua_State* L) {
    const char* name = lua_tostring(L, 1);
    // 0: not found, 1: compile failed, 2: the loader is already on the stack.
    // lua_error longjmps, so C++ objects must be destroyed first.
    int outcome = 0;
    {
        try {
            const std::filesystem::path source = name ? tealModuleFile(playProject, name) : std::filesystem::path{};
            if (!source.empty()) {
                std::string problem;
                const std::filesystem::path generated = doriax::editor::TealCompiler::build(playProject, source, &problem);
                std::string lua;
                if (generated.empty()) {
                    if (problem.empty()) problem = "Teal compiler failed";
                    lua_pushlstring(L, problem.data(), problem.size());
                    outcome = 1;
                } else if (!readFile(generated, lua)) {
                    lua_pushstring(L, "could not read the compiled Teal module");
                    outcome = 1;
                } else {
                    const std::string chunk = "@" + doriax::editor::FileUtils::pathToGenericUtf8(generated);
                    outcome = luaL_loadbuffer(L, lua.data(), lua.size(), chunk.c_str()) == LUA_OK ? 2 : 1;
                }
            }
        } catch (const std::exception& e) {
            lua_pushstring(L, e.what());
            outcome = 1;
        } catch (...) {
            lua_pushstring(L, "Teal module loader failed");
            outcome = 1;
        }
    }
    if (outcome == 0) {
        lua_pushstring(L, "\n\tno Teal file for this module");
        return 1;
    }
    if (outcome == 1)
        return lua_error(L);
    return 1;
}

}

// Compiles a .tl to generated Lua and returns the generated path. On failure
// returns an empty path and writes the reason to error when it is provided.
std::filesystem::path editor::TealCompiler::build(const Project* project, const std::filesystem::path& source, std::string* error) {
    if (!project) {
        if (error) *error = "no project";
        return {};
    }
    try {
    const std::filesystem::path fullPath = project->resolveLuaPath(project->normalizeToLuaRelative(source));
    auto fail = [&](const std::string& problem, bool log) {
        if (error)
            *error = problem;
        else if (log)
            Out::error("%s: %s", doriax::editor::FileUtils::pathToUtf8(fullPath.filename()).c_str(), problem.c_str());
    };
    if (error) error->clear();

    if (source.empty() || !editor::Util::isTealFile(doriax::editor::FileUtils::pathToGenericUtf8(fullPath))) {
        fail("not a Teal script", false);
        return {};
    }

    const std::filesystem::path output = outputPath(project, source);
    if (output.empty()) {
        fail(doriax::editor::FileUtils::pathToUtf8(fullPath.filename()) + " is outside the project's Lua directory", true);
        return {};
    }

    std::lock_guard<std::mutex> lock(compilerMutex);

    std::error_code ec;
    const auto sourceTime = std::filesystem::last_write_time(fullPath, ec);
    const std::string key = doriax::editor::FileUtils::pathToGenericUtf8(fullPath);
    if (ec) {
        builds.erase(key);
        fail("the script is missing", false);
        return {};
    }

    const std::string conflict = "a Lua script beside it has the same name";
    const std::filesystem::path sibling = std::filesystem::path(fullPath).replace_extension(".lua");
    const bool siblingLua = std::filesystem::exists(sibling, ec);
    const auto known = builds.find(key);
    // Deleting the sibling .lua does not touch the .tl mtime, so a cached conflict must not stick.
    const bool cachedConflict = known != builds.end() && known->second.error == conflict;
    const bool cacheMatchesSibling = siblingLua ? cachedConflict : !cachedConflict;
    if (known != builds.end() && known->second.checked && known->second.sourceTime == sourceTime && cacheMatchesSibling) {
        if (!known->second.error.empty()) {
            fail(known->second.error, false);
            return {};
        }
        // A deleted generated file must not stay a cache hit, or require never compiles it again.
        if (std::filesystem::exists(output, ec))
            return output;
    }

    if (siblingLua) {
        const bool alreadyReported = known != builds.end() && known->second.checked && known->second.error == conflict;
        CachedBuild& cached = builds[key];
        cached.sourceTime = sourceTime;
        cached.checked = true;
        cached.error = conflict;
        fail(conflict, !alreadyReported);
        return {};
    }

    std::string sourceText;
    std::string problem;
    if (!readFile(fullPath, sourceText))
        problem = "could not read " + doriax::editor::FileUtils::pathToUtf8(fullPath.filename());

    std::string lua;
    // Teal substitutes ? and then replaces the .lua suffix, so this finds .d.tl, .tl and .lua.
    std::string modulePath;
    const std::filesystem::path luaRoot = project->getLuaPath();
    if (!luaRoot.empty())
        modulePath = doriax::editor::FileUtils::pathToGenericUtf8(luaRoot) + "/?.lua";
    if (problem.empty() && !compileSource(sourceText, doriax::editor::FileUtils::pathToGenericUtf8(fullPath), modulePath, lua, problem)) {
        if (problem.empty()) problem = "Teal compiler failed";
    }

    if (problem.empty()) {
        const std::string stamped = "-- Written by the Doriax editor from " + doriax::editor::FileUtils::pathToUtf8(fullPath.filename()) + ". Edit the Teal file.\n" + lua;
        if (!doriax::editor::FileUtils::writeIfChanged(output, stamped))
            problem = "could not write the Lua for " + doriax::editor::FileUtils::pathToUtf8(fullPath.filename());
    }

    CachedBuild& cached = builds[key];
    cached.sourceTime = sourceTime;
    cached.checked = true;
    cached.error = problem;
    if (!problem.empty()) {
        fail(problem, true);
        return {};
    }
    return output;
    } catch (const std::exception& e) {
        const std::string problem = e.what();
        if (error) *error = problem;
        else Out::error("Teal compile failed: %s", problem.c_str());
        return {};
    } catch (...) {
        if (error) *error = "Teal compile failed";
        else Out::error("Teal compile failed");
        return {};
    }
}

// Drops generated Lua after a rename or delete. directory removes the whole
// generated subtree; otherwise only the matching .lua file is removed.
void editor::TealCompiler::remove(const Project* project, const std::filesystem::path& source, bool directory) {
    if (!project || source.empty()) return;

    std::error_code ec;
    try {
        if (directory) {
            // A directory's generated tree is <internal>/generated/teal/<relative>,
            // with no extension substitution: directories may contain dots too.
            const std::filesystem::path relative = project->normalizeToLuaRelative(source);
            if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..") {
                const std::filesystem::path root = project->getProjectInternalPath() / "generated" / "teal" / relative;
                std::filesystem::remove_all(root, ec);
            }
        } else {
            const std::filesystem::path script = outputPath(project, source);
            if (!script.empty())
                std::filesystem::remove(script, ec);
        }
    } catch (const std::exception&) {
        // A removed project path can no longer be normalized; there is nothing to drop.
    }

    std::lock_guard<std::mutex> lock(compilerMutex);
    builds.clear();
}

// Installs the Teal searcher once on the play Lua state, ahead of the normal
// module loader. Must run after Engine::systemInit replaces package.searchers.
void editor::TealCompiler::installRequireSearcher(const Project* project) {
    // Inserted at searchers[1], ahead of moduleLoader. The registry flag avoids a second insert on the same state.
    playProject = project;
    lua_State* L = doriax::LuaBinding::getLuaState();
    if (!L) return;

    lua_getfield(L, LUA_REGISTRYINDEX, searcherKey);
    const bool installed = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (installed) return;

    lua_getglobal(L, "package");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return;
    }
    lua_getfield(L, -1, "searchers");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 2);
        return;
    }

    const int searchers = lua_gettop(L);
    const int count = static_cast<int>(lua_rawlen(L, searchers));
    for (int i = count; i >= 1; --i) {
        lua_rawgeti(L, searchers, i);
        lua_rawseti(L, searchers, i + 1);
    }
    lua_pushcfunction(L, tealRequireSearcher);
    lua_rawseti(L, searchers, 1);
    lua_pop(L, 2);

    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, searcherKey);
}
