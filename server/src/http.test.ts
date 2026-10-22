import { mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { defaultConfig } from './config.ts';
import { GameServer } from './server.ts';

let dir: string;
let server: GameServer;
let base: string;

beforeEach(async () => {
  dir = mkdtempSync(join(tmpdir(), 'keen-web-'));
  writeFileSync(join(dir, 'index.html'), '<!doctype html><title>Keen</title>');
  writeFileSync(join(dir, 'build-id.txt'), 'abc123\n');
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
    expect(await res.json()).toEqual({ status: 'ok', games: 0, players: 0, buildId: 'abc123' });
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
