import { readFileSync, statSync } from 'node:fs';
import { join } from 'node:path';

/**
 * The build id is a hash of the wasm file, written next to it by `make web` as `build-id.txt`.
 * The page carries the same id, so a browser left open across an upgrade is told to reload. The
 * file is checked again when it changes, so a rebuilt web directory is picked up without a restart.
 */
export function createBuildIdReader(webDir: string): () => string {
  const path = join(webDir, 'build-id.txt');
  let mtime = -1;
  let id = 'unbuilt';
  return () => {
    try {
      const m = statSync(path).mtimeMs;
      if (m !== mtime) {
        id = readFileSync(path, 'utf8').trim() || 'unbuilt';
        mtime = m;
      }
    } catch {
      id = 'unbuilt';
      mtime = -1;
    }
    return id;
  };
}
