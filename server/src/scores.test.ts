import { existsSync, mkdtempSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { defaultScores, ScoreStore } from './scores.ts';

describe('the default table', () => {
  it('is the original one: seven entries of 100 points', () => {
    const t = defaultScores();
    expect(t).toHaveLength(7);
    expect(t.every((e) => e.score === 100 && e.parts === 0)).toBe(true);
    expect(t.map((e) => e.name)).toEqual([
      'Yorpy',
      'Gargile',
      'Zzapp!',
      'Yorpy',
      'Gargile',
      'Zzapp!',
      'Yorpy',
    ]);
  });
});

describe('ScoreStore insertion', () => {
  it('puts an entry above the first lower score and drops the last', () => {
    const store = new ScoreStore(null);
    expect(store.insert({ score: 500, name: 'A', parts: 15 })).toBe(0);
    expect(store.insert({ score: 300, name: 'B', parts: 3 })).toBe(1);
    expect(store.insert({ score: 400, name: 'C', parts: 0 })).toBe(1);
    expect(
      store
        .table()
        .map((e) => e.name)
        .slice(0, 4),
    ).toEqual(['A', 'C', 'B', 'Yorpy']);
    expect(store.table()).toHaveLength(7);
    expect(store.table()[6]?.score).toBe(100);
  });

  it('puts an equal score below the existing one, like the original', () => {
    const store = new ScoreStore(null);
    store.insert({ score: 500, name: 'A', parts: 0 });
    expect(store.insert({ score: 500, name: 'B', parts: 0 })).toBe(1);
  });

  it('does not take a score that is not above the lowest entry', () => {
    const store = new ScoreStore(null);
    expect(store.rank(100)).toBe(-1);
    expect(store.insert({ score: 100, name: 'X', parts: 0 })).toBe(-1);
    expect(store.insert({ score: 50, name: 'X', parts: 0 })).toBe(-1);
    expect(store.table()).toEqual(defaultScores());
  });
});

describe('ScoreStore on disk', () => {
  let dir: string;
  beforeEach(() => {
    dir = mkdtempSync(join(tmpdir(), 'keen-scores-'));
  });
  afterEach(() => {
    rmSync(dir, { recursive: true, force: true });
  });

  it('starts from the defaults when there is no file, without creating one', () => {
    const store = new ScoreStore(join(dir, 'sub', 'highscores.json'));
    expect(store.table()).toEqual(defaultScores());
    expect(existsSync(join(dir, 'sub', 'highscores.json'))).toBe(false);
  });

  it('writes through a temporary file and a rename, leaving only the table', () => {
    const file = join(dir, 'highscores.json');
    const store = new ScoreStore(file);
    store.insert({ score: 777, name: 'carl & sam', parts: 15 });
    expect(readdirSync(dir)).toEqual(['highscores.json']);
  });

  it('keeps the table over a restart', () => {
    const file = join(dir, 'highscores.json');
    const store = new ScoreStore(file);
    store.insert({ score: 777, name: 'carl & sam', parts: 15 });
    const again = new ScoreStore(file);
    expect(again.table()).toEqual(store.table());
    expect(again.table()[0]).toEqual({ score: 777, name: 'carl & sam', parts: 15 });
  });

  it('falls back to the defaults, and says so, for a damaged file', () => {
    const file = join(dir, 'highscores.json');
    writeFileSync(file, '[{"score":1}]');
    const logged: string[] = [];
    const store = new ScoreStore(file, (m) => logged.push(m));
    expect(store.table()).toEqual(defaultScores());
    expect(logged).toHaveLength(1);
  });
});
