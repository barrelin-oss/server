import { readFileSync, writeFileSync } from 'fs';
import { join, dirname } from 'path';
import { fileURLToPath } from 'url';
import { parse_cfg } from './parser.js';
import {
    item_schema, npc_schema, magic_schema, skill_schema, quest_schema,
    crusade_schema, build_schema, potion_schema,
} from './schemas.js';
import {
    emit_items, emit_npcs, emit_magic, emit_skills, emit_quests,
    emit_crusade, emit_build_recipes,
    emit_alchemy_recipes,
} from './emitters.js';

const __dir = dirname(fileURLToPath(import.meta.url));
const CFG_DIR  = join(__dir, '../../bin/game_configs/hbx configs');
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

    // Items (4 files merged — first occurrence wins for duplicate IDs)
    {
        const files = ['Item.cfg', 'Item2.cfg', 'Item3.cfg'];
        const all_raw = files.flatMap(f => parse_cfg(read(f), item_schema));
        const seen = new Set<number>();
        const all: typeof all_raw = [];
        let skipped = 0;
        for (const r of all_raw) {
            const id = r.id as number;
            if (seen.has(id)) { skipped++; continue; }
            seen.add(id);
            all.push(r);
        }
        if (skipped > 0) console.log(`  (skipped ${skipped} duplicate item entries)`);
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

    // Crusade structures
    {
        const recs = parse_cfg(read('Crusade.cfg'), crusade_schema);
        write('crusade_structures.yaml', emit_crusade(recs));
        console.log(`  crusade_structures: ${recs.length} entries`);
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
