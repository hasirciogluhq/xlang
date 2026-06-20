import * as fs from "node:fs";
import * as path from "node:path";
import { execFile } from "node:child_process";
import { promisify } from "node:util";
import * as vscode from "vscode";

const execFileAsync = promisify(execFile);

const ERROR_RE =
  /(?:lex|parse) error at line (\d+), column (\d+): (.+)/;

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

    const xlankPath = await resolveXlankPath();
    if (!xlankPath) {
      return;
    }

    const filePath = document.uri.fsPath;
    let stderr = "";

    try {
      await execFileAsync(xlankPath, ["parse", filePath], {
        cwd: path.dirname(filePath),
        env: buildEnv(),
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
    const match = ERROR_RE.exec(trimmed);
    if (!match) {
      continue;
    }

    const lineNumber = Number.parseInt(match[1], 10);
    const column = Number.parseInt(match[2], 10);
    const message = match[3];

    const startLine = Math.max(0, lineNumber - 1);
    const startCharacter = Math.max(0, column - 1);
    const endCharacter = document.lineAt(startLine).text.length;

    const range = new vscode.Range(
      startLine,
      startCharacter,
      startLine,
      Math.max(startCharacter + 1, endCharacter)
    );

    const diagnostic = new vscode.Diagnostic(
      range,
      message,
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = "xlank";
    return diagnostic;
  }

  if (stderr.includes("error:")) {
    const diagnostic = new vscode.Diagnostic(
      new vscode.Range(0, 0, 0, 1),
      stderr.trim(),
      vscode.DiagnosticSeverity.Error
    );
    diagnostic.source = "xlank";
    return diagnostic;
  }

  return undefined;
}

export async function resolveXlankPath(): Promise<string | undefined> {
  const config = vscode.workspace.getConfiguration("xlang");
  const configured = config.get<string>("xlankPath", "").trim();
  if (configured) {
    return configured;
  }

  const folders = vscode.workspace.workspaceFolders ?? [];
  for (const folder of folders) {
    const candidate = path.join(folder.uri.fsPath, "build", "xlank");
    if (fs.existsSync(candidate)) {
      return candidate;
    }
  }

  return "xlank";
}

export function buildEnv(): NodeJS.ProcessEnv {
  const env = { ...process.env };
  const folders = vscode.workspace.workspaceFolders ?? [];
  if (folders.length === 0) {
    return env;
  }

  const roots = folders.map((folder) => folder.uri.fsPath);
  const existing = env.XLANG_PATH?.split(":").filter(Boolean) ?? [];
  const merged = [...new Set([...roots, ...existing])];
  env.XLANG_PATH = merged.join(":");
  return env;
}

function isExecError(
  error: unknown
): error is { stderr?: Buffer | string; message: string } {
  return typeof error === "object" && error !== null && "message" in error;
}

export async function runXlank(
  args: string[],
  cwd: string
): Promise<{ stdout: string; stderr: string }> {
  const xlankPath = await resolveXlankPath();
  if (!xlankPath) {
    throw new Error("xlank binary not found. Build the compiler or set xlang.xlankPath.");
  }

  const result = await execFileAsync(xlankPath, args, {
    cwd,
    env: buildEnv(),
    timeout: 120_000,
    maxBuffer: 4 * 1024 * 1024,
  });

  return {
    stdout: result.stdout.toString(),
    stderr: result.stderr.toString(),
  };
}
