#!/usr/bin/env node
/**
 * Convert legacy Helbreath map .txt config files to YAML format.
 *
 * Usage:
 *   node convert_map_configs.js [input_dir] [output_dir]
 *
 *   Default: converts mapdata/*.txt to mapdata/*.yaml
 */

const fs = require('fs');
const path = require('path');

function parseTokens(line) {
    // Remove comments
    for (const commentChar of ['//', ';', '#']) {
        const idx = line.indexOf(commentChar);
        if (idx !== -1) {
            line = line.substring(0, idx);
        }
    }

    line = line.trim();
    if (!line || line.startsWith('[')) {
        return [];
    }

    // Replace = with space and split
    return line.replace(/=/g, ' ').split(/\s+/).filter(t => t.length > 0);
}

function parseMapConfig(filepath) {
    const config = {
        name: path.basename(filepath, path.extname(filepath)),
        initial_points: [],
        teleports: [],
        safe_zones: [],
        spawners: [],
        waypoints: [],
        npcs: [],
        npc_avoid_rects: [],
        fish_points: [],
        mineral_points: [],
        strategic_points: [],
        energy_sphere_creation_points: [],
        energy_sphere_goal_points: [],
        strike_points: [],
        item_events: [],
        dynamic_gate_coords: [],
        heldenian_towers: [],
        heldenian_winning_zones: [],
        heldenian_gate_doors: [],
    };

    const content = fs.readFileSync(filepath, 'utf8');
    const lines = content.split(/\r?\n/);

    for (const line of lines) {
        const tokens = parseTokens(line);
        if (tokens.length === 0) continue;

        const key = tokens[0].toLowerCase();

        if (key === 'map-location' && tokens.length >= 2) {
            config.name = tokens[1];
        }
        else if (key === 'initial-point' && tokens.length >= 4) {
            const id = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            if (!isNaN(id) && !isNaN(x) && !isNaN(y)) {
                config.initial_points.push({ id, x, y });
            }
        }
        else if (key === 'teleport-loc' && tokens.length >= 7) {
            const src_x = parseInt(tokens[1], 10);
            const src_y = parseInt(tokens[2], 10);
            const dest_map = tokens[3];
            const dest_x = parseInt(tokens[4], 10);
            const dest_y = parseInt(tokens[5], 10);
            const direction = parseInt(tokens[6], 10);
            if (!isNaN(src_x) && !isNaN(src_y) && !isNaN(dest_x) && !isNaN(dest_y) && !isNaN(direction)) {
                config.teleports.push({ src_x, src_y, dest_map, dest_x, dest_y, direction });
            }
        }
        else if (key === 'upper-level-limit' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.upper_level_limit = val;
        }
        else if (key === 'level-limit' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.level_limit = val;
        }
        else if (key === 'no-attack-area' && tokens.length >= 6) {
            const id = parseInt(tokens[1], 10);
            const left = parseInt(tokens[2], 10);
            const top = parseInt(tokens[3], 10);
            const right = parseInt(tokens[4], 10);
            const bottom = parseInt(tokens[5], 10);
            if (!isNaN(id) && !isNaN(left) && !isNaN(top) && !isNaN(right) && !isNaN(bottom)) {
                config.safe_zones.push({ id, left, top, right, bottom });
            }
        }
        else if (key === 'spot-mob-generator' && tokens.length >= 9) {
            const id = parseInt(tokens[1], 10);
            const type = parseInt(tokens[2], 10);
            const x1 = parseInt(tokens[3], 10);
            const y1 = parseInt(tokens[4], 10);
            const x2 = parseInt(tokens[5], 10);
            const y2 = parseInt(tokens[6], 10);
            const npc_type = parseInt(tokens[7], 10);
            const max_count = parseInt(tokens[8], 10);
            if (!isNaN(id) && !isNaN(type) && !isNaN(x1) && !isNaN(y1) &&
                !isNaN(x2) && !isNaN(y2) && !isNaN(npc_type) && !isNaN(max_count)) {
                config.spawners.push({ id, type, x1, y1, x2, y2, npc_type, max_count });
            }
        }
        else if (key === 'waypoint' && tokens.length >= 4) {
            const id = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            if (!isNaN(id) && !isNaN(x) && !isNaN(y)) {
                config.waypoints.push({ id, x, y });
            }
        }
        else if (key === 'maximum-object' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.max_objects = val;
        }
        else if (key === 'fixed-day-mode' && tokens.length >= 2) {
            config.fixed_day_mode = (tokens[1] === '1' || tokens[1].toLowerCase() === 'true');
        }
        else if (key === 'attack-mode' && tokens.length >= 2) {
            config.attack_enabled = (tokens[1] !== '0');
        }
        else if (key === 'npc' && tokens.length >= 13) {
            // npc = name movetype wp0 wp1 wp2 wp3 wp4 wp5 wp6 wp7 wp8 wp9 prefix
            const npc = {
                name: tokens[1],
                move_type: parseInt(tokens[2], 10),
                waypoints: [],
                name_prefix: tokens[12],
            };
            for (let i = 0; i < 10; i++) {
                const wp = parseInt(tokens[3 + i], 10);
                if (!isNaN(wp) && wp !== -1) {
                    npc.waypoints.push(wp);
                }
            }
            config.npcs.push(npc);
        }
        else if (key === 'random-mob-generator' && tokens.length >= 3) {
            const enabled = parseInt(tokens[1], 10);
            const level = parseInt(tokens[2], 10);
            if (!isNaN(enabled) && !isNaN(level)) {
                config.random_mob_generator = { enabled: enabled === 1, level };
            }
        }
        else if (key === 'npc-avoidrect' && tokens.length >= 6) {
            const id = parseInt(tokens[1], 10);
            const left = parseInt(tokens[2], 10);
            const top = parseInt(tokens[3], 10);
            const right = parseInt(tokens[4], 10);
            const bottom = parseInt(tokens[5], 10);
            if (!isNaN(id) && !isNaN(left) && !isNaN(top) && !isNaN(right) && !isNaN(bottom)) {
                config.npc_avoid_rects.push({ id, left, top, right, bottom });
            }
        }
        else if (key === 'fish-point' && tokens.length >= 4) {
            const id = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            if (!isNaN(id) && !isNaN(x) && !isNaN(y)) {
                config.fish_points.push({ id, x, y });
            }
        }
        else if (key === 'max-fish' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.max_fish = val;
        }
        else if (key === 'mineral-generator' && tokens.length >= 3) {
            const enabled = parseInt(tokens[1], 10);
            const level = parseInt(tokens[2], 10);
            if (!isNaN(enabled) && !isNaN(level)) {
                config.mineral_generator = { enabled: enabled === 1, level };
            }
        }
        else if (key === 'mineral-point' && tokens.length >= 4) {
            const id = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            if (!isNaN(id) && !isNaN(x) && !isNaN(y)) {
                config.mineral_points.push({ id, x, y });
            }
        }
        else if (key === 'max-mineral' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.max_mineral = val;
        }
        else if (key === 'type' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.type = val;
        }
        else if (key === 'strategic-point' && tokens.length >= 5) {
            const side = parseInt(tokens[1], 10);
            const value = parseInt(tokens[2], 10);
            const x = parseInt(tokens[3], 10);
            const y = parseInt(tokens[4], 10);
            if (!isNaN(side) && !isNaN(value) && !isNaN(x) && !isNaN(y)) {
                config.strategic_points.push({ side, value, x, y });
            }
        }
        else if (key === 'energy-sphere-creation-point' && tokens.length >= 4) {
            const type = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            if (!isNaN(type) && !isNaN(x) && !isNaN(y)) {
                config.energy_sphere_creation_points.push({ type, x, y });
            }
        }
        else if (key === 'energy-sphere-goal-point' && tokens.length >= 6) {
            const result = parseInt(tokens[1], 10);
            const aresden_x = parseInt(tokens[2], 10);
            const aresden_y = parseInt(tokens[3], 10);
            const elvine_x = parseInt(tokens[4], 10);
            const elvine_y = parseInt(tokens[5], 10);
            if (!isNaN(result) && !isNaN(aresden_x) && !isNaN(aresden_y) && !isNaN(elvine_x) && !isNaN(elvine_y)) {
                config.energy_sphere_goal_points.push({ result, aresden_x, aresden_y, elvine_x, elvine_y });
            }
        }
        else if (key === 'energy-sphere-auto-creation') {
            config.energy_sphere_auto_creation = true;
        }
        else if (key === 'strike-point' && tokens.length >= 14) {
            // strike-point = mapName x y hp effectX[5] effectY[5]
            const map_name = tokens[1];
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            const hp = parseInt(tokens[4], 10);
            const effect_x = [
                parseInt(tokens[5], 10), parseInt(tokens[6], 10), parseInt(tokens[7], 10),
                parseInt(tokens[8], 10), parseInt(tokens[9], 10)
            ];
            const effect_y = [
                parseInt(tokens[10], 10), parseInt(tokens[11], 10), parseInt(tokens[12], 10),
                parseInt(tokens[13], 10), parseInt(tokens[14], 10)
            ];
            if (!isNaN(x) && !isNaN(y) && !isNaN(hp)) {
                config.strike_points.push({ map_name, x, y, hp, effect_x, effect_y });
            }
        }
        else if (key === 'item-event' && tokens.length >= 7) {
            const item_name = tokens[1];
            const amount = parseInt(tokens[2], 10);
            const total = parseInt(tokens[3], 10);
            const month = parseInt(tokens[4], 10);
            const day = parseInt(tokens[5], 10);
            const total_num = parseInt(tokens[6], 10);
            if (!isNaN(amount) && !isNaN(total) && !isNaN(month) && !isNaN(day) && !isNaN(total_num)) {
                config.item_events.push({ item_name, amount, total, month, day, total_num });
            }
        }
        else if (key === 'mobevent-amount' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.mobevent_amount = val;
        }
        else if (key === 'apocalypsemobgentype' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.apocalypse_mob_gen_type = val;
        }
        else if (key === 'apocalypsebossmob' && tokens.length >= 6) {
            const npc_id = parseInt(tokens[1], 10);
            const x1 = parseInt(tokens[2], 10);
            const y1 = parseInt(tokens[3], 10);
            const x2 = parseInt(tokens[4], 10);
            const y2 = parseInt(tokens[5], 10);
            if (!isNaN(npc_id) && !isNaN(x1) && !isNaN(y1) && !isNaN(x2) && !isNaN(y2)) {
                config.apocalypse_boss_mob = { npc_id, x1, y1, x2, y2 };
            }
        }
        else if (key === 'dynamicgatetype' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.dynamic_gate_type = val;
        }
        else if (key === 'dynamicgatecoord' && tokens.length >= 8) {
            const x1 = parseInt(tokens[1], 10);
            const y1 = parseInt(tokens[2], 10);
            const x2 = parseInt(tokens[3], 10);
            const y2 = parseInt(tokens[4], 10);
            const dest_map = tokens[5];
            const dest_x = parseInt(tokens[6], 10);
            const dest_y = parseInt(tokens[7], 10);
            if (!isNaN(x1) && !isNaN(y1) && !isNaN(x2) && !isNaN(y2) && !isNaN(dest_x) && !isNaN(dest_y)) {
                config.dynamic_gate_coords.push({ x1, y1, x2, y2, dest_map, dest_x, dest_y });
            }
        }
        else if (key === 'recallimpossible' && tokens.length >= 2) {
            config.recall_impossible = (tokens[1] === '1' || tokens[1].toLowerCase() === 'true');
        }
        else if (key === 'apocalypsemap' && tokens.length >= 2) {
            config.apocalypse_map = (tokens[1] === '1' || tokens[1].toLowerCase() === 'true');
        }
        else if (key === 'citizenlimit' && tokens.length >= 2) {
            config.citizen_limit = (tokens[1] === '1' || tokens[1].toLowerCase() === 'true');
        }
        else if (key === 'heldenianmap' && tokens.length >= 2) {
            config.heldenian_map = (tokens[1] === '1' || tokens[1].toLowerCase() === 'true');
        }
        else if (key === 'heldenianmodemap' && tokens.length >= 2) {
            config.heldenian_mode_map = (tokens[1] === '1' || tokens[1].toLowerCase() === 'true');
        }
        else if (key === 'heldenianmar' && tokens.length >= 4) {
            const type_id = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            const side = parseInt(tokens[4], 10);
            if (!isNaN(type_id) && !isNaN(x) && !isNaN(y) && !isNaN(side)) {
                config.heldenian_towers.push({ type_id, x, y, side });
            }
        }
        else if (key === 'heldenianwinningzone' && tokens.length >= 6) {
            const id = parseInt(tokens[1], 10);
            const left = parseInt(tokens[2], 10);
            const top = parseInt(tokens[3], 10);
            const right = parseInt(tokens[4], 10);
            const bottom = parseInt(tokens[5], 10);
            if (!isNaN(id) && !isNaN(left) && !isNaN(top) && !isNaN(right) && !isNaN(bottom)) {
                config.heldenian_winning_zones.push({ id, left, top, right, bottom });
            }
        }
        else if (key === 'heldeniangatedoor' && tokens.length >= 4) {
            const dir = parseInt(tokens[1], 10);
            const x = parseInt(tokens[2], 10);
            const y = parseInt(tokens[3], 10);
            if (!isNaN(dir) && !isNaN(x) && !isNaN(y)) {
                config.heldenian_gate_doors.push({ dir, x, y });
            }
        }
        else if (key === 'special-event-id' && tokens.length >= 2) {
            const val = parseInt(tokens[1], 10);
            if (!isNaN(val)) config.special_event_id = val;
        }
    }

    return config;
}

