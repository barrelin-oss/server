// mapdata-legacy.mjs - converte os MapData/*.txt do servidor legado (Helbreath 3.x)
// para o formato bin/mapdata/<mapa>.yaml que o servidor moderno carrega.
//
// Uso: node mapdata-legacy.mjs <dir dos .txt> <NPC.cfg legado> <dir de saida> [--only-amd <dir com .amd>]
//
// Tokens legados tratados (formato "token = valores"):
//   map-location = <nome>
//   initial-point = <id> <x> <y>
//   spot-mob-generator = <id> <tipo> <x1> <y1> <x2> <y2> <tipo-npc> <max>   (tipo 1 = retangulo)
//   teleport-loc = <x> <y> <mapa-destino> <x> <y> <direcao>
//   no-attack-area = <id> <left> <top> <right> <bottom>
//   fish-point = <id> <x> <y>        waypoint = <id> <x> <y>       mineral-point = <id> <x> <y>
//   random-mob-generator = <ativo> <nivel>      level-limit = <n>     upper-level-limit = <n>
//   fixed-dayornight-mode = <1|0>   max-fish = <n>   max-mineral = <n>
// O tipo de NPC do gerador vira tambem npc_name (via NPC.cfg), que o servidor prefere ao
// numero: a tabela numerica spot_mob_mapping.h so conhece 22 tipos.

import { readFileSync, writeFileSync, readdirSync, existsSync, mkdirSync } from "node:fs";
import { join, basename } from "node:path";

const [srcDir, npcCfg, outDir, ...rest] = process.argv.slice(2);
if (!srcDir || !npcCfg || !outDir) {
    console.error("uso: node mapdata-legacy.mjs <dir txt> <NPC.cfg> <dir saida> [--only-amd <dir amd>]");
    process.exit(1);
}
const onlyAmdIdx = rest.indexOf("--only-amd");
const amdDir = onlyAmdIdx >= 0 ? rest[onlyAmdIdx + 1] : null;
const amdNames = amdDir
    ? new Set(readdirSync(amdDir).filter((f) => /\.amd$/i.test(f)).map((f) => f.replace(/\.amd$/i, "").toLowerCase()))
    : null;

// NPC.cfg: "Npc = <nome> <tipo> ..." (separado por tabs/espacos)
const typeToName = new Map();
for (const line of readFileSync(npcCfg, "latin1").split(/\r?\n/)) {
    const f = line.trim().split(/\s+/);
    if (f[0]?.toLowerCase() !== "npc" || f[1] !== "=") continue;
    const name = f[2];
    const type = Number(f[3]);
    if (name && Number.isFinite(type) && !typeToName.has(type)) typeToName.set(type, name);
}

const q = (s) => `"${String(s).replace(/"/g, '\\"')}"`;
const RESPAWN_MS = 15000; // o gerador legado repunha quase de imediato; 15 s e um ritmo classico razoavel

mkdirSync(outDir, { recursive: true });
let written = 0, skippedMaps = 0, waypointGens = 0, unknownTypes = new Map();

