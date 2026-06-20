import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";
import { ATOMIC_BOOL_METHODS, ATOMIC_INT_METHODS, CONN_METHODS, CONTEXT_METHODS, ERROR_METHODS, findGlobalRuntimeEntry, JSON_METHODS, LOCK_METHODS, ROUTER_METHODS, RWLOCK_METHODS, SERVER_METHODS, type CatalogEntry } from "./builtins";
import {
  type ImportBinding,
  type ParsedModule,
  type SymbolDoc,
  loadModuleFromDisk,
  parseXlangSource,
  moduleIdFromPath,
} from "./symbols";

export class LanguageIndex {
  private readonly moduleCache = new Map<string, ParsedModule>();
  private readonly fileCache = new Map<string, ParsedModule>();

  invalidate(uri?: vscode.Uri): void {
    if (uri) {
      this.fileCache.delete(uri.fsPath);
    }
    this.moduleCache.clear();
  }

  getDocumentModule(document: vscode.TextDocument): ParsedModule {
    const key = document.uri.fsPath;
    const cached = this.fileCache.get(key);
    if (cached) {
      return cached;
    }
    const moduleId = moduleIdFromPath(
      key,
      vscode.workspace.getWorkspaceFolder(document.uri)?.uri.fsPath
    );
    const parsed = parseXlangSource(document.getText(), key, moduleId);
    this.fileCache.set(key, parsed);
    return parsed;
  }

  getImportedModule(modulePath: string, contextFile: string): ParsedModule | undefined {
    const cacheKey = `${modulePath}::${contextFile}`;
    const hit = this.moduleCache.get(cacheKey);
    if (hit) {
      return hit;
    }
    const loaded = loadModuleFromDisk(modulePath, contextFile);
    if (loaded) {
      this.moduleCache.set(cacheKey, loaded);
    }
    return loaded;
  }

  resolveImportBindings(document: vscode.TextDocument): ImportBinding[] {
    return this.getDocumentModule(document).imports;
  }

  symbolsForImportAlias(document: vscode.TextDocument, alias: string): SymbolDoc[] {
    const binding = this.resolveImportBindings(document).find((b) => b.alias === alias);
    if (!binding) {
      return [];
    }

    const sub = binding.symbolName
      ? this.findSubmodule(binding.modulePath, binding.symbolName, document.uri.fsPath)
      : undefined;
    if (sub) {
      const mod = this.getImportedModule(sub, document.uri.fsPath);
      return mod?.symbols.filter((s) => s.exported) ?? [];
    }

    if (binding.kind === "symbol" && binding.symbolName) {
      const mod = this.getImportedModule(binding.modulePath, document.uri.fsPath);
      const sym = mod?.symbols.find((s) => s.name === binding.symbolName && s.exported);
      return sym ? [sym] : [];
    }

    const mod = this.getImportedModule(binding.modulePath, document.uri.fsPath);
    return mod?.symbols.filter((s) => s.exported) ?? [];
  }

  methodsForType(typeName: string): SymbolDoc[] {
    return [...ROUTER_METHODS, ...JSON_METHODS, ...LOCK_METHODS, ...RWLOCK_METHODS, ...ATOMIC_INT_METHODS, ...ATOMIC_BOOL_METHODS, ...CONTEXT_METHODS, ...SERVER_METHODS, ...CONN_METHODS, ...ERROR_METHODS]
      .filter((m) => m.receiver === typeName)
      .map(catalogToSymbolDoc);
  }

  localSymbols(document: vscode.TextDocument): SymbolDoc[] {
    const mod = this.getDocumentModule(document);
    const out = [...mod.symbols];
    for (const [name, typeName] of mod.locals) {
      out.push({
        name,
        kind: "variable",
        signature: `${name}: ${typeName}`,
        detail: typeName,
        documentation: `Local variable (\`${typeName}\`).`,
        exported: false,
        moduleId: mod.moduleId,
        filePath: mod.filePath,
        line: 0,
      });
    }
    return out;
  }

  resolveMemberAccess(document: vscode.TextDocument, objectExpr: string): SymbolDoc[] {
    const trimmed = objectExpr.trim();
    if (this.resolveImportBindings(document).some((b) => b.alias === trimmed)) {
      return this.symbolsForImportAlias(document, trimmed);
    }

    const localType = this.getDocumentModule(document).locals.get(trimmed);
    const paramType = localType ?? this.paramTypeForName(document, trimmed);
    if (paramType) {
      return mergeSymbols(this.methodsForType(paramType), this.findMethodsOnType(paramType, document));
    }

    return [];
  }

