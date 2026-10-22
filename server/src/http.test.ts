import { mkdirSync, mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { defaultConfig } from './config.ts';
import { GAME_FILES } from './gamefiles.ts';
import { createRouter } from './http.ts';
import { GameServer } from './server.ts';

let dir: string;
let server: GameServer;
let base: string;

beforeEach(async () => {
  dir = mkdtempSync(join(tmpdir(), 'keen-web-'));
  writeFileSync(join(dir, 'index.html'), '<!doctype html><title>Keen</title>');
  writeFileSync(join(dir, 'build-id.txt'), 'abc123\n');
  writeFileSync(join(dir, 'index.js'), 'console.log(1)');
  server = new GameServer({ config: { ...defaultConfig(), port: 0 }, webDir: dir });
  base = `http://127.0.0.1:${await server.listen()}`;
});

afterEach(async () => {
  await server.close();
  rmSync(dir, { recursive: true, force: true });
});

describe('http', () => {
  it('serves the web build index', async () => {
    const res = await fetch(`${base}/`);
    expect(res.status).toBe(200);
    expect(await res.text()).toContain('<title>Keen</title>');
    expect(res.headers.get('cache-control')).toBe('no-cache');
  });

  it('reports health with the build id', async () => {
    const res = await fetch(`${base}/healthz`);
    expect(await res.json()).toEqual({
      status: 'ok',
      games: 0,
      players: 0,
      buildId: 'abc123',
      gameData: 'missing',
    });
  });

  it('reports the release version when it is given one', async () => {
    const versioned = new GameServer({
      config: { ...defaultConfig(), port: 0 },
      webDir: dir,
      version: '1.2.3',
    });
    const port = await versioned.listen();
    const res = await fetch(`http://127.0.0.1:${port}/healthz`);
    expect(await res.json()).toMatchObject({ status: 'ok', version: '1.2.3' });
    await versioned.close();
  });

  it('answers 404 for missing files and does not leave the web directory', async () => {
    expect((await fetch(`${base}/nope.js`)).status).toBe(404);
    expect((await fetch(`${base}/../package.json`)).status).toBe(404);
  });
});

describe('http under a path prefix', () => {
  it('serves the page and its files under the prefix', async () => {
    expect(await (await fetch(`${base}/keen/`)).text()).toContain('<title>Keen</title>');
    expect(await (await fetch(`${base}/a/b/`)).text()).toContain('<title>Keen</title>');
    expect(await (await fetch(`${base}/keen/index.js`)).text()).toBe('console.log(1)');
    expect((await fetch(`${base}/keen/build-id.txt`)).status).toBe(200);
  });

  it('answers /healthz under any prefix', async () => {
    for (const path of ['/healthz', '/keen/healthz', '/a/b/healthz']) {
      expect(await (await fetch(`${base}${path}`)).json()).toMatchObject({ status: 'ok' });
    }
  });

  it('redirects a prefix without a slash and keeps the query', async () => {
    const res = await fetch(`${base}/keen?room=3`, { redirect: 'manual' });
    expect(res.status).toBe(302);
    expect(res.headers.get('location')).toBe('/keen/?room=3');
    const deep = await fetch(`${base}/a/b`, { redirect: 'manual' });
    expect(deep.headers.get('location')).toBe('/a/b/');
  });

  it('does not redirect to another host', async () => {
    const res = await fetch(`${base}//evil`, { redirect: 'manual' });
    expect(res.headers.get('location')).toBe('/evil/');
  });

  it('keeps missing files missing', async () => {
    expect((await fetch(`${base}/keen/nothing.js`)).status).toBe(404);
    expect((await fetch(`${base}/keen/data/NOPE.CK1`)).status).toBe(404);
  });
});

describe('router', () => {
  const route = createRouter(new Set(['index.html', 'index.js', 'sub/a.js']));

  it('refuses segments that climb or smuggle', () => {
    for (const path of ['/keen/../x', '/..', '/a/%2e%2e/b', '/a%5Cb', '/a%00b', '/%zz']) {
      expect(route(path)).toEqual({ kind: 'notFound' });
    }
  });

  it('drops the shortest prefix', () => {
    expect(route('/ws')).toEqual({ kind: 'ws' });
    expect(route('/keen/ws')).toEqual({ kind: 'ws' });
    expect(route('/a/b/c/ws')).toEqual({ kind: 'ws' });
    expect(route('/keen/sub/a.js')).toEqual({ kind: 'file', url: '/sub/a.js' });
    expect(route('/keen/data/index.json')).toEqual({ kind: 'dataIndex' });
    expect(route('/keen/data/LEVEL01.CK1')).toEqual({ kind: 'data', name: 'LEVEL01.CK1' });
    expect(route('/keen/data/level01.ck1')).toEqual({ kind: 'notFound' });
  });

  it('serves the index for a path ending in a slash', () => {
    expect(route('/')).toEqual({ kind: 'index' });
    expect(route('/keen/')).toEqual({ kind: 'index' });
  });
});

describe('game data', () => {
  let gameDir: string;
  let withData: GameServer;
  let dataBase: string;

  beforeEach(async () => {
    const dataDir = join(dir, 'state');
    gameDir = join(dataDir, 'game');
    mkdirSync(gameDir, { recursive: true });
    withData = new GameServer({ config: { ...defaultConfig(), port: 0 }, webDir: dir, dataDir });
    dataBase = `http://127.0.0.1:${await withData.listen()}`;
  });

  afterEach(async () => {
    await withData.close();
  });

  it('lists nothing and reports the data as missing while the directory is empty', async () => {
    expect(await (await fetch(`${dataBase}/data/index.json`)).json()).toEqual({
      files: [],
      complete: false,
    });
    expect(await (await fetch(`${dataBase}/healthz`)).json()).toMatchObject({
      gameData: 'missing',
    });
  });

  it('lists the present files and serves them byte for byte', async () => {
    const bytes = Buffer.from([0, 1, 2, 255, 254, 0, 7]);
    writeFileSync(join(gameDir, 'LEVEL01.CK1'), bytes);
    writeFileSync(join(gameDir, 'EGAHEAD.CK1'), 'hd');
    writeFileSync(join(gameDir, 'NOTES.TXT'), 'not a game file');
    const index = (await (await fetch(`${dataBase}/data/index.json`)).json()) as {
      files: { name: string; size: number }[];
      complete: boolean;
    };
    expect(index.complete).toBe(false);
    expect(index.files).toEqual([
      { name: 'EGAHEAD.CK1', size: 2 },
      { name: 'LEVEL01.CK1', size: 7 },
    ]);
    const res = await fetch(`${dataBase}/keen/data/LEVEL01.CK1`);
    expect(res.status).toBe(200);
    expect(Buffer.from(await res.arrayBuffer())).toEqual(bytes);
    const again = await fetch(`${dataBase}/data/LEVEL01.CK1`, {
      headers: { 'If-None-Match': res.headers.get('etag') ?? '' },
    });
    expect(again.status).toBe(304);
  });

  it('serves only files from the list', async () => {
    writeFileSync(join(gameDir, 'NOTES.TXT'), 'secret');
    writeFileSync(join(dir, 'state', 'highscores.json'), '{}');
    for (const path of [
      '/data/NOTES.TXT',
      '/data/highscores.json',
      '/data/../highscores.json',
      '/data/..%2Fhighscores.json',
      '/data/LEVEL01.CK1',
    ]) {
      expect((await fetch(`${dataBase}${path}`)).status).toBe(404);
    }
  });

  it('flips /healthz to ok when every game file is there', async () => {
    for (const name of GAME_FILES.slice(1)) writeFileSync(join(gameDir, name), name);
    expect(await (await fetch(`${dataBase}/healthz`)).json()).toMatchObject({
      gameData: 'missing',
    });
    writeFileSync(join(gameDir, GAME_FILES[0]!), 'x');
    expect(await (await fetch(`${dataBase}/healthz`)).json()).toMatchObject({ gameData: 'ok' });
    expect(readFileSync(join(gameDir, GAME_FILES[0]!), 'utf8')).toBe('x');
  });
});

describe('http without a build', () => {
  it('explains that the web build is missing', async () => {
    const empty = new GameServer({
      config: { ...defaultConfig(), port: 0 },
      webDir: join(dir, 'missing'),
    });
    const port = await empty.listen();
    const res = await fetch(`http://127.0.0.1:${port}/`);
    expect(res.status).toBe(404);
    expect(await res.text()).toContain('make web');
    await empty.close();
  });
});
