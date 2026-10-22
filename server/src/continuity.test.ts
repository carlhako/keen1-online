import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { client, inputs, makeHub, send, startedGame, type TestConn } from './testing.ts';
import { GOLIVE_GUARD } from './game.ts';
import { UP_CHECKPOINT } from './protocol.ts';
import type { Hub } from './hub.ts';

let hub: Hub;

beforeEach(() => {
  vi.useFakeTimers();
});

afterEach(() => {
  hub?.close();
  vi.useRealTimers();
});

const frames = (c: TestConn) => c.all('frame');

function upload(c: TestConn, frame: number, sum: number, data: number[]) {
  const head = Buffer.alloc(9);
  head[0] = UP_CHECKPOINT;
  head.writeUInt32LE(frame, 1);
  head.writeUInt32LE(sum, 5);
  hub.binary(c, Buffer.concat([head, Buffer.from(data)]));
}

describe('disconnects', () => {
  it('pauses the other player when a connection closes', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 10);
    a.clear();
    hub.disconnect(b);
    expect(a.last('paused')).toEqual({ t: 'paused', seats: [1] });
    // The game stops sealing.
    send(hub, a, { t: 'input', f: 10, b: 8 });
    expect(frames(a)).toHaveLength(0);
    const watcher = client(hub);
    expect(watcher.last('lobby')?.games[0]).toMatchObject({
      seats: 1,
      open: [1],
      phase: 'paused',
    });
  });

  it('drops a seat that sent no input for 15 seconds, and only the one being waited for', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 5);
    a.clear();
    b.clear();
    // a sent input for frame 5; b did not. The game waits for b.
    send(hub, a, { t: 'input', f: 5, b: 8 });
    vi.advanceTimersByTime(14_000);
    expect(a.all('paused')).toHaveLength(0);
    vi.advanceTimersByTime(2_000);
    expect(a.last('paused')).toEqual({ t: 'paused', seats: [1] });
    expect(b.last('left')).toBeDefined();
  });

  it('does not drop a player who is merely waiting for a slow partner', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 5);
    // b stalls (a hidden tab) but still sends an input about once a second.
    for (let f = 5; f < 30; f++) {
      vi.advanceTimersByTime(1000);
      send(hub, a, { t: 'input', f, b: 8 });
      send(hub, b, { t: 'input', f, b: 8 });
    }
    expect(a.all('paused')).toHaveLength(0);
    expect(frames(a)).toHaveLength(30);
  });

  it("resumes by itself when a stalled player's inputs come again within 15 seconds", () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 5);
    send(hub, a, { t: 'input', f: 5, b: 8 });
    vi.advanceTimersByTime(5_000);
    expect(frames(a)).toHaveLength(5);
    send(hub, b, { t: 'input', f: 5, b: 8 });
    expect(frames(a)).toHaveLength(6);
    expect(a.all('paused')).toHaveLength(0);
  });
});

describe('Continue single player', () => {
  it('removes the departed Keen and goes on with one player', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 10);
    hub.disconnect(b);
    send(hub, a, { t: 'input', f: 10, b: 8 });
    send(hub, a, { t: 'continue' });
    expect(a.last('resumed')).toBeDefined();
    const f = frames(a).at(-1);
    expect(f).toMatchObject({ n: 10, ev: [{ t: 'remove', seat: 1 }] });
    // From now on the game only waits for player 1.
    send(hub, a, { t: 'input', f: 11, b: 3 });
    expect(frames(a).at(-1)).toMatchObject({ n: 11, in: [3, 8] });
    const watcher = client(hub);
    expect(watcher.last('lobby')?.games[0]).toMatchObject({ seats: 1, phase: 'playing' });
  });

  it('ignores Continue when nobody is missing', () => {
    hub = makeHub();
    const [a] = startedGame(hub);
    a.clear();
    send(hub, a, { t: 'continue' });
    expect(a.messages).toEqual([]);
  });
});

