import { gzip } from 'node:zlib';
import { promisify } from 'node:util';

/** The name, under `data/`, of the archive that carries every game file in one download. */
export const BUNDLE_NAME = 'game.tar.gz';

const BLOCK = 512;

/** Writes `value` as a NUL-terminated octal field of `width` bytes at `off`. */
function octal(header: Buffer, off: number, width: number, value: number): void {
  header.write(value.toString(8).padStart(width - 1, '0') + '\0', off, width, 'ascii');
}

/**
 * A ustar archive of plain files (short ASCII names, mode 0644, owner 0, time 0), the format
 * `tar` reads and `web/pre.js` unpacks: a 512-byte header per file, its bytes padded to a whole
 * block, and two empty blocks at the end.
 */
export function tar(files: readonly { name: string; data: Buffer }[]): Buffer {
  const parts: Buffer[] = [];
  for (const { name, data } of files) {
    const header = Buffer.alloc(BLOCK);
    header.write(name, 0, 100, 'ascii');
    octal(header, 100, 8, 0o644);
    octal(header, 108, 8, 0);
    octal(header, 116, 8, 0);
    octal(header, 124, 12, data.length);
    octal(header, 136, 12, 0);
    header.write('0', 156, 'ascii');
    header.write('ustar\0' + '00', 257, 'ascii');
    // The checksum is the byte sum of the header with its own field read as spaces.
    header.fill(' ', 148, 156);
    let sum = 0;
    for (const b of header) sum += b;
    header.write(sum.toString(8).padStart(6, '0') + '\0 ', 148, 8, 'ascii');
    parts.push(header, data, Buffer.alloc((BLOCK - (data.length % BLOCK)) % BLOCK));
  }
  parts.push(Buffer.alloc(2 * BLOCK));
  return Buffer.concat(parts);
}

/** The gzip-compressed `tar` of `files`. */
export async function tarGz(files: readonly { name: string; data: Buffer }[]): Promise<Buffer> {
  return promisify(gzip)(tar(files), { level: 9 });
}
