import { appendFileSync, mkdirSync } from 'node:fs';
import { join } from 'node:path';
import type { ServerConfig } from './config.ts';
import { printable, recordScore } from './saves.ts';
import { SCORE_NAME_MAX, type ScoreStore } from './scores.ts';
import {
  CHAT_RATE,
  CHAT_WINDOW_MS,
  encodeRestore,
  INPUT_HORIZON,
  NEUTRAL_INPUT,
  SEATS,
  type GameEvent,
  type Location,
  type LobbyGame,
  type Phase,
  type ServerMessage,
  type Upload,
  UP_CHECKPOINT,
  UP_RESYNC,
} from './protocol.ts';

/** What a game needs from a connected browser. */
export interface Player {
  send(msg: ServerMessage): void;
  sendBinary(data: Buffer): void;
  /** The game dropped this player's seat; they are back in the lobby. */
  kicked(): void;
}

/** Frames between the golive message and the frame the new player's Keen appears on. */
export const GOLIVE_GUARD = 4;

/** A snapshot larger than this is not a game state. */
export const MAX_SNAPSHOT = 1 << 20;

/** Sums that nobody compared are forgotten once the game is this many frames further. */
const SUM_KEEP_FRAMES = 1000;

/** A team name that is not typed within this long is given up on. */
const SCORE_ENTRY_MS = 120_000;

/** A snapshot that never came after a `snap` request is forgotten after this long. */
const RESYNC_TIMEOUT_MS = 10_000;

export interface SealedFrame {
  n: number;
  inputs: number[];
  ev: GameEvent[];
}

/**
 * - `open`: nobody there.
 * - `syncing`: a browser is loading the checkpoint and the frames since; the game runs without it.
 * - `joining`: it caught up and may send inputs from `from`; its Keen appears on that frame.
 * - `live`: part of the game; every frame waits for its input.
 */
type SeatMode = 'open' | 'syncing' | 'joining' | 'live';

interface Seat {
  player: Player | null;
  mode: SeatMode;
  /** Rejoin token of the last occupant. */
  token: string;
  joinedAt: number;
  /** When the last input arrived (or the seat went live). */
  lastInput: number;
  /** When any message arrived. */
  lastActivity: number;
  from: number;
  inputs: Map<number, number>;
  /** Sums for frames up to here are stale (the browser was just resynced). */
  ignoreSumsBefore: number;
  /** Whether this browser was last told it uploads checkpoints. */
  duty: boolean;
}

interface Checkpoint {
  frame: number;
  sum: number;
  data: Buffer;
}

export interface GameDeps {
  config: ServerConfig;
  now: () => number;
  random: () => number;
  /** Called when something the lobby shows (or the number of games) changed. */
  changed: () => void;
  /** Called when the game is gone. */
  removed: (game: Game) => void;
  log: (message: string) => void;
  /** The shared high score table. */
  scores: ScoreStore;
  /** Called when the high score table changed. */
  scoresChanged: () => void;
  /** Debug option: a directory that receives one frame log per game. */
  frameLogDir?: string;
}

export class Game {
  readonly id: number;
  phase: Phase = 'waiting';
  loc: Location = { kind: 'waiting' };
  readonly seats: Seat[] = [];
  /** The next frame to seal. */
  head = 0;
  seed = 0;
  startMask = 0;
  /** Seats whose Keen is in the game, as the browsers' game knows them. */
  present = 0;
  checkpoint: Checkpoint | null = null;
  /** Sealed frames from the checkpoint (or from frame 0) on. */
  log: SealedFrame[] = [];
  private scheduled: { frame: number; ev: GameEvent; by?: number }[] = [];
  private sums = new Map<number, Map<number, number>>();
  /** Frames every live player reported the same checksum for. */
  private agreed = new Map<number, number>();
  private pendingCheckpoints = new Map<number, { sum: number; data: Buffer }>();
  private resync: { targets: number[]; since: number } | null = null;
  private lastSealAt = 0;
  private lastLocFrame = -1;
  private finished = new Set<number>();
  /** The shared score at the latest report (a `sum` carries it), bounding the high score entry. */
  private score = 0;
  private scoreFrame = -1;
  /** While scoring: the seat typing the team name, or -1. */
  private entrySeat = -1;
  private entrySince = 0;
  /** The record this game started from (a loaded save), base64. */
  private readonly record: string | null;
  /** The ending is playing: no inputs come, so nobody times out. */
  private inEnding = false;
  private emptyTimer: ReturnType<typeof setTimeout> | null = null;
  /** When each seat sent its recent chat lines, for the rate limit. */
  private chatTimes = new Map<number, number[]>();
  private logFile: string | null = null;