  private paramTypeForName(document: vscode.TextDocument, name: string): string | undefined {
    for (const sym of this.getDocumentModule(document).symbols) {
      if (sym.kind !== "function") {
        continue;
      }
      const paramsMatch = /\(([^)]*)\)/.exec(sym.signature);
      if (!paramsMatch) {
        continue;
      }
      for (const part of paramsMatch[1].split(",")) {
        const m = /^\s*(\w+)\s*:\s*(\w+)/.exec(part.trim());
        if (m && m[1] === name) {
          return m[2];
        }
      }
    }
    return undefined;
  }

  findSymbol(document: vscode.TextDocument, name: string, namespace?: string): SymbolDoc | undefined {
    if (namespace) {
      return this.symbolsForImportAlias(document, namespace).find((s) => s.name === name);
    }
    const local = this.localSymbols(document).find((s) => s.name === name);
    if (local) {
      return local;
    }
    for (const binding of this.resolveImportBindings(document)) {
      if (binding.alias === name) {
        return {
          name: binding.alias,
          kind: "module",
          signature: binding.modulePath,
          detail: "import",
          documentation: `Import alias for module \`${binding.modulePath}\`.`,
          exported: true,
          moduleId: binding.modulePath,
          filePath: document.uri.fsPath,
          line: 0,
        };
      }
    }
    const global = findGlobalRuntimeEntry(name);
    if (global) {
      return catalogToSymbolDoc(global);
    }
    return undefined;
  }

  listWorkspaceModules(contextFile: string): { path: string; documentation: string }[] {
    const results: { path: string; documentation: string }[] = [];
    const seen = new Set<string>();

    const addModule = (modulePath: string) => {
      if (seen.has(modulePath)) {
        return;
      }
      seen.add(modulePath);
      const mod = loadModuleFromDisk(modulePath, contextFile);
      const exportNames = mod?.symbols
        .filter((s) => s.exported)
        .map((s) => s.name)
        .slice(0, 10)
        .join(", ");
      results.push({
        path: modulePath,
        documentation: exportNames
          ? `Exports: \`${exportNames}\`${exportNames.length >= 10 ? "…" : ""}`
          : `Module \`${modulePath}\``,
      });
    };

    const walk = (absDir: string, rel: string) => {
      if (!fs.existsSync(absDir)) {
        return;
      }
      for (const entry of fs.readdirSync(absDir, { withFileTypes: true })) {
        if (entry.name.startsWith(".")) {
          continue;
        }
        const relPath = rel ? `${rel}/${entry.name.replace(/\.xlang$/, "")}` : entry.name.replace(/\.xlang$/, "");
        if (entry.isDirectory()) {
          addModule(relPath);
          walk(path.join(absDir, entry.name), relPath);
        } else if (entry.name.endsWith(".xlang")) {
          addModule(relPath);
        }
      }
    };

    for (const folder of vscode.workspace.workspaceFolders ?? []) {
      for (const sub of ["libs", "runtime"] as const) {
        walk(path.join(folder.uri.fsPath, sub), "");
        walk(path.join(folder.uri.fsPath, "..", sub), "");
      }
    }

    return results.sort((a, b) => a.path.localeCompare(b.path));
  }

  private findSubmodule(packagePath: string, name: string, contextFile: string): string | undefined {
    const resolved = loadModuleFromDisk(packagePath, contextFile);
    if (!resolved || !fs.existsSync(resolved.filePath) || !fs.statSync(resolved.filePath).isDirectory()) {
      return undefined;
    }
    for (const entry of fs.readdirSync(resolved.filePath)) {
      if (!entry.endsWith(".xlang")) {
        continue;
      }
      const stem = entry.replace(/\.xlang$/, "");
      if (stem.toLowerCase() === name.toLowerCase()) {
        return `${packagePath}/${stem}`;
      }
    }
    return undefined;
  }

  private findMethodsOnType(typeName: string, document: vscode.TextDocument): SymbolDoc[] {
    const out: SymbolDoc[] = [];
    for (const binding of this.resolveImportBindings(document)) {
      const mod = this.getImportedModule(binding.modulePath, document.uri.fsPath);
      if (!mod) {
        continue;
      }
      for (const sym of mod.symbols) {
        if (sym.receiver === typeName) {
          out.push(sym);
        }
      }
    }
    for (const sym of this.getDocumentModule(document).symbols) {
      if (sym.receiver === typeName) {
        out.push(sym);
      }
    }
    return out;
  }
}

function catalogToSymbolDoc(entry: CatalogEntry): SymbolDoc {
  return {
    name: entry.label,
    kind: entry.kind === vscode.CompletionItemKind.Struct ? "struct" : "function",
    signature: entry.detail,
    detail: entry.detail,
    documentation: entry.documentation,
    exported: true,
    receiver: entry.receiver,
    moduleId: "runtime",
    filePath: "",
    line: 0,
  };
}

function mergeSymbols(a: SymbolDoc[], b: SymbolDoc[]): SymbolDoc[] {
  const map = new Map<string, SymbolDoc>();
  for (const sym of [...a, ...b]) {
    map.set(sym.name, sym);
  }
  return [...map.values()];
}

let sharedIndex: LanguageIndex | undefined;

export function getLanguageIndex(): LanguageIndex {
  sharedIndex ??= new LanguageIndex();
  return sharedIndex;
}

export function resetLanguageIndex(): void {
  sharedIndex?.invalidate();
  sharedIndex = undefined;
}
