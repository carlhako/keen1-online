import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { client, makeHub, send, startedGame, TestConn, BUILD } from './testing.ts';
import { PROTOCOL_VERSION } from './protocol.ts';
import type { Hub } from './hub.ts';

let hub: Hub;

beforeEach(() => {
  vi.useFakeTimers();
});

afterEach(() => {
  hub?.close();
  vi.useRealTimers();
});

describe('handshake', () => {
  it('welcomes a matching hello and sends the lobby', () => {
    hub = makeHub();
    const c = client(hub);
    expect(c.messages[0]).toEqual({
      t: 'welcome',
      v: PROTOCOL_VERSION,
      cfg: { checkpointFrames: 240, inputLeadMax: 6, disconnectSec: 15 },
    });
    expect(c.messages[1]).toEqual({ t: 'lobby', games: [], count: 0, max: 5 });
    expect(c.closed).toBe(false);
  });

  it('rejects a wrong protocol version and closes', () => {
    hub = makeHub();
    const c = new TestConn();
    hub.connect(c);
    send(hub, c, { t: 'hello', v: PROTOCOL_VERSION + 1, build: BUILD });
    expect(c.messages).toEqual([{ t: 'reject', reason: 'version' }]);
    expect(c.closed).toBe(true);
  });

  it('rejects a wrong build id and closes', () => {
    hub = makeHub();
    const c = new TestConn();
    hub.connect(c);
    send(hub, c, { t: 'hello', v: PROTOCOL_VERSION, build: 'older' });
    expect(c.messages).toEqual([{ t: 'reject', reason: 'version' }]);
    expect(c.closed).toBe(true);
  });

  it('closes a connection that talks before hello', () => {
    hub = makeHub();
    const c = new TestConn();
    hub.connect(c);
    send(hub, c, { t: 'create' });
    expect(c.closed).toBe(true);
    expect(hub.games.size).toBe(0);
  });

  it('closes a connection that keeps sending malformed messages', () => {
    hub = makeHub();
    const c = client(hub);
    for (let i = 0; i < 4; i++) hub.message(c, '{nonsense');
    expect(c.closed).toBe(false);
    hub.message(c, JSON.stringify({ t: 'input', f: -1, b: 3 }));
    expect(c.closed).toBe(true);
  });

  it('answers pings', () => {
    hub = makeHub();
    const c = client(hub);
    send(hub, c, { t: 'ping', c: 1234 });
    expect(c.last('pong')).toEqual({ t: 'pong', c: 1234 });
  });
});

describe('lobby', () => {
  it('lists a new game as waiting, to everyone in the lobby', () => {
    hub = makeHub();
    const a = client(hub);
    const watcher = client(hub);
    watcher.clear();
    send(hub, a, { t: 'create' });
    expect(a.last('joined')).toMatchObject({ game: 1, seat: 0, phase: 'waiting' });
    expect(watcher.last('lobby')?.games).toEqual([
      { id: 1, seats: 1, open: [1], phase: 'waiting', loc: { kind: 'waiting' } },
    ]);
    // The creator is in its game and no longer gets the list.
    expect(a.all('lobby')).toHaveLength(1);
  });

  it('allows 5 games and refuses a sixth', () => {
    hub = makeHub();
    for (let i = 0; i < 5; i++) send(hub, client(hub), { t: 'create' });
    expect(hub.games.size).toBe(5);
    const sixth = client(hub);
    send(hub, sixth, { t: 'create' });
    expect(sixth.last('reject')).toEqual({ t: 'reject', reason: 'full', count: 5, max: 5 });
    expect(hub.games.size).toBe(5);
    expect(sixth.closed).toBe(false);
  });

  it('still lets visitors join a game with an open seat at the limit', () => {
    hub = makeHub();
    const first = client(hub);
    send(hub, first, { t: 'create' });
    for (let i = 0; i < 4; i++) send(hub, client(hub), { t: 'create' });
    const joiner = client(hub);
    send(hub, joiner, { t: 'join', game: 1 });
    expect(joiner.last('joined')).toMatchObject({ game: 1, seat: 1 });
  });

  it('does not let anybody join a game with 2 players', () => {
    hub = makeHub();
    startedGame(hub);
    const late = client(hub);
    send(hub, late, { t: 'join', game: 1 });
    expect(late.last('reject')).toEqual({ t: 'reject', reason: 'closed' });
    const lobby = late.last('lobby');
    expect(lobby?.games[0]).toMatchObject({ seats: 2, open: [] });
  });

  it('refuses a game that does not exist', () => {
    hub = makeHub();
    const c = client(hub);
    send(hub, c, { t: 'join', game: 99 });
    expect(c.last('reject')).toEqual({ t: 'reject', reason: 'nogame' });
  });

  it('refuses a second create while already in a game', () => {
    hub = makeHub();
    const c = client(hub);
    send(hub, c, { t: 'create' });
    send(hub, c, { t: 'create' });
    expect(c.last('reject')).toEqual({ t: 'reject', reason: 'busy' });
    expect(hub.games.size).toBe(1);
  });

  it('frees a slot when a waiting player leaves, and the retry works', () => {
    hub = makeHub({ maxGames: 1 });
    const a = client(hub);
    send(hub, a, { t: 'create' });
    const b = client(hub);
    send(hub, b, { t: 'create' });
    expect(b.last('reject')?.reason).toBe('full');
    send(hub, a, { t: 'leave' });
    expect(a.last('left')).toBeDefined();
    expect(hub.games.size).toBe(0);
    send(hub, b, { t: 'create' });
    expect(b.last('joined')).toMatchObject({ game: 2, seat: 0 });
  });

  it('sends the lobby to a client that left its game', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    a.clear();
    send(hub, a, { t: 'leave' });
    expect(a.last('left')).toBeDefined();
    expect(a.last('lobby')?.games[0]).toMatchObject({ id: 1, seats: 1, open: [0] });
    expect(b.last('paused')).toEqual({ t: 'paused', seats: [0] });
  });
});

