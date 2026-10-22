/**
 * Messages between the browser and the server, as JSON text frames (`t` names the type) and two
 * binary frames for snapshots. The model is lockstep: the server relays per-frame inputs and
 * "seals" each frame once every live seat's input for it is in. Browsers simulate sealed frames
 * only, in order, so every browser runs the same game.
 */

import type { SaveInfo } from './saves.ts';

/** Bumped when messages change in a way an older page cannot follow. */
export const PROTOCOL_VERSION = 1;

/** Seats in a game: players 1 and 2. */
export const SEATS = 2;

/** The input of a seat that has none: direction 8 (no direction), no buttons, no events. */
export const NEUTRAL_INPUT = 8;

/** How far ahead of the next frame to seal an input may be, in frames. */
export const INPUT_HORIZON = 120;

/** Input messages accepted per connection per second. */
export const INPUT_RATE = 60;

/** Malformed messages from one connection before it is closed. */
export const MALFORMED_LIMIT = 5;

/** A frame's input packs direction (0..8), the two buttons and the one-frame events. */
export const MAX_INPUT = 1 << 11;

/** The longest chat message, in characters (it must fit on one line of the game's screen). */
export const CHAT_MAX = 32;

/** Chat messages one seat may send per `CHAT_WINDOW_MS`. */
export const CHAT_RATE = 5;
export const CHAT_WINDOW_MS = 10_000;

/** The largest score a game record or a table entry may hold. */
export const MAX_SCORE = 99_999_999;

/**
 * - `scoring`: the ending is over; a team name may be typed and the table is shown, then everyone
 *   leaves.
 */
export type Phase = 'waiting' | 'playing' | 'paused' | 'empty' | 'scoring' | 'removed';

/** Where a game is: waiting to start, on the world map, or in a level. */
export type Location = { kind: 'waiting' } | { kind: 'map' } | { kind: 'level'; level: number };

export type RejectReason = 'version' | 'full' | 'nogame' | 'closed' | 'busy' | 'nosave' | 'invalid';

/** Something that happens on a frame, in the order listed. */
export type GameEvent =
  | { t: 'start'; seed: number; mask: number }
  | { t: 'add'; seat: number }
  | { t: 'remove'; seat: number };

/** A row of the high score table, in the original's layout. */
export interface ScoreRow {
  score: number;
  name: string;
  /** Ship parts held: bit 0 joystick, 1 battery, 2 vacuum, 3 whiskey. */
  parts: number;
}

export interface LobbyGame {
  id: number;
  /** Seats with a player, 0..2. */
  seats: number;
  /** Seats that may be taken. */
  open: number[];
  phase: Phase;
  loc: Location;
}

// ---- browser to server ----

export type ClientMessage =
  | { t: 'hello'; v: number; build: string }
  | { t: 'create' }
  /** Start a new game from a save held by the server. */
  | { t: 'load'; save: number }
  /** Store the shared game record (base64, 92 bytes) under a name; answered by `saved`. */
  | { t: 'save'; name: string; rec: string }
  /** The team name for the high score table, from the seat asked for it by `ended`. */
  | { t: 'score'; name: string; score: number; parts: number }
  | { t: 'join'; game: number; token?: string }
  | { t: 'single' }
  | { t: 'leave' }
  | { t: 'input'; f: number; b: number }
  /**
   * Checksum of the state before frame `f` is simulated; `l` is where the game is (0 map, n level)
   * and `s` the shared score, which bounds what the game may later enter in the high score table.
   */
  | { t: 'sum'; f: number; c: number; l: number; s?: number }
  /** A joining browser has caught up and is about to simulate frame `f`. */
  | { t: 'ready'; f: number }
  | { t: 'continue' }
  /** The ending cinematic started: no inputs will come until `finished`. */
  | { t: 'ending' }
  | { t: 'finished' }
  /** A chat line for everyone in the game (not game state: it never touches the frames). */
  | { t: 'chat'; text: string }
  | { t: 'ping'; c: number };

// ---- server to browser ----

export type ServerMessage =
  | {
      t: 'welcome';
      v: number;
      cfg: { checkpointFrames: number; inputLeadMax: number; disconnectSec: number };
    }
  | { t: 'reject'; reason: RejectReason; count?: number; max?: number }
  | { t: 'lobby'; games: LobbyGame[]; count: number; max: number }
  | { t: 'joined'; game: number; seat: number; token: string; phase: Phase }
  | { t: 'start'; seed: number; mask: number; rec?: string }
  /** A sealed frame: every seat's input, and the events that apply on it. */
  | { t: 'frame'; n: number; in: number[]; ev?: GameEvent[] }
  /**
   * Catch up from frame `f`: a binary snapshot follows unless `fresh`, then the sealed frames
   * from `f` on. `seed` and `mask` start a fresh game.
   */
  | { t: 'restore'; f: number; fresh: boolean; seed: number; mask: number; rec?: string }
  /** The joining browser may start sending inputs from frame `from`. */
  | { t: 'golive'; from: number }
  /** The server wants a snapshot of the state now, for a browser that drifted. */
  | { t: 'snap' }
  /** Whether this browser, the one in the game longest, uploads the checkpoints. */
  | { t: 'duty'; on: boolean }
  | { t: 'paused'; seats: number[] }
  | { t: 'resumed' }
  /**
   * The ending is over. `entry` is the seat typing the team name for the high score table, or -1
   * when no name is wanted (the score is too low, or the table is final): show the table.
   */
  | { t: 'ended'; entry: number }
  /** The saves on the server, newest first. */
  | { t: 'saves'; saves: SaveInfo[] }
  /** Whether the server stored the sender's save. */
  | { t: 'saved'; ok: boolean; id?: number }
  /** The high score table. */
  | { t: 'scores'; rows: ScoreRow[] }
  | { t: 'left' }
  /** A chat line from `seat`, sent to every player in the game, the sender too. */
  | { t: 'chat'; seat: number; text: string }
  | { t: 'pong'; c: number };

