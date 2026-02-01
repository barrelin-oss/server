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
    }

    return config;
}

function writeYaml(config, filepath) {
    const lines = [];

    lines.push(`name: ${config.name}`);

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