  constructor(
    id: number,
    private readonly deps: GameDeps,
    record: Buffer | null = null,
  ) {
    this.id = id;
    this.record = record ? record.toString('base64') : null;
    if (record) this.score = recordScore(record);
    for (let i = 0; i < SEATS; i++) {
      this.seats.push({
        player: null,
        mode: 'open',
        token: '',
        joinedAt: 0,
        lastInput: 0,
        lastActivity: 0,
        from: 0,
        inputs: new Map(),
        ignoreSumsBefore: 0,
        duty: false,
      });
    }
  }

  // ---- seats ----

  /** Seats with a player in them, in any mode but open. */
  occupied(): number {
    return this.seats.filter((s) => s.mode !== 'open').length;
  }

  openSeats(): number[] {
    const open: number[] = [];
    this.seats.forEach((s, i) => {
      if (s.mode === 'open') open.push(i);
    });
    return open;
  }

  seatOf(player: Player): number {
    return this.seats.findIndex((s) => s.player === player);
  }

  /** Seats that are part of the game now: live, or about to be. */
  private participants(): number[] {
    const out: number[] = [];
    this.seats.forEach((s, i) => {
      if (s.mode === 'live' || s.mode === 'joining') out.push(i);
    });
    return out;
  }

  private liveSeats(): number[] {
    const out: number[] = [];
    this.seats.forEach((s, i) => {
      if (s.mode === 'live') out.push(i);
    });
    return out;
  }

  private token(): string {
    let t = '';
    for (let i = 0; i < 4; i++) t += Math.floor(this.deps.random() * 2 ** 32).toString(36);
    return t;
  }

  /** The seat a joiner with this token gets: its own if open, else the lowest open seat. */
  pickSeat(token?: string): number {
    if (this.phase === 'scoring') return -1;
    const open = this.openSeats();
    if (token) {
      const own = open.find((i) => this.seats[i]?.token === token);
      if (own !== undefined) return own;
    }
    return open[0] ?? -1;
  }

  summary(): LobbyGame {
    return {
      id: this.id,
      seats: this.occupied(),
      open: this.phase === 'scoring' ? [] : this.openSeats(),
      phase: this.phase,
      loc: this.loc,
    };
  }

  /** Puts a player in an open seat. The caller has checked that the seat is open. */
  seat(seatNo: number, player: Player): void {
    const seat = this.seats[seatNo] as Seat;
    const now = this.deps.now();
    seat.player = player;
    seat.token = this.token();
    seat.joinedAt = now;
    seat.lastInput = seat.lastActivity = now;
    seat.inputs.clear();
    seat.ignoreSumsBefore = 0;
    this.clearEmptyTimer();
    player.send({
      t: 'joined',
      game: this.id,
      seat: seatNo,
      token: seat.token,
      phase: this.phase,
    });
    if (this.phase === 'waiting') {
      seat.mode = 'live';
      if (this.occupied() === SEATS) this.start();
    } else {
      seat.mode = 'syncing';
      this.sendCatchup(seatNo);
    }
    this.deps.changed();
  }

  // ---- starting ----

  /** The waiting player chose Play single player. */
  single(seatNo: number): void {
    if (this.phase !== 'waiting' || seatNo !== 0) return;
    this.start();
    this.deps.changed();
  }

