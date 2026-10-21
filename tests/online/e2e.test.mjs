// End-to-end tests: the real server, and two or three headless game clients (bot.mjs) playing
// through real WebSockets. Needs `make nodenet`, `npm run build` in server/ and the game data.
//
//   node --test tests/online/
import assert from 'node:assert/strict';
import { mkdirSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { after, afterEach, beforeEach, describe, it } from 'node:test';
import { Bot, commonSums, dataDir, root, sleep, tmpDir, until, readText } from './harness.mjs';
import { existsSync } from 'node:fs';
import { delayProxy } from './delay-proxy.mjs';

const { GameServer } = await import(join(root, 'server/dist/server.js'));
const { defaultConfig } = await import(join(root, 'server/dist/config.js'));

const RIGHT = 2;
const JUMP = 16;

let tmp, server, url, bots, logs;
let overrides = {};

beforeEach(async () => {
  overrides = {};
  tmp = tmpDir();
  const web = join(tmp.dir, 'web');
  mkdirSync(web);
  writeFileSync(join(web, 'index.html'), '<!doctype html>');
  writeFileSync(join(web, 'build-id.txt'), 'e2e\n');
  logs = [];
  bots = [];
});

/** Starts the server (tests change the configuration first). */
async function startServer(config = {}) {
  server = new GameServer({
    config: { ...defaultConfig(), port: 0, ...config },
    webDir: join(tmp.dir, 'web'),
    frameLogDir: join(tmp.dir, 'logs'),
    log: (m) => logs.push(m),
  });
  url = `http://127.0.0.1:${await server.listen()}`;
}

afterEach(async () => {
  await Promise.all(bots.map((b) => b.close()));
  await server?.close();
  server = null;
  tmp.remove();
});

/** The frame log the server wrote for game `id`: its text. */
const frameLog = (id = 1) => readText(join(tmp.dir, 'logs', `game-${id}.netlog`));
const frameWith = (event, id = 1) => {
  const m = frameLog(id).match(new RegExp(`^(\\d+) .*${event}`, 'm'));
  return m ? Number(m[1]) : null;
};

async function bot(name, via = url, env = {}) {
  const b = new Bot(name, via, 'e2e', env);
  bots.push(b);
  await b.ready();
  await until(async () => (await b.status()).state === 'lobby', `${name} in the lobby`);
  return b;
}

/** Both bots in a started two-player game. */
async function pair(config) {
  await startServer(config);
  const a = await bot('A');
  const b = await bot('B');
  await a.request('create');
  await until(async () => (await b.status()).lobby?.games.length === 1, 'B sees the game');
  await b.request('join', { game: 1 });
  await a.started();
  await b.started();
  return [a, b];
}

/** All checksums two bots reported for the same frame are the same, and there are some. */
async function assertInStep(a, b, minimum = 3) {
  const common = commonSums(await a.request('sums'), await b.request('sums'));
  assert.ok(common.length >= minimum, `only ${common.length} frames reported by both`);
  for (const [f, x, y] of common) assert.deepEqual(x, y, `bots differ at frame ${f}`);
  return common;
}

describe('two players', () => {
  it('play the same game from a lobby, in step, moving each Keen from its own client', async () => {
    const [a, b] = await pair();
    await a.waitFrame(100);
    await b.waitFrame(100);
    await assertInStep(a, b, 4);
    // Player 1 walks right on the map; both machines show it the same afterwards.
    const before = await a.request('sums');
    await a.request('hold', { bits: RIGHT });
    await a.waitFrame(250);
    await b.waitFrame(250);
    await a.request('hold', { bits: 8 });
    await b.request('hold', { bits: 8 });
    await a.waitFrame(400);
    await b.waitFrame(400);
    const common = await assertInStep(a, b, 10);
    const moved = (await a.request('sums'))[240];
    assert.notDeepEqual(moved, before[240] ?? null);
    assert.ok(common.length >= 10);
    // The server agrees: both seats reported, nothing drifted.
    assert.ok(!logs.some((l) => /desync/.test(l)), logs.join('\n'));
  });
});

describe('joining a running game', () => {
  it('a second client joins a one-player game mid-level and plays on in step', async () => {
    await startServer();
    const a = await bot('A');
    await a.request('create');
    await a.request('single');
    await a.started();
    await a.request('hold', { bits: RIGHT });
    await a.waitFrame(150);
    const b = await bot('B');
    await until(async () => (await b.status()).lobby?.games.length === 1, 'B sees the game');
    assert.equal((await b.status()).lobby.games[0].seats, 1);
    await b.request('join', { game: 1 });
    await b.started();
    await until(() => frameWith('add:2'), 'the add event in the frame log');
    const addFrame = frameWith('add:2');
    // Both reach well past the frame the second Keen appeared on.
    await a.waitFrame(addFrame + 100);
    await b.waitFrame(addFrame + 100);
    const common = (await assertInStep(a, b, 3)).filter(([f]) => f > addFrame);
    assert.ok(common.length >= 3, 'no checksums reported after the join');
    assert.equal((await b.status()).game.seat, 1);
    assert.deepEqual(server.hub.games.get(1).summary().open, []);
    // The longest-connected client keeps the server's checkpoint current (every 240 frames, and
    // at every level start): exactly one checkpoint, and the frames sealed since.
    await a.waitFrame(500);
    const game = server.hub.games.get(1);
    assert.ok(game.checkpoint, 'no checkpoint');
    assert.equal(game.checkpoint.frame % 240, 0);
    assert.equal(game.log[0].n, game.checkpoint.frame);
    assert.equal(game.log.length, game.head - game.checkpoint.frame);
    assert.ok(!logs.some((l) => /desync/.test(l)), logs.join('\n'));
  });
});

describe('the end of a game', () => {
  it('ends for both players when both have watched the ending, and they are back at the lobby', async () => {
    const [a, b] = await pair();
    await a.waitFrame(60);
    await a.request('finished');
    await sleep(200);
    assert.equal(server.hub.games.size, 1, 'one player finishing is not enough');
    await b.request('finished');
    await until(() => server.hub.games.size === 0, 'the game to end');
    await until(async () => (await a.status()).ended && (await b.status()).ended, 'both told');
  });
});

/** Both clients walk their Keens to level 1's entrance and start it: warps and jump presses. */
async function startLevelOne(a, b) {
  const at = (await a.status()).debug.head + 40;
  for (const bot of [a, b]) {
    await bot.request('warp', { frame: at, player: 0, x: 21, y: 39 });
    await bot.request('warp', { frame: at, player: 1, x: 21, y: 39 });
  }
  await a.waitFrame(at + 10);
  await b.waitFrame(at + 10);
  await a.request('hold', { bits: JUMP | 8, frames: 3 });
  await b.request('hold', { bits: JUMP | 8, frames: 3 });
  await until(() => /level/.test(JSON.stringify(server.hub.games.get(1)?.summary().loc)), 'level 1 to start');
  assert.deepEqual(server.hub.games.get(1).summary().loc, { kind: 'level', level: 1 });
}

describe('a game in a level', () => {
  it('a client joining mid-level appears on top of the other Keen and plays on in step', async () => {
    const [a, b] = await pair();
    await a.waitFrame(30);
    await startLevelOne(a, b);
    const lvl = (await a.status()).frame;
    await a.waitFrame(lvl + 80);
    b.kill();
    await until(async () => (await a.status()).paused?.length, 'A to be told');
    await a.request('continue');
    await until(async () => (await a.status()).paused === null, 'A resumed');
    await a.request('hold', { bits: RIGHT });
    await a.waitFrame(lvl + 200);
    const c = await bot('C');
    await until(async () => (await c.status()).lobby?.games.length === 1, 'C sees the game');
    assert.deepEqual((await c.status()).lobby.games[0].loc, { kind: 'level', level: 1 });
    await c.request('join', { game: 1 });
    await c.started();
    await until(() => frameWith('add:2'), 'the add event');
    const addFrame = frameWith('add:2');
    await a.request('hold', { bits: 8 });
    await a.waitFrame(addFrame + 120);
    await c.waitFrame(addFrame + 120);
    const common = (await assertInStep(a, c, 3)).filter(([f]) => f > addFrame);
    assert.ok(common.length >= 3, 'no checksums after the join');
    // A level start was a checkpoint: the joiner restored from it.
    assert.ok(server.hub.games.get(1).checkpoint, 'no checkpoint');
    assert.ok(!logs.some((l) => /desync/.test(l)), logs.join('\n'));
  });
});

describe('a drifted game', () => {
  it('is repaired from the other client\'s snapshot and both agree afterwards', async () => {
    const [a, b] = await pair();
    await a.waitFrame(60);
    await b.waitFrame(60);
    await b.request('corrupt', { kind: 1 });
    await until(() => logs.some((l) => /desync at frame \d+/.test(l)), 'the server to notice');
    const drift = Number(logs.find((l) => /desync at frame/.test(l)).match(/frame (\d+)/)[1]);
    await a.waitFrame(drift + 150);
    await b.waitFrame(drift + 150);
    const common = commonSums(await a.request('sums'), await b.request('sums'));
    const after = common.filter(([f]) => f > drift + 60);
    assert.ok(after.length >= 3, 'no checksums after the repair');
    for (const [f, x, y] of after) assert.deepEqual(x, y, `still drifted at frame ${f}`);
    // The server only had to repair it once.
    assert.equal(logs.filter((l) => /desync at frame/.test(l)).length, 1, logs.join('\n'));
  });
});

describe('disconnects', () => {
  it('pauses the partner, who continues alone; somebody else then joins and plays on', async () => {
    const [a, b] = await pair();
    await a.waitFrame(80);
    await b.waitFrame(80);
    b.kill();
    await until(async () => (await a.status()).paused?.length, 'A to be told');
    assert.deepEqual((await a.status()).paused, [1]);
    await sleep(400);          // the frames already sealed play out
    const frozen = (await a.status()).frame;
    await sleep(400);
    assert.equal((await a.status()).frame, frozen, 'the game should stand still');
    assert.equal(server.hub.games.get(1).summary().seats, 1);
    await a.request('continue');
    await until(async () => (await a.status()).paused === null, 'A resumed');
    await a.waitFrame(frozen + 40);
    // The seat is open: somebody joins mid-game.
    const c = await bot('C');
    await until(async () => (await c.status()).lobby?.games.length === 1, 'C sees the game');
    await c.request('join', { game: 1 });
    await c.started();
    await until(() => frameWith('add:2'), 'the add event');
    const addFrame = frameWith('add:2');
    await a.waitFrame(addFrame + 100);
    await c.waitFrame(addFrame + 100);
    const common = (await assertInStep(a, c, 3)).filter(([f]) => f > addFrame);
    assert.ok(common.length >= 3);
    assert.ok(!logs.some((l) => /desync/.test(l)), logs.join('\n'));
  });

  it('keeps a game with nobody in it, joinable, for the configured time, then removes it', async () => {
    const [a, b] = await pair({ emptyGameSec: 3 });
    await a.waitFrame(60);
    await b.waitFrame(60);
    a.kill();
    b.kill();
    await until(() => server.hub.games.get(1)?.summary().phase === 'empty', 'the game to be empty');
    assert.equal(server.hub.games.get(1).summary().seats, 0);
    // A client that arrives in time resumes where the game was.
    const c = await bot('C');
    await until(async () => (await c.status()).lobby?.games.length === 1, 'C sees the empty game');
    await c.request('join', { game: 1 });
    await c.started();
    await c.waitFrame(100);
    assert.equal(server.hub.games.get(1)?.summary().phase, 'playing');
    await c.request('leave');
    await until(() => server.hub.games.get(1)?.summary().phase === 'empty', 'empty again');
    await sleep(3500);
    assert.equal(server.hub.games.size, 0);
  });

  it('shows "Waiting for player 2" while a client stalls and goes on by itself', async () => {
    const [a, b] = await pair();
    await a.waitFrame(60);
    await b.waitFrame(60);
    await b.request('stall', { ms: 2500 });
    await until(async () => /Waiting for player 2/.test((await a.status()).waiting ?? ''), 'A to wait');
    const at = (await a.status()).frame;
    await until(async () => (await a.status()).frame > at + 30, 'A to go on');
    assert.equal((await a.status()).paused, null);
    assert.equal((await a.status()).waiting, null);
    await assertInStep(a, b, 2);
  });

  it('drops a client that stays silent for the configured time', async () => {
    const [a, b] = await pair({ disconnectSec: 2 });
    await a.waitFrame(60);
    await b.request('stall', { ms: 60000 });
    await until(async () => (await a.status()).paused?.length, 'A to be told', 8000);
    assert.deepEqual((await a.status()).paused, [1]);
    assert.ok(b.events.some((e) => e.event === 'left') || (await b.status()).state === 'lobby');
  });

  it('opens the seat when a player leaves from the menu', async () => {
    const [a, b] = await pair();
    await a.waitFrame(60);
    await b.request('leave');
    await until(async () => (await a.status()).paused?.length, 'A to be told');
    await until(async () => (await b.status()).state === 'lobby', 'B in the lobby');
    assert.equal((await b.status()).lobby.games[0].open.length, 1);
  });
});

describe('the server\'s frame log', () => {
  it('replays natively, and its checksums are the ones the browsers reported', async (t) => {
    const keen = join(root, 'keen1');
    if (!existsSync(keen)) return t.skip('no native build (make)');
    const [a, b] = await pair();
    await a.request('hold', { bits: RIGHT });
    await a.waitFrame(300);
    await b.waitFrame(300);
    await a.request('hold', { bits: 8 });
    await a.waitFrame(500);
    await b.waitFrame(500);
    const log = join(tmp.dir, 'logs', 'game-1.netlog');
    const sumsOut = join(tmp.dir, 'native.sums');
    const { spawnSync } = await import('node:child_process');
    const run = spawnSync(keen, ['--data', dataDir, '--replay', log, '--netsums', sumsOut], {
      env: { ...process.env, LD_LIBRARY_PATH: process.env.LD_LIBRARY_PATH ?? '' },
      encoding: 'utf8',
    });
    assert.equal(run.status, 0, run.stderr + run.stdout);
    assert.match(run.stdout, /netlog: end at frame \d+/);
    const native = new Map(readText(sumsOut).trim().split('\n').map((l) => l.split(' ')).map(([f, c]) => [Number(f), c]));
    const reported = [...frameLog().matchAll(/^sum (\d+) \d ([0-9a-f]{8}) /gm)].map((m) => [Number(m[1]), m[2]]);
    assert.ok(reported.length > 20, `only ${reported.length} reports in the log`);
    let compared = 0;
    for (const [f, c] of reported) {
      if (!native.has(f)) continue;
      assert.equal(native.get(f), c, `native and browser differ at frame ${f}`);
      compared++;
    }
    assert.ok(compared > 20, `only ${compared} frames compared`);
  });
});

describe('a slow connection', () => {
  /** Frames a second of a pair of bots over a 150 ms round trip. */
  async function speedOver150ms(env) {
    await startServer();
    const proxy = await delayProxy(new URL(url).port, 75);
    try {
      const real = { BOT_FRAME_MS: '41', ...env };      // the speed of the real game
      const a = await bot('A', `http://127.0.0.1:${proxy.port}`, real);
      const b = await bot('B', `http://127.0.0.1:${proxy.port}`, real);
      await a.request('create');
      await until(async () => (await b.status()).lobby?.games.length === 1, 'B sees the game');
      await b.request('join', { game: 1 });
      await a.started();
      await b.started();
      await a.waitFrame(30);
      const sa = await a.status();
      const f0 = await a.frame();
      const t0 = Date.now();
      await sleep(4000);
      const fps = ((await a.frame()) - f0) / ((Date.now() - t0) / 1000);
      await assertInStep(a, b, 3);
      return { sa, fps };
    } finally {
      await proxy.close();
    }
  }

  it('raises the input lead to hide a 150 ms round trip, so play stays at full speed', async () => {
    const { sa, fps } = await speedOver150ms({});
    assert.ok(sa.rtt > 120 && sa.rtt < 220, `measured round trip ${sa.rtt}`);
    assert.equal(sa.lead, Math.ceil(sa.rtt / 41) + 1);
    assert.ok(sa.lead >= 4, `lead ${sa.lead}`);
    // Full speed: a real game runs 24.3 frames a second.
    assert.ok(fps > 22, `only ${fps.toFixed(1)} frames a second`);
  });

  it('would stutter with the lead stuck at 2', async () => {
    const { fps } = await speedOver150ms({ BOT_LEAD: '2' });
    assert.ok(fps < 21, `${fps.toFixed(1)} frames a second`);   // about (lead + 1) / round trip
  });
});
