# xlang — VS Code Extension

Language support for **xlang** (`.xlang`, `.test.xlang`): syntax highlighting, IntelliSense, hover docs, go-to-definition, diagnostics, formatting, and run/build/test commands.

Built with **Bun** + TypeScript. Requires the [`xlang`](https://github.com/hasirciogluhq/xlang#readme) compiler for diagnostics and commands.

## Features

### IntelliSense & hover

| Feature | Description |
|---------|-------------|
| **Completion** | Keywords, types, global runtime (`print`, `fetch`, `ReadAll`, `NewLock`, …), compiler builtins, local symbols |
| **Import-aware** | `http.NewRouter()`, `json.parse()` — completes from `libs/` after import |
| **Global runtime** | `print`, `spawn`, `fetch`, `Now`, `Format`, `ReadAll`, `NewLock`, … — no import required |
| **Member access** | `r.Get(`, `ctx.JSON(`, `lock.Lock(`, `a.FetchAdd(` — methods on known receiver types |
| **Module picker** | Typing `import` suggests workspace modules (`http`, `json`, `http/router`, …) with export preview |
| **Selective import** | Snippets for `import expect from test`, `import router from http/router` |
| **Hover** | Markdown docs on builtins, exported functions, structs, and imported symbols |
| **Go to definition** | Jump to the `.xlang` file where an imported symbol is defined |

The extension parses `.xlang` sources locally (no LSP server). It scans `libs/` and `runtime/` in your workspace and resolves imports the same way as `xlang` (`XLANG_PATH`).

### Diagnostics

Runs `xlang parse` on open/save (optional debounce while typing). Shows parse errors and missing modules.

### Commands

| Command | Action |
|---------|--------|
| **xlang: Run Current File** | `xlang run <file>` |
| **xlang: Build Current File** | `xlang build <file>` |
| **xlang: Test Current File** | `xlang test <file>` or suite |
| **xlang: Run Test Suite** | `xlang test test/xlang` |
| **xlang: Format Document** | Built-in formatter |

Right-click an `.xlang` editor for Run / Test shortcuts.

### Snippets

`fn`, `struct`, `import`, `httpserver`, `expect`, `Test*`, `filio`, `jsonparse`, and more — see `snippets/xlang.json`.

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `xlang.compilerPath` | *(auto)* | Path to `xlang` binary |
| `xlang.enableDiagnostics` | `true` | Parse diagnostics on open/save |
| `xlang.diagnosticsOnType` | `false` | Re-parse while typing (debounced) |
| `xlang.formatter.tabSize` | `4` | Indent size for formatter |
| `xlang.testRoot` | `test/xlang` | Default test directory |

Auto-detect looks for `build/xlang` in the workspace root (or parent).

## Development

```bash
cd vscode
bun install
bun run compile      # → out/extension.js
bun run watch        # rebuild on change
bun run package      # → xlang-1.0.0.vsix
```

Press **F5** in VS Code to launch an Extension Development Host.

### Project layout

```
vscode/
├── src/
│   ├── extension.ts      # activation, commands
│   ├── completions.ts    # CompletionItemProvider
│   ├── hover.ts          # Hover + Definition providers
│   ├── symbols.ts        # .xlang symbol parser
│   ├── languageIndex.ts  # import resolution + module cache
│   ├── builtins.ts       # runtime/stdlib catalog + docs
│   ├── paths.ts          # XLANG_PATH / libs resolution
│   ├── diagnostics.ts    # xlang parse integration
│   └── formatter.ts
├── syntaxes/             # TextMate grammar
├── snippets/
└── language-configuration.json
```

## Install

```bash
cd vscode && bun run package
code --install-extension xlang-1.0.0.vsix
```

Or install from the marketplace when published.

## Example

```xlang
import * as http from http

fn handle_index(ctx: Context) {
    local html = ReadAll("examples/index.html")
    ctx.HTML(200, html)
    return 0
}

fn handle_ping(ctx: Context) {
    ctx.String(200, "pong")
    return 0
}

fn on_listen(info: ServerInfo) {
    return 0
}

fn main() {
    local r = http.NewRouter()
    r.Get("/", handle_index)
    r.Get("/ping", handle_ping)
    r.ListenAndServe("127.0.0.1", 8080, on_listen)
    return 0
}
```

Hover `ReadAll`, `Now`, or `ctx.JSON` for signatures. Runtime symbols (`print`, `fetch`, `Now`, `Format`, file I/O, sync) need no import.

## License

MIT