  private start(): void {
    this.seed = Math.floor(this.deps.random() * 2 ** 32) >>> 0;
    let mask = 0;
    this.seats.forEach((s, i) => {
      if (s.mode === 'live') mask |= 1 << i;
    });
    this.startMask = this.present = mask;
    this.phase = 'playing';
    this.loc = { kind: 'map' };
    this.head = 0;
    this.log = [];
    this.checkpoint = null;
    const now = this.deps.now();
    this.lastSealAt = now;
    for (const s of this.seats) {
      if (s.mode === 'live') {
        s.lastInput = now;
        s.player?.send(this.startMessage());
      }
    }
    this.deps.log(`game ${this.id} started (seed ${this.seed}, mask ${mask})`);
    this.openFrameLog();
    this.updateDuty();
  }

  private startMessage(): ServerMessage {
    const msg = { t: 'start', seed: this.seed, mask: this.startMask } as const;
    return this.record ? { ...msg, rec: this.record } : msg;
  }

  // ---- catching up ----

  /** Everything a browser needs to reach the current frame: a start or checkpoint, then frames. */
  private sendCatchup(seatNo: number): void {
    const player = this.seats[seatNo]?.player;
    if (!player) return;
    const cp = this.checkpoint;
    if (cp) {
      player.send({
        t: 'restore',
        f: cp.frame,
        fresh: false,
        seed: this.seed,
        mask: this.startMask,
      });
      player.sendBinary(encodeRestore(cp.frame, cp.data));
    } else {
      player.send({
        t: 'restore',
        f: 0,
        fresh: true,
        seed: this.seed,
        mask: this.startMask,
        ...(this.record ? { rec: this.record } : {}),
      });
    }
    for (const f of this.log) player.send(frameMessage(f));
  }

  /** A joining browser has caught up (`ready`): from now on it is part of the game. */
  ready(seatNo: number): void {
    const seat = this.seats[seatNo];
    if (!seat || seat.mode !== 'syncing') return;
    const running = this.phase === 'playing' && this.liveSeats().length > 0;
    const from = running ? this.head + GOLIVE_GUARD : this.head;
    seat.mode = 'joining';
    seat.from = from;
    seat.inputs.clear();
    // Whoever left a Keen behind goes first, then the newcomer appears on the lowest Keen.
    for (let s = 0; s < SEATS; s++) {
      if (s === seatNo) continue;
      const other = this.seats[s] as Seat;
      const occupied = other.mode === 'live' || other.mode === 'joining';
      if (!occupied && this.presentAt(s)) this.schedule(from, { t: 'remove', seat: s }, seatNo);
    }
    if (this.presentAt(seatNo)) this.schedule(from, { t: 'remove', seat: seatNo }, seatNo);
    this.schedule(from, { t: 'add', seat: seatNo }, seatNo);
    seat.player?.send({ t: 'golive', from });
    if (this.phase !== 'playing') {
      this.phase = 'playing';
      this.lastSealAt = this.deps.now();
      for (const i of this.liveSeats()) this.seats[i]?.player?.send({ t: 'resumed' });
    }
    this.deps.changed();
    this.trySeal();
  }

  /** Whether seat `s` has a Keen in the game once everything scheduled so far is applied. */
  private presentAt(s: number): boolean {
    let present = (this.present >> s) & 1;
    for (const e of this.scheduled) {
      if (e.ev.t === 'add' && e.ev.seat === s) present = 1;
      if (e.ev.t === 'remove' && e.ev.seat === s) present = 0;
    }
    return present === 1;
  }

  private schedule(frame: number, ev: GameEvent, by?: number): void {
    this.scheduled.push({ frame, ev, by });
  }

  // ---- leaving ----

  /** A player's connection closed, they left, or their input stopped. Opens the seat. */
  drop(seatNo: number): void {
    const seat = this.seats[seatNo];
    if (!seat || seat.mode === 'open') return;
    const wasLive = seat.mode === 'live';
    seat.player = null;
    seat.mode = 'open';
    seat.inputs.clear();
    this.finished.delete(seatNo);
    if (this.phase === 'scoring') {
      this.dropScoring(seatNo);
      return;
    }
    // A newcomer that had not appeared yet takes its pending events with it.
    this.scheduled = this.scheduled.filter((e) => e.by !== seatNo);
    if (this.phase === 'waiting') {
      this.remove('nobody waiting');
      return;
    }
    if (this.liveSeats().length === 0) {
      // A newcomer waiting to appear next to the player who just left cannot go on.
      this.seats.forEach((other, i) => {
        if (other.mode === 'joining') this.kick(i);
      });
    }
    if (this.phase === 'removed') return;
    if (this.occupied() === 0) {
      this.phase = 'empty';
      this.startEmptyTimer();
    } else if (this.liveSeats().length === 0) {
      // Only browsers that are still catching up are left.
      this.phase = 'empty';
    } else if (wasLive && this.phase === 'playing') {
      this.phase = 'paused';
      this.announcePause();
    } else if (wasLive && this.phase === 'paused') {
      this.announcePause();
    }
    this.updateDuty();
    this.deps.changed();
    this.trySeal();
  }

