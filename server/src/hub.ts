import { randomInt } from 'node:crypto';
import type { ServerConfig } from './config.ts';
import { Game, type GameDeps, type Player } from './game.ts';
import {
  INPUT_RATE,
  MALFORMED_LIMIT,
  PROTOCOL_VERSION,
  parseClientMessage,
  parseUpload,
  type ClientMessage,
  type LobbyGame,
  type RejectReason,
  type ServerMessage,
} from './protocol.ts';

/** One end of a WebSocket, as far as the hub is concerned. */
export interface Conn {
  send(data: string | Buffer): void;
  close(): void;
}

export interface HubOptions {
  config: ServerConfig;
  buildId: () => string;
  now?: () => number;
  random?: () => number;
  log?: (message: string) => void;
  /** Debug option: write one frame log per game into this directory. */
  frameLogDir?: string;
}

class Client implements Player {
  game: Game | null = null;
  hello = false;
  malformed = 0;
  /** Input rate window. */
  windowStart = 0;
  windowCount = 0;

  constructor(
    readonly conn: Conn,
    readonly id: number,
  ) {}

  send(msg: ServerMessage): void {
    this.conn.send(JSON.stringify(msg));
  }

  kicked(): void {
    this.game = null;
    this.send({ t: 'left' });
    this.onKicked?.();
  }

  onKicked?: () => void;

  sendBinary(data: Buffer): void {
    this.conn.send(data);
  }
}

/**
 * The lobby and the games. It knows nothing about sockets: `connect` takes anything that can send
 * and close, and the transport calls `message`, `binary` and `close` as things arrive.
 */
export class Hub {
  readonly games = new Map<number, Game>();
  private readonly clients = new Map<Conn, Client>();
  private nextGame = 1;
  private nextClient = 1;
  private lastLobby = '';
  private timer: ReturnType<typeof setInterval> | null = null;
  private readonly now: () => number;
  private readonly random: () => number;
  private readonly log: (message: string) => void;
  private readonly deps: GameDeps;

  constructor(private readonly opts: HubOptions) {
    this.now = opts.now ?? Date.now;
    this.random = opts.random ?? (() => randomInt(0, 2 ** 32) / 2 ** 32);
    this.log = opts.log ?? (() => {});
    this.deps = {
      config: opts.config,
      now: this.now,
      random: this.random,
      changed: () => this.publish(),
      removed: (game) => {
        this.games.delete(game.id);
        for (const c of this.clients.values()) if (c.game === game) c.game = null;
      },
      log: this.log,
      frameLogDir: opts.frameLogDir,
    };
    this.timer = setInterval(() => this.tick(), 1000);
    this.timer.unref?.();
  }

  get config(): ServerConfig {
    return this.opts.config;
  }

  get playerCount(): number {
    let n = 0;
    for (const g of this.games.values()) n += g.occupied();
    return n;
  }

  close(): void {
    if (this.timer) clearInterval(this.timer);
    this.timer = null;
    for (const g of this.games.values()) g.dispose();
  }

  private tick(): void {
    for (const g of [...this.games.values()]) g.checkTimeouts();
  }

  // ---- transport entry points ----

  connect(conn: Conn): void {
    const c = new Client(conn, this.nextClient++);
    c.onKicked = () => c.send(this.lobbyMessage());
    this.clients.set(conn, c);
  }

  /** The transport reports a closed connection. */
  disconnect(conn: Conn): void {
    const c = this.clients.get(conn);
    if (!c) return;
    this.clients.delete(conn);
    const game = c.game;
    c.game = null;
    game?.drop(game.seatOf(c));
  }

  message(conn: Conn, text: string): void {
    const c = this.clients.get(conn);
    if (!c) return;
    let raw: unknown;
    try {
      raw = JSON.parse(text);
    } catch {
      this.bad(c);
      return;
    }
    const m = parseClientMessage(raw);
    if (!m) {
      this.bad(c);
      return;
    }
    if (!c.hello) {
      if (m.t !== 'hello') {
        conn.close();
        return;
      }
      this.onHello(c, m);
      return;
    }
    this.touch(c);
    switch (m.t) {
      case 'hello':
        this.bad(c);
        break;
      case 'create':
        this.onCreate(c);
        break;
      case 'join':
        this.onJoin(c, m);
        break;
      case 'single':
        if (c.game) c.game.single(c.game.seatOf(c));
        break;
      case 'leave':
        this.onLeave(c);
        break;
      case 'input':
        this.onInput(c, m);
        break;
      case 'sum':
        if (c.game) c.game.sum(c.game.seatOf(c), m.f, m.c, m.l);
        break;
      case 'ready':
        if (c.game) c.game.ready(c.game.seatOf(c));
        break;
      case 'continue':
        if (c.game) c.game.continueSolo(c.game.seatOf(c));
        break;
      case 'ending':
        if (c.game) c.game.ending(c.game.seatOf(c));
        break;
      case 'finished':
        if (c.game) c.game.finish(c.game.seatOf(c));
        break;
      case 'ping':
        c.send({ t: 'pong', c: m.c });
        break;
    }
  }

