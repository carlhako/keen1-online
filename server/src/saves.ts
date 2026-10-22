import { mkdirSync, readdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

/** The shared game record of the C game (`gamestate_t` in src/game.h): 92 bytes, little endian. */
export const RECORD_BYTES = 92;

export const SAVE_NAME_MAX = 20;

/** What the lobby shows about a save. */
export interface SaveInfo {
  id: number;
  name: string;
  /** ISO 8601, when the save was made. */
  date: string;
  /** Levels completed, 0 to 16. */
  levels: number;
  score: number;
}

interface SaveFile extends SaveInfo {
  /** The record, base64. */
  record: string;
}

/** Printable ASCII only: the game's own font has nothing else. */
export function printable(text: string, max: number): boolean {
  return text.length >= 1 && text.length <= max && /^[\x20-\x7e]+$/.test(text);
}

/**
 * Checks a game record against the ranges the original game keeps it in: wrong size, flags that
 * are not 0 or 1, or a position outside the world map make it not a Keen 1 game. Returns why, or
 * null when it is fine.
 */
export function validateRecord(rec: Buffer): string | null {
  if (rec.length !== RECORD_BYTES) return `record is ${rec.length} bytes, not ${RECORD_BYTES}`;
  const flag = (offset: number, what: string): string | null => {
    const v = rec.readInt16LE(offset);
    return v === 0 || v === 1 ? null : `${what} is ${v}, not 0 or 1`;
  };
  // joystick, vacuum, whiskey, pogo, battery, then the four keycards, then 16 completed flags
  const names = ['joystick', 'vacuum', 'whiskey', 'pogo', 'battery'];
  for (let i = 0; i < 5; i++) {
    const bad = flag(i * 2, names[i] as string);
    if (bad) return bad;
  }
  for (let i = 0; i < 4; i++) {
    const bad = flag(10 + i * 2, `key ${i + 1}`);
    if (bad) return bad;
  }
  for (let i = 0; i < 16; i++) {
    const bad = flag(18 + i * 2, `level ${i + 1} done`);
    if (bad) return bad;
  }
  const lives = rec.readInt16LE(50);
  if (lives < -1 || lives > 99) return `lives is ${lives}`;
  const ammo = rec.readInt16LE(52);
  if (ammo < 0 || ammo > 999) return `ammo is ${ammo}`;
  const score = rec.readInt32LE(54);
  if (score < 0 || score > 99_999_999) return `score is ${score}`;
  // The map is under 256 tiles square, a tile being 0x1000 units.
  for (const offset of [58, 62]) {
    const v = rec.readInt32LE(offset);
    if (v < 0 || v >= 256 << 12) return `map position ${v} is off the map`;
  }
  for (const offset of [66, 70]) {
    const v = rec.readInt32LE(offset);
    if (v < -(1 << 24) || v > 1 << 24) return `map scroll ${v} is out of range`;
  }
  return null;
}

/** Levels completed in a valid record. */
export function levelsDone(rec: Buffer): number {
  let n = 0;
  for (let i = 0; i < 16; i++) n += rec.readInt16LE(18 + i * 2);
  return n;
}

export function recordScore(rec: Buffer): number {
  return rec.readInt32LE(54);
}

export class SaveStore {
  private readonly saves = new Map<number, SaveFile>();
  private nextId = 1;

  /**
   * `dir` holds one JSON file per save, `<id>.json`. Without one the saves live in memory only
   * (tests). Files that cannot be read are skipped and reported through `log`.
   */
  constructor(
    private readonly dir: string | null,
    private readonly log: (message: string) => void = () => {},
  ) {
    if (!dir) return;
    mkdirSync(dir, { recursive: true });
    for (const file of readdirSync(dir)) {
      const m = /^(\d+)\.json$/.exec(file);
      if (!m) continue;
      try {
        const raw = JSON.parse(readFileSync(join(dir, file), 'utf8')) as SaveFile;
        const rec = Buffer.from(raw.record, 'base64');
        const bad = validateRecord(rec);
        if (bad || raw.id !== Number(m[1]) || typeof raw.name !== 'string') {
          throw new Error(bad ?? 'not a save');
        }
        this.saves.set(raw.id, raw);
        this.nextId = Math.max(this.nextId, raw.id + 1);
      } catch (e) {
        this.log(`saves: skipping ${file}: ${e instanceof Error ? e.message : String(e)}`);
      }
    }
  }

  /** Every save, newest first. */
  list(): SaveInfo[] {
    return [...this.saves.values()]
      .map(({ id, name, date, levels, score }) => ({ id, name, date, levels, score }))
      .sort((a, b) => (a.date < b.date ? 1 : a.date > b.date ? -1 : b.id - a.id));
  }

  /** The record of a save, or null. */
  record(id: number): Buffer | null {
    const s = this.saves.get(id);
    return s ? Buffer.from(s.record, 'base64') : null;
  }

  /** Stores a save. Returns null when the name or the record is not acceptable or it cannot be written. */
  add(name: string, rec: Buffer, now: Date): SaveInfo | null {
    if (!printable(name, SAVE_NAME_MAX) || validateRecord(rec)) return null;
    const id = this.nextId;
    const save: SaveFile = {
      id,
      name,
      date: now.toISOString(),
      levels: levelsDone(rec),
      score: recordScore(rec),
      record: rec.toString('base64'),
    };
    if (this.dir) {
      try {
        const file = join(this.dir, `${id}.json`);
        writeFileSync(`${file}.tmp`, JSON.stringify(save));
        renameSync(`${file}.tmp`, file);
      } catch (e) {
        this.log(`saves: cannot write save ${id}: ${e instanceof Error ? e.message : String(e)}`);
        return null;
      }
    }
    this.saves.set(id, save);
    this.nextId = id + 1;
    return { id, name, date: save.date, levels: save.levels, score: save.score };
  }
}
