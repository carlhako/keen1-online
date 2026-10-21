// Shared pieces of the end-to-end tests: a server in this process and bots in child processes.
import { fork } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
export const root = resolve(here, '../..');
export const dataDir = process.env.DATA ?? resolve(root, 'shareware/data');

export function sleep(ms) {
  return new Promise((r) => setTimeout(r, ms));
}

export async function until(cond, what, ms = 15000) {
  const t = Date.now();
  for (;;) {
    const v = await cond();
    if (v) return v;
    if (Date.now() - t > ms) throw new Error(`timed out waiting for ${what}`);
    await sleep(15);
  }
}

export class Bot {
  constructor(name, serverUrl, buildId, env = {}) {
    this.name = name;
    this.child = fork(join(here, 'bot.mjs'), [serverUrl, buildId, dataDir], {
      stdio: ['ignore', 'inherit', 'inherit', 'ipc'],
      env: { ...process.env, ...env },
    });
    this.next = 1;
    this.waiting = new Map();
    this.events = [];
    this.child.on('message', (m) => {
      if (m.event) this.events.push(m);
      else this.waiting.get(m.id)?.(m.result);
    });
    this.dead = false;
    this.child.on('exit', () => (this.dead = true));
  }

  ready() {
    return until(() => this.events.some((e) => e.event === 'ready'), `${this.name} ready`);
  }

  request(cmd, args = {}) {
    return new Promise((resolve) => {
      const id = this.next++;
      this.waiting.set(id, resolve);
      this.child.send({ id, cmd, ...args });
    });
  }

  status() {
    return this.request('status');
  }

  async started() {
    await until(() => this.events.some((e) => e.event === 'started'), `${this.name} started`);
  }

  /** Frames this bot has reached (the frame the game is waiting for or simulating). */
  async frame() {
    return (await this.status()).frame;
  }

  async waitFrame(n, what = `${this.name} at frame ${n}`, ms) {
    await until(async () => (await this.frame()) >= n, what, ms);
  }

  kill() {
    this.child.kill('SIGKILL');
  }

  async close() {
    if (!this.dead) {
      this.request('exit');
      await sleep(50);
      this.child.kill('SIGKILL');
    }
  }
}

/** The checksums both bots reported for the frames they have in common. */
export function commonSums(a, b) {
  const out = [];
  for (const f of Object.keys(a)) if (f in b) out.push([Number(f), a[f], b[f]]);
  return out.sort((x, y) => x[0] - y[0]);
}

export function tmpDir() {
  const dir = mkdtempSync(join(tmpdir(), 'keen-e2e-'));
  return { dir, remove: () => rmSync(dir, { recursive: true, force: true }) };
}

export function readText(p) {
  return readFileSync(p, 'utf8');
}
