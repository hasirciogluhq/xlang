# xlang VSCode Extension

Syntax highlighting, snippets, completions, diagnostics, formatting, and run/build/test commands for `.xlang` files.

Build toolchain uses **Bun** (no npm/node required for development).

## Features

- TextMate grammar for keywords, types, strings, structs, runtime & test builtins
- Snippets (`main`, `fn`, `struct`, `testmain`, `describe`, `it`, `expect_*`, …)
- Completions for keywords, types, runtime (`fetch`, `json_get_*`, `spawn`) and test API
- Diagnostics via `xlank parse` (requires built compiler)
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
```

If you previously ran `npm install`, remove old artifacts first:

```bash
rm -rf node_modules package-lock.json
bun install
```

## Development

1. Open the `vscode/` folder in VS Code / Cursor
2. Press `F5` → Extension Development Host opens
3. Open any `.xlang` or `.test.xlang` file in the new window

Watch mode (rebuild on save):

```bash
bun run watch
```

## Package (.vsix)

```bash
bun run package
code --install-extension xlang-0.2.0.vsix
```

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `xlang.xlankPath` | `""` | Path to `xlank`. Empty = `build/xlank` in workspace, then PATH |
| `xlang.enableDiagnostics` | `true` | Show parse errors from `xlank parse` |
| `xlang.diagnosticsOnType` | `false` | Re-check while typing |
| `xlang.formatter.tabSize` | `4` | Indent size for formatter |
| `xlang.testRoot` | `test/xlang` | Directory passed to `xlank test` |

## Project layout

```
vscode/
├── package.json
├── bunfig.toml
├── scripts/
│   ├── build.ts
│   └── package.ts
├── language-configuration.json
├── syntaxes/xlang.tmLanguage.json
├── snippets/xlang.json
├── src/
│   ├── extension.ts
│   ├── completions.ts
│   ├── diagnostics.ts
│   └── formatter.ts
└── icons/
```

> **Note:** The extension runs inside VS Code's extension host (Node-compatible). Bun is only used to build and package the extension.
