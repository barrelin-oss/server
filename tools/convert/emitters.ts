import yaml from 'js-yaml';
import type { CfgRecord } from './parser.js';

// Emit a single flow-style line: { k: v, k: v, ... }
function flow(record: CfgRecord): string {
    return '  - ' + yaml.dump(record, { flowLevel: 0, lineWidth: -1 }).trimEnd();
}

// Emit a block-style entry
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
            id:        r.id,
            name:      r.name,
            type:      r.type,
            delay:     r.delay,
            duration:  r.duration,
            mana_cost: r.mana_cost,
            range1:    r.range1,
            range2:    r.range2,
            effect1:   { dice: r.e1_dice, sides: r.e1_sides, bonus: r.e1_bonus },
            effect2:   { dice: r.e2_dice, sides: r.e2_sides, bonus: r.e2_bonus },
            effect3:   { dice: r.e3_dice, sides: r.e3_sides, bonus: r.e3_bonus },
            int_req:   r.int_req,
            cost:      r.cost,
            category:  r.category,
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
        const entry: Record<string, unknown> = {
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
        const entry: Record<string, unknown> = {
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
        const entry: Record<string, unknown> = {
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