/** Binary frames. Browser to server: `UP_*`. Server to browser: `DOWN_RESTORE`. */
export const UP_CHECKPOINT = 1;
export const UP_RESYNC = 2;
export const DOWN_RESTORE = 1;

export interface Upload {
  kind: typeof UP_CHECKPOINT | typeof UP_RESYNC;
  frame: number;
  sum: number;
  data: Buffer;
}

/** `[kind u8][frame u32 le][checksum u32 le][snapshot ...]` */
export function parseUpload(buf: Buffer): Upload | null {
  if (buf.length < 9) return null;
  const kind = buf[0];
  if (kind !== UP_CHECKPOINT && kind !== UP_RESYNC) return null;
  return { kind, frame: buf.readUInt32LE(1), sum: buf.readUInt32LE(5), data: buf.subarray(9) };
}

/** `[DOWN_RESTORE u8][frame u32 le][snapshot ...]` */
export function encodeRestore(frame: number, data: Buffer): Buffer {
  const head = Buffer.alloc(5);
  head[0] = DOWN_RESTORE;
  head.writeUInt32LE(frame, 1);
  return Buffer.concat([head, data]);
}

function isInt(v: unknown, min: number, max: number): v is number {
  return typeof v === 'number' && Number.isInteger(v) && v >= min && v <= max;
}

/** Validates a parsed JSON message. Returns null when it is not a well-formed client message. */
export function parseClientMessage(raw: unknown): ClientMessage | null {
  if (typeof raw !== 'object' || raw === null) return null;
  const m = raw as Record<string, unknown>;
  switch (m.t) {
    case 'hello':
      return isInt(m.v, 0, 1e6) && typeof m.build === 'string' && m.build.length <= 64
        ? { t: 'hello', v: m.v, build: m.build }
        : null;
    case 'create':
    case 'single':
    case 'leave':
    case 'continue':
    case 'ending':
    case 'finished':
      return { t: m.t };
    case 'join':
      if (!isInt(m.game, 1, 2 ** 31)) return null;
      if (m.token !== undefined && (typeof m.token !== 'string' || m.token.length > 64))
        return null;
      return m.token === undefined
        ? { t: 'join', game: m.game }
        : { t: 'join', game: m.game, token: m.token };
    case 'input':
      return isInt(m.f, 0, 2 ** 32 - 1) && isInt(m.b, 0, MAX_INPUT - 1) && (m.b & 15) <= 8
        ? { t: 'input', f: m.f, b: m.b }
        : null;
    case 'sum':
      if (!isInt(m.f, 0, 2 ** 32 - 1) || !isInt(m.c, 0, 2 ** 32 - 1) || !isInt(m.l, 0, 16)) {
        return null;
      }
      if (m.s === undefined) return { t: 'sum', f: m.f, c: m.c, l: m.l };
      return isInt(m.s, 0, MAX_SCORE) ? { t: 'sum', f: m.f, c: m.c, l: m.l, s: m.s } : null;
    case 'save':
      return typeof m.name === 'string' &&
        m.name.length <= 64 &&
        typeof m.rec === 'string' &&
        m.rec.length <= 256
        ? { t: 'save', name: m.name, rec: m.rec }
        : null;
    case 'load':
      return isInt(m.save, 1, 2 ** 31) ? { t: 'load', save: m.save } : null;
    case 'score':
      return typeof m.name === 'string' &&
        m.name.length <= 64 &&
        isInt(m.score, 0, MAX_SCORE) &&
        isInt(m.parts, 0, 15)
        ? { t: 'score', name: m.name, score: m.score, parts: m.parts }
        : null;
    case 'ready':
      return isInt(m.f, 0, 2 ** 32 - 1) ? { t: 'ready', f: m.f } : null;
    case 'chat':
      return typeof m.text === 'string' &&
        m.text.length >= 1 &&
        m.text.length <= CHAT_MAX &&
        /^[\x20-\x7e]+$/.test(m.text)
        ? { t: 'chat', text: m.text }
        : null;
    case 'ping':
      return isInt(m.c, 0, Number.MAX_SAFE_INTEGER) ? { t: 'ping', c: m.c } : null;
    default:
      return null;
  }
}
