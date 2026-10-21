// A TCP proxy that holds every chunk back for `delayMs` in each direction, to play with an
// artificial round trip of 2 x delayMs: `node delay-proxy.mjs LISTEN_PORT TARGET_PORT DELAY_MS`.
// Also importable, for the tests.
import net from 'node:net';
import { fileURLToPath } from 'node:url';

export function delayProxy(targetPort, delayMs, listenPort = 0) {
  const sockets = new Set();
  const server = net.createServer((client) => {
    const upstream = net.connect(targetPort, '127.0.0.1');
    sockets.add(client).add(upstream);
    const pipe = (from, to) => {
      from.on('data', (chunk) => setTimeout(() => to.writable && to.write(chunk), delayMs));
      from.on('close', () => setTimeout(() => to.destroy(), delayMs));
      from.on('error', () => to.destroy());
    };
    pipe(client, upstream);
    pipe(upstream, client);
  });
  return new Promise((resolve) =>
    server.listen(listenPort, '127.0.0.1', () =>
      resolve({
        port: server.address().port,
        close: () => {
          for (const s of sockets) s.destroy();
          return new Promise((r) => server.close(r));
        },
      }),
    ),
  );
}

if (process.argv[1] === fileURLToPath(import.meta.url)) {
  const [, , listen, target, delay] = process.argv;
  const p = await delayProxy(Number(target), Number(delay), Number(listen));
  console.log(`proxy on ${p.port} -> ${target}, ${delay} ms each way`);
}
