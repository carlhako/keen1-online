import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { client, inputs, makeHub, send, startedGame, type TestConn } from './testing.ts';
import { encodeRestore, INPUT_HORIZON, UP_CHECKPOINT, UP_RESYNC } from './protocol.ts';
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

/** Browser to server snapshot upload. */
function upload(
  hubArg: Hub,
  c: TestConn,
  kind: number,
  frame: number,
  sum: number,
  data: number[],
) {
  const head = Buffer.alloc(9);
  head[0] = kind;
  head.writeUInt32LE(frame, 1);
  head.writeUInt32LE(sum, 5);
  hubArg.binary(c, Buffer.concat([head, Buffer.from(data)]));
}

describe('sealing frames', () => {
  it('seals a frame once every seat has sent its input and sends both inputs to both', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    send(hub, a, { t: 'input', f: 0, b: 2 });
    expect(frames(a)).toHaveLength(0);
    send(hub, b, { t: 'input', f: 0, b: 0x13 });
    expect(frames(a)).toHaveLength(1);
    expect(frames(a)[0]).toEqual({
      t: 'frame',
      n: 0,
      in: [2, 0x13],
      ev: [{ t: 'start', seed: a.last('start')?.seed, mask: 3 }],
    });
    expect(frames(b)[0]).toEqual(frames(a)[0]);
  });

  it('seals in frame order even when inputs arrive ahead and out of order', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    for (const f of [2, 0, 1]) send(hub, a, { t: 'input', f, b: f });
    expect(frames(a)).toHaveLength(0);
    for (const f of [1, 2, 0]) send(hub, b, { t: 'input', f, b: 3 + f });
    expect(frames(a).map((m) => m.n)).toEqual([0, 1, 2]);
    expect(frames(a).map((m) => m.in)).toEqual([
      [0, 3],
      [1, 4],
      [2, 5],
    ]);
  });

  it('holds a frame while one input is missing', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 3);
    expect(frames(a)).toHaveLength(3);
    send(hub, a, { t: 'input', f: 3, b: 8 });
    send(hub, a, { t: 'input', f: 4, b: 8 });
    expect(frames(a)).toHaveLength(3);
    send(hub, b, { t: 'input', f: 3, b: 8 });
    expect(frames(a)).toHaveLength(4);
  });

  it('only waits for the one seat in a one-player game, filling the other with neutral input', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    send(hub, a, { t: 'single' });
    send(hub, a, { t: 'input', f: 0, b: 4 });
    expect(frames(a)[0]?.in).toEqual([4, 8]);
  });
});

