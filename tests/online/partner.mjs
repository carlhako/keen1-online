// A bot that stays around for manual testing: a second player for a real browser tab.
//
//   node tests/online/partner.mjs SERVER_URL [CONTROL_PORT]
//
// Then, from another shell:  node tests/online/partnerctl.mjs '{"cmd":"status"}'
// (see bot.mjs for the commands). The bot connects with the server's current build id.
import net from 'node:net';
import { Bot, sleep } from './harness.mjs';

const server = process.argv[2] ?? 'http://localhost:8080';
const port = Number(process.argv[3] ?? 9001);
const health = await fetch(`${server}/healthz`).then((r) => r.json());
const env = { BOT_FRAME_MS: process.env.BOT_FRAME_MS ?? '41' };
let bot = new Bot('partner', server, health.buildId, env);
await bot.ready();

const ctl = net.createServer((socket) => {
  let buf = '';
  socket.on('data', async (d) => {
    buf += d;
    let i;
    while ((i = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, i);
      buf = buf.slice(i + 1);
      let reply;
      try {
        const m = JSON.parse(line);
        if (m.cmd === 'restart') {
          bot.kill();
          await sleep(100);
          bot = new Bot('partner', server, health.buildId, env);
          await bot.ready();
          reply = 'restarted';
        } else if (m.cmd === 'kill') {
          bot.kill();
          reply = 'killed';
        } else {
          const { cmd, ...args } = m;
          reply = await bot.request(cmd, args);
        }
      } catch (e) {
        reply = { error: String(e) };
      }
      socket.write(JSON.stringify(reply) + '\n');
    }
  });
});
ctl.listen(port, '127.0.0.1', () => console.log(`partner bot ready, control port ${port}`));
