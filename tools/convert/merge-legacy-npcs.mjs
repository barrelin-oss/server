// merge-legacy-npcs.mjs - acrescenta ao npcs.yaml os NPCs de um NPC.cfg legado (3.x) que
// ainda nao existem aqui (por nome, com os apelidos desta distribuicao), sem tocar nos
// que existem. Uso: node merge-legacy-npcs.mjs <NPC.cfg> [--apply]
//
// Colunas do NPC.cfg 3.x (cabecalho do arquivo):
//   Name Type HitDice DR HR MinBrvy GoldMin GoldMax ExpMin ExpMax ADT ADR Size Side
//   ActionLmt ATime RstM Magic DayWeek Chat Search RegenTime Attr AbsM MaxMana MagicR AttkRange AreaSize
// O npcs.yaml tem um unico exp: monstros ficam em ~1.4x a media legada e unidades de
// faccao em ~0.8x, que e a relacao dos que ja existiam nos dois lugares.

import { readFileSync, writeFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const YAML = join(here, "../../bin/game_configs/npcs.yaml");
const [cfg, ...rest] = process.argv.slice(2);
const apply = rest.includes("--apply");
if (!cfg) { console.error("uso: node merge-legacy-npcs.mjs <NPC.cfg> [--apply]"); process.exit(1); }

const ALIASES = new Map([
    ["Giant-Crayfish", "Giant-Cray-Fish"], ["Giant-Lizard", "Lizard"], ["Minotaurs", "Minotaurus"],
    ["MasterMage-Orc", "Master-Mage-Orc"],
]);

const yaml = readFileSync(YAML, "utf8");
const have = new Set([...yaml.matchAll(/\{name: ([^,]+),/g)].map((m) => m[1].trim().toLowerCase()));

const rows = [];
for (const line of readFileSync(cfg, "latin1").split(/\r?\n/)) {
    const t = line.trim().split(/\s+/);
    if (t[0] !== "Npc" || t[1] !== "=") continue;
    const [name, type, hd, dr, hr, brv, gmin, gmax, emin, emax, adt, adr, size, side, al, at, rstm, magic, dow, chat, search, regen, attr, absm, mana, mhr, range] = [t[2], ...t.slice(3).map(Number)];
    const yname = ALIASES.get(name) ?? name;
    if (have.has(yname.toLowerCase())) continue;
    const factor = side === 10 ? 1.4 : (side === 1 || side === 2) ? 0.8 : 1.0;
    const exp = Math.max(1, Math.round(((emin + emax) / 2) * factor));
    const gold = gmin > 1 || gmax > 1 ? `, gold_min: ${gmin}, gold_max: ${gmax}` : "";
    rows.push(`  - {name: ${yname}, sprite_id: ${type}, hit_dice: ${hd}, defense_ratio: ${dr}, hit_ratio: ${hr}, min_bravery: ${brv}, exp: ${exp}${gold}, attack_dice: ${adt}, attack_sides: ${adr}, size: ${size}, side: ${side}, action_limit: ${al}, action_time: ${at}, resist_magic: ${rstm}, magic_level: ${magic}, day_of_week: ${dow}, chat_msg: ${chat}, target_search_range: ${search}, regen_time: ${regen}, attribute: ${attr}, abs_damage: ${absm}, max_mana: ${mana}, magic_hit_ratio: ${mhr}, attack_range: ${range}}`);
    have.add(yname.toLowerCase());
}
console.log(`NPCs novos: ${rows.length}`);
for (const r of rows) console.log(r.slice(0, 110));
if (apply) {
    const header = `  # --- Added 2026-09-05 from the legacy NPC.cfg (centuu/HelbreathServer, Helbreath 3.82) by tools/convert/merge-legacy-npcs.mjs: templates that had no equivalent here by name (exp: monsters ~1.4x, faction units ~0.8x the legacy average). ---\n`;
    writeFileSync(YAML, yaml.replace(/\s*$/, "\n") + header + rows.join("\n") + "\n", "utf8");
    console.log(`npcs.yaml: +${rows.length}`);
}