describe('input validation', () => {
  it('drops inputs for frames already sealed', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 2);
    send(hub, a, { t: 'input', f: 1, b: 5 });
    send(hub, a, { t: 'input', f: 2, b: 5 });
    send(hub, b, { t: 'input', f: 2, b: 8 });
    expect(frames(a)[2]?.in).toEqual([5, 8]);
    // The old input did not change anything that was sealed.
    expect(frames(a)[1]?.in).toEqual([8, 8]);
  });

  it('drops inputs more than 120 frames ahead', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    send(hub, a, { t: 'input', f: INPUT_HORIZON + 1, b: 5 });
    send(hub, a, { t: 'input', f: INPUT_HORIZON, b: 6 });
    inputs(hub, [a, b], 0, INPUT_HORIZON);
    // Frame 120 was accepted, 121 was not: both seats can only seal up to 119 until b sends 120.
    expect(frames(a)).toHaveLength(INPUT_HORIZON);
    send(hub, b, { t: 'input', f: INPUT_HORIZON, b: 8 });
    expect(frames(a)).toHaveLength(INPUT_HORIZON + 1);
    expect(frames(a)[INPUT_HORIZON]?.in).toEqual([6, 8]);
  });

  it('keeps the first input for a frame', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    send(hub, a, { t: 'input', f: 0, b: 1 });
    send(hub, a, { t: 'input', f: 0, b: 2 });
    send(hub, b, { t: 'input', f: 0, b: 8 });
    expect(frames(a)[0]?.in).toEqual([1, 8]);
  });

  it('relays at most 60 input messages per second from one connection', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    for (let f = 0; f < 100; f++) send(hub, a, { t: 'input', f, b: 8 });
    for (let f = 0; f < 100; f++) send(hub, b, { t: 'input', f, b: 8 });
    // Each connection got 60 through; the other 40 were dropped.
    expect(frames(a)).toHaveLength(60);
    // A second later the next ones count again.
    vi.advanceTimersByTime(1000);
    send(hub, a, { t: 'input', f: 60, b: 8 });
    send(hub, b, { t: 'input', f: 60, b: 8 });
    expect(frames(a)).toHaveLength(61);
  });

  it('does not let a flooding player disturb the other game', () => {
    hub = makeHub();
    const [a1, b1] = startedGame(hub);
    const [a2, b2] = startedGame(hub);
    for (let f = 0; f < 200; f++) send(hub, a1, { t: 'input', f: f % 100, b: 8 });
    inputs(hub, [a2, b2], 0, 5);
    expect(frames(a2)).toHaveLength(5);
    expect(frames(b1)).toHaveLength(0);
  });

  it('treats malformed input as malformed, not as an input', () => {
    hub = makeHub();
    const [a] = startedGame(hub);
    hub.message(a, JSON.stringify({ t: 'input', f: 0, b: 1 << 11 }));
    hub.message(a, JSON.stringify({ t: 'input', f: 0, b: 9 }));
    hub.message(a, JSON.stringify({ t: 'input', f: 1.5, b: 1 }));
    expect(a.closed).toBe(false);
    hub.message(a, '[]');
    hub.message(a, '"x"');
    expect(a.closed).toBe(true);
  });

  it('ignores input from a connection that is not in a game', () => {
    hub = makeHub();
    const c = client(hub);
    send(hub, c, { t: 'input', f: 0, b: 8 });
    expect(c.closed).toBe(false);
  });
});

describe('checksums and desync', () => {
  it('compares checksums and asks the longest-connected player for a snapshot on a mismatch', () => {
    const log = vi.fn();
    hub = makeHub({}, { log });
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 25);
    send(hub, a, { t: 'sum', f: 24, c: 1000, l: 0 });
    send(hub, b, { t: 'sum', f: 24, c: 1000, l: 0 });
    expect(a.all('snap')).toHaveLength(0);
    // Frame 24 was compared above; now a mismatch at the next report.
    inputs(hub, [a, b], 25, 49);
    send(hub, a, { t: 'sum', f: 48, c: 77, l: 0 });
    send(hub, b, { t: 'sum', f: 48, c: 78, l: 0 });
    expect(a.all('snap')).toHaveLength(1);
    expect(b.all('snap')).toHaveLength(0);
    expect(log).toHaveBeenCalledWith(expect.stringMatching(/desync at frame 48/));
  });

  it('repairs the drifted browser from the reference snapshot and the frames since', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 50);
    send(hub, a, { t: 'sum', f: 48, c: 1, l: 0 });
    send(hub, b, { t: 'sum', f: 48, c: 2, l: 0 });
    b.clear();
    upload(hub, a, UP_RESYNC, 49, 9, [1, 2, 3]);
    expect(b.last('restore')).toEqual({
      t: 'restore',
      f: 49,
      fresh: false,
      seed: a.last('start')?.seed,
      mask: 3,
    });
    expect(b.binaries[0]).toEqual(encodeRestore(49, Buffer.from([1, 2, 3])));
    expect(b.all('frame').map((m) => m.n)).toEqual([49]);
    // The reference keeps playing undisturbed.
    expect(a.all('restore')).toHaveLength(0);
  });

  it("asks only once per drift and ignores the drifted browser's old sums afterwards", () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 100);
    send(hub, a, { t: 'sum', f: 48, c: 1, l: 0 });
    send(hub, b, { t: 'sum', f: 48, c: 2, l: 0 });
    send(hub, a, { t: 'sum', f: 72, c: 3, l: 0 });
    send(hub, b, { t: 'sum', f: 72, c: 4, l: 0 });
    expect(a.all('snap')).toHaveLength(1);
    upload(hub, a, UP_RESYNC, 90, 9, [7]);
    // A stale report from b (it simulated on its own state before the restore arrived).
    send(hub, a, { t: 'sum', f: 96, c: 5, l: 0 });
    send(hub, b, { t: 'sum', f: 96, c: 6, l: 0 });
    expect(a.all('snap')).toHaveLength(1);
  });
});

