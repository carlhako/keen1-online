import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { GAME_FILES } from './gamefiles.ts';

const installer = readFileSync(new URL('../../deploy/install.sh', import.meta.url), 'utf8');

describe('game file list', () => {
  const listed = [...installer.matchAll(/^([0-9a-f]{32}) {2}([A-Z0-9_.]+)$/gm)].map((m) => ({
    sum: m[1],
    name: m[2],
  }));

  it('is the same in the installer and the server', () => {
    expect(listed.map((f) => f.name).sort()).toEqual([...GAME_FILES].sort());
  });

  it('has 31 distinct files with an md5 each', () => {
    expect(new Set(GAME_FILES).size).toBe(31);
    expect(new Set(listed.map((f) => f.name)).size).toBe(31);
  });

  it('leaves out the files the game writes itself', () => {
    expect(GAME_FILES).not.toContain('SCORES.CK1');
    expect(GAME_FILES).not.toContain('CTLPANEL.CK1');
  });
});
