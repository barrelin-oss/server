#!/usr/bin/env node
/**
 * Convert legacy Helbreath GameConfigs .cfg files to YAML format.
 *
 * Usage:
 *   node convert_game_configs.js [input_dir] [output_dir]
 *
 *   Default: converts GameConfigs/*.cfg to GameConfigs/*.yaml
 */

const fs = require('fs');
const path = require('path');

// Parse a line into tokens (handles spaces, tabs, = signs)
function parseTokens(line) {
    // Remove comments
    for (const prefix of ['//', ';', '#']) {
        const idx = line.indexOf(prefix);
        if (idx !== -1) {
            line = line.substring(0, idx);
        }
    }

    line = line.trim();
    if (!line || line.startsWith('[')) {
        return [];
    }

    // Replace = and tabs with spaces, then split
    return line.replace(/[=\t]+/g, ' ').split(/\s+/).filter(t => t.length > 0);
}

// ============================================================
// Settings.cfg converter
// ============================================================
function convertSettings(content) {
    const settings = {
        rates: {},
        enemy_kill: {},
        admin: {},
        raid_times: {},
        chat: {},
        character_limits: {},
        costs: {},
        features: {}
    };

    for (const line of content.split(/\r?\n/)) {
        const trimmed = line.trim();

        // Skip comments and section headers
        if (!trimmed || trimmed.startsWith('//') || trimmed.startsWith(';') ||
            trimmed.startsWith('#') || trimmed.startsWith('[')) {
            continue;
        }

        // Parse key = value
        const match = trimmed.match(/^([a-z\-]+)\s*=\s*(.+)$/i);
        if (!match) continue;

        const key = match[1].toLowerCase().replace(/-/g, '_');
        let value = match[2].trim();

        // Convert value type
        if (value === 'ON' || value === 'OFF') {
            value = value === 'ON';
        } else if (/^-?\d+$/.test(value)) {
            value = parseInt(value, 10);
        }

        // Categorize
        if (key.includes('drop_rate') || key.includes('rate') && !key.includes('raid')) {
            settings.rates[key] = value;
        } else if (key.includes('enemy_kill') || key.includes('rating')) {
            settings.enemy_kill[key] = value;
        } else if (key.includes('admin')) {
            settings.admin[key] = value;
        } else if (key.includes('raid_time')) {
            settings.raid_times[key] = value;
        } else if (key.includes('chat') || key.includes('log')) {
            settings.chat[key] = value;
        } else if (key.includes('limit') || key.includes('level') || key.includes('max')) {
            settings.character_limits[key] = value;
        } else if (key.includes('cost')) {
            settings.costs[key] = value;
        } else {
            settings.features[key] = value;
        }
    }

    // Remove empty categories
    for (const cat of Object.keys(settings)) {
        if (Object.keys(settings[cat]).length === 0) {
            delete settings[cat];
        }
    }

    return settings;
}

