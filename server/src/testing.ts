import { vi } from 'vitest';
import { defaultConfig, type ServerConfig } from './config.ts';
import { Hub, type Conn, type HubOptions } from './hub.ts';
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
