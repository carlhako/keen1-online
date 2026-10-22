import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import type { Hub } from './hub.ts';
import { client, inputs, makeHub, record, send, startedGame, type TestConn } from './testing.ts';

let hub: Hub;

beforeEach(() => {
  vi.useFakeTimers();
  vi.setSystemTime(new Date('2026-05-01T09:00:00Z'));
});

afterEach(() => {
  hub?.close();
  vi.useRealTimers();
});

const b64 = (opts?: Parameters<typeof record>[0]): string => record(opts).toString('base64');

describe('saving', () => {
  it('stores the shared record under a name and tells the saver and the lobby', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    const watcher = client(hub);
    watcher.clear();
    inputs(hub, [a, b], 0, 3);
    a.clear();
    send(hub, b, { t: 'save', name: 'mars run', rec: b64({ score: 4200, done: [1, 2] }) });
    expect(b.last('saved')).toEqual({ t: 'saved', ok: true, id: 1 });
    expect(a.all('saved')).toHaveLength(0);
    expect(watcher.last('saves')?.saves).toEqual([
      { id: 1, name: 'mars run', date: '2026-05-01T09:00:01.000Z', levels: 2, score: 4200 },
    ]);
    // Players inside a game do not get the list.
    expect(a.all('saves')).toHaveLength(0);
  });

  it('shows the saves to a visitor who arrives later, newest first', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    send(hub, a, { t: 'save', name: 'first', rec: b64() });
    vi.advanceTimersByTime(5_000);
    send(hub, a, { t: 'save', name: 'second', rec: b64({ score: 50 }) });
    const late = client(hub);
    expect(late.last('saves')?.saves.map((s) => s.name)).toEqual(['second', 'first']);
  });

  it('refuses a save from a level', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    send(hub, a, { t: 'sum', f: 2, c: 1, l: 3 });
    expect(hub.games.get(1)?.loc).toEqual({ kind: 'level', level: 3 });
    send(hub, a, { t: 'save', name: 'in a level', rec: b64() });
    expect(a.last('saved')).toEqual({ t: 'saved', ok: false });
    expect(hub.saves.list()).toEqual([]);
  });

  it('refuses a save from someone who is not in a running game', () => {
    hub = makeHub();
    const lobby = client(hub);
    send(hub, lobby, { t: 'save', name: 'nowhere', rec: b64() });
    expect(lobby.last('saved')).toEqual({ t: 'saved', ok: false });
    const waiting = client(hub);
    send(hub, waiting, { t: 'create' });
    send(hub, waiting, { t: 'save', name: 'waiting', rec: b64() });
    expect(waiting.last('saved')).toEqual({ t: 'saved', ok: false });
    expect(hub.saves.list()).toEqual([]);
  });

  it('refuses a record that is not a Keen 1 game, storing nothing', () => {
    hub = makeHub();
    const [a] = startedGame(hub);
    const flags = record();
    flags.writeInt16LE(2, 18);
    send(hub, a, { t: 'save', name: 'flags', rec: flags.toString('base64') });
    send(hub, a, { t: 'save', name: 'short', rec: Buffer.alloc(40).toString('base64') });
    send(hub, a, { t: 'save', name: 'junk', rec: '!!!not base64!!!' });
    send(hub, a, { t: 'save', name: '', rec: b64() });
    expect(a.all('saved')).toEqual(Array(4).fill({ t: 'saved', ok: false }));
    expect(hub.saves.list()).toEqual([]);
  });

  it('keeps the game unchanged when a save fails', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    send(hub, a, { t: 'save', name: '', rec: b64() });
    expect(hub.games.get(1)?.phase).toBe('playing');
    expect(a.closed || b.closed).toBe(false);
  });
});

