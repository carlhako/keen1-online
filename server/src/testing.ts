import { vi } from 'vitest';
import { defaultConfig, type ServerConfig } from './config.ts';
import { Hub, type Conn, type HubOptions } from './hub.ts';
import { RECORD_BYTES } from './saves.ts';
import { PROTOCOL_VERSION, type ClientMessage, type ServerMessage } from './protocol.ts';

/** A connection that records what the hub sends it. */
export class TestConn implements Conn {
  messages: ServerMessage[] = [];
  binaries: Buffer[] = [];
  closed = false;

  send(data: string | Buffer): void {
    if (typeof data === 'string') this.messages.push(JSON.parse(data) as ServerMessage);
    else this.binaries.push(data);
  }

  close(): void {
    this.closed = true;
  }

  /** Messages of one type, oldest first. */
  all<T extends ServerMessage['t']>(t: T): Extract<ServerMessage, { t: T }>[] {
    return this.messages.filter((m) => m.t === t) as Extract<ServerMessage, { t: T }>[];
  }

  last<T extends ServerMessage['t']>(t: T): Extract<ServerMessage, { t: T }> | undefined {
    return this.all(t).at(-1);
  }

  /** Forgets what was received so far. */
  clear(): void {
    this.messages = [];
    this.binaries = [];
  }
}

export const BUILD = 'testbuild';

export function makeHub(config: Partial<ServerConfig> = {}, opts: Partial<HubOptions> = {}): Hub {
  let seed = 12345;
  return new Hub({
    config: { ...defaultConfig(), ...config },
    buildId: () => BUILD,
    // A small deterministic generator, so seeds and tokens differ but tests repeat.
    random: () => {
      seed = (seed * 1664525 + 1013904223) >>> 0;
      return seed / 2 ** 32;
    },
    ...opts,
  });
}

export function send(hub: Hub, conn: TestConn, msg: ClientMessage): void {
  hub.message(conn, JSON.stringify(msg));
}

/** A connected client that has said hello. */
export function client(hub: Hub): TestConn {
  const c = new TestConn();
  hub.connect(c);
  send(hub, c, { t: 'hello', v: PROTOCOL_VERSION, build: BUILD });
  return c;
}

/** Two players in a started game; returns their connections. */
export function startedGame(hub: Hub): [TestConn, TestConn] {
  const a = client(hub);
  const b = client(hub);
  send(hub, a, { t: 'create' });
  const id = a.last('joined')?.game as number;
  send(hub, b, { t: 'join', game: id });
  return [a, b];
}

/** Both players send neutral input for frames `from` to `to - 1`. */
export function inputs(hub: Hub, conns: TestConn[], from: number, to: number, b = 8): void {
  // A browser sends about 24 inputs a second; the clock moves so the rate limit stays out of it.
  vi.advanceTimersByTime(1000);
  for (let f = from; f < to; f++) {
    if (f > from && f % 50 === 0) vi.advanceTimersByTime(1000);
    for (const c of conns) send(hub, c, { t: 'input', f, b });
  }
}

/** A valid record: `done` lists the completed levels (1 to 16). */
export function record(opts: { score?: number; done?: number[]; mapx?: number } = {}): Buffer {
  const b = Buffer.alloc(RECORD_BYTES);
  b.writeInt16LE(4, 50); // lives
  b.writeInt32LE(opts.score ?? 0, 54);
  b.writeInt32LE(opts.mapx ?? 12 << 12, 58);
  b.writeInt32LE(30 << 12, 62);
  b.writeInt32LE((opts.mapx ?? 12 << 12) - 0x9000, 66);
  b.writeInt32LE(0, 70);
  for (const level of opts.done ?? []) b.writeInt16LE(1, 18 + (level - 1) * 2);
  return b;
}
