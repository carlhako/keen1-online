import { createHash } from 'node:crypto';
import { existsSync, readdirSync, statSync } from 'node:fs';
import { readFile, stat } from 'node:fs/promises';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { join } from 'node:path';
import sirv from 'sirv';
import { BUNDLE_NAME, tarGz } from './bundle.ts';
import { GAME_FILES } from './gamefiles.ts';

export interface Health {
  status: 'ok';
  games: number;
  players: number;
  buildId: string;
  /** Whether all the game files are in the game directory. */
  gameData: 'ok' | 'missing';
  /** The release the server was started from (`version` in package.json), when it knows. */
  version?: string;
}

export interface HttpOptions {
  /** Directory holding the web build. */
  webDir: string;
  /** Directory holding the game files (`<dataDir>/game`); without it the game data is missing. */
  gameDir?: string;
  health: () => Omit<Health, 'status' | 'gameData'>;
}

/** What a request path means. */
export type Route =
  | { kind: 'ws' }
  | { kind: 'healthz' }
  | { kind: 'dataIndex' }
  /** Every game file in one gzip-compressed tar archive. */
  | { kind: 'dataBundle' }
  | { kind: 'data'; name: string }
  /** A file of the web build; `url` is the path to hand to the file server. */
  | { kind: 'file'; url: string }
  | { kind: 'index' }
  | { kind: 'redirect'; to: string }
  | { kind: 'notFound' };

type Handler = (req: IncomingMessage, res: ServerResponse) => void;

const NOT_BUILT = 'The web build is missing. Run `make web` in the repository root.\n';

/** The files below `dir` as `/`-separated paths, taken once. */
function listFiles(dir: string, prefix = ''): Set<string> {
  const out = new Set<string>();
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      for (const f of listFiles(join(dir, entry.name), `${prefix}${entry.name}/`)) out.add(f);
    } else {
      out.add(prefix + entry.name);
    }
  }
  return out;
}

/**
 * Builds the function that tells what a path asks for, wherever a reverse proxy mounted the game.
 * The decoded path is cut into segments and, from the full path down, the shortest prefix whose
 * remainder names something we serve (`/ws`, `/healthz`, `data/...`, a web file) is dropped. A path
 * that names nothing and has no extension is taken to be the game's own prefix and sent to its
 * slash form, so relative URLs on the page resolve under the same prefix. Segments that could
 * climb out of a directory are refused before anything is matched.
 */
export function createRouter(webFiles: ReadonlySet<string>): (rawPath: string) => Route {
  const gameFiles = new Set(GAME_FILES);
  return (rawPath) => {
    let path: string;
    try {
      path = decodeURIComponent(rawPath);
    } catch {
      return { kind: 'notFound' };
    }
    const segs = path.split('/').filter((s) => s !== '');
    if (segs.some((s) => s === '.' || s === '..' || s.includes('\\') || s.includes('\0'))) {
      return { kind: 'notFound' };
    }
    if (path.endsWith('/')) return { kind: 'index' };
    for (let i = 0; i < segs.length; i++) {
      const tail = segs.slice(i);
      const joined = tail.join('/');
      if (joined === 'ws') return { kind: 'ws' };
      if (joined === 'healthz') return { kind: 'healthz' };
      if (joined === 'data/index.json') return { kind: 'dataIndex' };
      if (joined === `data/${BUNDLE_NAME}`) return { kind: 'dataBundle' };
      if (tail.length === 2 && tail[0] === 'data' && gameFiles.has(tail[1]!)) {
        return { kind: 'data', name: tail[1]! };
      }
      if (webFiles.has(joined))
        return { kind: 'file', url: '/' + tail.map(encodeURIComponent).join('/') };
    }
    const last = segs[segs.length - 1] ?? '';
    if (last !== '' && !last.includes('.')) {
      return { kind: 'redirect', to: '/' + segs.map(encodeURIComponent).join('/') + '/' };
    }
    return { kind: 'notFound' };
  };
}

function notFound(res: ServerResponse): void {
  res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end('Not found\n');
}

/** Which of the game files are in `gameDir`, with their sizes. */
function presentGameFiles(gameDir: string | undefined): { name: string; size: number }[] {
  if (!gameDir) return [];
  const out: { name: string; size: number }[] = [];
  for (const name of GAME_FILES) {
    try {
      const st = statSync(join(gameDir, name));
      if (st.isFile()) out.push({ name, size: st.size });
    } catch {
      // not there
    }
  }
  return out;
}

/** True when every game file is in `gameDir`. */
export function gameDataComplete(gameDir: string | undefined): boolean {
  return presentGameFiles(gameDir).length === GAME_FILES.length;
}

/**
 * Serves `GET /healthz`, the game files and the web build, under any path prefix. Web files are
 * looked up in a list taken when the server starts and game files only by name from a fixed list,
 * so a request path can never reach outside those directories. Nothing is cached without a check:
 * the page, wasm and data must stay in step with the build id the server announces.
 */
