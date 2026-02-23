# CFG → YAML Converter — Design

## Overview

A TypeScript CLI tool (`tools/convert/`) that reads all legacy Helbreath `.cfg` game data files and emits fresh YAML files for the modern server. Replaces all hand-converted YAML files with data directly derived from the authoritative legacy source, eliminating column-mapping bugs like the NPC cfg fix from 2026-02-14.

## Goals

- 1:1 conversion of legacy positional cfg fields to named YAML fields
- Correct field names from the legacy decode functions (not the historical misnomers in the old items.yaml)
- Single command, no arguments — converts all 10 cfg types at once
- Output overwrites existing YAML files in `bin/game_configs/`

## Location

```
tools/convert/
├── convert.ts      # main entry point
├── parser.ts       # generic tokenizer + schema-driven field extraction
├── schemas.ts      # field schema definitions for all 10 cfg types
├── emitters.ts     # per-type YAML serialization (flow/block/nested)
└── package.json    # copy from tools/migrate/
```

## Invocation

```bash
cd tools/convert && npx tsx convert.ts
```

Reads from: `../../bin/game_configs/old configs/<name>.cfg`
Writes to:  `../../bin/game_configs/<name>.yaml`

## Approach: Schema-Driven Parser

A single generic tokenizer mirrors Game.cpp's `CStrTok` with `seps = "= \t\n"`. Each cfg type is described by a schema defining its trigger word and ordered field definitions. The parser skips `//` comments and section headers (`[CONFIG]`, `[ITEMS]`, etc.).

```typescript
type field_type = 'int' | 'string' | 'bool' | 'price';
// 'price': negative value → emit is_for_sale: false, price: abs(value)

interface field_def {
    name: string;
    type: field_type;
}

interface cfg_schema {
    trigger: string;       // e.g. "Item", "magic", "teleport"
    end_marker?: string;   // e.g. "[ENDITEMLIST]", "[END]"
    fields: field_def[];
}
```

## Converters (10 total)

| Cfg file(s) | Output YAML | Top-level key | Style |
|-------------|-------------|---------------|-------|
| Item.cfg, Item2.cfg, Item3.cfg, Item4.cfg | items.yaml | `items` | flow, one entry per line |
| Npc.cfg | npcs.yaml | `npcs` | flow |
| Magic.cfg | magic.yaml | `magic` | block, nested effect objects |
| Skill.cfg | skills.yaml | `skills` | flow; prepend hard-coded `defaults` block |
| Quest.cfg | quests.yaml | `quests` | flow, all 27 fields |
| Teleport.cfg | teleports.yaml | `teleports` | block |
| Crusade.cfg | crusade_structures.yaml | `crusade_structures` | flow |
| CraftItem.cfg | craft_recipes.yaml | `crafting_recipes` | block with `ingredients` array |
| BuildItem.cfg | build_recipes.yaml | `build_recipes` | block with `ingredients` array |
| Potion.cfg | recipes.yaml | `alchemy_recipes` | block with `ingredients` array |

## Item Field Schema (26 positional fields)

Corrects the historical misnomers from the original hand-conversion:

| Position | Legacy field (CItem) | YAML field | Old misnomer |
|----------|----------------------|------------|--------------|
| 1 | m_sIDnum | id | — |
| 2 | m_cName | name | — |
| 3 | m_cItemType | type | — |
| 4 | m_cEquipPos | equip_pos | — |
| 5 | m_sItemEffectType | effect_type | color_r1 |
| 6 | m_sItemEffectValue1 | effect_value1 | color_g1 |
| 7 | m_sItemEffectValue2 | effect_value2 | color_b1 |
| 8 | m_sItemEffectValue3 | effect_value3 | color_r2 |
| 9 | m_sItemEffectValue4 | effect_value4 | color_g2 |
| 10 | m_sItemEffectValue5 | effect_value5 | color_b2 |
| 11 | m_sItemEffectValue6 | effect_value6 | weight |
| 12 | m_wMaxLifeSpan | durability | durability ✓ |
| 13 | m_sSpecialEffect | special_effect | equip_type |
| 14 | m_sSprite | sprite | sprite_id |
| 15 | m_sSpriteFrame | sprite_frame | price |
| 16 | m_wPrice | price (+is_for_sale) | attack_range |
| 17 | m_wWeight | weight | skill_type |
| 18 | m_cApprValue | appr_value | attack_bonus |
| 19 | m_cSpeed | speed | hit_prob |
| 20 | m_sLevelLimit | level_limit | dodge_prob |
| 21 | m_cGenderLimit | gender_limit | defense |
| 22 | m_sSpecialEffectValue1 | special_effect_value1 | level_limit |
| 23 | m_sSpecialEffectValue2 | special_effect_value2 | is_two_handed |
| 24 | m_sRelatedSkill | related_skill | unk1 |
| 25 | m_cCategory | category | effect1 |
| 26 | m_cItemColor | item_color | effect2 |

**Note:** The C++ YAML loaders (item_registry.cpp, etc.) will need to be updated separately to use the new field names.

## Special Cases

### Items: 4 cfg files merged
Item.cfg, Item2.cfg, Item3.cfg, Item4.cfg are parsed in order and merged into a single `items.yaml`. Duplicate IDs are a fatal error. Output sorted by ID.

### Price field: `is_for_sale` derivation
When `price < 0`: emit `is_for_sale: false, price: <abs value>`.
When `price >= 0`: emit `price: <value>` only (is_for_sale defaults to true in the loader).

### Magic: grouped effect values
12 positional effect values (columns 7–18 of the magic cfg) are grouped as 3 effect objects:
```yaml
effect1: { dice: 2, sides: 4, bonus: 1 }
effect2: { dice: 2, sides: 4, bonus: 1 }
effect3: { dice: 0, sides: 0, bonus: 0 }
```
Magic cfg columns (from `//` comment header):
`num name type delay last mana_cost range1 range2 e1dice e1sides e1bonus e2dice e2sides e2bonus e3dice e3sides e3bonus req_int cost category attribute`

### Recipes: variable-length ingredients
CraftItem, BuildItem, and Potion cfg files all have 6 ingredient pairs `(item_id, count)` with `-1` as the "no ingredient" sentinel. The schema flattens these as 12 positional fields. The emitter filters pairs where `item_id == -1` and emits the rest as:
```yaml
ingredients:
  - { item_id: 657, count: 1 }
  - { item_id: 356, count: 1 }
```

### Teleport: string booleans
`TRUE`/`FALSE` strings in the cfg → `true`/`false` YAML bools.

### Skills: hard-coded defaults block
The `defaults` block (level tiers/multipliers) is not in the cfg — it is hard-coded in the emitter and written at the top of skills.yaml before the skill entries.

## Quest fields (all 27)

From `_bDecodeQuestConfigFileContents` field order:
`id, side, type, target_type, max_count, from_id, min_level, max_level, req_skill, req_skill_pct, time_limit, assign_type, reward_type1, reward_amount1, reward_type2, reward_amount2, reward_type3, reward_amount3, cont, cont_limit, resp_mode, map, x, y, range, quest_id, req_cont`

## Implementation Notes

- `parser.ts` should export a single `parse_cfg(content: string, schema: cfg_schema): Record<string, unknown>[]` function
- `emitters.ts` should export per-type emitter functions that take parsed records and return a YAML string
- Use `js-yaml` (or hand-roll the simple flow/block serialization — the formats are predictable enough)
- Print a summary on completion: `items: 1045 entries, npcs: 312 entries, ...`