describe('loading', () => {
  function saved(opts?: Parameters<typeof record>[0]): void {
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    send(hub, a, { t: 'save', name: 'a save', rec: b64(opts) });
    send(hub, a, { t: 'leave' });
    send(hub, b, { t: 'leave' });
    vi.advanceTimersByTime(61_000); // an empty game is kept for a minute
    expect(hub.games.size).toBe(0);
  }

  it('creates a waiting game whose start carries the record', () => {
    hub = makeHub();
    saved({ score: 900, done: [1] });
    const c = client(hub);
    send(hub, c, { t: 'load', save: 1 });
    expect(c.last('joined')).toMatchObject({ seat: 0, phase: 'waiting' });
    expect(c.all('start')).toHaveLength(0);
    send(hub, c, { t: 'single' });
    expect(c.last('start')).toMatchObject({ mask: 1, rec: b64({ score: 900, done: [1] }) });
  });

  it('gives both players the record when the partner joins', () => {
    hub = makeHub();
    saved();
    const a = client(hub);
    send(hub, a, { t: 'load', save: 1 });
    const b = client(hub);
    send(hub, b, { t: 'join', game: a.last('joined')?.game as number });
    expect(a.last('start')?.rec).toBe(b64());
    expect(b.last('start')?.rec).toBe(b64());
  });

  it('gives a player who joins a loaded game after it started the record too', () => {
    hub = makeHub();
    saved({ score: 20 });
    const a = client(hub);
    send(hub, a, { t: 'load', save: 1 });
    send(hub, a, { t: 'single' });
    const b = client(hub);
    send(hub, b, { t: 'join', game: a.last('joined')?.game as number });
    expect(b.last('restore')).toMatchObject({ fresh: true, rec: b64({ score: 20 }) });
  });

  it('does not put a record in a new game', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    expect(a.last('start')?.rec).toBeUndefined();
    expect(b.last('start')?.rec).toBeUndefined();
  });

  it('counts a loaded game toward the limit and refuses past it', () => {
    hub = makeHub({ maxGames: 2 });
    saved();
    const first = client(hub);
    send(hub, first, { t: 'load', save: 1 });
    send(hub, client(hub), { t: 'create' });
    expect(hub.games.size).toBe(2);
    const late = client(hub);
    send(hub, late, { t: 'load', save: 1 });
    expect(late.last('reject')).toEqual({ t: 'reject', reason: 'full', count: 2, max: 2 });
    expect(hub.games.size).toBe(2);
  });

  it('refuses a save that does not exist, and a visitor already in a game', () => {
    hub = makeHub();
    saved();
    const c = client(hub);
    send(hub, c, { t: 'load', save: 42 });
    expect(c.last('reject')).toEqual({ t: 'reject', reason: 'nosave' });
    send(hub, c, { t: 'create' });
    send(hub, c, { t: 'load', save: 1 });
    expect(c.last('reject')).toEqual({ t: 'reject', reason: 'busy' });
  });

  it('can start several games from the same save', () => {
    hub = makeHub();
    saved();
    send(hub, client(hub), { t: 'load', save: 1 });
    send(hub, client(hub), { t: 'load', save: 1 });
    expect(hub.games.size).toBe(2);
  });

  it('survives a restart of the hub: saves are read back from disk', async () => {
    const { mkdtempSync, rmSync } = await import('node:fs');
    const { tmpdir } = await import('node:os');
    const { join } = await import('node:path');
    const dir = mkdtempSync(join(tmpdir(), 'keen-hub-'));
    try {
      hub = makeHub({}, { dataDir: dir });
      saved({ score: 5 });
      hub.close();
      hub = makeHub({}, { dataDir: dir });
      const c = client(hub);
      expect(c.last('saves')?.saves.map((s) => s.name)).toEqual(['a save']);
      send(hub, c, { t: 'load', save: 1 });
      expect(c.last('joined')).toBeDefined();
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  });
});