export function createHttpHandler(opts: HttpOptions): {
  handler: Handler;
  route: (url: string) => Route;
} {
  const built = existsSync(opts.webDir);
  const files = built
    ? sirv(opts.webDir, {
        etag: true,
        gzip: true,
        brotli: true,
        dev: false,
        setHeaders: (res) => res.setHeader('Cache-Control', 'no-cache'),
      })
    : null;
  const router = createRouter(built ? listFiles(opts.webDir) : new Set());
  const route = (url: string): Route => router((url.split('?')[0] ?? '') || '/');

  const handler: Handler = (req, res) => {
    const target = route(req.url ?? '/');
    const readOnly = req.method === 'GET' || req.method === 'HEAD';
    if (target.kind === 'healthz') {
      if (!readOnly) {
        res.writeHead(405, { Allow: 'GET, HEAD' }).end();
        return;
      }
      const gameData = gameDataComplete(opts.gameDir) ? 'ok' : 'missing';
      const body = JSON.stringify({ status: 'ok', ...opts.health(), gameData } satisfies Health);
      res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
      res.end(req.method === 'HEAD' ? undefined : body);
      return;
    }
    if (target.kind === 'dataIndex') {
      if (!readOnly) return notFound(res);
      const present = presentGameFiles(opts.gameDir);
      const body = JSON.stringify({
        files: present,
        complete: present.length === GAME_FILES.length,
      });
      res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-cache' });
      res.end(req.method === 'HEAD' ? undefined : body);
      return;
    }
    if (target.kind === 'data') {
      if (!readOnly || !opts.gameDir) return notFound(res);
      void sendGameFile(req, res, join(opts.gameDir, target.name));
      return;
    }
    if (target.kind === 'dataBundle') {
      if (!readOnly || !opts.gameDir) return notFound(res);
      void sendBundle(req, res, opts.gameDir);
      return;
    }
    if (target.kind === 'redirect') {
      const query = (req.url ?? '').split('?').slice(1).join('?');
      res.writeHead(302, {
        Location: target.to + (query ? `?${query}` : ''),
        'Cache-Control': 'no-store',
      });
      res.end();
      return;
    }
    if (!files) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end(NOT_BUILT);
      return;
    }
    if (target.kind === 'notFound' || target.kind === 'ws') return notFound(res);
    // The index or a file: sirv sees the path without any prefix.
    req.url = target.kind === 'index' ? '/' : target.url;
    files(req, res, () => notFound(res));
  };
  return { handler, route };
}

async function sendGameFile(
  req: IncomingMessage,
  res: ServerResponse,
  path: string,
): Promise<void> {
  let data: Buffer;
  let etag: string;
  try {
    const st = await stat(path);
    data = await readFile(path);
    etag = `W/"${data.length}-${Math.round(st.mtimeMs)}"`;
  } catch {
    notFound(res);
    return;
  }
  if (req.headers['if-none-match'] === etag) {
    res.writeHead(304, { ETag: etag, 'Cache-Control': 'no-cache' }).end();
    return;
  }
  res.writeHead(200, {
    'Content-Type': 'application/octet-stream',
    'Content-Length': data.length,
    'Cache-Control': 'no-cache',
    ETag: etag,
  });
  res.end(req.method === 'HEAD' ? undefined : data);
}

/** The last archive built, kept until a game file changes. */
let bundleCache: { key: string; etag: string; body: Promise<Buffer> } | null = null;

/**
 * Sends every game file as one `tar.gz` (see `bundle.ts`), for networks whose web filters block
 * the single binary files. Only a complete set is sent; the archive is built once and rebuilt when
 * a file's size or time changes.
 */
async function sendBundle(
  req: IncomingMessage,
  res: ServerResponse,
  gameDir: string,
): Promise<void> {
  let key = '';
  try {
    for (const name of GAME_FILES) {
      const st = await stat(join(gameDir, name));
      if (!st.isFile()) throw new Error(name);
      key += `${name}:${st.size}:${Math.round(st.mtimeMs)};`;
    }
  } catch {
    notFound(res);
    return;
  }
  if (bundleCache?.key !== key) {
    const body = Promise.all(
      GAME_FILES.map(async (name) => ({ name, data: await readFile(join(gameDir, name)) })),
    ).then(tarGz);
    const hash = createHash('sha256').update(key).digest('hex').slice(0, 16);
    bundleCache = { key, etag: `W/"${hash}"`, body };
    body.catch(() => {
      if (bundleCache?.body === body) bundleCache = null;
    });
  }
  const { etag, body } = bundleCache;
  let data: Buffer;
  try {
    data = await body;
  } catch {
    notFound(res);
    return;
  }
  if (req.headers['if-none-match'] === etag) {
    res.writeHead(304, { ETag: etag, 'Cache-Control': 'no-cache' }).end();
    return;
  }
  res.writeHead(200, {
    'Content-Type': 'application/gzip',
    'Content-Length': data.length,
    'Cache-Control': 'no-cache',
    ETag: etag,
  });
  res.end(req.method === 'HEAD' ? undefined : data);
}
