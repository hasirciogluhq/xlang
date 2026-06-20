import * as fs from "node:fs";
import * as path from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import * as vscode from "vscode";

const execFileAsync = promisify(execFile);

const DIAGNOSTIC_SOURCE = "xlang";

const PARSE_ERROR_RE =
  /(?:lex|parse) error at line (\d+), column (\d+): (.+)/;

const MODULE_ERROR_RE = /module not found:\s*(\S+)/;

export class XlangDiagnostics {
  private readonly collection: vscode.DiagnosticCollection;
  private readonly pending = new Map<string, NodeJS.Timeout>();

  constructor(collection: vscode.DiagnosticCollection) {
    this.collection = collection;
  }

  dispose(): void {
    for (const timer of this.pending.values()) {
      clearTimeout(timer);
    }
    this.pending.clear();
    this.collection.dispose();
  }

  schedule(document: vscode.TextDocument, delayMs = 300): void {
    const key = document.uri.toString();
    const existing = this.pending.get(key);
    if (existing) {
      clearTimeout(existing);
    }

    const timer = setTimeout(() => {
      this.pending.delete(key);
      void this.refresh(document);
    }, delayMs);

    this.pending.set(key, timer);
  }

  clear(document: vscode.TextDocument): void {
    this.collection.delete(document.uri);
  }

  async refresh(document: vscode.TextDocument): Promise<void> {
    const config = vscode.workspace.getConfiguration("xlang");
    if (!config.get<boolean>("enableDiagnostics", true)) {
      this.collection.delete(document.uri);
      return;
    }

    if (document.uri.scheme !== "file") {
      return;
    }

    const compilerPath = await resolveCompilerPath();
    if (!compilerPath) {
      return;
    }

    const filePath = document.uri.fsPath;
    let stderr = "";

    try {
      await execFileAsync(compilerPath, ["parse", filePath], {
        cwd: path.dirname(filePath),
        env: buildEnv(filePath),
        timeout: 15_000,
        maxBuffer: 1024 * 1024,
      });
      this.collection.set(document.uri, []);
      return;
    } catch (error: unknown) {
      if (!isExecError(error)) {
        return;
      }
      stderr = error.stderr?.toString() ?? error.message;
    }

    const diagnostic = parseDiagnostic(stderr, document);
    this.collection.set(document.uri, diagnostic ? [diagnostic] : []);
  }
}

function parseDiagnostic(
  stderr: string,
  document: vscode.TextDocument
): vscode.Diagnostic | undefined {
  for (const line of stderr.split("\n")) {
    const trimmed = line.replace(/^error:\s*/, "");
    const match = PARSE_ERROR_RE.exec(trimmed);
    if (!match) {
      continue;
    }

    const lineNumber = Number.parseInt(match[1], 10);
    const column = Number.parseInt(match[2], 10);
    const message = match[3];

    const startLine = Math.max(0, lineNumber - 1);
    const startCharacter = Math.max(0, column - 1);
    const endCharacter = document.lineAt(startLine).text.length;

    const diagnostic = new vscode.Diagnostic(
      new vscode.Range(
        startLine,
        startCharacter,
        startLine,
        Math.max(startCharacter + 1, endCharacter)
      ),
      message,
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = DIAGNOSTIC_SOURCE;
    return diagnostic;
  }

  const moduleMatch = MODULE_ERROR_RE.exec(stderr);
  if (moduleMatch) {
    const moduleName = moduleMatch[1];
    const diagnostic = new vscode.Diagnostic(
      findImportRange(document, moduleName),
      `module not found: ${moduleName}`,
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = DIAGNOSTIC_SOURCE;
    return diagnostic;
  }

  if (stderr.includes("error:")) {
    const message = stderr
      .split("\n")
      .map((line) => line.replace(/^error:\s*/, ""))
      .filter(Boolean)
      .join("\n")
      .trim();

    const diagnostic = new vscode.Diagnostic(
      new vscode.Range(0, 0, 0, 1),
      message,
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = DIAGNOSTIC_SOURCE;
    return diagnostic;
  }

  return undefined;
}

function findImportRange(
  document: vscode.TextDocument,
  moduleName: string
): vscode.Range {
  const root = moduleName.split("/")[0];
  for (let i = 0; i < document.lineCount; i += 1) {
    const text = document.lineAt(i).text;
    if (!text.includes("import")) {
      continue;
    }
    if (text.includes(moduleName) || text.includes(root)) {
      return document.lineAt(i).range;
    }
  }
  return new vscode.Range(0, 0, 0, 1);
}

export async function resolveCompilerPath(): Promise<string | undefined> {
  return resolveXlankPath();
}

export async function resolveXlankPath(): Promise<string | undefined> {
  const config = vscode.workspace.getConfiguration("xlang");
  const configured =
    config.get<string>("compilerPath", "").trim() ||
    config.get<string>("xlankPath", "").trim();
  if (configured) {
    return configured;
  }

  const folders = vscode.workspace.workspaceFolders ?? [];
  for (const folder of folders) {
    for (const base of [folder.uri.fsPath, path.join(folder.uri.fsPath, "..")]) {
      const candidate = path.normalize(path.join(base, "build", "xlank"));
      if (fs.existsSync(candidate)) {
        return candidate;
      }
    }
  }

  return "xlank";
}

export function buildEnv(filePath?: string): NodeJS.ProcessEnv {
  const env = { ...process.env };
  const paths = collectModuleSearchPaths(filePath);
  const existing = env.XLANG_PATH?.split(":").filter(Boolean) ?? [];
  env.XLANG_PATH = [...new Set([...paths, ...existing])].join(":");
  return env;
}

function collectModuleSearchPaths(filePath?: string): string[] {
  const paths: string[] = [];
  const seen = new Set<string>();

  const addDir = (dir: string) => {
    const norm = path.normalize(dir);
    if (seen.has(norm) || !fs.existsSync(norm)) {
      return;
    }
    seen.add(norm);
    paths.push(norm);
  };

  const addRuntime = (base: string) => {
    addDir(path.join(base, "runtime"));
  };

  const addLibs = (base: string) => {
    addDir(path.join(base, "libs"));
  };

  if (filePath) {
    let dir = path.dirname(filePath);
    for (let i = 0; i < 12; i += 1) {
      addRuntime(dir);
      addLibs(dir);
      addDir(dir);
      const parent = path.dirname(dir);
      if (parent === dir) {
        break;
      }
      dir = parent;
    }
  }

  for (const folder of vscode.workspace.workspaceFolders ?? []) {
    const root = folder.uri.fsPath;
    addRuntime(root);
    addLibs(root);
    addRuntime(path.join(root, ".."));
    addLibs(path.join(root, ".."));
    addDir(root);
    addDir(path.join(root, ".."));
  }

  return paths;
}

function isExecError(
  error: unknown
): error is { stderr?: Buffer | string; message: string } {
  return typeof error === "object" && error !== null && "message" in error;
}

export async function runCompiler(
  args: string[],
  cwd: string,
  filePath?: string
): Promise<{ stdout: string; stderr: string }> {
  const compilerPath = await resolveCompilerPath();
  if (!compilerPath) {
    throw new Error(
      "xlang compiler not found. Build the project or set xlang.compilerPath."
    );
  }

  const result = await execFileAsync(compilerPath, args, {
    cwd,
    env: buildEnv(filePath),
    timeout: 120_000,
    maxBuffer: 4 * 1024 * 1024,
  });

  return {
    stdout: result.stdout.toString(),
    stderr: result.stderr.toString(),
  };
}

/** @deprecated use runCompiler */
export const runXlank = runCompiler;