  /** Drops a seat whose player is still connected: they are sent back to the lobby. */
  kick(seatNo: number): void {
    const player = this.seats[seatNo]?.player;
    this.drop(seatNo);
    player?.kicked();
  }

  /** Tells the remaining players who is gone. */
  private announcePause(): void {
    const gone: number[] = [];
    for (let s = 0; s < SEATS; s++) {
      const seat = this.seats[s] as Seat;
      if (seat.mode === 'open' && ((this.present >> s) & 1 || this.scheduledAdds(s))) gone.push(s);
    }
    for (const i of this.liveSeats()) this.seats[i]?.player?.send({ t: 'paused', seats: gone });
  }

  private scheduledAdds(s: number): boolean {
    return this.scheduled.some((e) => e.ev.t === 'add' && e.ev.seat === s);
  }

  /** Continue single player: the Keens of the seats that left are removed and play goes on. */
  continueSolo(seatNo: number): void {
    if (this.phase !== 'paused' || this.seats[seatNo]?.mode !== 'live') return;
    for (let s = 0; s < SEATS; s++) {
      if ((this.seats[s] as Seat).mode === 'open' && this.presentAt(s)) {
        this.schedule(this.head, { t: 'remove', seat: s });
      }
    }
    this.phase = 'playing';
    this.lastSealAt = this.deps.now();
    for (const i of this.liveSeats()) this.seats[i]?.player?.send({ t: 'resumed' });
    this.deps.changed();
    this.trySeal();
  }

  private startEmptyTimer(): void {
    this.clearEmptyTimer();
    const ms = this.deps.config.emptyGameSec * 1000;
    this.emptyTimer = setTimeout(() => this.remove('empty too long'), ms);
  }

  private clearEmptyTimer(): void {
    if (this.emptyTimer) clearTimeout(this.emptyTimer);
    this.emptyTimer = null;
  }

  /** The ending started. */
  ending(seatNo: number): void {
    if (this.phase === 'playing' && this.seats[seatNo]?.mode === 'live') this.inEnding = true;
  }

  /**
   * A player said the ending is over; when every live player has, the game moves on to the high
   * score table: the lowest seat types the team name if the score qualifies.
   */
  finish(seatNo: number): void {
    if (this.phase !== 'playing' || this.seats[seatNo]?.mode !== 'live') return;
    this.finished.add(seatNo);
    const live = this.liveSeats();
    if (!live.every((s) => this.finished.has(s))) return;
    this.phase = 'scoring';
    this.inEnding = false;
    this.entrySeat = this.deps.scores.rank(this.score) >= 0 ? Math.min(...live) : -1;
    this.entrySince = this.deps.now();
    this.deps.log(`game ${this.id} finished with ${this.score} points`);
    this.announceEnded();
    this.deps.changed();
  }

  private announceEnded(): void {
    for (const s of this.seats) s.player?.send({ t: 'ended', entry: this.entrySeat });
  }

  /** The team name for the table. Returns false when it is refused. */
  submitScore(seatNo: number, name: string, score: number, parts: number): boolean {
    if (this.phase !== 'scoring' || seatNo !== this.entrySeat) return false;
    if (!printable(name, SCORE_NAME_MAX) || score > this.score) return false;
    if (this.deps.scores.insert({ score, name, parts }) < 0) return false;
    this.deps.scoresChanged();
    this.entrySeat = -1;
    this.announceEnded();
    return true;
  }

