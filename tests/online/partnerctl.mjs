// Sends one command to partner.mjs and prints the answer: node partnerctl.mjs '{"cmd":"status"}'
import net from 'node:net';

const port = Number(process.env.PARTNER_PORT ?? 9001);
const socket = net.connect(port, '127.0.0.1', () => socket.write(process.argv[2] + '\n'));
socket.on('data', (d) => {
  process.stdout.write(d);
  socket.end();
});
socket.on('error', (e) => {
  console.error(String(e));
  process.exit(1);
});
