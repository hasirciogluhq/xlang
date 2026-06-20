import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";
import { collectModuleSearchPaths } from "./paths";

export type SymbolKind = "function" | "struct" | "global" | "variable" | "module" | "field";

export interface SymbolDoc {
  name: string;
  kind: SymbolKind;
  signature: string;
  detail: string;
  documentation: string;
  exported: boolean;
  receiver?: string;
  returnType?: string;
  moduleId: string;
  filePath: string;
  line: number;
}

export interface StructDoc {
  name: string;
  fields: { name: string; type: string }[];
  exported: boolean;
  filePath: string;
  line: number;
}

export interface ImportBinding {
  alias: string;
  modulePath: string;
  kind: "namespace" | "symbol" | "submodule";
  symbolName?: string;
}

export interface ParsedModule {
  moduleId: string;
  filePath: string;
  symbols: SymbolDoc[];
  structs: Map<string, StructDoc>;
  imports: ImportBinding[];
  locals: Map<string, string>;
  functions: string[];
}

const EXPORT_FN = /^\s*export\s+fn\s+(\w+)\s*\(([^)]*)\)(?:\s*:\s*([\w*]+))?/;
const FN = /^\s*fn\s+(\w+)\s*\(([^)]*)\)(?:\s*:\s*([\w*]+))?/;
const EXPORT_STRUCT = /^\s*export\s+struct\s+(\w+)/;
const STRUCT = /^\s*struct\s+(\w+)/;
const EXPORT_GLOBAL = /^\s*export\s+(\w+)\s*:/;
const LOCAL = /^\s*local\s+(\w+)(?:\s*:\s*([\w]+))?\s*=\s*(.+)$/;
const IMPORT_FROM = /^\s*import\s+(.+?)\s+from\s+([\w./]+)\s*$/;
const IMPORT_ALIAS = /^\s*import\s+(\w+)\s+from\s+([\w./]+)\s*$/;
const IMPORT_BARE = /^\s*import\s+([\w./]+)(?:\s+as\s+(\w+))?\s*$/;
const FROM_IMPORT = /^\s*from\s+([\w./]+)\s+import\s+(.+?)\s*$/;

function parseFirstParamType(params: string): string | undefined {
  const first = params.split(",")[0]?.trim();
  if (!first) {
    return undefined;
  }
  const colon = first.indexOf(":");
  if (colon === -1) {
    return undefined;
  }
  return first.slice(colon + 1).trim().replace(/\*$/, "");
}

function formatSignature(name: string, params: string, ret?: string): string {
  const inner = params.trim();
  return ret ? `${name}(${inner}) → ${ret}` : `${name}(${inner})`;
}

