import { createServer, type Server } from 'node:http';
import type { AddressInfo } from 'node:net';
import { join } from 'node:path';
import { WebSocketServer, type WebSocket } from 'ws';
import { createBuildIdReader } from './buildid.ts';
import type { ServerConfig } from './config.ts';
import { createHttpHandler } from './http.ts';
import { Hub, type Conn } from './hub.ts';

export interface ServerOptions {
  config: ServerConfig;
  /** Directory holding the web build (`build/web`). */
  webDir: string;
  log?: (message: string) => void;
  /** Debug option: write one frame log per game into this directory. */
  frameLogDir?: string;
  /** Where saves and high scores are kept; without it they live in memory only (tests). */
  dataDir?: string;
  /** The game files the page loads; defaults to `game/` in `dataDir`. */
  gameDir?: string;
  /** The release version, reported by `/healthz` so an installer can tell which one answered. */
  version?: string;
}

/** Seconds between WebSocket pings. They keep idle sockets open behind a proxy. */
const PING_SEC = 10;

/** Messages are small; a snapshot is the largest thing a browser sends. */
const MAX_PAYLOAD = 2 * 1024 * 1024;

export class GameServer {
  readonly config: ServerConfig;
  readonly buildId: () => string;
  readonly hub: Hub;
  private readonly http: Server;
  private readonly wss: WebSocketServer;
  private readonly pinger: ReturnType<typeof setInterval>;

  constructor(opts: ServerOptions) {
    this.config = opts.config;
    this.buildId = createBuildIdReader(opts.webDir);
    this.hub = new Hub({
      config: opts.config,
      buildId: this.buildId,
      log: opts.log,
      frameLogDir: opts.frameLogDir,
      dataDir: opts.dataDir,
    });
    const { handler, route } = createHttpHandler({
      webDir: opts.webDir,
      gameDir: opts.gameDir ?? (opts.dataDir ? join(opts.dataDir, 'game') : undefined),
      health: () => ({
        games: this.hub.games.size,
        players: this.hub.playerCount,
        buildId: this.buildId(),
        version: opts.version,
      }),
    });
    this.http = createServer(handler);
    this.wss = new WebSocketServer({ noServer: true, maxPayload: MAX_PAYLOAD });
    this.http.on('upgrade', (req, socket, head) => {
      if (route(req.url ?? '').kind !== 'ws') {
        socket.end('HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n');
        return;
      }
      this.wss.handleUpgrade(req, socket, head, (ws) => this.accept(ws));
    });
    // A socket that does not answer a ping before the next one is dead.
    this.pinger = setInterval(() => {
      for (const ws of this.wss.clients) {
        const alive = (ws as WebSocket & { alive?: boolean }).alive;
        if (alive === false) {
          ws.terminate();
          continue;
        }
        (ws as WebSocket & { alive?: boolean }).alive = false;
        ws.ping();
      }
    }, PING_SEC * 1000);
    this.pinger.unref();
  }

  private accept(ws: WebSocket): void {
    const conn: Conn = {
      send: (data) => {
        if (ws.readyState === ws.OPEN) ws.send(data);
      },
      close: () => ws.close(),
    };
    (ws as WebSocket & { alive?: boolean }).alive = true;
    ws.on('pong', () => {
      (ws as WebSocket & { alive?: boolean }).alive = true;
    });
    this.hub.connect(conn);
    ws.on('message', (data, isBinary) => {
      if (isBinary) {
        const buf = Array.isArray(data)
          ? Buffer.concat(data)
          : data instanceof ArrayBuffer
            ? Buffer.from(data)
            : data;
        this.hub.binary(conn, buf);
      } else {
        this.hub.message(conn, data.toString());
      }
    });
    ws.on('close', () => this.hub.disconnect(conn));
    ws.on('error', () => ws.terminate());
  }

  /** Starts listening; resolves with the port (useful when the configured port is 0). */
  listen(): Promise<number> {
    return new Promise((resolve, reject) => {
      this.http.once('error', reject);
      this.http.listen(this.config.port, () => {
        this.http.off('error', reject);
        resolve((this.http.address() as AddressInfo).port);
      });
    });
  }

  close(): Promise<void> {
    clearInterval(this.pinger);
    this.hub.close();
    for (const ws of this.wss.clients) ws.terminate();
    return new Promise((resolve) => {
      this.wss.close(() => {
        this.http.closeAllConnections();
        this.http.close(() => resolve());
      });
    });
  }
}