describe('the high score table after the ending', () => {
  /** A two-player game that reached the ending with this score. */
  function ended(score: number): [TestConn, TestConn] {
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 5);
    send(hub, a, { t: 'sum', f: 4, c: 1, l: 5, s: score });
    send(hub, b, { t: 'sum', f: 4, c: 1, l: 5, s: score });
    send(hub, a, { t: 'ending' });
    send(hub, b, { t: 'ending' });
    send(hub, a, { t: 'finished' });
    send(hub, b, { t: 'finished' });
    return [a, b];
  }

  it('asks player 1 for a team name when the score qualifies', () => {
    hub = makeHub();
    const [a, b] = ended(2000);
    expect(a.last('ended')).toEqual({ t: 'ended', entry: 0 });
    expect(b.last('ended')).toEqual({ t: 'ended', entry: 0 });
  });

  it('stores the entry, tells everybody, and then lets both see the table', () => {
    hub = makeHub();
    const watcher = client(hub);
    const [a, b] = ended(2000);
    watcher.clear();
    send(hub, a, { t: 'score', name: 'carl & sam', score: 2000, parts: 15 });
    const top = { score: 2000, name: 'carl & sam', parts: 15 };
    expect(hub.scores.table()[0]).toEqual(top);
    for (const c of [a, b, watcher]) expect(c.last('scores')?.rows[0]).toEqual(top);
    expect(a.last('ended')).toEqual({ t: 'ended', entry: -1 });
    expect(b.last('ended')).toEqual({ t: 'ended', entry: -1 });
    // The table arrives before the signal to show it.
    const order = b.messages.map((m) => m.t).filter((t) => t === 'scores' || t === 'ended');
    expect(order.slice(-2)).toEqual(['scores', 'ended']);
  });

  it('asks nobody when the score is too low', () => {
    hub = makeHub();
    const [a, b] = ended(100);
    expect(a.last('ended')).toEqual({ t: 'ended', entry: -1 });
    expect(b.last('ended')).toEqual({ t: 'ended', entry: -1 });
    send(hub, a, { t: 'score', name: 'late', score: 100, parts: 0 });
    expect(a.last('reject')).toEqual({ t: 'reject', reason: 'invalid' });
    expect(hub.scores.table()[0]?.name).toBe('Yorpy');
  });

  it('asks the only player of a single-player game, whichever seat it is', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    send(hub, a, { t: 'leave' });
    send(hub, b, { t: 'continue' });
    inputs(hub, [b], 3, 6);
    send(hub, b, { t: 'sum', f: 5, c: 1, l: 5, s: 900 });
    send(hub, b, { t: 'finished' });
    expect(b.last('ended')).toEqual({ t: 'ended', entry: 1 });
    send(hub, b, { t: 'score', name: 'solo', score: 900, parts: 15 });
    expect(hub.scores.table()[0]?.name).toBe('solo');
  });

  it('refuses a score from a game that never reached the ending', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    send(hub, a, { t: 'sum', f: 2, c: 1, l: 1, s: 5000 });
    send(hub, a, { t: 'score', name: 'cheat', score: 5000, parts: 15 });
    expect(a.last('reject')).toEqual({ t: 'reject', reason: 'invalid' });
    const lobby = client(hub);
    send(hub, lobby, { t: 'score', name: 'cheat', score: 5000, parts: 15 });
    expect(lobby.last('reject')).toEqual({ t: 'reject', reason: 'invalid' });
    expect(hub.scores.table()).toEqual(makeHub().scores.table());
  });

  it('refuses a score above the last one the game reported', () => {
    hub = makeHub();
    const [a] = ended(2000);
    send(hub, a, { t: 'score', name: 'greedy', score: 2001, parts: 15 });
    expect(a.last('reject')).toEqual({ t: 'reject', reason: 'invalid' });
    expect(hub.scores.table()[0]?.name).toBe('Yorpy');
    // The entry is still open for an honest one.
    send(hub, a, { t: 'score', name: 'honest', score: 1990, parts: 15 });
    expect(hub.scores.table()[0]).toEqual({ score: 1990, name: 'honest', parts: 15 });
  });

  it('refuses names that are empty, too long or not printable', () => {
    hub = makeHub();
    const [a] = ended(2000);
    for (const name of ['', 'thirteen char', 'tab\there', 'ünï']) {
      send(hub, a, { t: 'score', name, score: 2000, parts: 15 });
    }
    expect(a.all('reject').filter((r) => r.reason === 'invalid')).toHaveLength(4);
    expect(hub.scores.table()[0]?.name).toBe('Yorpy');
    send(hub, a, { t: 'score', name: 'twelve chars', score: 2000, parts: 15 });
    expect(hub.scores.table()[0]?.name).toBe('twelve chars');
  });

  it('refuses a name from the player who was not asked for it', () => {
    hub = makeHub();
    const [, b] = ended(2000);
    send(hub, b, { t: 'score', name: 'wrong one', score: 2000, parts: 15 });
    expect(b.last('reject')).toEqual({ t: 'reject', reason: 'invalid' });
    expect(hub.scores.table()[0]?.name).toBe('Yorpy');
  });

  it('refuses a second entry once the table is final', () => {
    hub = makeHub();
    const [a] = ended(2000);
    send(hub, a, { t: 'score', name: 'first', score: 2000, parts: 15 });
    send(hub, a, { t: 'score', name: 'second', score: 2000, parts: 15 });
    expect(a.last('reject')).toEqual({ t: 'reject', reason: 'invalid' });
    expect(hub.scores.table().filter((e) => e.score === 2000)).toHaveLength(1);
  });

  it('gives up on a name nobody types, and when its typist leaves', () => {
    hub = makeHub();
    const [a, b] = ended(2000);
    vi.advanceTimersByTime(121_000);
    expect(b.last('ended')).toEqual({ t: 'ended', entry: -1 });
    expect(a.last('ended')).toEqual({ t: 'ended', entry: -1 });
    hub.close();

    hub = makeHub();
    const [c, d] = ended(2000);
    hub.disconnect(c);
    expect(d.last('ended')).toEqual({ t: 'ended', entry: -1 });
    expect(hub.scores.table()[0]?.name).toBe('Yorpy');
  });

  it('keeps the finished game out of the way of newcomers until everybody left', () => {
    hub = makeHub();
    const [a, b] = ended(100);
    const late = client(hub);
    expect(late.last('lobby')?.games[0]).toMatchObject({ phase: 'scoring', open: [] });
    send(hub, a, { t: 'leave' });
    send(hub, late, { t: 'join', game: 1 });
    expect(late.last('reject')).toEqual({ t: 'reject', reason: 'closed' });
    send(hub, b, { t: 'leave' });
    expect(hub.games.size).toBe(0);
  });

  it('shows the same table to a visitor in the lobby', () => {
    hub = makeHub();
    const [a] = ended(2000);
    send(hub, a, { t: 'score', name: 'carl & sam', score: 2000, parts: 3 });
    const visitor = client(hub);
    expect(visitor.last('scores')?.rows[0]).toEqual({ score: 2000, name: 'carl & sam', parts: 3 });
    expect(visitor.last('scores')?.rows).toHaveLength(7);
  });
});