describe('joining a running game', () => {
  /** A solo game of player 1 at frame 40 with a checkpoint at frame 24. */
  function soloGame() {
    const a = client(hub);
    send(hub, a, { t: 'create' });
    send(hub, a, { t: 'single' });
    inputs(hub, [a], 0, 40);
    send(hub, a, { t: 'sum', f: 24, c: 9, l: 0 });
    upload(a, 24, 9, [1, 2, 3]);
    return a;
  }

  it('catches the joiner up from the checkpoint and the frames since', () => {
    hub = makeHub();
    const a = soloGame();
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    expect(b.last('joined')).toMatchObject({ seat: 1, phase: 'playing' });
    expect(b.last('restore')).toMatchObject({ f: 24, fresh: false });
    expect(b.binaries).toHaveLength(1);
    expect(b.binaries[0]?.readUInt32LE(1)).toBe(24);
    expect(b.all('frame').map((m) => m.n)).toEqual(Array.from({ length: 16 }, (_, i) => 24 + i));
    // The joiner is not in the game yet: the solo player keeps going.
    send(hub, a, { t: 'input', f: 40, b: 8 });
    expect(frames(b).at(-1)?.n).toBe(40);
  });

  it('catches up from the start when there is no checkpoint yet', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    send(hub, a, { t: 'single' });
    inputs(hub, [a], 0, 5);
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    expect(b.last('restore')).toMatchObject({ f: 0, fresh: true, mask: 1 });
    expect(b.binaries).toHaveLength(0);
    expect(frames(b).map((m) => m.n)).toEqual([0, 1, 2, 3, 4]);
  });

  it('adds the player at an agreed frame without stopping the first player', () => {
    hub = makeHub();
    const a = soloGame();
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    send(hub, b, { t: 'ready', f: 40 });
    const from = 40 + GOLIVE_GUARD;
    expect(b.last('golive')).toEqual({ t: 'golive', from });
    // Frames before `from` seal on player 1's input alone.
    inputs(hub, [a], 40, from);
    expect(frames(a).at(-1)?.n).toBe(from - 1);
    // At `from` the game waits for the newcomer's input too.
    send(hub, a, { t: 'input', f: from, b: 8 });
    expect(frames(a).at(-1)?.n).toBe(from - 1);
    send(hub, b, { t: 'input', f: from, b: 5 });
    expect(frames(a).at(-1)).toMatchObject({ n: from, in: [8, 5], ev: [{ t: 'add', seat: 1 }] });
    expect(frames(b).at(-1)).toEqual(frames(a).at(-1));
    const watcher = client(hub);
    expect(watcher.last('lobby')?.games[0]).toMatchObject({ seats: 2, open: [] });
  });

  it('shows 2/2 as soon as the joiner takes the seat', () => {
    hub = makeHub();
    soloGame();
    const watcher = client(hub);
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    expect(watcher.last('lobby')?.games[0]).toMatchObject({ seats: 2, open: [] });
  });

  it('resumes a paused game with both players when someone joins', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 10);
    hub.disconnect(b);
    const c = client(hub);
    send(hub, c, { t: 'join', game: 1 });
    expect(c.last('restore')).toMatchObject({ f: 0, fresh: true, mask: 3 });
    a.clear();
    send(hub, c, { t: 'ready', f: 10 });
    expect(c.last('golive')).toEqual({ t: 'golive', from: 10 });
    expect(a.last('resumed')).toBeDefined();
    // The seat's old Keen is removed and the new one added in the same frame.
    send(hub, a, { t: 'input', f: 10, b: 8 });
    send(hub, c, { t: 'input', f: 10, b: 8 });
    expect(frames(a).at(-1)).toMatchObject({
      n: 10,
      ev: [
        { t: 'remove', seat: 1 },
        { t: 'add', seat: 1 },
      ],
    });
  });

  it('drops a newcomer who vanishes before appearing, without disturbing the game', () => {
    hub = makeHub();
    const a = soloGame();
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    send(hub, b, { t: 'ready', f: 40 });
    hub.disconnect(b);
    inputs(hub, [a], 40, 60);
    expect(frames(a).at(-1)?.n).toBe(59);
    expect(
      frames(a)
        .flatMap((m) => m.ev ?? [])
        .filter((e) => e.t === 'add'),
    ).toHaveLength(0);
    expect(a.all('paused')).toHaveLength(0);
  });

  it('drops a newcomer that stops answering while catching up', () => {
    hub = makeHub();
    const a = soloGame();
    const b = client(hub);
    send(hub, b, { t: 'join', game: 1 });
    for (let f = 40; f < 56; f++) {
      vi.advanceTimersByTime(1000);
      send(hub, a, { t: 'input', f, b: 8 });
    }
    expect(b.last('left')).toBeDefined();
    expect(hub.games.get(1)?.openSeats()).toEqual([1]);
    expect(a.all('paused')).toHaveLength(0);
  });
});

