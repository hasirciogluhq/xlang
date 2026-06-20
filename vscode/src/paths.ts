import * as fs from "node:fs";
import * as path from "node:path";
import * as vscode from "vscode";

export function collectModuleSearchPaths(filePath?: string): string[] {
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

  if (filePath) {
    let dir = path.dirname(filePath);
    for (let i = 0; i < 12; i += 1) {
      addDir(path.join(dir, "runtime"));
      addDir(path.join(dir, "libs"));
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
    addDir(path.join(root, "runtime"));
    addDir(path.join(root, "libs"));
    addDir(path.join(root, "..", "runtime"));
    addDir(path.join(root, "..", "libs"));
    addDir(root);
  }

  return paths;
}

export function buildEnv(filePath?: string): NodeJS.ProcessEnv {
  const env = { ...process.env };
  const paths = collectModuleSearchPaths(filePath);
  const existing = env.XLANG_PATH?.split(":").filter(Boolean) ?? [];
  env.XLANG_PATH = [...new Set([...paths, ...existing])].join(":");
  return env;
}
