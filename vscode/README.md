# xlang — VS Code Extension

Language support for **xlang** (`.xlang`, `.test.xlang`): syntax highlighting, IntelliSense, hover docs, go-to-definition, diagnostics, formatting, and run/build/test commands.

Built with **Bun** + TypeScript. Requires the [`xlank`](https://github.com/hasirciogluhq/xlang#readme) compiler for diagnostics and commands.

## Features

### IntelliSense & hover

| Feature | Description |
|---------|-------------|
| **Completion** | Keywords, types, runtime/builtins, local symbols, import aliases |
| **Import-aware** | `http.NewRouter()` completes from `libs/http/` exports after `import * as http from http` |
| **Member access** | `r.Get(`, `req.RespondText(`, `data.Int(` — methods on `Router`, `Request`, `Json`, … |
| **Module picker** | Typing `import` suggests workspace modules (`http`, `json`, `http/router`, …) with export preview |
| **Hover** | Markdown docs on builtins, exported functions, structs, and imported symbols |
| **Go to definition** | Jump to the `.xlang` file where an imported symbol is defined |

The extension parses `.xlang` sources locally (no LSP server). It scans `libs/` and `runtime/` in your workspace and resolves imports the same way as `xlank` (`XLANG_PATH`).

### Diagnostics

Runs `xlank parse` on open/save (optional debounce while typing). Shows parse errors and missing modules.

### Commands

| Command | Action |
|---------|--------|
| **xlang: Run Current File** | `xlank run <file>` |
| **xlang: Build Current File** | `xlank build <file>` |
| **xlang: Test Current File** | `xlank test <file>` or suite |
| **xlang: Run Test Suite** | `xlank test test/xlang` |
| **xlang: Format Document** | Built-in formatter |

Right-click an `.xlang` editor for Run / Test shortcuts.

### Snippets

`fn`, `struct`, `import`, `httpserver`, `expect`, `Test*`, and more — see `snippets/xlang.json`.

## Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `xlang.compilerPath` | *(auto)* | Path to `xlank` binary |
| `xlang.enableDiagnostics` | `true` | Parse diagnostics on open/save |
| `xlang.diagnosticsOnType` | `false` | Re-parse while typing (debounced) |
| `xlang.formatter.tabSize` | `4` | Indent size for formatter |
| `xlang.testRoot` | `test/xlang` | Default test directory |

Auto-detect looks for `build/xlank` in the workspace root (or parent).

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
│   ├── diagnostics.ts    # xlank parse integration
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

fn handle_ping(req: Request) {
    req.RespondText(200, "pong")
    return 0
}

fn on_listen(info: ServerInfo) {
    print("running at %s://%s:%d", info.Protocol(), info.Hostname(), info.Port())
    return 0
}

fn main() {
    local r = http.NewRouter()
    r.Get("/ping", handle_ping)
    r.ListenAndServe("127.0.0.1", 8080, on_listen)
    return 0
}
```

Hover `r.ListenAndServe` or `req.RespondText` for signatures. Completion after `req.` lists Request methods; after `r.` lists Router methods.

## License

MIT