describe('rejoin', () => {
  it('gives a returning player the same seat through the token', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    const token = b.last('joined')?.token as string;
    inputs(hub, [a, b], 0, 3);
    hub.disconnect(a);
    hub.disconnect(b);
    const back = client(hub);
    send(hub, back, { t: 'join', game: 1, token });
    expect(back.last('joined')).toMatchObject({ seat: 1 });
    expect(back.last('joined')?.token).not.toBe(token);
  });

  it('gives somebody else the open seat when they have no token', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    hub.disconnect(b);
    const other = client(hub);
    send(hub, other, { t: 'join', game: 1, token: 'not-a-token' });
    expect(other.last('joined')).toMatchObject({ seat: 1 });
  });
});

describe('empty games', () => {
  it('keeps an empty game for 60 seconds, listed as 0/2 and joinable', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    hub.disconnect(a);
    hub.disconnect(b);
    expect(hub.games.size).toBe(1);
    vi.advanceTimersByTime(59_000);
    const watcher = client(hub);
    expect(watcher.last('lobby')?.games[0]).toMatchObject({
      seats: 0,
      open: [0, 1],
      phase: 'empty',
    });
    vi.advanceTimersByTime(2_000);
    expect(hub.games.size).toBe(0);
    expect(watcher.last('lobby')?.games).toEqual([]);
  });

  it('lets one player return within the minute and play on, alone', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 10);
    hub.disconnect(a);
    hub.disconnect(b);
    vi.advanceTimersByTime(30_000);
    const back = client(hub);
    send(hub, back, { t: 'join', game: 1 });
    send(hub, back, { t: 'ready', f: 10 });
    send(hub, back, { t: 'input', f: 10, b: 8 });
    // Both old Keens go, and the returning player's appears: they are alone in the game.
    expect(frames(back).at(-1)).toMatchObject({
      n: 10,
      ev: [
        { t: 'remove', seat: 1 },
        { t: 'remove', seat: 0 },
        { t: 'add', seat: 0 },
      ],
    });
    // Playing on, the game is not removed after the minute it would have lasted empty.
    for (let f = 11; f < 80; f++) {
      vi.advanceTimersByTime(1000);
      send(hub, back, { t: 'input', f, b: 8 });
    }
    expect(hub.games.size).toBe(1);
  });

  it('no longer counts a removed game toward the limit', () => {
    hub = makeHub({ maxGames: 1 });
    const [a, b] = startedGame(hub);
    hub.disconnect(a);
    hub.disconnect(b);
    const c = client(hub);
    send(hub, c, { t: 'create' });
    expect(c.last('reject')?.reason).toBe('full');
    vi.advanceTimersByTime(61_000);
    send(hub, c, { t: 'create' });
    expect(c.last('joined')).toBeDefined();
  });

  it('uses the configured time', () => {
    hub = makeHub({ emptyGameSec: 5 });
    const [a, b] = startedGame(hub);
    hub.disconnect(a);
    hub.disconnect(b);
    vi.advanceTimersByTime(6_000);
    expect(hub.games.size).toBe(0);
  });
});

describe('leaving and ending', () => {
  it('opens the seat when a player leaves from the menu, like a disconnect', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    send(hub, b, { t: 'leave' });
    expect(a.last('paused')).toEqual({ t: 'paused', seats: [1] });
    expect(b.last('left')).toBeDefined();
  });

  it('moves to the table after both players finished the ending, and goes when they leave', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    send(hub, a, { t: 'ending' });
    send(hub, b, { t: 'ending' });
    // Nobody times out while the ending plays.
    vi.advanceTimersByTime(60_000);
    expect(hub.games.size).toBe(1);
    send(hub, a, { t: 'finished' });
    expect(a.last('ended')).toBeUndefined();
    send(hub, b, { t: 'finished' });
    // The score is too low for the table: no name is asked for.
    expect(a.last('ended')).toEqual({ t: 'ended', entry: -1 });
    expect(b.last('ended')).toEqual({ t: 'ended', entry: -1 });
    expect(hub.games.size).toBe(1);
    send(hub, a, { t: 'leave' });
    expect(hub.games.size).toBe(1);
    send(hub, b, { t: 'leave' });
    expect(hub.games.size).toBe(0);
  });

  it('ends a one-player game when its player finishes and leaves', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    send(hub, a, { t: 'single' });
    send(hub, a, { t: 'finished' });
    expect(a.last('ended')).toEqual({ t: 'ended', entry: -1 });
    send(hub, a, { t: 'leave' });
    expect(hub.games.size).toBe(0);
  });
});