export function parseXlangSource(source: string, filePath: string, moduleId: string): ParsedModule {
  const symbols: SymbolDoc[] = [];
  const structs = new Map<string, StructDoc>();
  const imports: ImportBinding[] = [];
  const locals = new Map<string, string>();
  const functions: string[] = [];
  const lines = source.split("\n");

  let currentStruct: StructDoc | undefined;
  let braceDepth = 0;

  for (let i = 0; i < lines.length; i += 1) {
    const line = lines[i];
    const trimmed = line.trim();

    if (trimmed.startsWith("//")) {
      continue;
    }

    const importFrom = IMPORT_FROM.exec(line);
    if (importFrom) {
      parseImportClauseList(importFrom[1], importFrom[2], imports);
      continue;
    }

    const importAlias = IMPORT_ALIAS.exec(line);
    if (importAlias && importAlias[2].includes("/")) {
      imports.push({ alias: importAlias[1], modulePath: importAlias[2], kind: "namespace" });
      continue;
    }

    const fromImport = FROM_IMPORT.exec(line);
    if (fromImport) {
      for (const part of fromImport[2].split(",")) {
        const spec = part.trim();
        const asSplit = spec.split(/\s+as\s+/);
        const name = asSplit[0]?.trim();
        const alias = asSplit[1]?.trim() ?? name;
        if (name) {
          imports.push({ alias: alias!, modulePath: fromImport[1], kind: "symbol", symbolName: name });
        }
      }
      continue;
    }

    const bareImport = IMPORT_BARE.exec(line);
    if (bareImport && !line.includes(" from ")) {
      imports.push({
        alias: bareImport[2] ?? bareImport[1].split("/").pop() ?? bareImport[1],
        modulePath: bareImport[1],
        kind: "namespace",
      });
      continue;
    }

    if (IMPORT_ALIAS.exec(line) && !line.includes("/")) {
      const m = IMPORT_ALIAS.exec(line)!;
      if (m[1] === m[2]) {
        imports.push({ alias: m[1], modulePath: m[2], kind: "namespace" });
      } else {
        imports.push({ alias: m[1], modulePath: m[2], kind: "symbol", symbolName: m[1] });
      }
      continue;
    }

    const exportStruct = EXPORT_STRUCT.exec(line);
    if (exportStruct) {
      currentStruct = {
        name: exportStruct[1],
        fields: [],
        exported: true,
        filePath,
        line: i,
      };
      structs.set(exportStruct[1], currentStruct);
      symbols.push(makeStructSymbol(currentStruct, moduleId));
      braceDepth = (line.match(/\{/g) ?? []).length - (line.match(/\}/g) ?? []).length;
      continue;
    }

    const struct = STRUCT.exec(line);
    if (struct && !line.includes("export")) {
      currentStruct = {
        name: struct[1],
        fields: [],
        exported: false,
        filePath,
        line: i,
      };
      structs.set(struct[1], currentStruct);
      braceDepth = (line.match(/\{/g) ?? []).length - (line.match(/\}/g) ?? []).length;
      continue;
    }

    if (currentStruct && braceDepth > 0) {
      braceDepth += (line.match(/\{/g) ?? []).length - (line.match(/\}/g) ?? []).length;
      const field = /^\s*(\w+)\s*:\s*([\w*]+(?:\s+array\s+\w+)?)/.exec(line);
      if (field) {
        currentStruct.fields.push({ name: field[1], type: field[2].trim() });
      }
      if (braceDepth <= 0) {
        currentStruct = undefined;
      }
      continue;
    }

    const exportFn = EXPORT_FN.exec(line);
    if (exportFn) {
      const receiver = parseFirstParamType(exportFn[2]);
      symbols.push(makeFnSymbol(exportFn[1], exportFn[2], exportFn[3], moduleId, filePath, i, true, receiver));
      functions.push(exportFn[1]);
      continue;
    }

    const fn = FN.exec(line);
    if (fn && !line.includes("declare")) {
      const receiver = parseFirstParamType(fn[2]);
      symbols.push(makeFnSymbol(fn[1], fn[2], fn[3], moduleId, filePath, i, false, receiver));
      functions.push(fn[1]);
    }

    const exportGlobal = EXPORT_GLOBAL.exec(line);
    if (exportGlobal) {
      symbols.push({
        name: exportGlobal[1],
        kind: "global",
        signature: exportGlobal[1],
        detail: "global",
        documentation: `Exported global \`${exportGlobal[1]}\` from \`${moduleId}\`.`,
        exported: true,
        moduleId,
        filePath,
        line: i,
      });
    }

    const local = LOCAL.exec(line);
    if (local) {
      const explicit = local[2];
      const inferred = explicit ?? inferTypeFromRhs(local[3], symbols);
      if (inferred) {
        locals.set(local[1], inferred);
      }
    }
  }

  return { moduleId, filePath, symbols, structs, imports, locals, functions };
}

function parseImportClauseList(clause: string, modulePath: string, out: ImportBinding[]): void {
  for (const part of clause.split(",")) {
    const trimmed = part.trim();
    const starAs = /^\*\s+as\s+(\w+)$/.exec(trimmed);
    if (starAs) {
      out.push({ alias: starAs[1], modulePath, kind: "namespace" });
      continue;
    }
    const asSplit = trimmed.split(/\s+as\s+/);
    const name = asSplit[0]?.trim();
    const alias = asSplit[1]?.trim() ?? name;
    if (name) {
      out.push({ alias: alias!, modulePath, kind: "symbol", symbolName: name });
    }
  }
}

function inferTypeFromRhs(rhs: string, symbols: SymbolDoc[]): string | undefined {
  const call = /^([\w.]+)\([^)]*\)$/.exec(rhs.trim());
  if (!call) {
    return undefined;
  }
  const method = call[1].includes(".") ? call[1].split(".").pop()! : call[1];
  const fn = symbols.find((s) => s.name === method);
  if (fn?.returnType) {
    return fn.returnType;
  }
  if (method === "NewRouter" || method === "Group") {
    return "Router";
  }
  if (method === "parse") {
    return "Json";
  }
  return undefined;
}

function makeStructSymbol(st: StructDoc, moduleId: string): SymbolDoc {
  const fields = st.fields.map((f) => `- \`${f.name}\`: ${f.type}`).join("\n");
  return {
    name: st.name,
    kind: "struct",
    signature: `struct ${st.name}`,
    detail: "struct",
    documentation: fields
      ? `Struct from \`${moduleId}\`:\n\n${fields}`
      : `Struct \`${st.name}\` from \`${moduleId}\`.`,
    exported: st.exported,
    moduleId,
    filePath: st.filePath,
    line: st.line,
  };
}

