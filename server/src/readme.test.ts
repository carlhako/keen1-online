import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { defaultConfig } from './config.ts';

const readme = readFileSync(new URL('../README.md', import.meta.url), 'utf8');
const protocol = readFileSync(new URL('./protocol.ts', import.meta.url), 'utf8');

/** The `t` values of the union type that starts at `start` and ends at the next export. */
function messageTypes(start: string): string[] {
  const from = protocol.indexOf(start);
  expect(from).toBeGreaterThan(-1);
  const rest = protocol.slice(from + start.length);
  const end = rest.search(/\n(export |\/\/ ----)/);
  const block = end < 0 ? rest : rest.slice(0, end);
  return [...block.matchAll(/\bt: '([a-z]+)'/g)].map((m) => m[1] as string);
}

/** The first column of the table under the heading `title` in the README. */
function documented(title: string): string[] {
  const at = readme.indexOf(`### ${title}`);
  expect(at).toBeGreaterThan(-1);
  const rest = readme.slice(at + title.length + 4);
  const end = rest.search(/\n#{2,3} /);
  const section = end < 0 ? rest : rest.slice(0, end);
  return [...section.matchAll(/^\| `([a-z]+)`\s*\|/gm)].map((m) => m[1] as string);
}

describe('server/README.md', () => {
  it('documents every message the browser can send, and nothing else', () => {
    const impl = messageTypes('export type ClientMessage =');
    expect(impl.length).toBeGreaterThan(8);
    expect([...documented('Browser to server')].sort()).toEqual([...impl].sort());
  });

  it('documents every message the server can send, and nothing else', () => {
    const impl = messageTypes('export type ServerMessage =');
    expect(impl.length).toBeGreaterThan(8);
    expect([...documented('Server to browser')].sort()).toEqual([...impl].sort());
  });

  it('documents the events a sealed frame can carry', () => {
    for (const t of messageTypes('export type GameEvent =')) {
      expect(readme).toContain(`{"t":"${t}"`);
    }
  });

  it('lists every reject reason', () => {
    const m = protocol.match(/export type RejectReason =([^;]*);/);
    const reasons = [...(m?.[1] ?? '').matchAll(/'([a-z]+)'/g)].map((x) => x[1] as string);
    expect(reasons.length).toBeGreaterThan(3);
    for (const r of reasons) expect(readme).toContain(`\`${r}\``);
  });

  it('documents every configuration key with its default', () => {
    for (const [key, value] of Object.entries(defaultConfig())) {
      const shown = typeof value === 'string' ? `"${value}"` : String(value);
      expect(readme).toMatch(new RegExp(`\\| \`${key}\`\\s*\\| ${shown}\\s*\\|`));
    }
  });
});
