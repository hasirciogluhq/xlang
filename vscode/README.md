# xlang VSCode Extension

Syntax highlighting, snippets, completions, diagnostics, formatting, and run/build/test for `.xlang`.

Build with **Bun**: `bun install && bun run compile && bun run package`

## v0.5.0 fixes

- Diagnostic source label is **xlang** (not xlank — fixes confusing `json` + `xlank` display)
- Module resolution: walks up to `runtime/` for `import json from json` etc.
- Compiler `parse` subcommand now loads embedded runtime (same as test/build)

## Commands

| Command | Action |
|---------|--------|
| xlang: Run Current File | `xlank run` |
| xlang: Test Current File | `xlank test <file>` |
| xlang: Run Test Suite | `xlank test test/xlang` |

> Binary name is still `build/xlank`; configure via `xlang.compilerPath`.

## Test API (current)

```xlang
fn TestExample() {
    expect(1 + 1).toEqual(2)
    expect("ok").toEqual("ok")
    expect(flag).toBeTrue()
    return 0
}
```

## Install

```bash
code --install-extension xlang-0.5.0.vsix
```
