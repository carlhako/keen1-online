import { existsSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

export interface ServerConfig {
  /** TCP port for the web build and `/ws`. */
  port: number;
  /** Games that may exist at once, in any phase. */
  maxGames: number;
  /** The most frames ahead a browser may be asked to schedule its input. */
  inputLeadMax: number;
  /** Seconds without input before a seat is dropped. */
  disconnectSec: number;
  /** Seconds an empty game is kept. */
  emptyGameSec: number;
  /** Frames between checkpoints (a multiple of the 24-frame checksum interval). */
  checkpointFrames: number;
  /** Where saves and the high score table are kept. A relative path is taken from the working directory. */
  dataDir: string;
}

type NumericKey = Exclude<keyof ServerConfig, 'dataDir'>;

/** How often browsers report a checksum, in frames. */
export const CHECKSUM_FRAMES = 24;

interface Field {
  def: number;
  min: number;
  max: number;
  /** Extra rule, with the text that explains it. */
  rule?: (v: number) => boolean;
  ruleText?: string;
}

const FIELDS: { [K in NumericKey]: Field } = {
  port: { def: 8080, min: 0, max: 65535 },
  maxGames: { def: 5, min: 1, max: 100 },
  inputLeadMax: { def: 6, min: 2, max: 60 },
  disconnectSec: { def: 600, min: 1, max: 3600 },
  emptyGameSec: { def: 60, min: 0, max: 86400 },
  checkpointFrames: {
    def: 240,
    min: CHECKSUM_FRAMES,
    max: 24000,
    rule: (v) => v % CHECKSUM_FRAMES === 0,
    ruleText: `a multiple of ${CHECKSUM_FRAMES}`,
  },
};

const NUMERIC_KEYS = Object.keys(FIELDS) as NumericKey[];
const KEYS: string[] = [...NUMERIC_KEYS, 'dataDir'];

export const DEFAULT_DATA_DIR = 'data';

export function defaultConfig(): ServerConfig {
  const cfg = { dataDir: DEFAULT_DATA_DIR } as ServerConfig;
  for (const key of NUMERIC_KEYS) cfg[key] = FIELDS[key].def;
  return cfg;
}

export type ParseResult =
  | { ok: true; config: ServerConfig; warnings: string[] }
  | { ok: false; errors: string[]; warnings: string[] };

function isRecord(v: unknown): v is Record<string, unknown> {
  return typeof v === 'object' && v !== null && !Array.isArray(v);
}

/** Validates an already-parsed config file. Reports every problem at once, with key names. */
export function parseConfig(raw: unknown): ParseResult {
  if (!isRecord(raw)) {
    return { ok: false, errors: ['config: must be a JSON object'], warnings: [] };
  }
  const warnings = Object.keys(raw)
    .filter((k) => !KEYS.includes(k))
    .map((k) => `unknown config key "${k}" (ignored)`);
  const errors: string[] = [];
  const config = defaultConfig();
  if ('dataDir' in raw) {
    const v = raw.dataDir;
    if (typeof v !== 'string' || v.trim() === '') errors.push('dataDir: must be a directory path');
    else config.dataDir = v;
  }
  for (const key of NUMERIC_KEYS) {
    if (!(key in raw)) continue;
    const f = FIELDS[key];
    const v = raw[key];
    if (typeof v !== 'number' || !Number.isInteger(v)) {
      errors.push(`${key}: must be an integer`);
    } else if (v < f.min || v > f.max) {
      errors.push(`${key}: must be between ${f.min} and ${f.max}`);
    } else if (f.rule && !f.rule(v)) {
      errors.push(`${key}: must be ${f.ruleText}`);
    } else {
      config[key] = v;
    }
  }
  return errors.length ? { ok: false, errors, warnings } : { ok: true, config, warnings };
}

export interface ConfigIo {
  env: Record<string, string | undefined>;
  /** Directory a relative config path is read from. */
  cwd: string;
  exists: (path: string) => boolean;
  read: (path: string) => string;
}

const realIo: ConfigIo = {
  env: process.env,
  // npm runs a script from the package directory but records where it was invoked in INIT_CWD.
  cwd: process.env.INIT_CWD ?? process.cwd(),
  exists: existsSync,
  read: (path) => readFileSync(path, 'utf8'),
};

export type LoadResult =
  | { ok: true; config: ServerConfig; warnings: string[]; notes: string[] }
  | { ok: false; errors: string[]; warnings: string[] };

/**
 * Loads the configuration: the JSON file named by `KEEN_CONFIG` or `./config.json` (defaults when
 * it does not exist), validated, then the `PORT` environment variable applied on top.
 */
export function loadConfig(io: ConfigIo = realIo): LoadResult {
  const path = resolve(io.cwd, io.env.KEEN_CONFIG ?? 'config.json');
  const notes: string[] = [];
  let raw: unknown = {};
  if (io.exists(path)) {
    try {
      raw = JSON.parse(io.read(path));
    } catch (e) {
      return {
        ok: false,
        errors: [`${path}: ${e instanceof Error ? e.message : String(e)}`],
        warnings: [],
      };
    }
    notes.push(`loaded config from ${path}`);
  } else {
    notes.push(`no config file at ${path}; using default settings`);
  }
  const result = parseConfig(raw);
  if (!result.ok) return result;
  const config = { ...result.config };
  const port = io.env.PORT;
  if (port !== undefined && port !== '') {
    const n = Number(port);
    if (!Number.isInteger(n) || n < 0 || n > 65535) {
      return {
        ok: false,
        errors: [`PORT: "${port}" is not a valid port`],
        warnings: result.warnings,
      };
    }
    config.port = n;
  }
  return { ok: true, config, warnings: result.warnings, notes };
}

export interface StartupIo extends ConfigIo {
  log: (message: string) => void;
  error: (message: string) => void;
  exit: (code: number) => never;
}

/** Loads the configuration for startup: prints notes and warnings, and exits 1 on any error. */
export function loadConfigOrExit(
  io: StartupIo = {
    ...realIo,
    log: (m) => console.log(m),
    error: (m) => console.error(m),
    exit: (code) => process.exit(code),
  },
): ServerConfig {
  const result = loadConfig(io);
  for (const w of result.warnings) io.error(`warning: ${w}`);
  if (!result.ok) {
    for (const e of result.errors) io.error(`config error: ${e}`);
    return io.exit(1);
  }
  for (const n of result.notes) io.log(n);
  return result.config;
}