  binary(conn: Conn, data: Buffer): void {
    const c = this.clients.get(conn);
    if (!c || !c.hello) {
      conn.close();
      return;
    }
    const up = parseUpload(data);
    if (!up) {
      this.bad(c);
      return;
    }
    this.touch(c);
    if (c.game) c.game.upload(c.game.seatOf(c), up);
  }

  // ---- handlers ----

  private bad(c: Client): void {
    if (++c.malformed >= MALFORMED_LIMIT) {
      this.log(`client ${c.id}: too many malformed messages, closing`);
      c.conn.close();
    }
  }

  private touch(c: Client): void {
    if (c.game) c.game.touch(c.game.seatOf(c));
  }

  private onHello(c: Client, m: Extract<ClientMessage, { t: 'hello' }>): void {
    if (m.v !== PROTOCOL_VERSION || m.build !== this.opts.buildId()) {
      this.reject(c, 'version');
      c.conn.close();
      return;
    }
    c.hello = true;
    c.send({
      t: 'welcome',
      v: PROTOCOL_VERSION,
      cfg: {
        checkpointFrames: this.opts.config.checkpointFrames,
        inputLeadMax: this.opts.config.inputLeadMax,
        disconnectSec: this.opts.config.disconnectSec,
      },
    });
    c.send(this.lobbyMessage());
  }

  private reject(c: Client, reason: RejectReason, extra?: { count: number; max: number }): void {
    c.send({ t: 'reject', reason, ...extra });
  }

  private onCreate(c: Client): void {
    if (c.game) {
      this.reject(c, 'busy');
      return;
    }
    if (this.games.size >= this.opts.config.maxGames) {
      this.reject(c, 'full', { count: this.games.size, max: this.opts.config.maxGames });
      return;
    }
    const game = new Game(this.nextGame++, this.deps);
    this.games.set(game.id, game);
    c.game = game;
    this.log(`game ${game.id} created`);
    game.seat(0, c);
  }

  private onJoin(c: Client, m: Extract<ClientMessage, { t: 'join' }>): void {
    if (c.game) {
      this.reject(c, 'busy');
      return;
    }
    const game = this.games.get(m.game);
    if (!game) {
      this.reject(c, 'nogame');
      return;
    }
    const seat = game.pickSeat(m.token);
    if (seat < 0) {
      this.reject(c, 'closed');
      return;
    }
    c.game = game;
    game.seat(seat, c);
  }

  private onInput(c: Client, m: Extract<ClientMessage, { t: 'input' }>): void {
    if (!c.game) return;
    const t = this.now();
    if (t - c.windowStart >= 1000) {
      c.windowStart = t;
      c.windowCount = 0;
    }
    if (++c.windowCount > INPUT_RATE) return;
    c.game.input(c.game.seatOf(c), m.f, m.b);
  }

  /** The client leaves its game from the Esc menu: its seat opens and it is back in the lobby. */
  private onLeave(c: Client): void {
    const game = c.game;
    if (!game) return;
    c.game = null;
    game.drop(game.seatOf(c));
    c.send({ t: 'left' });
    c.send(this.lobbyMessage());
  }

  // ---- lobby ----

  lobbyMessage(): ServerMessage {
    const games: LobbyGame[] = [...this.games.values()].map((g) => g.summary());
    return { t: 'lobby', games, count: this.games.size, max: this.opts.config.maxGames };
  }

  /** Sends the game list to everyone who is looking at it, when it changed. */
  private publish(): void {
    const msg = this.lobbyMessage();
    const json = JSON.stringify(msg);
    if (json === this.lastLobby) return;
    this.lastLobby = json;
    for (const c of this.clients.values()) {
      if (c.hello && !c.game) c.conn.send(json);
    }
  }
}