  /** Someone left after the ending: the game goes when the last one does. */
  private dropScoring(seatNo: number): void {
    if (this.occupied() === 0) {
      this.remove('finished');
      return;
    }
    if (seatNo === this.entrySeat) {
      this.entrySeat = -1;
      this.announceEnded();
    }
    this.deps.changed();
  }

  /**
   * A chat line from a seat, passed on to everybody in the game (the sender too, so the line shows
   * once it is through). Returns false when it is dropped: an empty seat, or too many lines lately.
   */
  chat(seatNo: number, text: string): boolean {
    const mode = this.seats[seatNo]?.mode;
    if (mode === undefined || mode === 'open') return false;
    const now = this.deps.now();
    const recent = (this.chatTimes.get(seatNo) ?? []).filter((t) => now - t < CHAT_WINDOW_MS);
    if (recent.length >= CHAT_RATE) return false;
    recent.push(now);
    this.chatTimes.set(seatNo, recent);
    for (const s of this.seats) {
      if (s.mode !== 'open') s.player?.send({ t: 'chat', seat: seatNo, text });
    }
    return true;
  }

  /** Removes the game; whoever is still in it is back in the lobby. */
  remove(why: string): void {
    this.clearEmptyTimer();
    this.phase = 'removed';
    this.deps.log(`game ${this.id} removed (${why})`);
    this.deps.removed(this);
    for (const s of this.seats) {
      s.player = null;
      s.mode = 'open';
    }
    this.deps.changed();
  }

  /** Close of the server: forget timers. */
  dispose(): void {
    this.clearEmptyTimer();
  }

  // ---- inputs and sealing ----

  /** An input for frame `f`. Returns false when it was dropped. */
  input(seatNo: number, f: number, b: number): boolean {
    const seat = this.seats[seatNo];
    if (!seat || (seat.mode !== 'live' && seat.mode !== 'joining')) return false;
    if (f < this.head || f - this.head > INPUT_HORIZON) return false;
    if (seat.mode === 'joining' && f < seat.from) return false;
    if (seat.inputs.has(f)) return false;
    seat.inputs.set(f, b);
    seat.lastInput = this.deps.now();
    this.trySeal();
    return true;
  }

  touch(seatNo: number): void {
    const seat = this.seats[seatNo];
    if (seat) seat.lastActivity = this.deps.now();
  }

  private requires(seat: Seat, n: number): boolean {
    return seat.mode === 'live' || (seat.mode === 'joining' && n >= seat.from);
  }

  /** Seals every frame whose inputs are all in. */
  private trySeal(): void {
    if (this.phase !== 'playing') return;
    for (;;) {
      const n = this.head;
      const inputs: number[] = [];
      let needed = 0;
      for (const seat of this.seats) {
        if (this.requires(seat, n)) {
          const b = seat.inputs.get(n);
          if (b === undefined) return;
          inputs.push(b);
          needed++;
        } else {
          inputs.push(NEUTRAL_INPUT);
        }
      }
      if (needed === 0) return;
      const ev: GameEvent[] = [];
      if (n === 0) ev.push({ t: 'start', seed: this.seed, mask: this.startMask });
      const rest: typeof this.scheduled = [];
      for (const e of this.scheduled) {
        if (e.frame === n) ev.push(e.ev);
        else rest.push(e);
      }
      this.scheduled = rest;
      for (const e of ev) {
        if (e.t === 'add') {
          this.present |= 1 << e.seat;
          const seat = this.seats[e.seat];
          if (seat && seat.mode === 'joining') {
            seat.mode = 'live';
            seat.lastInput = this.deps.now();
            this.updateDuty();
          }
        } else if (e.t === 'remove') {
          this.present &= ~(1 << e.seat);
        }
      }
      const frame: SealedFrame = { n, inputs, ev };
      this.log.push(frame);
      this.head = n + 1;
      this.lastSealAt = this.deps.now();
      for (const seat of this.seats) seat.inputs.delete(n);
      const msg = frameMessage(frame);
      for (const seat of this.seats) {
        if (seat.mode !== 'open') seat.player?.send(msg);
      }
      this.writeFrameLog(frame);
    }
  }

