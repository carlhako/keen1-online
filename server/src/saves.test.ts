import { mkdtempSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { afterEach, beforeEach, describe, expect, it } from 'vitest';
import { SaveStore, validateRecord } from './saves.ts';
import { record } from './testing.ts';

describe('validateRecord', () => {
  it('accepts a game record', () => {
    expect(validateRecord(record({ score: 1234, done: [1, 2] }))).toBeNull();
  });

  it('refuses the wrong size', () => {
    expect(validateRecord(Buffer.alloc(91))).toMatch(/91 bytes/);
    expect(validateRecord(Buffer.alloc(93))).toMatch(/93 bytes/);
  });

  it('refuses flags that are not 0 or 1', () => {
    const done = record();
    done.writeInt16LE(2, 20);
    expect(validateRecord(done)).toMatch(/level 2 done/);
    const item = record();
    item.writeInt16LE(-1, 0);
    expect(validateRecord(item)).toMatch(/joystick/);
    const key = record();
    key.writeInt16LE(7, 12);
    expect(validateRecord(key)).toMatch(/key 2/);
  });

  it('refuses values outside the original ranges', () => {
    const score = record();
    score.writeInt32LE(-5, 54);
    expect(validateRecord(score)).toMatch(/score/);
    const where = record({ mapx: 999 << 12 });
    expect(validateRecord(where)).toMatch(/off the map/);
    const lives = record();
    lives.writeInt16LE(500, 50);
    expect(validateRecord(lives)).toMatch(/lives/);
  });
});

describe('SaveStore in memory', () => {
  it('lists saves newest first and returns their records', () => {
    const store = new SaveStore(null);
    const a = store.add('first', record({ score: 100 }), new Date('2026-01-01T10:00:00Z'));
    const b = store.add(
      'second',
      record({ score: 900, done: [1, 3] }),
      new Date('2026-01-02T10:00:00Z'),
    );
    const c = store.add('third', record(), new Date('2026-01-01T12:00:00Z'));
    expect(store.list().map((s) => s.name)).toEqual(['second', 'third', 'first']);
    expect(b).toMatchObject({ name: 'second', levels: 2, score: 900 });
    expect(a?.id).toBe(1);
    expect(c?.id).toBe(3);
    expect(store.record(2)).toEqual(record({ score: 900, done: [1, 3] }));
    expect(store.record(99)).toBeNull();
  });

  it('orders saves made at the same moment by id, newest id first', () => {
    const store = new SaveStore(null);
    const when = new Date('2026-01-01T10:00:00Z');
    store.add('a', record(), when);
    store.add('b', record(), when);
    expect(store.list().map((s) => s.name)).toEqual(['b', 'a']);
  });

  it('refuses malformed records and bad names, storing nothing', () => {
    const store = new SaveStore(null);
    const bad = record();
    bad.writeInt16LE(3, 18);
    expect(store.add('bad', bad, new Date())).toBeNull();
    expect(store.add('', record(), new Date())).toBeNull();
    expect(store.add('x'.repeat(21), record(), new Date())).toBeNull();
    expect(store.add('tab\there', record(), new Date())).toBeNull();
    expect(store.add('ünïcode', record(), new Date())).toBeNull();
    expect(store.list()).toEqual([]);
  });
});

describe('SaveStore on disk', () => {
  let dir: string;
  beforeEach(() => {
    dir = mkdtempSync(join(tmpdir(), 'keen-saves-'));
  });
  afterEach(() => {
    rmSync(dir, { recursive: true, force: true });
  });

  it('keeps one file per save and reloads them after a restart', () => {
    const store = new SaveStore(join(dir, 'saves'));
    store.add('mars run', record({ score: 4200, done: [1] }), new Date('2026-03-01T00:00:00Z'));
    store.add('night two', record({ score: 9000, done: [1, 2] }), new Date('2026-03-02T00:00:00Z'));
    expect(readdirSync(join(dir, 'saves')).sort()).toEqual(['1.json', '2.json']);

    const again = new SaveStore(join(dir, 'saves'));
    expect(again.list()).toEqual(store.list());
    expect(again.record(1)).toEqual(record({ score: 4200, done: [1] }));
    // Ids carry on after the highest one, so a restart never overwrites a save.
    expect(again.add('later', record(), new Date())?.id).toBe(3);
  });

  it('skips a file that is not a valid save and says so', () => {
    const logged: string[] = [];
    const store = new SaveStore(join(dir, 'saves'), (m) => logged.push(m));
    store.add('fine', record(), new Date());
    writeFileSync(join(dir, 'saves', '7.json'), '{"id":7,"name":"x","record":"AAAA"}');
    writeFileSync(join(dir, 'saves', '8.json'), 'not json');
    writeFileSync(join(dir, 'saves', 'notes.txt'), 'ignored');
    const again = new SaveStore(join(dir, 'saves'), (m) => logged.push(m));
    expect(again.list().map((s) => s.name)).toEqual(['fine']);
    expect(logged.filter((m) => m.includes('skipping')).length).toBe(2);
  });
});
