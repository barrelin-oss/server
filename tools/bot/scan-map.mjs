// Analisa um .amd e sugere areas andaveis para spawners/initial points.
// Uso: node scan-map.mjs <arquivo.amd> [janela=40]
import { readFileSync } from "node:fs";

const path = process.argv[2];
if (!path) {
    console.error("uso: node scan-map.mjs <arquivo.amd> [janela]");
    process.exit(1);
}
const win = Number(process.argv[3] ?? 40);

const buf = readFileSync(path);
const header = buf.subarray(0, 256).toString("latin1").replaceAll("\0", " ");
const getVal = (key) => {
    const m = header.match(new RegExp(`${key}[ =\t]+(\\d+)`));
    return m ? Number(m[1]) : 0;
};
const w = getVal("MAPSIZEX");
const h = getVal("MAPSIZEY");
const ts = getVal("TILESIZE");
if (!w || !h || !ts) {
    console.error("header invalido:", header.slice(0, 120));
    process.exit(1);
}
console.log(`mapa ${w}x${h}, tile_size=${ts}`);

// walk[y][x] = 1 se andavel (leitura na mesma ordem do servidor: y externo, x interno)
const walk = new Uint8Array(w * h);
let off = 256;
let total = 0;
for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
        const flags = buf[off + 8];
        const type = buf.readInt16LE(off);
        const ok = (flags & 0x80) === 0 && type !== 19; // nao bloqueado e nao agua
        if (ok) {
            walk[y * w + x] = 1;
            total++;
        }
        off += ts;
    }
}
console.log(`tiles andaveis: ${total} de ${w * h} (${((100 * total) / (w * h)).toFixed(1)}%)`);

// melhor janela win x win por soma de andaveis (amostragem de passo win/2)
const step = Math.max(1, Math.floor(win / 2));
const best = [];
for (let y = 0; y + win <= h; y += step) {
    for (let x = 0; x + win <= w; x += step) {
        let s = 0;
        for (let dy = 0; dy < win; dy++) {
            for (let dx = 0; dx < win; dx++) s += walk[(y + dy) * w + (x + dx)];
        }
        best.push({ x, y, ratio: s / (win * win) });
    }
}
best.sort((a, b) => b.ratio - a.ratio);
console.log(`top 5 janelas ${win}x${win} (x1,y1 -> x2,y2, % andavel):`);
for (const b of best.slice(0, 5)) {
    const cx = b.x + Math.floor(win / 2);
    const cy = b.y + Math.floor(win / 2);
    const centerOk = walk[cy * w + cx] ? "centro andavel" : "CENTRO BLOQUEADO";
    console.log(`  ${b.x},${b.y} -> ${b.x + win - 1},${b.y + win - 1}  ${(100 * b.ratio).toFixed(1)}%  (centro ${cx},${cy}: ${centerOk})`);
}
