// A headless online client for the end-to-end tests: the game itself (the Node build from
// `make nodenet`) with the same web/net.js bridge the page uses, driven over IPC by e2e.mjs.
//
//   node bot.mjs SERVER_URL BUILD_ID DATA_DIR
//
// Requests arrive as {id, cmd, ...} and are answered with {id, result}. The bot also reports
// {event, ...} messages, for example when the game starts.
import { createRequire } from 'node:module';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const [, , serverUrl, buildId, dataDir] = process.argv;
const here = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const url = new URL(serverUrl);

// What net.js expects from a page.
globalThis.window = globalThis;
globalThis.location = { protocol: url.protocol, host: url.host };
const store = new Map();
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
};
globalThis.KEEN_BUILD_ID = buildId;

const log = [];
let started = false;
const createKeen = require(resolve(here, '../../build/nodenet/keen1.js'));
const Module = await createKeen({
  print: (t) => log.push(String(t)),
  printErr: (t) => log.push('ERR ' + t),
});

const net = Module.net;
const sums = {};
let lastFrame = -1;
let framesSent = 0;
let stalledUntil = 0;
let hold = { bits: 8, frames: Infinity };

// A headless game would run as fast as frames arrive and flood the server (which allows 60 inputs
// a second per player). Like a real browser it paces itself, except while catching up.
const frameMs = Number(process.env.BOT_FRAME_MS ?? 25);
let lastDeliver = 0;
const origFrame = net.frame;
net.frame = (f) => {
  lastFrame = f;
  if (Date.now() < stalledUntil) return null;
  const now = performance.now();
  if (frameMs > 0 && net.backlog(f) <= 12 && now - lastDeliver < frameMs) return null;
  const m = origFrame(f);
  if (m) lastDeliver = now;
  return m;
};
const origSum = net.sum;
net.sum = (f, c, l) => {
  sums[f] = { c: c >>> 0, l };
  origSum(f, c, l);
};
const origSend = net.sendInput;
net.sendInput = (f, b) => {
  framesSent++;
  origSend(f, b);
};
net.botInput = () => {
  if (hold.frames <= 0) hold = { bits: 8, frames: Infinity };
  hold.frames--;
  return hold.bits;
};
if (process.env.BOT_LEAD) net.lead = () => Number(process.env.BOT_LEAD);    // a test can pin the lead
net.onboot = () => {
  if (started) return;
  started = true;
  process.send({ event: 'started' });
  Module.callMain(['--bot', '--data', dataDir]);
};
net.onleave = () => process.send({ event: 'left' });
net.onchange = () => {};
net.connect();

function status() {
  return {
    state: net.state,
    game: net.game,
    paused: net.paused,
    waiting: net.waiting,
    duty: net.duty,
    rtt: net.rtt,
    lead: net.lead(),
    lobby: net.lobby,
    rejected: net.rejected,
    frame: lastFrame,
    sent: framesSent,
    debug: net.debug(),
    ended: !!net.ended,
  };
}

const handlers = {
  status: () => status(),
  create: () => net.create(),
  single: () => net.single(),
  join: ({ game }) => net.join(game),
  leave: () => net.leave(),
  continue: () => net.continueSolo(),
  hold: ({ bits, frames }) => {
    hold = { bits, frames: frames ?? Infinity };
  },
  stall: ({ ms }) => {
    stalledUntil = Date.now() + ms;
  },
  corrupt: ({ kind }) => net.testCommand(kind ?? 1),
  // Test aids with the same arguments on every client: warp {frame, player, x, y}; parts {frame, parts}
  warp: ({ frame, player, x, y }) => net.testCommand(3, [frame, player, x, y]),
  parts: ({ frame, parts }) => net.testCommand(4, [frame, parts]),
  sums: () => sums,
  log: () => log.slice(),
  finished: () => net.finished(),
  exit: () => {
    setTimeout(() => process.exit(0), 10);
  },
};

process.on('message', (m) => {
  const h = handlers[m.cmd];
  let result;
  try {
    result = h ? h(m) : undefined;
  } catch (e) {
    result = { error: String(e) };
  }
  process.send({ id: m.id, result: result ?? null });
});
process.send({ event: 'ready' });
