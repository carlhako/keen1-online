import { mkdirSync, readFileSync, renameSync, writeFileSync } from 'node:fs';
import { dirname } from 'node:path';
import { printable } from './saves.ts';

export const SCORE_ENTRIES = 7;
export const SCORE_NAME_MAX = 12;

/** A table row, in the original's layout. `parts` has bit 0 joystick, 1 battery, 2 vacuum, 3 whiskey. */
export interface ScoreEntry {
  score: number;
  name: string;
  parts: number;
}

/** The table the original game starts with. */
export function defaultScores(): ScoreEntry[] {
  const names = ['Yorpy', 'Gargile', 'Zzapp!'];
  return Array.from({ length: SCORE_ENTRIES }, (_, i) => ({
    score: 100,
    name: names[i % 3] as string,
    parts: 0,
  }));
}

function validEntries(raw: unknown): raw is ScoreEntry[] {
  return (
    Array.isArray(raw) &&
    raw.length === SCORE_ENTRIES &&
    raw.every(
      (e: unknown) =>
        typeof e === 'object' &&
        e !== null &&
        Number.isInteger((e as ScoreEntry).score) &&
        (e as ScoreEntry).score >= 0 &&
        typeof (e as ScoreEntry).name === 'string' &&
        printable((e as ScoreEntry).name, SCORE_NAME_MAX) &&
        Number.isInteger((e as ScoreEntry).parts) &&
        (e as ScoreEntry).parts >= 0 &&
        (e as ScoreEntry).parts <= 15,
    )
  );
}

export class ScoreStore {
  private entries: ScoreEntry[] = defaultScores();

  /**
   * `file` holds the table as JSON, rewritten whole through a temporary file and a rename so a
   * crash never leaves half a table. Without one the table lives in memory only (tests).
   */
  constructor(
    private readonly file: string | null,
    private readonly log: (message: string) => void = () => {},
  ) {
    if (!file) return;
    mkdirSync(dirname(file), { recursive: true });
    try {
      const raw: unknown = JSON.parse(readFileSync(file, 'utf8'));
      if (!validEntries(raw)) throw new Error('not a table of 7 entries');
      this.entries = raw;
    } catch (e) {
      if ((e as NodeJS.ErrnoException).code !== 'ENOENT') {
        this.log(
          `highscores: ${file} unusable, starting from the default table: ${e instanceof Error ? e.message : String(e)}`,
        );
      }
    }
  }

  table(): ScoreEntry[] {
    return this.entries.map((e) => ({ ...e }));
  }

  /** Where a score would go (0 is the top), or -1 when it is not above the lowest entry. */
  rank(score: number): number {
    return this.entries.findIndex((e) => score > e.score);
  }

  /** Inserts an entry at its place, as the original does: below equal scores. Returns the rank, or -1. */
  insert(entry: ScoreEntry): number {
    const at = this.rank(entry.score);
    if (at < 0) return -1;
    const next = [...this.entries];
    next.splice(at, 0, { ...entry });
    next.length = SCORE_ENTRIES;
    if (this.file) {
      try {
        writeFileSync(`${this.file}.tmp`, JSON.stringify(next));
        renameSync(`${this.file}.tmp`, this.file);
      } catch (e) {
        this.log(`highscores: cannot write: ${e instanceof Error ? e.message : String(e)}`);
        return -1;
      }
    }
    this.entries = next;
    return at;
  }
}
