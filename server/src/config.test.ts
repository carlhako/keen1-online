import { describe, expect, it } from 'vitest';
import { defaultConfig, loadConfig, parseConfig, type ConfigIo } from './config.ts';

function io(files: Record<string, string>, env: Record<string, string> = {}): ConfigIo {
  return {
    env,
    cwd: '/srv/keen',
    exists: (p) => p in files,
    read: (p) => files[p] as string,
  };
}

describe('parseConfig', () => {
  it('gives the documented defaults', () => {
    const r = parseConfig({});
    expect(r).toEqual({
      ok: true,
      warnings: [],
      config: {
        port: 8080,
        maxGames: 5,
        inputLeadMax: 6,
        disconnectSec: 15,
        emptyGameSec: 60,
        checkpointFrames: 240,
        dataDir: 'data',
      },
    });
    expect(defaultConfig().maxGames).toBe(5);
  });

  it('applies overrides and keeps the other defaults', () => {
    const r = parseConfig({ port: 9000, maxGames: 2, emptyGameSec: 0 });
    expect(r.ok && r.config).toMatchObject({ port: 9000, maxGames: 2, emptyGameSec: 0 });
    expect(r.ok && r.config.checkpointFrames).toBe(240);
  });

  it('takes a data directory and refuses an empty one', () => {
    const r = parseConfig({ dataDir: '/var/lib/keen1-online' });
    expect(r.ok && r.config.dataDir).toBe('/var/lib/keen1-online');
    expect(parseConfig({ dataDir: '' }).ok).toBe(false);
    expect(parseConfig({ dataDir: 5 }).ok).toBe(false);
  });

  it('rejects bad values, all at once', () => {
    const r = parseConfig({
      port: 70000,
      maxGames: 0,
      inputLeadMax: 2.5,
      disconnectSec: '15',
      checkpointFrames: 100,
    });
    expect(r.ok).toBe(false);
    if (r.ok) return;
    expect(r.errors).toEqual([
      'port: must be between 0 and 65535',
      'maxGames: must be between 1 and 100',
      'inputLeadMax: must be an integer',
      'disconnectSec: must be an integer',
      'checkpointFrames: must be a multiple of 24',
    ]);
  });

  it('warns about unknown keys and rejects a non-object', () => {
    const r = parseConfig({ colour: 'red' });
    expect(r.ok && r.warnings).toEqual(['unknown config key "colour" (ignored)']);
    expect(parseConfig([]).ok).toBe(false);
    expect(parseConfig(null).ok).toBe(false);
  });
});

describe('loadConfig', () => {
  it('uses defaults without a file', () => {
    const r = loadConfig(io({}));
    expect(r.ok && r.config.port).toBe(8080);
    expect(r.ok && r.notes[0]).toMatch(/no config file/);
  });

  it('reads config.json from the working directory', () => {
    const r = loadConfig(io({ '/srv/keen/config.json': '{"maxGames": 3}' }));
    expect(r.ok && r.config.maxGames).toBe(3);
  });

  it('honours KEEN_CONFIG and PORT', () => {
    const files = { '/etc/keen.json': '{"port": 1234}' };
    const r = loadConfig(io(files, { KEEN_CONFIG: '/etc/keen.json', PORT: '4321' }));
    expect(r.ok && r.config.port).toBe(4321);
  });

  it('refuses a bad PORT, bad JSON and bad values', () => {
    expect(loadConfig(io({}, { PORT: 'abc' })).ok).toBe(false);
    expect(loadConfig(io({ '/srv/keen/config.json': '{oops' })).ok).toBe(false);
    expect(loadConfig(io({ '/srv/keen/config.json': '{"maxGames": -1}' })).ok).toBe(false);
  });
});