  /** Called about once a second: drops the seats whose input the game has waited for too long. */
  checkTimeouts(): void {
    const now = this.deps.now();
    const limit = this.deps.config.disconnectSec * 1000;
    if (this.resync && now - this.resync.since > RESYNC_TIMEOUT_MS) this.resync = null;
    if (this.phase === 'scoring' && this.entrySeat >= 0 && now - this.entrySince > SCORE_ENTRY_MS) {
      this.deps.log(`game ${this.id}: no team name was typed`);
      this.entrySeat = -1;
      this.announceEnded();
    }
    if (this.phase === 'playing' && !this.inEnding) {
      const blocked: number[] = [];
      this.seats.forEach((seat, i) => {
        if (this.requires(seat, this.head) && !seat.inputs.has(this.head)) blocked.push(i);
      });
      for (const i of blocked) {
        const seat = this.seats[i] as Seat;
        if (now - Math.max(seat.lastInput, this.lastSealAt) > limit) {
          this.deps.log(`game ${this.id}: player ${i + 1} sent no input for ${limit / 1000} s`);
          this.kick(i);
        }
      }
    }
    // A browser that stopped answering while it was catching up.
    this.seats.forEach((seat, i) => {
      if ((seat.mode === 'syncing' || seat.mode === 'joining') && now - seat.lastActivity > limit) {
        this.kick(i);
      }
    });
  }

  // ---- checksums, checkpoints, resync ----

  /** Tells the browsers which of them uploads checkpoints (the one in the game longest). */
  private updateDuty(): void {
    const duty = this.dutySeat();
    this.seats.forEach((seat, i) => {
      const on = i === duty;
      if (seat.duty !== on && seat.mode !== 'open') {
        seat.duty = on;
        seat.player?.send({ t: 'duty', on });
      } else if (seat.mode === 'open') {
        seat.duty = false;
      }
    });
  }

  /** The seat that has been in the game longest. */
  private dutySeat(): number {
    let best = -1;
    this.seats.forEach((s, i) => {
      if (s.mode !== 'live') return;
      if (best < 0 || s.joinedAt < (this.seats[best] as Seat).joinedAt) best = i;
    });
    return best;
  }

  sum(seatNo: number, f: number, c: number, loc: number, score?: number): void {
    const seat = this.seats[seatNo];
    if (!seat || seat.mode !== 'live') return;
    if (score !== undefined && f >= this.scoreFrame && f <= this.head) {
      this.score = score;
      this.scoreFrame = f;
    }
    if (f >= this.lastLocFrame && f <= this.head) {
      this.lastLocFrame = f;
      const next: Location = loc === 0 ? { kind: 'map' } : { kind: 'level', level: loc };
      if (JSON.stringify(next) !== JSON.stringify(this.loc)) {
        this.loc = next;
        this.deps.changed();
      }
    }
    if (f < seat.ignoreSumsBefore || f > this.head) return;
    let reports = this.sums.get(f);
    if (!reports) {
      reports = new Map();
      this.sums.set(f, reports);
    }
    reports.set(seatNo, c);
    this.writeFrameLogLine(`sum ${f} ${seatNo + 1} ${c.toString(16).padStart(8, '0')} ${loc}`);
    this.compare(f, reports);
    this.tryCommit(f);
    for (const old of this.sums.keys()) {
      if (old < this.head - SUM_KEEP_FRAMES) this.sums.delete(old);
    }
    for (const old of this.agreed.keys()) {
      if (old < this.head - SUM_KEEP_FRAMES) this.agreed.delete(old);
    }
  }

