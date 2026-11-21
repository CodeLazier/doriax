# Teal TODO

Remaining follow-up work that is not specific to the built-in editor. Editor-related improvements are listed in [TEAL.md](TEAL.md).

## Export

- [ ] Review whether the extra `.tl` skips in `copyAssets()` and `copyLua()` are still needed. Standard export already compiles Teal files and ships only their `.lua`, so these filters may be redundant outside assets/Lua root overlap.

## Generated projects and runtime

- [ ] Add an end-to-end test for `require("scripts.name")` in a generated project, including the `LuaBinding::setTealModulePath()` fallback.
- [ ] Define and test module name handling for directories whose names contain dots.
- [ ] Check the generated-project fallback against non-ASCII project paths and absolute paths.

## Cross-platform

- [ ] Add Linux and macOS build and runtime coverage for Teal scripts, generated projects, and exports.
- [ ] Audit remaining non-Teal uses of narrow `.string()` paths for non-ASCII Windows paths.
- [ ] Handle case-only name collisions consistently on case-insensitive file systems.
- [ ] Consider canonical-path checks for Teal modules so symbolic links cannot escape the Lua root.

## Performance

- [ ] Avoid recompiling every `.tl` file on each generated-project configure when nothing changed.
- [ ] Invalidate the Teal build cache when `tl.lua`, `doriax.d.tl`, or module search settings change.

## Engineering

- [ ] Encapsulate the global Teal compiler state so it is testable and safe for parallel use.
- [ ] Add unit tests for Teal symbol parsing, parameter hints, record inheritance, export filtering, and module path resolution.
