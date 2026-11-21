# Teal Support

Doriax now supports Teal as an alternative scripting language for project scripts. Teal code is written in `.tl` files and can be used alongside existing Lua workflows.

## What is new

- Create Teal scripts directly from an entity's Script component.
- Create standalone Teal modules from the Resources window.
- Edit `.tl` files in the built-in editor with Teal syntax highlighting and snippets.
- Use engine API completion, record field completion, and constructor parameter hints while writing Teal.
- Define script properties and register engine events as with Lua scripts.
- Play projects without a separate compile step; Teal scripts are prepared automatically.
- Type-check Teal scripts against the engine API.
- Import other Teal modules with `require`.
- Include the compiled Lua from Teal scripts in generated projects and exported builds. Export never ships `.tl` source files.
- Rename, move, and delete Teal scripts together with their generated files.
- Keep existing Lua projects working unchanged.

## Build option

Teal support is controlled by the CMake option `DORIAX_ENABLE_TEAL`, enabled by default. Configure with `-DDORIAX_ENABLE_TEAL=OFF` to build an editor without the Teal compiler, Teal editor mode, Teal completion, or Teal menu entries. Existing `.tl` files are not treated as scripts in that build.

## Recommended editor setup

The built-in editor is designed for quick edits, small scripts, and inspecting project scripts in place. It is not a full language server.

For the best day-to-day experience, use an external editor with a Teal language server, such as VS Code, Cursor, or Neovim with a Teal LSP client. Point the language server at the project's Lua root and the generated `doriax.d.tl` declaration. A language server adds capabilities the built-in editor does not provide:

- Type diagnostics and error lists
- Hover types and signatures
- Go to definition and find references
- Rename symbols
- More accurate completion across modules
- Source-level navigation while editing

Use the built-in editor when it is convenient; use an external editor with LSP for larger Teal codebases and for the most accurate type feedback.

## Built-in editor: current and planned

The built-in editor already provides Teal syntax highlighting, snippets, engine API completion, record field completion, constructor parameter hints, and type annotation completion.

Planned editor improvements:

- Refresh project symbols and parameter hints for unsaved edits, without requiring save or reopen.
- Support multi-line function signatures in parameter hints.
- Split parameter lists correctly when a parameter type contains nested commas.
- Show multiple overloads for project-defined functions and methods.
- Improve receiver type inference for module-qualified calls such as `scripts.ex.new(`.
- Parse inherited fields and methods from custom Teal records, not only `LuaScript`.
- Cover nested records, generic types, and function-typed record fields.
- Improve type annotation detection for `local value:Type` without a space.
- Add completion for Teal and Lua standard library members such as `string`, `table`, `math`, and `utf8`.
- Surface type errors as editor diagnostics instead of only reporting syntax errors.
- Add source mapping so Teal breakpoints can follow `.tl` lines.
- Provide a reusable class/inheritance helper in the standalone module template.

The built-in editor intentionally focuses on the engine API and project symbols. Full language-server behavior is best supplied by an external Teal LSP.

## Generated projects and export

Generated projects and exported builds contain the compiled Lua for Teal scripts. Export places those Lua files in the correct Lua directory and does not ship `.tl` source files.

## Teal declarations

Each engine class is declared twice in `doriax.d.tl`:

- `IName` is the interface used for inheritance and polymorphic parameters.
- `Name` is the concrete record used for values, static constants, and `__call` constructors.

Use `Vector3` for concrete values and `Vector3.ZERO`; use `IVector3` when a value may be any implementation of that interface. Constructors remain callable as `Vector3(...)`; no `Vector3.new` method is exposed because the engine binding does not export one. This keeps static constants and constructors visible to Teal language servers without changing the runtime API.

## Remaining work

Non-editor follow-up work is tracked in [TODO.md](TODO.md).