  /** Once every live player reported frame `f`, the reports agree or the game has drifted. */
  private compare(f: number, reports: Map<number, number>): void {
    const live = this.liveSeats().filter((s) => (this.seats[s] as Seat).ignoreSumsBefore <= f);
    if (live.length === 0 || !live.every((s) => reports.has(s))) return;
    const first = reports.get(live[0] as number) as number;
    if (live.every((s) => reports.get(s) === first)) {
      this.agreed.set(f, first);
      return;
    }
    if (this.resync) return;
    const duty = this.dutySeat();
    const dutySum = reports.get(duty);
    const targets = live.filter((s) => s !== duty && reports.get(s) !== dutySum);
    if (targets.length === 0) return;
    const detail = live.map((s) => `player ${s + 1} ${reports.get(s)}`).join(', ');
    this.deps.log(
      `game ${this.id}: desync at frame ${f}: ${detail}; resyncing from player ${duty + 1}`,
    );
    this.writeFrameLogLine(`desync ${f}`);
    this.resync = { targets, since: this.deps.now() };
    this.seats[duty]?.player?.send({ t: 'snap' });
  }

  /** A binary snapshot upload from a browser. */
  upload(seatNo: number, up: Upload): void {
    const seat = this.seats[seatNo];
    if (!seat || seat.mode !== 'live') return;
    if (up.data.length === 0 || up.data.length > MAX_SNAPSHOT || up.frame > this.head) return;
    if (up.kind === UP_RESYNC) {
      this.resyncUpload(seatNo, up);
      return;
    }
    if (up.kind !== UP_CHECKPOINT) return;
    if (this.checkpoint && up.frame <= this.checkpoint.frame) return;
    this.pendingCheckpoints.set(up.frame, { sum: up.sum, data: up.data });
    while (this.pendingCheckpoints.size > 3) {
      const oldest = Math.min(...this.pendingCheckpoints.keys());
      this.pendingCheckpoints.delete(oldest);
    }
    this.tryCommit(up.frame);
  }

  /** A checkpoint counts once every live player reported the same checksum for its frame. */
  private tryCommit(f: number): void {
    const pending = this.pendingCheckpoints.get(f);
    if (!pending || this.agreed.get(f) !== pending.sum) return;
    this.commit({ frame: f, sum: pending.sum, data: pending.data });
  }

  private commit(cp: Checkpoint): void {
    this.checkpoint = cp;
    this.log = this.log.filter((fr) => fr.n >= cp.frame);
    for (const k of this.pendingCheckpoints.keys())
      if (k <= cp.frame) this.pendingCheckpoints.delete(k);
    for (const k of this.agreed.keys()) if (k < cp.frame) this.agreed.delete(k);
  }

  private resyncUpload(seatNo: number, up: Upload): void {
    if (!this.resync || seatNo !== this.dutySeat()) return;
    const targets = this.resync.targets;
    this.resync = null;
    this.commit({ frame: up.frame, sum: up.sum, data: up.data });
    for (const t of targets) {
      const seat = this.seats[t];
      if (!seat || seat.mode !== 'live') continue;
      seat.ignoreSumsBefore = this.head + 1;
      this.sendCatchup(t);
    }
  }

  // ---- frame log (debug) ----

  private openFrameLog(): void {
    if (!this.deps.frameLogDir) return;
    try {
      mkdirSync(this.deps.frameLogDir, { recursive: true });
      this.logFile = join(this.deps.frameLogDir, `game-${this.id}.netlog`);
      appendFileSync(
        this.logFile,
        `# keen1-online netlog\nseed ${this.seed}\nmask ${this.startMask}\n`,
      );
    } catch (e) {
      this.deps.log(`frame log: ${e instanceof Error ? e.message : String(e)}`);
      this.logFile = null;
    }
  }

  private writeFrameLogLine(line: string): void {
    if (!this.logFile) return;
    try {
      appendFileSync(this.logFile, line + '\n');
    } catch {
      this.logFile = null;
    }
  }

  private writeFrameLog(frame: SealedFrame): void {
    if (!this.logFile) return;
    const ev = frame.ev
      .filter((e) => e.t !== 'start')
      .map((e) => `${e.t}:${(e as { seat: number }).seat + 1}`);
    this.writeFrameLogLine(
      `${frame.n} ${frame.inputs.join(' ')}${ev.length ? ' ' + ev.join(' ') : ''}`,
    );
  }
}

export function frameMessage(f: SealedFrame): ServerMessage {
  return f.ev.length
    ? { t: 'frame', n: f.n, in: f.inputs, ev: f.ev }
    : { t: 'frame', n: f.n, in: f.inputs };
}