function writeSettingsYaml(settings, filepath) {
    const lines = [];

    for (const [category, values] of Object.entries(settings)) {
        lines.push(`${category}:`);
        for (const [key, value] of Object.entries(values)) {
            if (typeof value === 'boolean') {
                lines.push(`  ${key}: ${value}`);
            } else if (typeof value === 'number') {
                lines.push(`  ${key}: ${value}`);
            } else {
                lines.push(`  ${key}: "${value}"`);
            }
        }
        lines.push('');
    }

    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// AdminSettings.cfg converter
// ============================================================
function convertAdminSettings(content) {
    const commands = [];

    for (const line of content.split(/\r?\n/)) {
        const trimmed = line.trim();
        if (!trimmed || trimmed.startsWith('//') || trimmed.startsWith(';')) continue;

        // Parse Admin-Level-/command = level
        const match = trimmed.match(/^Admin-Level-\/(\S+)\s*=\s*(\d+)$/i);
        if (match) {
            commands.push({
                command: match[1],
                level: parseInt(match[2], 10)
            });
        }
    }

    return commands;
}

function writeAdminSettingsYaml(commands, filepath) {
    const lines = ['admin_commands:'];

    for (const cmd of commands) {
        lines.push(`  - { command: ${cmd.command}, level: ${cmd.level} }`);
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// Skill.cfg converter
// ============================================================
function convertSkills(content) {
    const skills = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 3 || tokens[0].toLowerCase() !== 'skill') continue;

        // skill = id name type v1 v2 v3 v4 v5 v6
        const skill = {
            id: parseInt(tokens[1], 10),
            name: tokens[2],
            type: parseInt(tokens[3] || '0', 10)
        };

        // Only add optional values if present
        const v1 = parseInt(tokens[4] || '0', 10);
        const v2 = parseInt(tokens[5] || '0', 10);

        if (v1 !== 0) skill.value1 = v1;
        if (v2 !== 0) skill.value2 = v2;

        skills.push(skill);
    }

    return skills;
}

function writeSkillsYaml(skills, filepath) {
    const lines = ['skills:'];

    for (const s of skills) {
        let obj = `id: ${s.id}, name: ${s.name}, type: ${s.type}`;
        if (s.value1 !== undefined) obj += `, value1: ${s.value1}`;
        if (s.value2 !== undefined) obj += `, value2: ${s.value2}`;
        lines.push(`  - { ${obj} }`);
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// Item.cfg converter
// Item = id name type equip_pos ... (32 fields total)
// ============================================================
const ITEM_FIELDS = [
    'id', 'name', 'type', 'equip_pos',
    'color_r1', 'color_g1', 'color_b1',
    'color_r2', 'color_g2', 'color_b2',
    'weight', 'durability', 'equip_type', 'sprite_id',
    'price', 'attack_range', 'skill_type', 'attack_bonus',
    'hit_prob', 'dodge_prob', 'defense', 'level_limit',
    'is_two_handed', 'unk1', 'effect1', 'effect2', 'effect3',
    'is_unique', 'unk2', 'unk3', 'unk4', 'unk5'
];

function convertItems(content) {
    const items = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 5 || tokens[0].toLowerCase() !== 'item') continue;

        // Build item object with named fields
        const item = {};
        for (let i = 1; i < tokens.length && i <= ITEM_FIELDS.length; i++) {
            const field = ITEM_FIELDS[i - 1];
            const value = tokens[i];

            // Keep name as string, convert others to numbers
            if (field === 'name') {
                item[field] = value;
            } else {
                const num = parseInt(value, 10);
                if (!isNaN(num)) {
                    item[field] = num;
                }
            }
        }

        if (item.id !== undefined && item.name) {
            items.push(item);
        }
    }

    return items;
}

function writeItemsYaml(items, filepath) {
    const lines = ['items:'];

    for (const item of items) {
        // Build a compact inline object
        const parts = [];
        for (const [key, value] of Object.entries(item)) {
            if (typeof value === 'string') {
                parts.push(`${key}: ${value}`);
            } else {
                parts.push(`${key}: ${value}`);
            }
        }
        lines.push(`  - { ${parts.join(', ')} }`);
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// NPC.cfg converter
// Npc = name type hp defense ... (27 fields)
// ============================================================
const NPC_FIELDS = [
    'name', 'sprite_id', 'hp', 'defense', 'level',
    'exp_min', 'exp_max', 'gold_min', 'gold_max',
    'attack_dice', 'attack_sides', 'is_undead', 'unk1',
    'side', 'attack_speed', 'move_speed', 'magic_level', 'unk2',
    'is_peaceful', 'detection_range', 'spawn_delay',
    'attack_range', 'gold_on_death', 'mp', 'magic_power', 'magic_hit_chance', 'unk3'
];

function convertNpcs(content) {
    const npcs = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 5 || tokens[0].toLowerCase() !== 'npc') continue;

        const npc = {};
        for (let i = 1; i < tokens.length && i <= NPC_FIELDS.length; i++) {
            const field = NPC_FIELDS[i - 1];
            const value = tokens[i];

            if (field === 'name') {
                npc[field] = value;
            } else {
                const num = parseInt(value, 10);
                if (!isNaN(num)) {
                    npc[field] = num;
                }
            }
        }

        if (npc.name) {
            npcs.push(npc);
        }
    }

    return npcs;
}

function writeNpcsYaml(npcs, filepath) {
    const lines = ['npcs:'];

    for (const npc of npcs) {
        const parts = [];
        for (const [key, value] of Object.entries(npc)) {
            parts.push(`${key}: ${value}`);
        }
        lines.push(`  - { ${parts.join(', ')} }`);
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// Magic.cfg converter
// magic = id name type delay last mana_cost range1 range2 ... (many fields)
// ============================================================
const MAGIC_FIELDS = [
    'id', 'name', 'type', 'delay', 'duration', 'mana_cost',
    'range1', 'range2',
    'effect1_dice', 'effect1_sides', 'effect1_bonus',
    'effect2_dice', 'effect2_sides', 'effect2_bonus',
    'effect3_dice', 'effect3_sides', 'effect3_bonus',
    'int_req', 'cost', 'category', 'attribute'
];

function convertMagic(content) {
    const spells = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 5 || tokens[0].toLowerCase() !== 'magic') continue;

        const spell = {};
        for (let i = 1; i < tokens.length && i <= MAGIC_FIELDS.length; i++) {
            const field = MAGIC_FIELDS[i - 1];
            const value = tokens[i];

            if (field === 'name') {
                spell[field] = value;
            } else {
                const num = parseInt(value, 10);
                if (!isNaN(num)) {
                    spell[field] = num;
                }
            }
        }

        if (spell.id !== undefined && spell.name) {
            spells.push(spell);
        }
    }

    return spells;
}

function writeMagicYaml(spells, filepath) {
    const lines = ['magic:'];

    for (const spell of spells) {
        const parts = [];
        for (const [key, value] of Object.entries(spell)) {
            parts.push(`${key}: ${value}`);
        }
        lines.push(`  - { ${parts.join(', ')} }`);
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// BuildItem.cfg converter
// BuildItem = result_name skill_req (item_id count flag)×6 skill_limit success_rate unk
// ============================================================
function convertBuildItems(content) {
    const recipes = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 5 || tokens[0].toLowerCase() !== 'builditem') continue;

        const recipe = {
            result: tokens[1],
            skill_req: parseInt(tokens[2], 10),
            ingredients: []
        };

        // Parse ingredients (triplets of id, count, flag) - 6 slots
        // Format: item_id count flag × 6, then skill_limit success_rate unk
        let i = 3;
        for (let slot = 0; slot < 6 && i + 2 < tokens.length; slot++) {
            const id = parseInt(tokens[i], 10);
            const count = parseInt(tokens[i + 1], 10);
            // Skip flag (tokens[i + 2])
            if (id > 0 && count > 0) {
                recipe.ingredients.push({ item_id: id, count: count });
            }
            i += 3;
        }

        // Last three fields are skill_limit, success_rate, unk
        if (i < tokens.length) {
            recipe.skill_limit = parseInt(tokens[i], 10) || 0;
        }
        if (i + 1 < tokens.length) {
            recipe.success_rate = parseInt(tokens[i + 1], 10) || 0;
        }

        recipes.push(recipe);
    }

    return recipes;
}

function writeBuildItemsYaml(recipes, filepath) {
    const lines = ['build_recipes:'];

    for (const r of recipes) {
        lines.push(`  - result: ${r.result}`);
        lines.push(`    skill_req: ${r.skill_req}`);
        lines.push(`    skill_limit: ${r.skill_limit}`);
        lines.push(`    success_rate: ${r.success_rate}`);
        lines.push('    ingredients:');
        for (const ing of r.ingredients) {
            lines.push(`      - { item_id: ${ing.item_id}, count: ${ing.count} }`);
        }
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// Potion.cfg converter (includes both potion and crafting)
// potion = id result_name ingredient1 count1 ... skill_limit difficulty
// crafting = id result_name ingredient1 count1 ... skill_limit difficulty
// ============================================================
function convertPotions(content) {
    const potions = [];
    const crafting = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 5) continue;

        const type = tokens[0].toLowerCase();
        if (type !== 'potion' && type !== 'crafting') continue;

        const recipe = {
            id: parseInt(tokens[1], 10),
            result: tokens[2],
            ingredients: [],
            skill_limit: 0,
            difficulty: 0
        };

        // Parse ingredients (pairs of item_id, count) - 6 pairs max
        let i = 3;
        for (let pair = 0; pair < 6 && i + 1 < tokens.length; pair++) {
            const id = parseInt(tokens[i], 10);
            const count = parseInt(tokens[i + 1], 10);
            if (id > 0) {
                recipe.ingredients.push({ item_id: id, count: count });
            }
            i += 2;
        }

        // Last two fields
        if (i < tokens.length) {
            recipe.skill_limit = parseInt(tokens[i], 10) || 0;
        }
        if (i + 1 < tokens.length) {
            recipe.difficulty = parseInt(tokens[i + 1], 10) || 0;
        }

        if (type === 'potion') {
            potions.push(recipe);
        } else {
            crafting.push(recipe);
        }
    }

    return { potions, crafting };
}

function writePotionsYaml(data, filepath) {
    const lines = [];

    if (data.potions.length > 0) {
        lines.push('alchemy_recipes:');
        for (const r of data.potions) {
            lines.push(`  - id: ${r.id}`);
            lines.push(`    result: ${r.result}`);
            lines.push(`    skill_limit: ${r.skill_limit}`);
            lines.push(`    difficulty: ${r.difficulty}`);
            lines.push('    ingredients:');
            for (const ing of r.ingredients) {
                lines.push(`      - { item_id: ${ing.item_id}, count: ${ing.count} }`);
            }
        }
        lines.push('');
    }

    if (data.crafting.length > 0) {
        lines.push('crafting_recipes:');
        for (const r of data.crafting) {
            lines.push(`  - id: ${r.id}`);
            lines.push(`    result: ${r.result}`);
            lines.push(`    skill_limit: ${r.skill_limit}`);
            lines.push(`    difficulty: ${r.difficulty}`);
            lines.push('    ingredients:');
            for (const ing of r.ingredients) {
                lines.push(`      - { item_id: ${ing.item_id}, count: ${ing.count} }`);
            }
        }
        lines.push('');
    }

    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// Quest.cfg converter
// quest = id side type target_npc target_count ... map ... reward_item_id
// ============================================================
function convertQuests(content) {
    const quests = [];

    for (const line of content.split(/\r?\n/)) {
        const tokens = parseTokens(line);
        if (tokens.length < 10 || tokens[0].toLowerCase() !== 'quest') continue;

        // Parse basic quest info (positional format is complex)
        const quest = {
            id: parseInt(tokens[1], 10),
            side: parseInt(tokens[2], 10),  // 1 = aresden, 2 = elvine
            type: parseInt(tokens[3], 10),
            target_npc: parseInt(tokens[4], 10),
            target_count: parseInt(tokens[5], 10)
        };

        // Find the map name (it's a string in the middle of numbers)
        for (let i = 6; i < tokens.length; i++) {
            if (isNaN(parseInt(tokens[i], 10))) {
                quest.map = tokens[i];
                // Reward is typically near the end
                if (i + 4 < tokens.length) {
                    quest.reward_item = parseInt(tokens[i + 4], 10);
                }
                break;
            }
        }

        quests.push(quest);
    }

    return quests;
}

function writeQuestsYaml(quests, filepath) {
    const lines = ['quests:'];

    for (const q of quests) {
        const parts = [`id: ${q.id}`, `side: ${q.side}`, `type: ${q.type}`];
        parts.push(`target_npc: ${q.target_npc}`, `target_count: ${q.target_count}`);
        if (q.map) parts.push(`map: ${q.map}`);
        if (q.reward_item) parts.push(`reward_item: ${q.reward_item}`);
        lines.push(`  - { ${parts.join(', ')} }`);
    }

    lines.push('');
    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

// ============================================================
// Main conversion logic
// ============================================================
const converters = {
    'settings.cfg': {
        convert: convertSettings,
        write: writeSettingsYaml,
        output: 'settings.yaml'
    },
    'adminsettings.cfg': {
        convert: convertAdminSettings,
        write: writeAdminSettingsYaml,
        output: 'admin_settings.yaml'
    },
    'skill.cfg': {
        convert: convertSkills,
        write: writeSkillsYaml,
        output: 'skills.yaml'
    },
    'item.cfg': {
        convert: convertItems,
        write: writeItemsYaml,
        output: 'items.yaml'
    },
    // Note: Item2.cfg and Item3.cfg have been merged into Item.cfg
    'npc.cfg': {
        convert: convertNpcs,
        write: writeNpcsYaml,
        output: 'npcs.yaml'
    },
    'magic.cfg': {
        convert: convertMagic,
        write: writeMagicYaml,
        output: 'magic.yaml'
    },
    'builditem.cfg': {
        convert: convertBuildItems,
        write: writeBuildItemsYaml,
        output: 'build_recipes.yaml'
    },
    'potion.cfg': {
        convert: convertPotions,
        write: writePotionsYaml,
        output: 'recipes.yaml'
    },
    'quest.cfg': {
        convert: convertQuests,
        write: writeQuestsYaml,
        output: 'quests.yaml'
    }
};

function main() {
    let inputDir = 'GameConfigs';
    let outputDir = 'GameConfigs';

    if (process.argv.length >= 3) {
        inputDir = process.argv[2];
    }
    if (process.argv.length >= 4) {
        outputDir = process.argv[3];
    }

    if (!fs.existsSync(inputDir)) {
        console.error(`Error: Input directory not found: ${inputDir}`);
        process.exit(1);
    }

    if (!fs.existsSync(outputDir)) {
        fs.mkdirSync(outputDir, { recursive: true });
    }

    console.log(`Converting GameConfigs from: ${inputDir}`);
    console.log(`Output directory: ${outputDir}`);
    console.log('');

    let converted = 0;
    let skipped = 0;

    for (const file of fs.readdirSync(inputDir)) {
        const lowerFile = file.toLowerCase();
        const converter = converters[lowerFile];

        if (!converter) {
            // Skip files we don't have converters for
            if (lowerFile.endsWith('.cfg')) {
                console.log(`  Skipping: ${file} (no converter)`);
                skipped++;
            }
            continue;
        }

        try {
            const inputPath = path.join(inputDir, file);
            const outputPath = path.join(outputDir, converter.output);

            const content = fs.readFileSync(inputPath, 'utf8');
            const data = converter.convert(content);
            converter.write(data, outputPath);

            console.log(`  ${file} -> ${converter.output}`);
            converted++;
        } catch (e) {
            console.error(`  Error converting ${file}: ${e.message}`);
        }
    }

    console.log('');
    console.log(`Done. Converted ${converted} files, skipped ${skipped}.`);
}

main();
