// merge-legacy-items.ts - acrescenta ao items.yaml os itens de um Item*.cfg legado (3.x) que
// ainda nao existem aqui, sem tocar nos que existem.
//
// Uso: npx tsx merge-legacy-items.ts <Item.cfg> [<Item2.cfg> ...] [--apply]
//
// "Ja existe" e decidido pelo NOME, nao pelo id: as distribuicoes 3.x reordenaram ids
// (no centuu o id 6 e KightDagger, aqui e Claymore) e grafam de outro jeito
// (Excaliber/Excalibur, TemplerSword/TemplarSword, Dick/Dirk). Um nome legado e
// considerado o mesmo item quando, normalizado (minusculas, so letras e digitos), e igual,
// contem/esta contido, ou fica a distancia de edicao <= 2 de um nome daqui.
// Os itens novos entram com o id legado se ele estiver livre; senao recebem o proximo id
// livre a partir de 1001. Ids existentes nunca mudam: loot_tables.yaml e shops.yaml os usam.
// Sem --apply so imprime o que faria.

import { readFileSync, writeFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { parse_cfg } from './parser.js';
import { item_schema } from './schemas.js';

const __dir = dirname(fileURLToPath(import.meta.url));
const YAML = join(__dir, '../../bin/game_configs/items.yaml');

const args = process.argv.slice(2);
const apply = args.includes('--apply');
const files = args.filter(a => !a.startsWith('--'));
if (files.length === 0) {
    console.error('uso: npx tsx merge-legacy-items.ts <Item.cfg> [...] [--apply]');
    process.exit(1);
}

const norm = (s: string) => s.toLowerCase().replace(/[^a-z0-9]/g, '');
const edit_distance = (a: string, b: string): number => {
    const dp: number[] = Array.from({ length: b.length + 1 }, (_, j) => j);
    for (let i = 1; i <= a.length; i++) {
        let prev = dp[0];
        dp[0] = i;
        for (let j = 1; j <= b.length; j++) {
            const tmp = dp[j];
            dp[j] = Math.min(dp[j] + 1, dp[j - 1] + 1, prev + (a[i - 1] === b[j - 1] ? 0 : 1));
            prev = tmp;
        }
    }
    return dp[b.length];
};

// Existing items: id -> name, the normalized name set, and the identity columns per id.
// Both files descend from the same 3.x id space and HBX renamed a lot of entries
// (706 DarkKnightHauberk here is MasterHauberk(M) there, IndigoDye is Dye(Indigo)), so a
// legacy record whose id exists here with the same type/equip_pos/sprite/sprite_frame is
// the same item under another label and is not added again.
const yaml = readFileSync(YAML, 'utf-8');
const existing_ids = new Set<number>();
const existing_names: string[] = [];
const identity_by_id = new Map<number, string>();
const identity = (r: Record<string, unknown>) => `${r.type}/${r.equip_pos}/${r.sprite}/${r.sprite_frame}`;
for (const m of yaml.matchAll(/\{id: (\d+), name: ([^,]+),([^}]*)\}/g)) {
    const id = Number(m[1]);
    existing_ids.add(id);
    existing_names.push(norm(m[2]));
    const cols: Record<string, unknown> = {};
    for (const kv of m[3].matchAll(/(\w+): ([^,]+)/g)) cols[kv[1]] = kv[2].trim();
    identity_by_id.set(id, identity(cols));
}
const same_item = (legacy: string): string | null => {
    const n = norm(legacy);
    // Exact, or a spelling variant of a long enough name (Excaliber/Excalibur,
    // KightDagger/KnightDagger, Knecklace/Necklace). No containment: "Dagger+1" is not
    // "Dagger" and "GiantSword" is not "Sword".
    for (const e of existing_names) {
        if (e === n) return e;
        if (n.length >= 8 && e.length >= 8 && Math.abs(e.length - n.length) <= 2 && edit_distance(e, n) <= 2) return e;
    }
    return null;
};

// Legacy records (first occurrence of a name wins)
const records = files.flatMap(f => parse_cfg(readFileSync(f, 'latin1'), item_schema));
const seen = new Set<string>();
const fresh: Record<string, unknown>[] = [];
const skipped: string[] = [];
for (const r of records) {
    const name = String(r.name);
    const n = norm(name);
    if (seen.has(n)) continue;
    seen.add(n);
    const legacy_id = Number(r.id);
    if (identity_by_id.get(legacy_id) === identity(r)) { skipped.push(`${name}=id${legacy_id}`); continue; }
    const match = same_item(name);
    if (match) { skipped.push(`${name}~${match}`); continue; }
    fresh.push(r);
}

// Ids: keep the legacy one when free, otherwise the next free id from 1001
let next = 1001;
const taken = new Set(existing_ids);
const renumbered: string[] = [];
for (const r of fresh) {
    const legacy_id = Number(r.id);
    if (!taken.has(legacy_id)) {
        taken.add(legacy_id);
        continue;
    }
    while (taken.has(next)) next++;
    renumbered.push(`${r.name}: ${legacy_id}->${next}`);
    r.id = next;
    taken.add(next);
}
fresh.sort((a, b) => Number(a.id) - Number(b.id));

// Same flow style as emitters.ts (one item per line)
const flow = (r: Record<string, unknown>): string =>
    '  - {' + Object.entries(r).map(([k, v]) => `${k}: ${v}`).join(', ') + '}';

console.log(`legado: ${records.length} registros, ${seen.size} nomes; ja existem (nome igual/parecido): ${skipped.length}; novos: ${fresh.length}`);
if (renumbered.length) console.log(`renumerados (id legado ocupado): ${renumbered.length}\n  ${renumbered.join('\n  ')}`);
console.log(`parecidos ignorados (amostra): ${skipped.slice(0, 15).join(' ')}`);
console.log(`novos: ${fresh.map(r => `${r.id}:${r.name}`).join(' ')}`);

if (apply) {
    const header = `  # --- Added 2026-09-05 from the legacy Item.cfg/Item2.cfg/Item3.cfg (centuu/HelbreathServer, Helbreath 3.82) by tools/convert/merge-legacy-items.ts: items that had no equivalent here by name. Legacy ids kept when free, otherwise renumbered from 1001. ---\n`;
    const out = yaml.replace(/\s*$/, '\n') + header + fresh.map(flow).join('\n') + '\n';
    writeFileSync(YAML, out, 'utf-8');
    console.log(`items.yaml: +${fresh.length} itens`);
}
