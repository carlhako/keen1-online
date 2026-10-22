import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import WebSocket from 'ws';
import { defaultConfig } from './config.ts';
import { PROTOCOL_VERSION, type ServerMessage } from './protocol.ts';
import { GameServer } from './server.ts';

let dir: string;
let server: GameServer;
let url: string;
const open: WebSocket[] = [];

beforeEach(async () => {
  dir = mkdtempSync(join(tmpdir(), 'keen-web-'));
  writeFileSync(join(dir, 'index.html'), '<!doctype html>');
  writeFileSync(join(dir, 'build-id.txt'), 'build42\n');
  server = new GameServer({ config: { ...defaultConfig(), port: 0 }, webDir: dir });
  url = `ws://127.0.0.1:${await server.listen()}/ws`;
});

afterEach(async () => {
  for (const ws of open.splice(0)) ws.terminate();
  await server.close();
  rmSync(dir, { recursive: true, force: true });
});

/** Opens a socket and collects what the server sends. */
function connect(path = url): {
  ws: WebSocket;
  messages: ServerMessage[];
  closed: Promise<number>;
} {
  const ws = new WebSocket(path);
  open.push(ws);
  const messages: ServerMessage[] = [];
  ws.on('message', (data, isBinary) => {
    if (!isBinary) messages.push(JSON.parse(data.toString()) as ServerMessage);
  });
  // A refused upgrade is an error on the client side, then a close.
  ws.on('error', () => {});
  const closed = new Promise<number>((resolve) => ws.on('close', (code) => resolve(code)));
  return { ws, messages, closed };
}

async function until(cond: () => boolean): Promise<void> {
  for (let i = 0; i < 200; i++) {
    if (cond()) return;
    await new Promise((r) => setTimeout(r, 10));
  }
  throw new Error('timed out');
}

describe('/ws', () => {
  it('welcomes a matching hello and sends the lobby', async () => {
    const { ws, messages } = connect();
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', v: PROTOCOL_VERSION, build: 'build42' }));
    await until(() => messages.length >= 2);
    expect(messages[0]?.t).toBe('welcome');
    expect(messages[1]).toEqual({ t: 'lobby', games: [], count: 0, max: 5 });
  });

  it('rejects an outdated page and closes the socket', async () => {
    const { ws, messages, closed } = connect();
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', v: PROTOCOL_VERSION, build: 'old-build' }));
    await closed;
    expect(messages).toEqual([{ t: 'reject', reason: 'version' }]);
  });

  it('accepts the build id of a rebuilt web directory without a restart', async () => {
    writeFileSync(join(dir, 'build-id.txt'), 'rebuilt\n');
    const { ws, messages } = connect();
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', v: PROTOCOL_VERSION, build: 'rebuilt' }));
    await until(() => messages.length >= 1);
    expect(messages[0]?.t).toBe('welcome');
  });

  it('plays a two-socket game: create, join, start, and a sealed frame', async () => {
    const hello = { t: 'hello', v: PROTOCOL_VERSION, build: 'build42' };
    const a = connect();
    const b = connect();
    await Promise.all([a, b].map((c) => new Promise((r) => c.ws.on('open', r))));
    a.ws.send(JSON.stringify(hello));
    b.ws.send(JSON.stringify(hello));
    await until(() => a.messages.length >= 2 && b.messages.length >= 2);
    a.ws.send(JSON.stringify({ t: 'create' }));
    await until(() => a.messages.some((m) => m.t === 'joined'));
    await until(() => b.messages.some((m) => m.t === 'lobby' && m.games.length === 1));
    b.ws.send(JSON.stringify({ t: 'join', game: 1 }));
    await until(() => a.messages.some((m) => m.t === 'start'));
    a.ws.send(JSON.stringify({ t: 'input', f: 0, b: 2 }));
    b.ws.send(JSON.stringify({ t: 'input', f: 0, b: 3 }));
    await until(
      () => a.messages.some((m) => m.t === 'frame') && b.messages.some((m) => m.t === 'frame'),
    );
    const fa = a.messages.find((m) => m.t === 'frame');
    expect(fa).toMatchObject({ t: 'frame', n: 0, in: [2, 3] });
    expect(b.messages.find((m) => m.t === 'frame')).toEqual(fa);
    expect(
      await fetch(url.replace('ws:', 'http:').replace('/ws', '/healthz')).then((r) => r.json()),
    ).toMatchObject({
      games: 1,
      players: 2,
    });
  });

  it('does not upgrade other paths', async () => {
    const { closed } = connect(url.replace('/ws', '/other'));
    await closed;
    const { closed: prefixed } = connect(url.replace('/ws', '/keen/other'));
    await prefixed;
  });

  it('upgrades under a path prefix', async () => {
    const { ws, messages } = connect(url.replace('/ws', '/keen/ws'));
    await new Promise((r) => ws.on('open', r));
    ws.send(JSON.stringify({ t: 'hello', v: PROTOCOL_VERSION, build: 'build42' }));
    await until(() => messages.length >= 2);
    expect(messages[0]?.t).toBe('welcome');
    expect(messages[1]?.t).toBe('lobby');
  });

  it('frees the seat when the socket closes', async () => {
    const hello = { t: 'hello', v: PROTOCOL_VERSION, build: 'build42' };
    const a = connect();
    await new Promise((r) => a.ws.on('open', r));
    a.ws.send(JSON.stringify(hello));
    a.ws.send(JSON.stringify({ t: 'create' }));
    await until(() => a.messages.some((m) => m.t === 'joined'));
    expect(server.hub.games.size).toBe(1);
    a.ws.close();
    await until(() => server.hub.games.size === 0);
  });
});