function writeYaml(config, filepath) {
    const lines = [];

    lines.push(`name: ${config.name}`);

    // Scalar properties
    if (config.type !== undefined) {
        lines.push(`type: ${config.type}`);
    }
    if (config.upper_level_limit !== undefined) {
        lines.push(`upper_level_limit: ${config.upper_level_limit}`);
    }
    if (config.level_limit !== undefined) {
        lines.push(`level_limit: ${config.level_limit}`);
    }
    if (config.max_objects !== undefined) {
        lines.push(`max_objects: ${config.max_objects}`);
    }
    if (config.fixed_day_mode !== undefined) {
        lines.push(`fixed_day_mode: ${config.fixed_day_mode}`);
    }
    if (config.attack_enabled !== undefined) {
        lines.push(`attack_enabled: ${config.attack_enabled}`);
    }
    if (config.recall_impossible !== undefined) {
        lines.push(`recall_impossible: ${config.recall_impossible}`);
    }
    if (config.apocalypse_map !== undefined) {
        lines.push(`apocalypse_map: ${config.apocalypse_map}`);
    }
    if (config.citizen_limit !== undefined) {
        lines.push(`citizen_limit: ${config.citizen_limit}`);
    }
    if (config.heldenian_map !== undefined) {
        lines.push(`heldenian_map: ${config.heldenian_map}`);
    }
    if (config.heldenian_mode_map !== undefined) {
        lines.push(`heldenian_mode_map: ${config.heldenian_mode_map}`);
    }
    if (config.special_event_id !== undefined) {
        lines.push(`special_event_id: ${config.special_event_id}`);
    }
    if (config.mobevent_amount !== undefined) {
        lines.push(`mobevent_amount: ${config.mobevent_amount}`);
    }
    if (config.apocalypse_mob_gen_type !== undefined) {
        lines.push(`apocalypse_mob_gen_type: ${config.apocalypse_mob_gen_type}`);
    }
    if (config.dynamic_gate_type !== undefined) {
        lines.push(`dynamic_gate_type: ${config.dynamic_gate_type}`);
    }
    if (config.energy_sphere_auto_creation !== undefined) {
        lines.push(`energy_sphere_auto_creation: ${config.energy_sphere_auto_creation}`);
    }
    if (config.max_fish !== undefined) {
        lines.push(`max_fish: ${config.max_fish}`);
    }
    if (config.max_mineral !== undefined) {
        lines.push(`max_mineral: ${config.max_mineral}`);
    }

    // Object properties
    if (config.random_mob_generator !== undefined) {
        lines.push(`random_mob_generator:`);
        lines.push(`  enabled: ${config.random_mob_generator.enabled}`);
        lines.push(`  level: ${config.random_mob_generator.level}`);
    }
    if (config.mineral_generator !== undefined) {
        lines.push(`mineral_generator:`);
        lines.push(`  enabled: ${config.mineral_generator.enabled}`);
        lines.push(`  level: ${config.mineral_generator.level}`);
    }
    if (config.apocalypse_boss_mob !== undefined) {
        lines.push(`apocalypse_boss_mob:`);
        lines.push(`  npc_id: ${config.apocalypse_boss_mob.npc_id}`);
        lines.push(`  x1: ${config.apocalypse_boss_mob.x1}`);
        lines.push(`  y1: ${config.apocalypse_boss_mob.y1}`);
        lines.push(`  x2: ${config.apocalypse_boss_mob.x2}`);
        lines.push(`  y2: ${config.apocalypse_boss_mob.y2}`);
    }

    // Array properties
    if (config.initial_points.length > 0) {
        lines.push('');
        lines.push('initial_points:');
        for (const ip of config.initial_points) {
            lines.push(`  - { id: ${ip.id}, x: ${ip.x}, y: ${ip.y} }`);
        }
    }

    if (config.safe_zones.length > 0) {
        lines.push('');
        lines.push('safe_zones:');
        for (const sz of config.safe_zones) {
            lines.push(`  - { id: ${sz.id}, left: ${sz.left}, top: ${sz.top}, right: ${sz.right}, bottom: ${sz.bottom} }`);
        }
    }

    if (config.npc_avoid_rects.length > 0) {
        lines.push('');
        lines.push('npc_avoid_rects:');
        for (const nar of config.npc_avoid_rects) {
            lines.push(`  - { id: ${nar.id}, left: ${nar.left}, top: ${nar.top}, right: ${nar.right}, bottom: ${nar.bottom} }`);
        }
    }

    if (config.spawners.length > 0) {
        lines.push('');
        lines.push('spawners:');
        for (const sp of config.spawners) {
            lines.push(`  - { id: ${sp.id}, type: ${sp.type}, x1: ${sp.x1}, y1: ${sp.y1}, x2: ${sp.x2}, y2: ${sp.y2}, npc_type: ${sp.npc_type}, max_count: ${sp.max_count} }`);
        }
    }

    if (config.waypoints.length > 0) {
        lines.push('');
        lines.push('waypoints:');
        for (const wp of config.waypoints) {
            lines.push(`  - { id: ${wp.id}, x: ${wp.x}, y: ${wp.y} }`);
        }
    }

    if (config.teleports.length > 0) {
        lines.push('');
        lines.push('teleports:');
        for (const tp of config.teleports) {
            lines.push(`  - { src_x: ${tp.src_x}, src_y: ${tp.src_y}, dest_map: ${tp.dest_map}, dest_x: ${tp.dest_x}, dest_y: ${tp.dest_y}, direction: ${tp.direction} }`);
        }
    }

    if (config.npcs.length > 0) {
        lines.push('');
        lines.push('npcs:');
        for (const npc of config.npcs) {
            const waypointStr = npc.waypoints.length > 0 ? `, waypoints: [${npc.waypoints.join(', ')}]` : '';
            lines.push(`  - { name: ${npc.name}, move_type: ${npc.move_type}${waypointStr}, name_prefix: ${npc.name_prefix} }`);
        }
    }

    if (config.fish_points.length > 0) {
        lines.push('');
        lines.push('fish_points:');
        for (const fp of config.fish_points) {
            lines.push(`  - { id: ${fp.id}, x: ${fp.x}, y: ${fp.y} }`);
        }
    }

    if (config.mineral_points.length > 0) {
        lines.push('');
        lines.push('mineral_points:');
        for (const mp of config.mineral_points) {
            lines.push(`  - { id: ${mp.id}, x: ${mp.x}, y: ${mp.y} }`);
        }
    }

    if (config.strategic_points.length > 0) {
        lines.push('');
        lines.push('strategic_points:');
        for (const sp of config.strategic_points) {
            lines.push(`  - { side: ${sp.side}, value: ${sp.value}, x: ${sp.x}, y: ${sp.y} }`);
        }
    }

    if (config.energy_sphere_creation_points.length > 0) {
        lines.push('');
        lines.push('energy_sphere_creation_points:');
        for (const escp of config.energy_sphere_creation_points) {
            lines.push(`  - { type: ${escp.type}, x: ${escp.x}, y: ${escp.y} }`);
        }
    }

    if (config.energy_sphere_goal_points.length > 0) {
        lines.push('');
        lines.push('energy_sphere_goal_points:');
        for (const esgp of config.energy_sphere_goal_points) {
            lines.push(`  - { result: ${esgp.result}, aresden_x: ${esgp.aresden_x}, aresden_y: ${esgp.aresden_y}, elvine_x: ${esgp.elvine_x}, elvine_y: ${esgp.elvine_y} }`);
        }
    }

    if (config.strike_points.length > 0) {
        lines.push('');
        lines.push('strike_points:');
        for (const spt of config.strike_points) {
            lines.push(`  - map_name: ${spt.map_name}`);
            lines.push(`    x: ${spt.x}`);
            lines.push(`    y: ${spt.y}`);
            lines.push(`    hp: ${spt.hp}`);
            lines.push(`    effect_x: [${spt.effect_x.join(', ')}]`);
            lines.push(`    effect_y: [${spt.effect_y.join(', ')}]`);
        }
    }

    if (config.item_events.length > 0) {
        lines.push('');
        lines.push('item_events:');
        for (const ie of config.item_events) {
            lines.push(`  - { item_name: ${ie.item_name}, amount: ${ie.amount}, total: ${ie.total}, month: ${ie.month}, day: ${ie.day}, total_num: ${ie.total_num} }`);
        }
    }

    if (config.dynamic_gate_coords.length > 0) {
        lines.push('');
        lines.push('dynamic_gate_coords:');
        for (const dgc of config.dynamic_gate_coords) {
            lines.push(`  - { x1: ${dgc.x1}, y1: ${dgc.y1}, x2: ${dgc.x2}, y2: ${dgc.y2}, dest_map: ${dgc.dest_map}, dest_x: ${dgc.dest_x}, dest_y: ${dgc.dest_y} }`);
        }
    }

    if (config.heldenian_towers.length > 0) {
        lines.push('');
        lines.push('heldenian_towers:');
        for (const ht of config.heldenian_towers) {
            lines.push(`  - { type_id: ${ht.type_id}, x: ${ht.x}, y: ${ht.y}, side: ${ht.side} }`);
        }
    }

    if (config.heldenian_winning_zones.length > 0) {
        lines.push('');
        lines.push('heldenian_winning_zones:');
        for (const hwz of config.heldenian_winning_zones) {
            lines.push(`  - { id: ${hwz.id}, left: ${hwz.left}, top: ${hwz.top}, right: ${hwz.right}, bottom: ${hwz.bottom} }`);
        }
    }

    if (config.heldenian_gate_doors.length > 0) {
        lines.push('');
        lines.push('heldenian_gate_doors:');
        for (const hgd of config.heldenian_gate_doors) {
            lines.push(`  - { dir: ${hgd.dir}, x: ${hgd.x}, y: ${hgd.y} }`);
        }
    }

    lines.push('');  // Trailing newline

    fs.writeFileSync(filepath, lines.join('\n'), 'utf8');
}

function main() {
    // Default paths
    let inputDir = 'mapdata';
    let outputDir = 'mapdata';

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

    // Find all .txt files (case insensitive)
    const files = fs.readdirSync(inputDir);
    const txtFiles = files.filter(f => f.toLowerCase().endsWith('.txt'));

    console.log(`Converting ${txtFiles.length} map config files...`);

    let converted = 0;
    for (const txtFile of txtFiles) {
        try {
            const txtPath = path.join(inputDir, txtFile);
            const config = parseMapConfig(txtPath);
            const yamlPath = path.join(outputDir, path.basename(txtFile, path.extname(txtFile)).toLowerCase() + '.yaml');
            writeYaml(config, yamlPath);
            console.log(`  ${txtFile} -> ${path.basename(yamlPath)}`);
            converted++;
        } catch (e) {
            console.error(`  Error converting ${txtFile}: ${e.message}`);
        }
    }

    console.log(`\nDone. Converted ${converted}/${txtFiles.length} files.`);
}

main();