function makeFnSymbol(
  name: string,
  params: string,
  ret: string | undefined,
  moduleId: string,
  filePath: string,
  line: number,
  exported: boolean,
  receiver?: string
): SymbolDoc {
  const sig = formatSignature(name, params, ret);
  const doc = [`\`${sig}\` from \`${moduleId}\`.`];
  if (receiver) {
    doc.push(`Method on \`${receiver}\`.`);
  }
  if (params.trim()) {
    doc.push(`Parameters: \`${params.trim()}\``);
  }
  return {
    name,
    kind: "function",
    signature: sig,
    detail: ret ? `→ ${ret}` : "function",
    documentation: doc.join("\n\n"),
    exported,
    receiver,
    returnType: ret,
    moduleId,
    filePath,
    line,
  };
}

export function resolveModuleFile(modulePath: string, contextFile: string): string | undefined {
  const searchPaths = collectModuleSearchPaths(contextFile);
  for (const root of searchPaths) {
    const file = path.join(root, `${modulePath}.xlang`);
    if (fs.existsSync(file)) {
      return file;
    }
    const dir = path.join(root, modulePath);
    if (fs.existsSync(dir) && fs.statSync(dir).isDirectory()) {
      return dir;
    }
  }
  const relative = path.join(path.dirname(contextFile), `${modulePath}.xlang`);
  if (fs.existsSync(relative)) {
    return relative;
  }
  return undefined;
}

export function moduleIdFromPath(filePath: string, searchRoot?: string): string {
  const normalized = path.normalize(filePath);
  for (const marker of ["libs", "runtime"]) {
    const parts = normalized.split(path.sep);
    const idx = parts.indexOf(marker);
    if (idx !== -1) {
      return parts.slice(idx + 1).join("/").replace(/\.xlang$/, "");
    }
  }
  if (searchRoot) {
    return path.relative(searchRoot, normalized).replace(/\.xlang$/, "").replace(/\\/g, "/");
  }
  return path.basename(normalized, ".xlang");
}

export function loadModuleFromDisk(modulePath: string, contextFile: string): ParsedModule | undefined {
  const resolved = resolveModuleFile(modulePath, contextFile);
  if (!resolved) {
    return undefined;
  }
  if (fs.statSync(resolved).isDirectory()) {
    return loadPackageDir(resolved, modulePath);
  }
  const source = fs.readFileSync(resolved, "utf8");
  return parseXlangSource(source, resolved, moduleIdFromPath(resolved));
}

function loadPackageDir(dirPath: string, moduleId: string): ParsedModule {
  const merged: ParsedModule = {
    moduleId,
    filePath: dirPath,
    symbols: [],
    structs: new Map(),
    imports: [],
    locals: new Map(),
    functions: [],
  };

  for (const file of fs.readdirSync(dirPath).filter((f) => f.endsWith(".xlang")).sort()) {
    const full = path.join(dirPath, file);
    const part = parseXlangSource(fs.readFileSync(full, "utf8"), full, `${moduleId}/${file.replace(/\.xlang$/, "")}`);
    for (const sym of part.symbols) {
      if (sym.exported && !merged.symbols.some((s) => s.name === sym.name)) {
        merged.symbols.push({ ...sym, moduleId });
      }
    }
    for (const [name, st] of part.structs) {
      if (st.exported && !merged.structs.has(name)) {
        merged.structs.set(name, st);
      }
    }
  }
  return merged;
}

export function symbolToLocation(sym: SymbolDoc): vscode.Location {
  return new vscode.Location(vscode.Uri.file(sym.filePath), new vscode.Position(sym.line, 0));
}

export function symbolToHover(sym: SymbolDoc): vscode.MarkdownString {
  const md = new vscode.MarkdownString();
  md.supportSnippet = true;
  md.appendMarkdown(`**${sym.name}** \`${sym.detail}\`\n\n`);
  md.appendMarkdown(sym.documentation);
  if (sym.moduleId) {
    md.appendMarkdown(`\n\n---\n*module:* \`${sym.moduleId}\``);
  }
  return md;
}

export function symbolToCompletion(sym: SymbolDoc, sortPrefix: string): vscode.CompletionItem {
  const kind =
    sym.kind === "struct"
      ? vscode.CompletionItemKind.Struct
      : sym.kind === "global" || sym.kind === "variable"
        ? vscode.CompletionItemKind.Variable
        : sym.kind === "module"
          ? vscode.CompletionItemKind.Module
          : vscode.CompletionItemKind.Function;
  const item = new vscode.CompletionItem(sym.name, kind);
  item.detail = sym.signature;
  item.documentation = symbolToHover(sym);
  item.sortText = `${sortPrefix}_${sym.name}`;
  if (sym.kind === "function" && !sym.receiver) {
    item.insertText = new vscode.SnippetString(`${sym.name}($0)`);
  }
  return item;
}