describe('starting a game', () => {
  it('starts with the second player and gives both the same seed', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    expect(a.all('start')).toHaveLength(0);
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    const sa = a.last('start');
    const sb = b.last('start');
    expect(sa).toBeDefined();
    expect(sa).toEqual(sb);
    expect(sa?.mask).toBe(3);
    expect(b.last('joined')).toMatchObject({ seat: 1 });
  });

  it('starts one-player when Play single player is chosen, leaving the seat open', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    send(hub, a, { t: 'single' });
    expect(a.last('start')).toMatchObject({ mask: 1 });
    const watcher = client(hub);
    expect(watcher.last('lobby')?.games[0]).toMatchObject({
      seats: 1,
      open: [1],
      phase: 'playing',
      loc: { kind: 'map' },
    });
  });

  it('ignores Play single player from anyone but the waiting first player', () => {
    hub = makeHub();
    const [a] = startedGame(hub);
    a.clear();
    send(hub, a, { t: 'single' });
    expect(a.all('start')).toHaveLength(0);
  });

  it('removes a waiting game when its player closes the connection', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    hub.disconnect(a);
    expect(hub.games.size).toBe(0);
  });
});

describe('chat', () => {
  it('passes a line to everybody in the game, the sender too, and not to the lobby', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    const watcher = client(hub);
    send(hub, b, { t: 'chat', text: 'over here' });
    expect(a.last('chat')).toEqual({ t: 'chat', seat: 1, text: 'over here' });
    expect(b.last('chat')).toEqual({ t: 'chat', seat: 1, text: 'over here' });
    expect(watcher.all('chat')).toEqual([]);
  });

  it('ignores a line from somebody in the lobby', () => {
    hub = makeHub();
    const [a] = startedGame(hub);
    const loner = client(hub);
    send(hub, loner, { t: 'chat', text: 'hello?' });
    expect(a.all('chat')).toEqual([]);
    expect(loner.closed).toBe(false);
  });

  it('refuses lines that are too long or not printable', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    send(hub, a, { t: 'chat', text: 'x'.repeat(33) });
    send(hub, a, { t: 'chat', text: 'tab\there' });
    send(hub, a, { t: 'chat', text: '' });
    expect(b.all('chat')).toEqual([]);
  });

  it('drops lines beyond five in ten seconds', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    for (let i = 0; i < 7; i++) send(hub, a, { t: 'chat', text: `line ${i}` });
    expect(b.all('chat').map((m) => m.text)).toEqual([
      'line 0',
      'line 1',
      'line 2',
      'line 3',
      'line 4',
    ]);
    vi.advanceTimersByTime(10_000);
    send(hub, a, { t: 'chat', text: 'later' });
    expect(b.last('chat')?.text).toBe('later');
  });
});
