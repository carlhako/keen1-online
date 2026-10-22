import { existsSync, readFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadConfigOrExit } from './config.ts';
import { gameDataComplete } from './http.ts';
import { GameServer } from './server.ts';

const config = loadConfigOrExit();

// A release carries the web build next to server/ (../web); a checkout has it in ../build/web.
function findWebDir(): string {
  if (process.env.KEEN_WEB_DIR) return resolve(process.env.KEEN_WEB_DIR);
  const here = fileURLToPath(new URL('..', import.meta.url));
  for (const rel of ['../web', '../build/web']) {
    const dir = resolve(here, rel);
    if (existsSync(resolve(dir, 'index.html'))) return dir;
  }
  return resolve(here, '../build/web');
}

const webDir = findWebDir();
console.log(`serving ${webDir}`);
// Debug option: KEEN_FRAMELOG=dir writes the sealed frames of every game to dir/game-N.netlog,
// which `keen1 --replay` can play back natively.
const frameLogDir = process.env.KEEN_FRAMELOG ? resolve(process.env.KEEN_FRAMELOG) : undefined;
if (frameLogDir) console.log(`writing frame logs to ${frameLogDir}`);
const dataDir = resolve(config.dataDir);
console.log(`keeping saves and high scores in ${dataDir}`);
const gameDir = join(dataDir, 'game');
console.log(
  gameDataComplete(gameDir)
    ? `serving the game files from ${gameDir}`
    : `NO GAME DATA in ${gameDir}: the page cannot start a game (see the README, "Game data")`,
);
function readVersion(): string | undefined {
  try {
    const pkg = JSON.parse(
      readFileSync(fileURLToPath(new URL('../package.json', import.meta.url)), 'utf8'),
    ) as { version?: unknown };
    return typeof pkg.version === 'string' ? pkg.version : undefined;
  } catch {
    return undefined;
  }
}

const server = new GameServer({
  config,
  version: readVersion(),
  webDir,
  frameLogDir,
  dataDir,
  gameDir,
  log: (message) => console.log(`${new Date().toISOString()} ${message}`),
});
const port = await server.listen();
console.log(`Keen online server listening on http://localhost:${port}`);

function shutdown(): void {
  console.log('shutting down');
  void server.close().then(() => process.exit(0));
}
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