describe('checkpoints', () => {
  it('keeps exactly one checkpoint and the frames sealed since', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 240);
    // Both players agree on frame 240's state; the longest-connected one uploads it.
    send(hub, a, { t: 'sum', f: 240, c: 77, l: 0 });
    send(hub, b, { t: 'sum', f: 240, c: 77, l: 0 });
    upload(hub, a, UP_CHECKPOINT, 240, 77, [9, 9, 9]);
    inputs(hub, [a, b], 240, 250);
    const game = hub.games.get(1)!;
    expect(game.checkpoint?.frame).toBe(240);
    expect(game.checkpoint?.data).toEqual(Buffer.from([9, 9, 9]));
    expect(game.log.map((f) => f.n)).toEqual(Array.from({ length: 10 }, (_, i) => 240 + i));
    // A newer checkpoint replaces it.
    inputs(hub, [a, b], 250, 481);
    send(hub, a, { t: 'sum', f: 480, c: 5, l: 0 });
    send(hub, b, { t: 'sum', f: 480, c: 5, l: 0 });
    upload(hub, b, UP_CHECKPOINT, 480, 5, [4]);
    expect(game.checkpoint?.frame).toBe(480);
    expect(game.log[0]?.n).toBe(480);
  });

  it('does not accept a checkpoint the players have not agreed on', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 25);
    send(hub, a, { t: 'sum', f: 24, c: 5, l: 0 });
    upload(hub, a, UP_CHECKPOINT, 24, 5, [1]);
    expect(hub.games.get(1)?.checkpoint).toBeNull();
    send(hub, b, { t: 'sum', f: 24, c: 6, l: 0 });
    expect(hub.games.get(1)?.checkpoint).toBeNull();
  });

  it("accepts the upload before or after the partner's report", () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    inputs(hub, [a, b], 0, 25);
    send(hub, b, { t: 'sum', f: 24, c: 5, l: 0 });
    send(hub, a, { t: 'sum', f: 24, c: 5, l: 0 });
    upload(hub, a, UP_CHECKPOINT, 24, 5, [1]);
    expect(hub.games.get(1)?.checkpoint?.frame).toBe(24);
  });

  it('refuses a checkpoint from the future and an empty one', () => {
    hub = makeHub();
    const a = client(hub);
    send(hub, a, { t: 'create' });
    send(hub, a, { t: 'single' });
    inputs(hub, [a], 0, 3);
    send(hub, a, { t: 'sum', f: 24, c: 5, l: 0 });
    upload(hub, a, UP_CHECKPOINT, 24, 5, [1]);
    expect(hub.games.get(1)?.checkpoint).toBeNull();
    inputs(hub, [a], 3, 30);
    upload(hub, a, UP_CHECKPOINT, 24, 5, []);
    expect(hub.games.get(1)?.checkpoint).toBeNull();
  });

  it('tracks where the game is from the reports', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    const watcher = client(hub);
    inputs(hub, [a, b], 0, 30);
    send(hub, a, { t: 'sum', f: 24, c: 5, l: 6 });
    expect(watcher.last('lobby')?.games[0]?.loc).toEqual({ kind: 'level', level: 6 });
    send(hub, a, { t: 'sum', f: 29, c: 5, l: 0 });
    expect(watcher.last('lobby')?.games[0]?.loc).toEqual({ kind: 'map' });
  });
});

describe('checkpoint duty', () => {
  it('goes to the player who has been in the game longest, and passes on when they leave', () => {
    hub = makeHub();
    const [a, b] = startedGame(hub);
    expect(a.last('duty')).toEqual({ t: 'duty', on: true });
    expect(b.last('duty')).toBeUndefined();
    hub.disconnect(a);
    expect(b.last('duty')).toEqual({ t: 'duty', on: true });
  });

  it('does not go to a newcomer who is still catching up', () => {
    hub = makeHub();
    const solo = client(hub);
    send(hub, solo, { t: 'create' });
    send(hub, solo, { t: 'single' });
    const joiner = client(hub);
    send(hub, joiner, { t: 'join', game: 1 });
    expect(joiner.last('duty')).toBeUndefined();
    expect(solo.last('duty')).toEqual({ t: 'duty', on: true });
  });
});
