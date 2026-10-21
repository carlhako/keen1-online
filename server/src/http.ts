import { existsSync } from 'node:fs';
import type { IncomingMessage, ServerResponse } from 'node:http';
import sirv from 'sirv';

export interface Health {
  status: 'ok';
  games: number;
  players: number;
  buildId: string;
}

export interface HttpOptions {
  /** Directory holding the web build. */
  webDir: string;
  health: () => Omit<Health, 'status'>;
}

type Handler = (req: IncomingMessage, res: ServerResponse) => void;

const NOT_BUILT = 'The web build is missing. Run `make web` in the repository root.\n';

/**
 * Serves `GET /healthz` and the web build. Files are looked up in a list taken when the server
 * starts, so a request path can never reach outside the web directory. Nothing is cached without
 * a check: the page, wasm and data must stay in step with the build id the server announces.
 */
export function createHttpHandler(opts: HttpOptions): Handler {
  const files = existsSync(opts.webDir)
    ? sirv(opts.webDir, {
        etag: true,
        gzip: true,
        brotli: true,
        dev: false,
        setHeaders: (res) => res.setHeader('Cache-Control', 'no-cache'),
      })
    : null;

  return (req, res) => {
    const path = (req.url ?? '/').split('?')[0];
    if (path === '/healthz') {
      if (req.method !== 'GET' && req.method !== 'HEAD') {
        res.writeHead(405, { Allow: 'GET, HEAD' }).end();
        return;
      }
      const body = JSON.stringify({ status: 'ok', ...opts.health() } satisfies Health);
      res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
      res.end(req.method === 'HEAD' ? undefined : body);
      return;
    }
    if (!files) {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end(NOT_BUILT);
      return;
    }
    files(req, res, () => {
      res.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end('Not found\n');
    });
  };
}