for (const file of readdirSync(srcDir).filter((f) => /\.txt$/i.test(f)).sort()) {
    const base = basename(file).replace(/\.txt$/i, "").toLowerCase();
    if (amdNames && !amdNames.has(base)) { skippedMaps++; continue; }
    const m = { name: base, initial: [], spawners: [], teleports: [], safe: [], fish: [], waypoints: [], minerals: [], fixedNpcs: [], extra: {} };
    for (const raw of readFileSync(join(srcDir, file), "latin1").split(/\r?\n/)) {
        const line = raw.replace(/\/\/.*$/, "").trim();
        if (!line || !line.includes("=")) continue;
        const [lhs, rhs] = line.split("=", 2);
        const tok = lhs.trim().toLowerCase();
        const v = rhs.trim().split(/\s+/);
        const n = v.map(Number);
        switch (tok) {
            case "map-location": m.extra.location = v[0].toLowerCase(); break; // regiao (cidade) a que o mapa pertence, nao o nome do mapa
            case "initial-point": m.initial.push({ id: n[0], x: n[1], y: n[2] }); break;
            case "spot-mob-generator":
                if (n[1] !== 1) { waypointGens++; break; } // tipo 2 = caminho de waypoints, sem equivalente ainda
                m.spawners.push({ id: n[0], x1: n[2], y1: n[3], x2: n[4], y2: n[5], npcType: n[6], max: n[7] });
                break;
            case "teleport-loc": m.teleports.push({ src_x: n[0], src_y: n[1], dest_map: v[2].toLowerCase(), dest_x: n[3], dest_y: n[4], direction: n[5] }); break;
            case "no-attack-area": if (n[1] >= 0 && n[2] >= 0) m.safe.push({ left: n[1], top: n[2], right: n[3], bottom: n[4] }); break; // -10 -10 -10 -10 = desligado
            case "fish-point": m.fish.push({ id: n[0], x: n[1], y: n[2] }); break;
            case "waypoint": m.waypoints.push({ id: n[0], x: n[1], y: n[2] }); break;
            case "npc": m.fixedNpcs.push({ name: v[0], waypoint: n[2] }); break; // npc = <nome> <modo> <waypoint...>: NPC fixo (lojista, oficial) no primeiro waypoint
            case "mineral-point": m.minerals.push(n.length >= 3 ? { id: n[0], x: n[1], y: n[2] } : { id: m.minerals.length, x: n[0], y: n[1] }); break;
            case "random-mob-generator": m.extra.random = { enabled: n[0] === 1, level: n[1] ?? 0 }; break;
            case "maximum-object": m.extra.max_objects = n[0]; break; // teto de mobs do gerador aleatorio (o servidor limita por config)
            case "level-limit": m.extra.level_limit = n[0]; break;
            case "upper-level-limit": m.extra.upper_level_limit = n[0]; break;
            case "fixed-dayornight-mode": m.extra.fixed_day_mode = n[0] === 1; break;
            case "max-fish": m.extra.max_fish = n[0]; break;
            case "max-mineral": m.extra.max_mineral = n[0]; break;
            default: break; // heldeniantower, strike-point, item-event, npc, maximum-object, type...
        }
    }
    const out = [];
    out.push(`# Convertido de MapData/${file} (Helbreath 3.82, centuu/HelbreathServer) por tools/convert/mapdata-legacy.mjs`);
    out.push(`# Geradores de tipo 2 (caminho de waypoints) nao tem equivalente e foram omitidos.`);
    out.push(`name: ${q(m.name)}`);
    if (m.extra.location) out.push(`# location: ${m.extra.location}`);
    if (m.extra.level_limit != null) out.push(`level_limit: ${m.extra.level_limit}`);
    if (m.extra.upper_level_limit != null) out.push(`upper_level_limit: ${m.extra.upper_level_limit}`);
    if (m.extra.fixed_day_mode != null) out.push(`fixed_day_mode: ${m.extra.fixed_day_mode}`);
    if (m.extra.max_fish != null) out.push(`max_fish: ${m.extra.max_fish}`);
    if (m.extra.max_mineral != null) out.push(`max_mineral: ${m.extra.max_mineral}`);
    if (m.extra.random) {
        out.push(`random_mob_generator:`, `  enabled: ${m.extra.random.enabled}`, `  level: ${m.extra.random.level}`);
        if (m.extra.max_objects != null) out.push(`  max_mobs: ${m.extra.max_objects}`);
    }
    if (m.initial.length) { out.push(`initial_points:`); for (const p of m.initial) out.push(`  - { id: ${p.id}, x: ${p.x}, y: ${p.y} }`); }
    if (m.safe.length) { out.push(`safe_zones:`); for (const s of m.safe) out.push(`  - { left: ${s.left}, top: ${s.top}, right: ${s.right}, bottom: ${s.bottom} }`); }
    for (const f of m.fixedNpcs) {
        const wp = m.waypoints.find((w) => w.id === f.waypoint) ?? m.waypoints[0];
        if (!wp) continue;
        m.spawners.push({ id: 1000 + m.spawners.length, x1: wp.x, y1: wp.y, x2: wp.x, y2: wp.y, npcType: 0, max: 1, fixedName: f.name });
    }
    if (m.spawners.length) {
        out.push(`spawners:`);
        for (const s of m.spawners) {
            const name = s.fixedName ?? typeToName.get(s.npcType);
            if (!name) unknownTypes.set(s.npcType, (unknownTypes.get(s.npcType) ?? 0) + 1);
            out.push(`  - { id: ${s.id}, type: 0, x1: ${Math.min(s.x1, s.x2)}, y1: ${Math.min(s.y1, s.y2)}, x2: ${Math.max(s.x1, s.x2)}, y2: ${Math.max(s.y1, s.y2)}, npc_type: ${s.npcType}${name ? `, npc_name: ${q(name)}` : ""}, max_count: ${s.max}, respawn_time_ms: ${RESPAWN_MS} }`);
        }
    }
    if (m.teleports.length) { out.push(`teleports:`); for (const t of m.teleports) out.push(`  - { src_x: ${t.src_x}, src_y: ${t.src_y}, dest_map: ${q(t.dest_map)}, dest_x: ${t.dest_x}, dest_y: ${t.dest_y}, direction: ${t.direction} }`); }
    if (m.fish.length) { out.push(`fish_points:`); for (const p of m.fish) out.push(`  - { id: ${p.id}, x: ${p.x}, y: ${p.y} }`); }
    if (m.minerals.length) { out.push(`mineral_points:`); for (const p of m.minerals) out.push(`  - { id: ${p.id}, x: ${p.x}, y: ${p.y} }`); }
    if (m.waypoints.length) { out.push(`waypoints:`); for (const p of m.waypoints) out.push(`  - { id: ${p.id}, x: ${p.x}, y: ${p.y} }`); }
    writeFileSync(join(outDir, `${m.name}.yaml`), out.join("\n") + "\n", "utf8");
    written++;
}
console.log(`mapas escritos: ${written}, sem .amd local: ${skippedMaps}, geradores de waypoint omitidos: ${waypointGens}`);
if (unknownTypes.size) console.log("tipos de NPC sem nome no NPC.cfg:", [...unknownTypes.entries()].map(([t, c]) => `${t}(${c})`).join(" "));
