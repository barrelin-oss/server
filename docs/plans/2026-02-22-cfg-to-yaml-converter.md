# CFG → YAML Converter — Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build a TypeScript CLI (`tools/convert/`) that reads all legacy Helbreath `.cfg` game data files and writes fresh YAML files to `bin/game_configs/`, using field names derived directly from the legacy C++ decode functions.

**Architecture:** Schema-driven: each cfg type is a `CfgSchema` (trigger word + ordered `FieldDef[]`). A single generic tokenizer splits on `"= \t\n"` (matching Game.cpp's `CStrTok`) and assigns positional tokens to named fields. Per-type emitters serialize to YAML.

**Tech Stack:** TypeScript, tsx (already in tools/migrate), `js-yaml` for YAML serialization.

---

## Field Schemas (reference — confirmed from Game.cpp decode functions)

### Items — trigger `Item`, end `[ENDITEMLIST]`, 26 fields
`id, name, type, equip_pos, effect_type, effect_value1, effect_value2, effect_value3, effect_value4, effect_value5, effect_value6, durability, special_effect, sprite, sprite_frame, price*, weight, appr_value, speed, level_limit, gender_limit, special_effect_value1, special_effect_value2, related_skill, category, item_color`

*`price`: negative → emit `is_for_sale: false, price: Math.abs(v)`; positive → emit `price: v` only.
Four files merged: `Item.cfg`, `Item2.cfg`, `Item3.cfg`, `Item4.cfg` → `items.yaml`.

### NPCs — trigger `Npc`, positional index, 27 fields
`name, sprite_id, hit_dice, defense_ratio, hit_ratio, min_bravery, exp_dice_min, exp_dice_max, gold_dice_min, gold_dice_max, attack_dice, attack_sides, size, side, action_limit, action_time, resist_magic, magic_level, day_of_week, chat_msg, target_search_range, regen_time, attribute, abs_damage, max_mana, magic_hit_ratio, attack_range`

Note: older Npc.cfg files may have fewer than 27 fields — use optional fields (undefined = omit from output).

### Magic — trigger `magic`, explicit id, 21 fields
`id, name, type, delay, duration, mana_cost, range1, range2, e1_dice, e1_sides, e1_bonus, e2_dice, e2_sides, e2_bonus, e3_dice, e3_sides, e3_bonus, int_req, cost, category, attribute`

Emitted with nested effect objects:
```yaml
effect1: { dice: 2, sides: 4, bonus: 1 }
effect2: { dice: 0, sides: 0, bonus: 0 }
effect3: { dice: 0, sides: 0, bonus: 0 }
```

### Skills — trigger `skill`, explicit id, 9 fields
`id, name, type, value1, value2, value3, value4, value5, value6`

Prepend hard-coded `defaults` block (level tiers) before skill entries.

### Quests — trigger `quest`, explicit id, 27 fields
`id, side, type, target_type, max_count, from_id, min_level, max_level, req_skill, req_skill_pct, time_limit, assign_type, reward_type1, reward_amount1, reward_type2, reward_amount2, reward_type3, reward_amount3, contribution, contribution_limit, resp_mode, map, x, y, range, quest_id, req_contribution`

### Teleports — trigger `teleport`, string fields, 11 fields
`npc_name, src_map, tgt_map, x, y, cost, min_level, max_level, side, allow_neutral, allow_criminal`

`allow_neutral` and `allow_criminal` are `TRUE`/`FALSE` strings → emit as bool.
End marker: `[END]`.

### Crusade Structures — trigger `crusade-structure`, explicit id, 4 fields
`id, map_name, type, x, y`

### CraftItem — trigger `crafting`, explicit id, 14 fields
`id, result, i0_id, i0_count, i1_id, i1_count, i2_id, i2_count, i3_id, i3_count, i4_id, i4_count, i5_id, i5_count, skill_limit, difficulty`

Filter pairs where `item_id == -1`. Emit as `ingredients: [{item_id, count}]`.

### BuildItem — trigger `BuildItem`, no explicit id, 23 fields
`name, skill_limit, i0_id, i0_count, i0_value, i1_id, i1_count, i1_value, i2_id, i2_count, i2_value, i3_id, i3_count, i3_value, i4_id, i4_count, i4_value, i5_id, i5_count, i5_value, average_value, max_skill, attribute`

Filter triples where `item_id == 0`. Emit as `ingredients: [{item_id, count, value}]`.

### Alchemy (Potion) — trigger `potion`, explicit id, 16 fields
`id, result, i0_id, i0_count, i1_id, i1_count, i2_id, i2_count, i3_id, i3_count, i4_id, i4_count, i5_id, i5_count, skill_limit, difficulty`

Filter pairs where `item_id == -1`. Output → `recipes.yaml`, key `alchemy_recipes`.

---

## Task 1: Project scaffold

**Files:**
- Create: `tools/convert/package.json`
- Create: `tools/convert/tsconfig.json`

**Step 1: Create package.json**

```json
{
  "name": "hb-convert",
  "version": "1.0.0",
  "private": true,
  "type": "module",
  "scripts": {
    "convert": "tsx convert.ts"
  },
  "dependencies": {
    "js-yaml": "^4.1.0"
  },
  "devDependencies": {
    "@types/js-yaml": "^4.0.9",
    "tsx": "^4.19.2",
    "typescript": "^5.7.2"
  }
}
```

**Step 2: Create tsconfig.json**

```json
{
  "compilerOptions": {
    "target": "ES2022",
    "module": "ES2022",
    "moduleResolution": "bundler",
    "strict": true,
    "esModuleInterop": true,
    "skipLibCheck": true
  },
  "include": ["*.ts"]
}
```

**Step 3: Install deps**

```bash
cd tools/convert && npm install
```

Expected: `node_modules/` created, no errors.

---

## Task 2: Generic parser (`parser.ts`)

**Files:**
- Create: `tools/convert/parser.ts`

The parser mirrors Game.cpp's `CStrTok` with `seps = "= \t\n"`.

**Step 1: Write parser.ts**

```typescript
export type FieldType = 'int' | 'string' | 'bool' | 'price';

export interface FieldDef {
    name: string;
    type: FieldType;
    optional?: boolean;  // if true, missing at end of record is ok (emitted as undefined)
}

export interface CfgSchema {
    trigger: string;
    end_marker?: string;
    fields: FieldDef[];
}

export type CfgRecord = Record<string, unknown>;

// Splits content into tokens using the same separators as Game.cpp CStrTok.
// seps = "= \t\n\r" — splits on equals, space, tab, newline, carriage return.
function tokenize(content: string): string[] {
    return content
        .split(/[= \t\n\r]+/)
        .filter(t => t.length > 0 && !t.startsWith('//'));
}

// Strips line comments from content before tokenizing.
function strip_comments(content: string): string {
    return content
        .split('\n')
        .map(line => {
            const ci = line.indexOf('//');
            return ci >= 0 ? line.slice(0, ci) : line;
        })
        .join('\n');
}

function parse_value(token: string, type: FieldType): unknown {
    switch (type) {
        case 'int':   return parseInt(token, 10);
        case 'string': return token;
        case 'bool':  return token.toUpperCase() === 'TRUE';
        case 'price': {
            const v = parseInt(token, 10);
            return v;  // caller handles is_for_sale logic
        }
    }
}

export function parse_cfg(content: string, schema: CfgSchema): CfgRecord[] {
    const clean = strip_comments(content);
    const tokens = tokenize(clean);
    const records: CfgRecord[] = [];

    let i = 0;
    while (i < tokens.length) {
        const tok = tokens[i];

        if (schema.end_marker && tok === schema.end_marker) break;

        // Check for trigger (case-sensitive, prefix match like Game.cpp's memcmp)
        if (tok === schema.trigger) {
            i++;
            const record: CfgRecord = {};

            for (const field of schema.fields) {
                if (i >= tokens.length) {
                    if (field.optional) break;
                    throw new Error(
                        `Ran out of tokens reading field '${field.name}' ` +
                        `(record so far: ${JSON.stringify(record)})`
                    );
                }
                const raw = tokens[i++];
                const value = parse_value(raw, field.type);

                if (field.type === 'price') {
                    const n = value as number;
                    if (n < 0) {
                        record['is_for_sale'] = false;
                        record[field.name] = Math.abs(n);
                    } else {
                        record[field.name] = n;
                    }
                } else {
                    record[field.name] = value;
                }
            }

            records.push(record);
        } else {
            i++;
        }
    }

    return records;
}
```

**Step 2: Verify it compiles**

```bash
cd tools/convert && npx tsc --noEmit
```

Expected: no errors.

---

## Task 3: Schemas (`schemas.ts`)

**Files:**
- Create: `tools/convert/schemas.ts`

**Step 1: Write schemas.ts**

```typescript
import type { CfgSchema } from './parser.js';

export const item_schema: CfgSchema = {
    trigger: 'Item',
    end_marker: '[ENDITEMLIST]',
    fields: [
        { name: 'id',                    type: 'int' },
        { name: 'name',                  type: 'string' },
        { name: 'type',                  type: 'int' },
        { name: 'equip_pos',             type: 'int' },
        { name: 'effect_type',           type: 'int' },
        { name: 'effect_value1',         type: 'int' },
        { name: 'effect_value2',         type: 'int' },
        { name: 'effect_value3',         type: 'int' },
        { name: 'effect_value4',         type: 'int' },
        { name: 'effect_value5',         type: 'int' },
        { name: 'effect_value6',         type: 'int' },
        { name: 'durability',            type: 'int' },
        { name: 'special_effect',        type: 'int' },
        { name: 'sprite',                type: 'int' },
        { name: 'sprite_frame',          type: 'int' },
        { name: 'price',                 type: 'price' },
        { name: 'weight',                type: 'int' },
        { name: 'appr_value',            type: 'int' },
        { name: 'speed',                 type: 'int' },
        { name: 'level_limit',           type: 'int' },
        { name: 'gender_limit',          type: 'int' },
        { name: 'special_effect_value1', type: 'int' },
        { name: 'special_effect_value2', type: 'int' },
        { name: 'related_skill',         type: 'int' },
        { name: 'category',              type: 'int' },
        { name: 'item_color',            type: 'int' },
    ],
};

export const npc_schema: CfgSchema = {
    trigger: 'Npc',
    fields: [
        { name: 'name',                 type: 'string' },
        { name: 'sprite_id',            type: 'int' },
        { name: 'hit_dice',             type: 'int' },
        { name: 'defense_ratio',        type: 'int' },
        { name: 'hit_ratio',            type: 'int' },
        { name: 'min_bravery',          type: 'int' },
        { name: 'exp_dice_min',         type: 'int' },
        { name: 'exp_dice_max',         type: 'int' },
        { name: 'gold_dice_min',        type: 'int' },
        { name: 'gold_dice_max',        type: 'int' },
        { name: 'attack_dice',          type: 'int' },
        { name: 'attack_sides',         type: 'int' },
        { name: 'size',                 type: 'int' },
        { name: 'side',                 type: 'int' },
        { name: 'action_limit',         type: 'int' },
        { name: 'action_time',          type: 'int' },
        { name: 'resist_magic',         type: 'int' },
        { name: 'magic_level',          type: 'int' },
        { name: 'day_of_week',          type: 'int' },
        { name: 'chat_msg',             type: 'int' },
        { name: 'target_search_range',  type: 'int' },
        { name: 'regen_time',           type: 'int' },
        { name: 'attribute',            type: 'int' },
        { name: 'abs_damage',           type: 'int', optional: true },
        { name: 'max_mana',             type: 'int', optional: true },
        { name: 'magic_hit_ratio',      type: 'int', optional: true },
        { name: 'attack_range',         type: 'int', optional: true },
    ],
};

export const magic_schema: CfgSchema = {
    trigger: 'magic',
    fields: [
        { name: 'id',         type: 'int' },
        { name: 'name',       type: 'string' },
        { name: 'type',       type: 'int' },
        { name: 'delay',      type: 'int' },
        { name: 'duration',   type: 'int' },
        { name: 'mana_cost',  type: 'int' },
        { name: 'range1',     type: 'int' },
        { name: 'range2',     type: 'int' },
        { name: 'e1_dice',    type: 'int' },
        { name: 'e1_sides',   type: 'int' },
        { name: 'e1_bonus',   type: 'int' },
        { name: 'e2_dice',    type: 'int' },
        { name: 'e2_sides',   type: 'int' },
        { name: 'e2_bonus',   type: 'int' },
        { name: 'e3_dice',    type: 'int' },
        { name: 'e3_sides',   type: 'int' },
        { name: 'e3_bonus',   type: 'int' },
        { name: 'int_req',    type: 'int' },
        { name: 'cost',       type: 'int' },
        { name: 'category',   type: 'int' },
        { name: 'attribute',  type: 'int' },
    ],
};

export const skill_schema: CfgSchema = {
    trigger: 'skill',
    fields: [
        { name: 'id',     type: 'int' },
        { name: 'name',   type: 'string' },
        { name: 'type',   type: 'int' },
        { name: 'value1', type: 'int' },
        { name: 'value2', type: 'int' },
        { name: 'value3', type: 'int' },
        { name: 'value4', type: 'int' },
        { name: 'value5', type: 'int' },
        { name: 'value6', type: 'int' },
    ],
};

export const quest_schema: CfgSchema = {
    trigger: 'quest',
    fields: [
        { name: 'id',                 type: 'int' },
        { name: 'side',               type: 'int' },
        { name: 'type',               type: 'int' },
        { name: 'target_type',        type: 'int' },
        { name: 'max_count',          type: 'int' },
        { name: 'from_id',            type: 'int' },
        { name: 'min_level',          type: 'int' },
        { name: 'max_level',          type: 'int' },
        { name: 'req_skill',          type: 'int' },
        { name: 'req_skill_pct',      type: 'int' },
        { name: 'time_limit',         type: 'int' },
        { name: 'assign_type',        type: 'int' },
        { name: 'reward_type1',       type: 'int' },
        { name: 'reward_amount1',     type: 'int' },
        { name: 'reward_type2',       type: 'int' },
        { name: 'reward_amount2',     type: 'int' },
        { name: 'reward_type3',       type: 'int' },
        { name: 'reward_amount3',     type: 'int' },
        { name: 'contribution',       type: 'int' },
        { name: 'contribution_limit', type: 'int' },
        { name: 'resp_mode',          type: 'int' },
        { name: 'map',                type: 'string' },
        { name: 'x',                  type: 'int' },
        { name: 'y',                  type: 'int' },
        { name: 'range',              type: 'int' },
        { name: 'quest_id',           type: 'int' },
        { name: 'req_contribution',   type: 'int' },
    ],
};

export const teleport_schema: CfgSchema = {
    trigger: 'teleport',
    end_marker: '[END]',
    fields: [
        { name: 'npc_name',       type: 'string' },
        { name: 'src_map',        type: 'string' },
        { name: 'tgt_map',        type: 'string' },
        { name: 'x',             type: 'int' },
        { name: 'y',             type: 'int' },
        { name: 'cost',           type: 'int' },
        { name: 'min_level',      type: 'int' },
        { name: 'max_level',      type: 'int' },
        { name: 'side',           type: 'string' },
        { name: 'allow_neutral',  type: 'bool' },
        { name: 'allow_criminal', type: 'bool' },
    ],
};

export const crusade_schema: CfgSchema = {
    trigger: 'crusade-structure',
    fields: [
        { name: 'id',       type: 'int' },
        { name: 'map_name', type: 'string' },
        { name: 'type',     type: 'int' },
        { name: 'x',        type: 'int' },
        { name: 'y',        type: 'int' },
    ],
};

// CraftItem and Potion: 6 ingredient pairs (item_id, count), -1 = empty
export const craft_schema: CfgSchema = {
    trigger: 'crafting',
    fields: [
        { name: 'id',         type: 'int' },
        { name: 'result',     type: 'string' },
        { name: 'i0_id',      type: 'int' }, { name: 'i0_count', type: 'int' },
        { name: 'i1_id',      type: 'int' }, { name: 'i1_count', type: 'int' },
        { name: 'i2_id',      type: 'int' }, { name: 'i2_count', type: 'int' },
        { name: 'i3_id',      type: 'int' }, { name: 'i3_count', type: 'int' },
        { name: 'i4_id',      type: 'int' }, { name: 'i4_count', type: 'int' },
        { name: 'i5_id',      type: 'int' }, { name: 'i5_count', type: 'int' },
        { name: 'skill_limit', type: 'int' },
        { name: 'difficulty',  type: 'int' },
    ],
};

// BuildItem: 6 ingredient triples (item_id, count, value), 0 = empty
export const build_schema: CfgSchema = {
    trigger: 'BuildItem',
    fields: [
        { name: 'result',       type: 'string' },
        { name: 'skill_limit',  type: 'int' },
        { name: 'i0_id',        type: 'int' }, { name: 'i0_count', type: 'int' }, { name: 'i0_value', type: 'int' },
        { name: 'i1_id',        type: 'int' }, { name: 'i1_count', type: 'int' }, { name: 'i1_value', type: 'int' },
        { name: 'i2_id',        type: 'int' }, { name: 'i2_count', type: 'int' }, { name: 'i2_value', type: 'int' },
        { name: 'i3_id',        type: 'int' }, { name: 'i3_count', type: 'int' }, { name: 'i3_value', type: 'int' },
        { name: 'i4_id',        type: 'int' }, { name: 'i4_count', type: 'int' }, { name: 'i4_value', type: 'int' },
        { name: 'i5_id',        type: 'int' }, { name: 'i5_count', type: 'int' }, { name: 'i5_value', type: 'int' },
        { name: 'average_value', type: 'int' },
        { name: 'max_skill',     type: 'int' },
        { name: 'attribute',     type: 'int' },
    ],
};

export const potion_schema: CfgSchema = {
    trigger: 'potion',
    fields: [
        { name: 'id',         type: 'int' },
        { name: 'result',     type: 'string' },
        { name: 'i0_id',      type: 'int' }, { name: 'i0_count', type: 'int' },
        { name: 'i1_id',      type: 'int' }, { name: 'i1_count', type: 'int' },
        { name: 'i2_id',      type: 'int' }, { name: 'i2_count', type: 'int' },
        { name: 'i3_id',      type: 'int' }, { name: 'i3_count', type: 'int' },
        { name: 'i4_id',      type: 'int' }, { name: 'i4_count', type: 'int' },
        { name: 'i5_id',      type: 'int' }, { name: 'i5_count', type: 'int' },
        { name: 'skill_limit', type: 'int' },
        { name: 'difficulty',  type: 'int' },
    ],
};
```

**Step 2: Verify it compiles**

```bash
cd tools/convert && npx tsc --noEmit
```

Expected: no errors.

---

## Task 4: Emitters (`emitters.ts`)

**Files:**
- Create: `tools/convert/emitters.ts`

These convert parsed records into YAML strings. Uses `js-yaml` for serialization.

**Step 1: Write emitters.ts**

```typescript
import yaml from 'js-yaml';
import type { CfgRecord } from './parser.js';

// Emit a single flow-style line: { k: v, k: v, ... }
function flow(record: CfgRecord): string {
    return '  - ' + yaml.dump(record, { flowLevel: 0, lineWidth: -1 }).trimEnd();
}

// Emit a block-style entry with optional nested fields
function block(record: CfgRecord): string {
    return yaml.dump([record], { indent: 2, lineWidth: -1 })
        .split('\n')
        .map(l => '  ' + l)
        .join('\n')
        .trimEnd();
}

// ---- Recipe ingredient helpers ----

function extract_pair_ingredients(rec: CfgRecord): Array<{item_id: number, count: number}> {
    const out = [];
    for (let i = 0; i < 6; i++) {
        const id = rec[`i${i}_id`] as number;
        const count = rec[`i${i}_count`] as number;
        if (id !== -1) out.push({ item_id: id, count });
    }
    return out;
}

function extract_triple_ingredients(rec: CfgRecord): Array<{item_id: number, count: number, value: number}> {
    const out = [];
    for (let i = 0; i < 6; i++) {
        const id = rec[`i${i}_id`] as number;
        const count = rec[`i${i}_count`] as number;
        const value = rec[`i${i}_value`] as number;
        if (id !== 0) out.push({ item_id: id, count, value });
    }
    return out;
}

// ---- Per-type emitters ----

export function emit_items(records: CfgRecord[]): string {
    const lines = ['items:'];
    // Sort by id
    const sorted = [...records].sort((a, b) => (a.id as number) - (b.id as number));
    for (const r of sorted) lines.push(flow(r));
    return lines.join('\n') + '\n';
}

export function emit_npcs(records: CfgRecord[]): string {
    const lines = ['npcs:'];
    for (const r of records) lines.push(flow(r));
    return lines.join('\n') + '\n';
}

export function emit_magic(records: CfgRecord[]): string {
    const lines = ['magic:'];
    for (const r of records) {
        const entry: Record<string, unknown> = {
            id:       r.id,
            name:     r.name,
            type:     r.type,
            delay:    r.delay,
            duration: r.duration,
            mana_cost: r.mana_cost,
            range1:   r.range1,
            range2:   r.range2,
            effect1:  { dice: r.e1_dice, sides: r.e1_sides, bonus: r.e1_bonus },
            effect2:  { dice: r.e2_dice, sides: r.e2_sides, bonus: r.e2_bonus },
            effect3:  { dice: r.e3_dice, sides: r.e3_sides, bonus: r.e3_bonus },
            int_req:  r.int_req,
            cost:     r.cost,
            category: r.category,
            attribute: r.attribute,
        };
        lines.push(block(entry));
    }
    return lines.join('\n') + '\n';
}

const SKILL_DEFAULTS = `defaults:
  max_level: 100
  tiers:
    - { max_level: 20, multiplier: 10 }
    - { max_level: 40, multiplier: 25 }
    - { max_level: 60, multiplier: 50 }
    - { max_level: 80, multiplier: 75 }
    - { max_level: 90, multiplier: 100 }
    - { max_level: 200, multiplier: 125 }
`;

export function emit_skills(records: CfgRecord[]): string {
    const lines = [SKILL_DEFAULTS, 'skills:'];
    for (const r of records) lines.push(flow(r));
    return lines.join('\n') + '\n';
}

export function emit_quests(records: CfgRecord[]): string {
    const lines = ['quests:'];
    for (const r of records) lines.push(flow(r));
    return lines.join('\n') + '\n';
}

export function emit_teleports(records: CfgRecord[]): string {
    const lines = ['teleports:'];
    for (const r of records) lines.push(block(r));
    return lines.join('\n') + '\n';
}

export function emit_crusade(records: CfgRecord[]): string {
    const lines = ['crusade_structures:'];
    for (const r of records) lines.push(flow(r));
    return lines.join('\n') + '\n';
}

export function emit_craft_recipes(records: CfgRecord[]): string {
    const lines = ['crafting_recipes:'];
    for (const r of records) {
        const entry = {
            id:          r.id,
            result:      r.result,
            skill_limit: r.skill_limit,
            difficulty:  r.difficulty,
            ingredients: extract_pair_ingredients(r),
        };
        lines.push(block(entry));
    }
    return lines.join('\n') + '\n';
}

export function emit_build_recipes(records: CfgRecord[]): string {
    const lines = ['build_recipes:'];
    for (const r of records) {
        const entry = {
            result:        r.result,
            skill_limit:   r.skill_limit,
            average_value: r.average_value,
            max_skill:     r.max_skill,
            attribute:     r.attribute,
            ingredients:   extract_triple_ingredients(r),
        };
        lines.push(block(entry));
    }
    return lines.join('\n') + '\n';
}

export function emit_alchemy_recipes(records: CfgRecord[]): string {
    const lines = ['alchemy_recipes:'];
    for (const r of records) {
        const entry = {
            id:          r.id,
            result:      r.result,
            skill_limit: r.skill_limit,
            difficulty:  r.difficulty,
            ingredients: extract_pair_ingredients(r),
        };
        lines.push(block(entry));
    }
    return lines.join('\n') + '\n';
}
```

**Step 2: Verify it compiles**

```bash
cd tools/convert && npx tsc --noEmit
```

Expected: no errors.

---

## Task 5: Main entry point (`convert.ts`)

**Files:**
- Create: `tools/convert/convert.ts`

**Step 1: Write convert.ts**

```typescript
import { readFileSync, writeFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { parse_cfg } from './parser.js';
import {
    item_schema, npc_schema, magic_schema, skill_schema, quest_schema,
    teleport_schema, crusade_schema, craft_schema, build_schema, potion_schema,
} from './schemas.js';
import {
    emit_items, emit_npcs, emit_magic, emit_skills, emit_quests,
    emit_teleports, emit_crusade, emit_craft_recipes, emit_build_recipes,
    emit_alchemy_recipes,
} from './emitters.js';

const __dir = dirname(fileURLToPath(import.meta.url));
const CFG_DIR  = join(__dir, '../../bin/game_configs/old configs');
const YAML_DIR = join(__dir, '../../bin/game_configs');

function read(name: string): string {
    return readFileSync(join(CFG_DIR, name), 'utf-8');
}

function write(name: string, content: string): void {
    const path = join(YAML_DIR, name);
    writeFileSync(path, content, 'utf-8');
    console.log(`  wrote ${name}`);
}

function run(): void {
    console.log('Converting cfg → yaml...\n');

    // Items (4 files merged)
    {
        const files = ['Item.cfg', 'Item2.cfg', 'Item3.cfg', 'Item4.cfg'];
        const all = files.flatMap(f => parse_cfg(read(f), item_schema));
        // Duplicate id check
        const seen = new Set<number>();
        for (const r of all) {
            const id = r.id as number;
            if (seen.has(id)) throw new Error(`Duplicate item id: ${id}`);
            seen.add(id);
        }
        write('items.yaml', emit_items(all));
        console.log(`  items: ${all.length} entries`);
    }

    // NPCs
    {
        const recs = parse_cfg(read('Npc.cfg'), npc_schema);
        write('npcs.yaml', emit_npcs(recs));
        console.log(`  npcs: ${recs.length} entries`);
    }

    // Magic
    {
        const recs = parse_cfg(read('Magic.cfg'), magic_schema);
        write('magic.yaml', emit_magic(recs));
        console.log(`  magic: ${recs.length} entries`);
    }

    // Skills
    {
        const recs = parse_cfg(read('Skill.cfg'), skill_schema);
        write('skills.yaml', emit_skills(recs));
        console.log(`  skills: ${recs.length} entries`);
    }

    // Quests
    {
        const recs = parse_cfg(read('Quest.cfg'), quest_schema);
        write('quests.yaml', emit_quests(recs));
        console.log(`  quests: ${recs.length} entries`);
    }

    // Teleports
    {
        const recs = parse_cfg(read('Teleport.cfg'), teleport_schema);
        write('teleports.yaml', emit_teleports(recs));
        console.log(`  teleports: ${recs.length} entries`);
    }

    // Crusade structures
    {
        const recs = parse_cfg(read('Crusade.cfg'), crusade_schema);
        write('crusade_structures.yaml', emit_crusade(recs));
        console.log(`  crusade_structures: ${recs.length} entries`);
    }

    // CraftItem recipes
    {
        const recs = parse_cfg(read('CraftItem.cfg'), craft_schema);
        write('craft_recipes.yaml', emit_craft_recipes(recs));
        console.log(`  craft_recipes: ${recs.length} entries`);
    }

    // BuildItem recipes
    {
        const recs = parse_cfg(read('BuildItem.cfg'), build_schema);
        write('build_recipes.yaml', emit_build_recipes(recs));
        console.log(`  build_recipes: ${recs.length} entries`);
    }

    // Alchemy (Potion) recipes
    {
        const recs = parse_cfg(read('Potion.cfg'), potion_schema);
        write('recipes.yaml', emit_alchemy_recipes(recs));
        console.log(`  alchemy_recipes: ${recs.length} entries`);
    }

    console.log('\nDone.');
}

run();
```

**Step 2: Run it**

```bash
cd tools/convert && npx tsx convert.ts
```

Expected output:
```
Converting cfg → yaml...

  wrote items.yaml
  items: <N> entries
  wrote npcs.yaml
  npcs: <N> entries
  ...
Done.
```

If a file throws: the error message will name the cfg and field where parsing failed. Fix the schema (field count, trigger word, or optional flags) and re-run.

---

## Task 6: Verify output

**Step 1: Spot-check items.yaml**

```bash
head -5 bin/game_configs/items.yaml
grep "name: Dagger" bin/game_configs/items.yaml | head -3
```

Verify: `id: 1`, `name: Dagger`, `effect_type:`, `durability:`, `price:` present. No `color_r1` anywhere.

**Step 2: Spot-check magic.yaml**

```bash
head -20 bin/game_configs/magic.yaml
```

Verify: `effect1: {dice: N, sides: N, bonus: N}` structure present.

**Step 3: Spot-check recipe files**

```bash
head -15 bin/game_configs/craft_recipes.yaml
head -15 bin/game_configs/recipes.yaml
head -15 bin/game_configs/build_recipes.yaml
```

Verify: `ingredients:` array present, no `-1` item_id entries.

**Step 4: Spot-check quests.yaml**

```bash
head -5 bin/game_configs/quests.yaml
```

Verify: full 27-field entries including `from_id`, `time_limit`, `reward_type1..3`, `map`, `req_contribution`.

**Step 5: Spot-check npcs.yaml**

```bash
head -5 bin/game_configs/npcs.yaml
grep "name: Slime" bin/game_configs/npcs.yaml
```

Verify: all expected fields present.

---

## Troubleshooting

**"Ran out of tokens reading field X"** — The cfg file has fewer fields than the schema expects. Mark trailing fields `optional: true` in the schema, or check that the trigger word matches exactly (case-sensitive).

**Wrong field count (e.g. npc)** — The Npc.cfg in `old configs/` may be an older build with fewer columns than the full decode function. The `optional: true` flags on the last 4 NPC fields handle this.

**Duplicate item id error** — Item2/3/4.cfg contains an id already in Item.cfg. Log which id, check the cfg files for intentional duplicates.

**YAML looks wrong (wrong values in known fields)** — The schema field order doesn't match the cfg column order. Cross-reference with the `_bDecodeXxxConfigFileContents` function in `Game.cpp` counting `cReadModeB` cases 1 through N.
