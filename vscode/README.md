# xlang VSCode Extension

Syntax highlighting, snippets, completions, diagnostics, formatting, and run/build/test commands for `.xlang` files.

Build toolchain uses **Bun** (no npm/node required for development).

## Features

- TextMate grammar: imports (`import http from http`), HTTP/JSON/runtime/test/compiler builtins, method calls
- Snippets: `main`, `httpserver`, `importfrom`, `testmain`, `describe`, `it`, `expect_*`, …
- Completions: keywords, types, modules (`http`, `http/router`, `net`, `json`), runtime & syscall APIs
- Diagnostics via `xlank parse` — auto-adds `runtime/` to `XLANG_PATH`
- Document formatter (brace-based indent)
- Commands:
  - **xlang: Run Current File** — `xlank run`
  - **xlang: Build Current File** — `xlank build`
  - **xlang: Test Current File** — `xlank run` for `*.test.xlang`, else `xlank test`
  - **xlang: Run Test Suite** — `xlank test` (default: `test/xlang/`)

## Requirements

- [Bun](https://bun.sh) ≥ 1.2
- VS Code / Cursor ≥ 1.85
- Built `xlank` compiler (`build/xlank`) for diagnostics and run/test commands

## Setup

```bash
cd vscode
bun install
bun run compile
bun run package
code --install-extension xlang-0.3.0.vsix
```

## Development

1. Open the `vscode/` folder (or repo root) in VS Code / Cursor
2. Press `F5` → Extension Development Host
3. Open any `.xlang` or `.test.xlang` file

```bash
bun run watch    # rebuild on save
bun run package  # produce .vsix
```

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `xlang.xlankPath` | `""` | Path to `xlank`. Empty = `build/xlank` in workspace (or parent), then PATH |
| `xlang.enableDiagnostics` | `true` | Show parse errors from `xlank parse` |
| `xlang.diagnosticsOnType` | `false` | Re-check while typing |
| `xlang.formatter.tabSize` | `4` | Indent size for formatter |
| `xlang.testRoot` | `test/xlang` | Directory passed to `xlank test` |

## Supported APIs (completions / highlighting)

| Area | Examples |
|------|----------|
| Runtime | `print`, `fetch`, `spawn`, `wait_all`, `cpu` |
| JSON | `json_get_int/string/bool`, `json_has_key`, `json_is_null` |
| HTTP | `NewRouter`, `Get`, `Group`, `Mount`, `URLParam`, `ListenAndServe`, `RespondText` |
| Compiler | `str_len`, `str_eq`, `str_find`, `array_push`, `invoke0` |
| Test | `describe`, `it`, `expect_eq`, `expect_str_eq`, `test_summary` |

## Project layout

```
vscode/
├── package.json
├── scripts/build.ts
├── syntaxes/xlang.tmLanguage.json
├── snippets/xlang.json
└── src/
    ├── extension.ts
    ├── completions.ts
    ├── diagnostics.ts
    └── formatter.ts
```
