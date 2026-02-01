# Legacy Magic System Documentation

**System:** Magic/Spell Casting
**Complexity:** High (~3,000+ lines across Game.cpp)
**Primary Files:** `Magic.cpp/h`, magic handling in `Game.cpp`, `Client.h`

---

## Table of Contents

1. [Overview](#1-overview)
2. [Core Data Structures](#2-core-data-structures)
3. [Magic Types (Effect Categories)](#3-magic-types-effect-categories)
4. [Complete Spell List](#4-complete-spell-list)
5. [Elemental Attributes](#5-elemental-attributes)
6. [Spell Casting Mechanics](#6-spell-casting-mechanics)
7. [Hit Ratio Calculation](#7-hit-ratio-calculation)
8. [Damage Calculation](#8-damage-calculation)
9. [Magic Resistance](#9-magic-resistance)
10. [Healing Mechanics](#10-healing-mechanics)
11. [Stamina Mechanics](#11-stamina-mechanics)
12. [Mana Regeneration](#12-mana-regeneration)
13. [Spell Learning System](#13-spell-learning-system)
14. [NPC Magic Casting](#14-npc-magic-casting)
15. [Weather Effects](#15-weather-effects)
16. [Special Spell Mechanics](#16-special-spell-mechanics)
17. [Area of Effect Mechanics](#17-area-of-effect-mechanics)
18. [Spell Duration & Effects](#18-spell-duration--effects)
19. [Combat Interaction](#19-combat-interaction)
20. [Constants & Limits](#20-constants--limits)
21. [Formula Summary](#21-formula-summary)

---

## 1. Overview

The Helbreath magic system is a type-based spell casting mechanic with:

- **33 distinct spell effect types** (magic types)
- **~50 individual spells** organized into 10 circles of difficulty
- **4 elemental attributes** with damage absorption mechanics
- **Skill mastery progression** for casting success
- **Complex hit/resistance calculations**
- **Area-of-effect, linear, and single-target spells**
- **Status effects** including buffs, debuffs, and DoTs

Magic is central to combat, providing damage, healing, crowd control, and utility effects.

---

## 2. Core Data Structures

### CMagic Class

**File:** `Magic.h`

```cpp
class CMagic {
public:
    CMagic();

    char  m_cName[31];           // Spell name (max 30 chars + null)

    short m_sType;               // Magic type (1-33, defines behavior)
    DWORD m_dwDelayTime;         // Cast delay in milliseconds
    DWORD m_dwLastTime;          // Effect duration in seconds

    // Effect values - interpretation depends on m_sType
    short m_sValue1;             // Usually: mana cost
    short m_sValue2;             // Usually: AOE radius X or dice count
    short m_sValue3;             // Usually: AOE radius Y or dice sides
    short m_sValue4;             // Usually: damage dice count / effect strength
    short m_sValue5;             // Usually: damage dice sides / duration
    short m_sValue6;             // Usually: damage bonus / effect value
    short m_sValue7;             // Secondary damage dice count
    short m_sValue8;             // Secondary damage dice sides
    short m_sValue9;             // Secondary damage bonus
    short m_sValue10;            // Tertiary effect values
    short m_sValue11;            // Tertiary effect values
    short m_sValue12;            // Tertiary effect values

    short m_sIntLimit;           // Intelligence requirement to learn/cast
    int   m_iGoldCost;           // Gold cost to learn (-1 = cannot purchase)

    char  m_cCategory;           // 0=Attack, 1=Defense/Utility, 2=Buff
    int   m_iAttribute;          // Element: 1=Earth, 2=Air, 3=Fire, 4=Water
};
```

### Player Magic Fields

**File:** `Client.h`

```cpp
// Spell knowledge
char m_cMagicMastery[DEF_MAXMAGICTYPE];      // 100 slots, 0=unknown, 1=learned

// Active effects
char m_cMagicEffectStatus[DEF_MAXMAGICEFFECTS]; // 100 concurrent effect slots

// Mana pool
int  m_iMP;                      // Current mana points
int  m_iMaxMP;                   // Maximum mana (calculated from INT/MAG)

// Casting modifiers
int  m_iManaSaveRatio;           // % mana cost reduction from items
int  m_iAddMagicalDamage;        // Flat magical damage bonus
int  m_iAddResistMagic;          // Magic resistance bonus

// Stats affecting magic
int  m_iMag;                     // Magic stat (0-255)
int  m_iInt;                     // Intelligence stat (0-255)

// Anti-cheat
DWORD m_dwMagicFreqTime;         // Last cast timestamp for hack detection
int   m_iSpellCount;             // Pre-cast counter

// Casting state
BOOL  m_bMagicConfirm;           // Currently casting flag
BOOL  m_bMagicPauseTime;         // Cast paused flag

// Mana regeneration
DWORD m_dwMPTime;                // Last MP regen timestamp

// Status flags
BOOL  m_bInhibition;             // Silenced - cannot cast

// Elemental absorption (% damage reduction)
int   m_iAddAbsEarth;            // Earth damage reduction %
int   m_iAddAbsAir;              // Air/Lightning damage reduction %
int   m_iAddAbsFire;             // Fire damage reduction %
int   m_iAddAbsWater;            // Ice/Water damage reduction %
```

### NPC Magic Fields

**File:** `Npc.h`

```cpp
char  m_cMagicLevel;             // NPC magic power level
int   m_iMagicHitRatio;          // Base hit ratio for spells
char  m_cResistMagic;            // Magic resistance (0-100)
char  m_cResistPoison;           // Poison resistance
```

---

## 3. Magic Types (Effect Categories)

33 magic types define spell behavior:

| Type | Constant | Name | Description |
|------|----------|------|-------------|
| 1 | `DEF_MAGICTYPE_DAMAGE_SPOT` | Single Target Damage | Damages one entity at target location |
| 2 | `DEF_MAGICTYPE_HPUP_SPOT` | Healing | Restores HP to single target |
| 3 | `DEF_MAGICTYPE_DAMAGE_AREA` | Area Damage (with center) | Damages all in area including center |
| 4 | `DEF_MAGICTYPE_SPDOWN_SPOT` | SP Drain (spot) | Reduces target stamina |
| 5 | `DEF_MAGICTYPE_SPDOWN_AREA` | SP Drain (area) | Area stamina reduction |
| 6 | `DEF_MAGICTYPE_SPUP_SPOT` | Stamina Recovery | Restores stamina to single target |
| 7 | `DEF_MAGICTYPE_SPUP_AREA` | Area Stamina Recovery | Restores stamina to area |
| 8 | `DEF_MAGICTYPE_TELEPORT` | Teleportation | Teleports caster to target location |
| 9 | `DEF_MAGICTYPE_SUMMON` | Creature Summon | Summons NPC minion |
| 10 | `DEF_MAGICTYPE_CREATE` | Object Creation | Creates items (food) |
| 11 | `DEF_MAGICTYPE_PROTECT` | Protection Buff | Increases defense/magic resistance |
| 12 | `DEF_MAGICTYPE_HOLDOBJECT` | Hold/Paralyze | Immobilizes target |
| 13 | `DEF_MAGICTYPE_INVISIBILITY` | Invisibility | Makes target invisible |
| 14 | `DEF_MAGICTYPE_CREATE_DYNAMIC` | Dynamic Object | Creates persistent ground effects |
| 15 | `DEF_MAGICTYPE_POSSESSION` | Possession | Possess target body |
| 16 | `DEF_MAGICTYPE_CONFUSE` | Confusion/Illusion | Confuses target or creates illusion |
| 17 | `DEF_MAGICTYPE_POISON` | Poison | Applies poison damage-over-time |
| 18 | `DEF_MAGICTYPE_BERSERK` | Berserk | Increases attack power |
| 19 | `DEF_MAGICTYPE_DAMAGE_LINEAR` | Linear Damage | Line/cone damage path |
| 20 | `DEF_MAGICTYPE_POLYMORPH` | Shape Change | Transforms target appearance |
| 21 | `DEF_MAGICTYPE_DAMAGE_AREA_NOSPOT` | Area Damage (no center) | AOE without center hit |
| 22 | `DEF_MAGICTYPE_TREMOR` | Ground Tremor | Earthquake damage with knockback |
| 23 | `DEF_MAGICTYPE_ICE` | Ice/Freeze | Ice-based damage with slow |
| 24 | (unused) | | |
| 25 | `DEF_MAGICTYPE_DAMAGE_AREA_NOSPOT_SPDOWN` | Area + SP Drain | Combined AOE damage + stamina drain |
| 26 | `DEF_MAGICTYPE_ICE_LINEAR` | Blizzard | Linear ice damage |
| 27 | (unused) | | |
| 28 | `DEF_MAGICTYPE_DAMAGE_AREA_ARMOR_BREAK` | Armor Breaking AOE | AOE with armor penetration |
| 29 | `DEF_MAGICTYPE_CANCELLATION` | Spell Cancellation | Removes active spell effects |
| 30 | `DEF_MAGICTYPE_DAMAGE_LINEAR_SPDOWN` | Linear + SP Drain | Line damage + stamina drain |
| 31 | `DEF_MAGICTYPE_INHIBITION` | Inhibition | Silences target casting |
| 32 | `DEF_MAGICTYPE_RESURRECTION` | Resurrection | Revives dead player |
| 33 | `DEF_MAGICTYPE_SCAN` | Scan Information | Reveals target information |

---

## 4. Complete Spell List

### Spell Configuration Format

```
magic = [ID] [Name] [Type] [DelayMS] [DurationS] [V1-V12...] [IntReq] [Cost] [Category] [Attribute]
```

### All Spells by Circle

#### Circle 1 (Spell ID 0-9) - Beginner

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 0 | Magic-Missile | 1 (Damage Spot) | 8 | 18 | Air | Basic single target damage |
| 1 | Heal | 2 (HP Up) | 15 | 20 | Water | Basic healing spell |
| 2 | Create-Food | 10 (Create) | 18 | 18 | None | Creates food items |
| 3 | GM-Kill | 21 (Damage Area) | 10 | 10 | Special | Admin-only instant kill |

#### Circle 2 (Spell ID 10-19)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 10 | Energy-Bolt | 3 (Damage Area) | 15 | 24 | Air | Small AOE damage |
| 11 | Staminar-Drain | 5 (SP Down Area) | 14 | 22 | None | AOE stamina reduction |
| 12 | Recall | 8 (Teleport) | 15 | 20 | None | Teleport to recall point |
| 13 | Defense-Shield | 11 (Protect) | 19 | 26 | None | +3 defense buff (60s) |
| 14 | Celebrating-Light | 5 (SP Down) | 20 | 25 | None | Utility/visual effect |

#### Circle 3 (Spell ID 20-29)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 20 | Fire-Ball | 3 (Damage Area) | 40 | 26 | Fire | Fire AOE damage |
| 21 | Great-Heal | 2 (HP Up) | 28 | 28 | None | Powerful healing |
| 23 | Staminar-Recovery | 7 (SP Up Area) | 20 | 20 | None | AOE stamina restore |
| 24 | Protection-From-Arrow | 11 (Protect) | 22 | 20 | None | Physical defense buff (60s) |
| 25 | Hold-Person | 12 (Hold) | 24 | 26 | None | Paralyze target (30s) |
| 26 | Possession | 15 (Possess) | 25 | 26 | None | Possess target |
| 27 | Poison | 17 (Poison) | 28 | 29 | Earth | DoT poison (300 ticks) |
| 28 | Great-Staminar-Recov | 7 (SP Up Area) | 45 | 30 | None | Large AOE stamina restore |

#### Circle 4 (Spell ID 30-39)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 30 | Fire-Strike | 3 (Damage Area) | 36 | 34 | Fire | Fire damage |
| 31 | Summon-Creature | 9 (Summon) | 35 | 38 | Earth | Summons NPC ally |
| 32 | Invisibility | 13 (Invis) | 31 | 30 | None | Invisibility (60s) |
| 33 | Protection-From-Magic | 11 (Protect) | 35 | 32 | None | Magic defense buff (60s) |
| 34 | Detect-Invisibility | 13 (Invis) | 33 | 30 | None | Reveals invisible |
| 35 | Paralyze | 12 (Hold) | 35 | 36 | None | Strong paralyze (50s) |
| 36 | Cure | 17 (Poison) | 32 | 35 | Earth | Cures poison |
| 37 | Lightning-Arrow | 1 (Damage Spot) | 40 | 38 | Air | Lightning single target |
| 38 | Tremor | 22 (Tremor) | 34 | 33 | Earth | Ground shake damage |

#### Circle 5 (Spell ID 40-49)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 40 | Fire-Wall | 14 (Create Dynamic) | 42 | 45 | Fire | Fire field (30s) |
| 41 | Fire-Field | 14 (Create Dynamic) | 48 | 48 | Fire | Larger fire field (30s) |
| 42 | Confuse-Language | 16 (Confuse) | 40 | 42 | None | Language confusion (20s) |
| 43 | Lightning | 1 (Damage Spot) | 44 | 47 | Air | Strong lightning |
| 44 | Great-Defense-Shield | 11 (Protect) | 45 | 46 | None | +4 defense buff (40s) |
| 45 | Chill-Wind | 23 (Ice) | 48 | 50 | Water | Ice damage |
| 46 | Poison-Cloud | 14 (Create Dynamic) | 48 | 49 | Earth | Poison field (30s) |
| 47 | Triple-Energy-Bolt | 3 (Damage Area) | 40 | 45 | Air | Triple bolt damage |

#### Circle 6 (Spell ID 50-59)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 50 | Berserk | 18 (Berserk) | 57 | 59 | None | Attack power buff (40s) |
| 51 | Lightning-Bolt | 19 (Damage Linear) | 58 | 58 | Air | Linear lightning |
| 53 | Mass-Poison | 17 (Poison) | 54 | 52 | Earth | AOE poison (600 ticks) |
| 54 | Spike-Field | 14 (Create Dynamic) | 56 | 56 | Earth | Spike trap (60s) |
| 55 | Ice-Storm | 14 (Create Dynamic) | 58 | 59 | Water | Ice field (60s) |
| 56 | Mass-Lightning-Arrow | 1 (Damage Spot) | 55 | 53 | Air | Multi-target lightning |
| 57 | Ice-Strike | 23 (Ice) | 59 | 60 | Water | Strong ice damage |

#### Circle 7 (Spell ID 60-69)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 60 | Energy-Strike | 21 (Damage Area Nospot) | 65 | 67 | Air | Energy AOE |
| 61 | Mass-Fire-Strike | 3 (Damage Area) | 80 | 85 | Fire | Large fire AOE |
| 62 | Confusion | 16 (Confuse) | 78 | 75 | None | Confusion effect (20s) |
| 63 | Mass-Chill-Wind | 23 (Ice) | 90 | 93 | Water | Large ice AOE |
| 64 | Earthworm-Strike | 25 (Nospot+SP) | 80 | 97 | Earth | Earth damage + SP drain |
| 65 | Absolute-Magic-Protect | 11 (Protect) | 250 | 300 | None | 100% magic immunity (60s) |
| 66 | Armor-Break | 28 (Armor Break) | 290 | 350 | Earth | Armor penetration AOE |

#### Circle 8 (Spell ID 70-79)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 70 | Bloody-Shock-Wave | 19 (Damage Linear) | 120 | 250 | Special | Powerful linear damage |
| 71 | Mass-Confusion | 16 (Confuse) | 125 | 130 | None | AOE confusion (20s) |
| 72 | Mass-Ice-Strike | 23 (Ice) | 120 | 133 | Water | Mass ice damage |
| 73 | Cloud-Kill | 14 (Create Dynamic) | 130 | 120 | Earth | Deadly poison cloud (60s) |
| 74 | Lightning-Strike | 21 (Damage Area Nospot) | 60 | 123 | Air | Lightning AOE |
| 76 | Cancellation | 29 (Cancel) | 120 | 450 | None | Removes all spell effects |
| 77 | Illusion-Movement | 16 (Confuse) | 130 | 350 | Air | Movement illusion (10s) |

#### Circle 9 (Spell ID 80-89)

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 80 | Illusion | 16 (Confuse) | 143 | 150 | None | Visual illusion (10s) |
| 81 | Meteor-Strike | 21 (Damage Area Nospot) | 60 | 200 | Fire | Meteor impact damage |
| 82 | Mass-Magic-Missile | 21 (Damage Area Nospot) | 160 | 250 | Air | Many missiles AOE |
| 83 | Inhibition-Casting | 31 (Inhibition) | 180 | 450 | None | Silence target (60s) |
| 84 | Mass-Illusion | 16 (Confuse) | 200 | 280 | None | AOE illusion (20s) |

#### Circle 10 (Spell ID 90-99) - Master

| ID | Name | Type | Mana | Int Req | Element | Description |
|----|------|------|------|---------|---------|-------------|
| 90 | Hellfire | 21 (Damage Area Nospot) | 500 | 400 | Fire | Ultimate fire spell |
| 91 | Blizzard | 26 (Ice Linear) | 220 | 200 | Water | Linear ice wave (20s) |
| 92 | Fiery-Shock-Wave | 19 (Damage Linear) | 280 | 450 | Fire | Fire shock wave |
| 93 | Mass-Blizzard | 26 (Ice Linear) | 320 | 450 | Water | Mass ice wave (20s) |
| 94 | Resurrection | 32 (Resurrect) | 200 | 0 | Air | Revive dead player |
| 95 | Mass-Illusion-Movement | 16 (Confuse) | 200 | 450 | Air | Mass movement illusion |
| 96 | Earth-Shock-Wave | 30 (Linear+SP) | 240 | 350 | Earth | Earth wave + SP drain |
| 97 | Fury-Of-Thor | 24 (Polymorph) | 480 | 500 | Air | Transform spell |
| 98 | Strike-of-the-Ghosts | 30 (Linear+SP) | 500 | 500 | Special | Ghost strike + SP drain |

---

## 5. Elemental Attributes

### Element Definitions

| Value | Element | Associated Spells | Resistance Stat |
|-------|---------|-------------------|-----------------|
| 0 | None | Utility spells, buffs | - |
| 1 | Earth | Tremor, Poison, Earthworm | `m_iAddAbsEarth` |
| 2 | Air | Lightning, Energy, Magic Missile | `m_iAddAbsAir` |
| 3 | Fire | Fireball, Fire Strike, Hellfire | `m_iAddAbsFire` |
| 4 | Water | Ice Storm, Blizzard, Chill Wind | `m_iAddAbsWater` |
| 6 | Special | Bloody Shock Wave, Ghost Strike | No absorption |
| 100 | Admin | GM-Kill | No absorption |

### Elemental Absorption Formula

```cpp
// Applied during damage calculation
if (target_absorption[element] > 0) {
    int reduction = damage * (absorption / 100);
    final_damage = damage - reduction;
    if (final_damage < 0) final_damage = 0;
}

// Example: 100 fire damage vs 30% fire absorption
// reduction = 100 * (30/100) = 30
// final_damage = 100 - 30 = 70
```

---

## 6. Spell Casting Mechanics

### Casting Requirements

Before a spell can be cast, these conditions must be met:

```cpp
// 1. Spell knowledge check
if (m_cMagicMastery[spell_id] != 1) {
    // Player doesn't know this spell
    return FAIL;
}

// 2. Intelligence requirement
if (m_iInt < spell->m_sIntLimit) {
    // INT too low to cast
    return FAIL;
}

// 3. Mana check
int mana_cost = CalculateManaCost(spell);
if (m_iMP < mana_cost) {
    // Not enough mana
    return FAIL;
}

// 4. Silence check
if (m_bInhibition == TRUE) {
    // Player is silenced
    return FAIL;
}

// 5. Hunger check
if (m_iHungerStatus <= 10) {
    // Too hungry to cast (10 = 0.1% = nearly starving)
    return FAIL;
}

// 6. Weapon restriction check
// Can only cast with:
// - Staff weapons (item types 34-39)
// - Empty right hand
// Cannot cast with shield or two-handed weapons
if (!ValidWeaponForCasting()) {
    return FAIL;
}

// 7. Rate limiting (anti-cheat)
if (timeGetTime() - m_dwMagicFreqTime < 1000) {
    // Casting too fast (< 1 second between casts)
    return FAIL;
}
```

### Mana Cost Calculation

```cpp
int CalculateManaCost(int spell_id) {
    int base_cost = m_pMagicConfigList[spell_id]->m_sValue1;

    // Safe zone penalty (+40% cost outside fight zones)
    if (m_bIsSafeAttackMode && !m_bIsFightZone) {
        base_cost += (base_cost / 2) - (base_cost / 10);
        // Effectively: base_cost * 1.4
    }

    // Mana save ratio from items (% reduction)
    if (m_iManaSaveRatio > 0) {
        int saved = (base_cost * m_iManaSaveRatio) / 100;
        base_cost -= saved;
        if (base_cost <= 0) base_cost = 1;
    }

    // Staff weapon penalty
    if (equipped_weapon_type == 34) {  // Basic staff
        base_cost += 20;
    }

    return base_cost;
}
```

---

## 7. Hit Ratio Calculation

The hit ratio determines spell success probability, accounting for caster skill, spell difficulty, and environmental factors.

### Magic Circle System

Spells are organized into 10 circles based on ID:

```cpp
int GetMagicCircle(int spell_id) {
    return (spell_id / 10) + 1;
}

// Spell 0-9   = Circle 1 (easiest)
// Spell 10-19 = Circle 2
// Spell 20-29 = Circle 3
// ...
// Spell 90-99 = Circle 10 (hardest)
```

### Base Probability by Circle

```cpp
int circle_base_prob[] = {
    0,    // Unused
    300,  // Circle 1: 300%
    250,  // Circle 2: 250%
    200,  // Circle 3: 200%
    150,  // Circle 4: 150%
    100,  // Circle 5: 100%
    80,   // Circle 6: 80%
    70,   // Circle 7: 70%
    60,   // Circle 8: 60%
    50,   // Circle 9: 50%
    40    // Circle 10: 40%
};

int level_penalty[] = {
    0,    // Unused
    5,    // Circle 1
    5,    // Circle 2
    8,    // Circle 3
    8,    // Circle 4
    10,   // Circle 5
    14,   // Circle 6
    28,   // Circle 7
    32,   // Circle 8
    36,   // Circle 9
    40    // Circle 10
};
```

### Complete Hit Ratio Formula

```cpp
int CalculateMagicHitRatio(int caster_handle, int spell_id) {
    int magic_circle = GetMagicCircle(spell_id);

    // Base from magic skill mastery
    double skill_mastery = m_cSkillMastery[4];  // Magic skill index
    if (skill_mastery == 0) skill_mastery = 1.0;

    double ratio = (skill_mastery / 100.0) * circle_base_prob[magic_circle];
    int result = (int)ratio;

    // Intelligence bonus (if INT > 50)
    if (m_iInt > 50) {
        result += (m_iInt - 50) / 2;
    }

    // Level vs Circle comparison
    int player_magic_level = m_iLevel / 10;  // Player's effective magic level

    if (magic_circle != player_magic_level) {
        if (magic_circle > player_magic_level) {
            // Casting higher circle spell than level allows - PENALTY
            int diff = magic_circle - player_magic_level;
            int penalty = diff * level_penalty[magic_circle];
            result -= penalty;
        } else {
            // Casting lower circle spell - BONUS
            int diff = player_magic_level - magic_circle;
            result += 5 * diff;
        }
    }

    // Weather impact (rain hurts casting)
    switch (map_weather_status) {
        case 1:  // Light rain
            result -= result / 24;  // ~4% reduction
            break;
        case 2:  // Medium rain
            result -= result / 12;  // ~8% reduction
            break;
        case 3:  // Heavy rain
            result -= result / 5;   // 20% reduction
            break;
    }

    // Special weapon effects
    if (m_iSpecialWeaponEffectType == 10) {  // Magic boost weapon
        result += (m_iSpecialWeaponEffectValue * 3);
    }

    // Minimum of 1
    if (result <= 0) result = 1;

    return result;
}
```

---

## 8. Damage Calculation

### Single Target Damage (Effect_Damage_Spot)

```cpp
void Effect_Damage_Spot(
    int caster_handle,
    char caster_type,      // PLAYER or NPC
    int target_handle,
    char target_type,
    short sV1,             // Damage dice count
    short sV2,             // Damage dice sides
    short sV3,             // Damage bonus
    BOOL apply_resistance,
    int element            // 1=Earth, 2=Air, 3=Fire, 4=Water
) {
    // Base damage roll
    int damage = iDice(sV1, sV2) + sV3;
    if (damage <= 0) damage = 0;

    // Caster modifications (player only)
    if (caster_type == DEF_OWNERTYPE_PLAYER) {

        // Hero armor set bonus
        if (m_cHeroArmourBonus == 2) {
            damage += 4;
        }

        // MAG stat scaling
        double mag_modifier;
        if (m_iMag <= 0) {
            mag_modifier = 1.0;
        } else {
            mag_modifier = (double)m_iMag / 3.3;
        }
        damage += (int)(damage * (mag_modifier / 100.0));

        // Flat magical damage bonus from items
        damage += m_iAddMagicalDamage;
        if (damage <= 0) damage = 0;

        // Fight zone bonus (+33%)
        if (m_bIsFightZone) {
            damage += damage / 3;
        }

        // Heldenian map bonus (+33%)
        if (bCheckHeldenianMap) {
            damage += damage / 3;
        }

        // Crusade duty bonus (magic specialist)
        if (bIsCrusadeMode && m_iCrusadeDuty == 1) {
            if (m_iLevel <= 80) {
                damage += (damage * 7) / 10;    // +70%
            } else if (m_iLevel <= 100) {
                damage += damage / 2;            // +50%
            } else {
                damage += damage / 3;            // +33%
            }
        }
    }

    // Target elemental resistance
    if (apply_resistance && target_type == DEF_OWNERTYPE_PLAYER) {
        int absorption = 0;
        switch (element) {
            case 1: absorption = target->m_iAddAbsEarth; break;
            case 2: absorption = target->m_iAddAbsAir;   break;
            case 3: absorption = target->m_iAddAbsFire;  break;
            case 4: absorption = target->m_iAddAbsWater; break;
        }
        if (absorption > 0) {
            damage -= (int)(damage * (absorption / 100.0));
        }
    }

    // Magic damage reduction items
    if (target_type == DEF_OWNERTYPE_PLAYER) {
        switch (target->m_iMagicDamageSaveItemIndex) {
            case 335:  // Specific item reduces damage to 80%
                damage = (int)(damage * 0.8);
                break;
            // Additional items...
        }
    }

    if (damage < 0) damage = 0;

    // Apply damage to target
    ApplyDamage(target_handle, target_type, damage, caster_handle);
}
```

### Area Damage (Effect_Damage_Area)

```cpp
void Effect_Damage_Area(
    int caster_handle,
    char caster_type,
    int map_index,
    int center_x, int center_y,    // AOE center
    short radius_x, short radius_y, // AOE dimensions
    short sV1, short sV2, short sV3, // Center damage (dice + bonus)
    short sV4, short sV5, short sV6, // Ring damage (dice + bonus)
    BOOL include_center,
    int element
) {
    // Iterate over rectangular area
    for (int iy = center_y - radius_y; iy <= center_y + radius_y; iy++) {
        for (int ix = center_x - radius_x; ix <= center_x + radius_x; ix++) {

            // Get entity at this tile
            short target_handle;
            char target_type;
            m_pMapList[map_index]->GetOwner(&target_handle, &target_type, ix, iy);

            if (target_handle == 0) continue;  // Empty tile

            // Check if center tile
            if (ix == center_x && iy == center_y) {
                if (include_center) {
                    // Full damage at center
                    Effect_Damage_Spot(caster_handle, caster_type,
                        target_handle, target_type,
                        sV1, sV2, sV3, TRUE, element);
                }
            } else {
                // Reduced damage in ring
                Effect_Damage_Spot(caster_handle, caster_type,
                    target_handle, target_type,
                    sV4, sV5, sV6, TRUE, element);
            }
        }
    }
}
```

---

## 9. Magic Resistance

### Resistance Check Function

```cpp
BOOL bCheckResistingMagicSuccess(
    char cAttackerDir,      // Attacker facing direction
    short sTargetH,         // Target handle
    char cTargetType,       // PLAYER or NPC
    int iHitRatio           // Caster's hit ratio
) {
    // Invincibility status = 100% resist
    if (target->m_iStatus & 0x400000) {
        return TRUE;
    }

    // Admin immunity
    if (target->m_iAdminUserLevel > 0) {
        return TRUE;
    }

    // Calculate target's magic resistance
    int resist_ratio = 0;

    if (cTargetType == DEF_OWNERTYPE_PLAYER) {
        // Base from magic resistance skill
        resist_ratio = m_cSkillMastery[3];  // Magic resistance skill

        // Equipment bonuses
        resist_ratio += m_iAddMR;

        // MAG stat bonus (if > 50)
        if (m_iMag > 50) {
            resist_ratio += (m_iMag - 50);
        }

        // Item bonuses
        resist_ratio += m_iAddResistMagic;
    } else {
        // NPC base resistance
        resist_ratio = m_pNpcList[sTargetH]->m_cResistMagic;
    }

    // Protection spell effects
    char protect_level = target->m_cMagicEffectStatus[11];
    if (protect_level == 5) {
        // Absolute Magic Protection = 100% resist
        return TRUE;
    }
    if (iHitRatio < 10000 && protect_level == 2) {
        // Protection-From-Magic active
        return TRUE;
    }

    // Normalize hit ratio
    if (iHitRatio >= 10000) {
        iHitRatio -= 10000;
    }

    // Minimum resistance
    if (resist_ratio < 1) resist_ratio = 1;

    // Hero armor magic resistance bonus
    if (cAttackerDir != 0 && target->m_cHeroArmourBonus == 2) {
        iHitRatio += 50;
    }

    // Final hit chance calculation
    double ratio = (double)iHitRatio / (double)resist_ratio * 50.0;
    int final_hit_chance = (int)ratio;

    // Clamp to min/max
    if (final_hit_chance < DEF_MINIMUMHITRATIO) {
        final_hit_chance = DEF_MINIMUMHITRATIO;  // 15%
    }
    if (final_hit_chance > DEF_MAXIMUMHITRATIO) {
        final_hit_chance = DEF_MAXIMUMHITRATIO;  // 95%
    }

    // 100% hit = 0% resist
    if (final_hit_chance >= 100) {
        return FALSE;
    }

    // Roll dice
    if (iDice(1, 100) <= final_hit_chance) {
        return FALSE;  // Failed to resist (spell hits)
    }

    // Successfully resisted - gain skill experience
    if (cTargetType == DEF_OWNERTYPE_PLAYER) {
        CalculateSSN_SkillIndex(sTargetH, 3, 1);  // Magic resistance skill
    }

    return TRUE;  // Resisted
}
```

### Ice Resistance

Ice spells have a separate resistance check:

```cpp
BOOL bCheckResistingIceSuccess(
    char cAttackerDir,
    short sTargetH,
    char cTargetType,
    int iHitRatio
) {
    int ice_resist = 0;

    if (cTargetType == DEF_OWNERTYPE_PLAYER) {
        // Water absorption * 2
        ice_resist = target->m_iAddAbsWater * 2;

        // Warm effect immunity (30 second buff from certain items)
        if ((timeGetTime() - target->m_dwWarmEffectTime) < 30000) {
            return TRUE;  // 100% immune during warm effect
        }
    } else {
        // NPCs: reduced ice resistance (70% of magic resist)
        ice_resist = target->m_cResistMagic - (target->m_cResistMagic / 3);
    }

    if (ice_resist < 1) ice_resist = 1;

    // Roll dice
    if (iDice(1, 100) <= ice_resist) {
        return TRUE;  // Resisted
    }

    return FALSE;
}
```

---

## 10. Healing Mechanics

### HP Recovery (Effect_HpUp_Spot)

```cpp
void Effect_HpUp_Spot(
    short sCasterH, char cCasterType,
    short sTargetH, char cTargetType,
    short sV1,      // Heal dice count
    short sV2,      // Heal dice sides
    short sV3       // Heal bonus
) {
    int heal_amount = iDice(sV1, sV2) + sV3;

    if (cTargetType == DEF_OWNERTYPE_PLAYER) {
        // Calculate max HP
        int max_hp = (3 * m_iVit) + (2 * m_iLevel) + (m_iStr / 2);

        // Side effect reduction (from certain debuffs)
        if (m_iSideEffect_MaxHPdown != 0) {
            max_hp = max_hp - (max_hp / m_iSideEffect_MaxHPdown);
        }

        // Apply healing
        if (m_iHP < max_hp) {
            m_iHP += heal_amount;
            if (m_iHP > max_hp) m_iHP = max_hp;
            if (m_iHP <= 0) m_iHP = 1;

            SendNotifyMsg(NULL, sTargetH, DEF_NOTIFY_HP, NULL, NULL, NULL, NULL);
        }
    } else if (cTargetType == DEF_OWNERTYPE_NPC) {
        // NPC max HP = hit dice * 4
        int max_hp = m_pNpcList[sTargetH]->m_iHitDice * 4;

        if (m_iHP < max_hp) {
            m_iHP += heal_amount;
            if (m_iHP > max_hp) m_iHP = max_hp;
            if (m_iHP <= 0) m_iHP = 1;
        }
    }
}
```

### Max HP Formula

```cpp
int CalculateMaxHP_Player(int vit, int level, int str) {
    return (3 * vit) + (2 * level) + (str / 2);
}

int CalculateMaxHP_NPC(int hit_dice) {
    return hit_dice * 4;
}
```

---

## 11. Stamina Mechanics

### Stamina Drain (Effect_SpDown_Spot)

```cpp
void Effect_SpDown_Spot(
    short sCasterH, char cCasterType,
    short sTargetH, char cTargetType,
    short sV1, short sV2, short sV3
) {
    int sp_drain = iDice(sV1, sV2) + sV3;

    if (cTargetType == DEF_OWNERTYPE_PLAYER) {
        // Check invincibility
        if ((m_iStatus & 0x400000) != 0) return;

        // Check stamina protection buff
        if (m_iTimeLeft_FirmStaminar == 0) {
            m_iSP -= sp_drain;
            if (m_iSP < 0) m_iSP = 0;

            SendNotifyMsg(NULL, sTargetH, DEF_NOTIFY_SP, NULL, NULL, NULL, NULL);
        }
    }
}
```

### Stamina Recovery (Effect_SpUp_Spot)

```cpp
void Effect_SpUp_Spot(
    short sCasterH, char cCasterType,
    short sTargetH, char cTargetType,
    short sV1, short sV2, short sV3
) {
    int sp_restore = iDice(sV1, sV2) + sV3;
    int max_sp = (2 * m_iStr) + (2 * m_iLevel);

    if (m_iSP < max_sp) {
        m_iSP += sp_restore;
        if (m_iSP > max_sp) m_iSP = max_sp;

        SendNotifyMsg(NULL, sTargetH, DEF_NOTIFY_SP, NULL, NULL, NULL, NULL);
    }
}
```

### Max SP Formula

```cpp
int CalculateMaxSP(int str, int level) {
    return (2 * str) + (2 * level);
}
```

---

## 12. Mana Regeneration

### Periodic MP Recovery

```cpp
void TimeManaPointsUp(int iClientH) {
    // Called periodically (every DEF_MPUPTIME = 20 seconds)

    if (m_iMP >= m_iMaxMP) return;  // Already full

    // Base regen from INT
    int regen = (m_iInt / 10) + 1;

    // Magic skill bonus
    regen += (m_cSkillMastery[4] / 5);

    // Equipment bonuses
    regen += m_iAddMPRecovery;

    // Apply regeneration
    m_iMP += regen;
    if (m_iMP > m_iMaxMP) m_iMP = m_iMaxMP;

    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MP, NULL, NULL, NULL, NULL);
}
```

### Max MP Calculation

```cpp
int CalculateMaxMP(int intelligence, int mag, int level) {
    return (2 * intelligence) + mag + (level / 2);
}
```

---

## 13. Spell Learning System

### Learning a Spell

```cpp
void RequestStudyMagicHandler(int iClientH, char* pName, BOOL bIsPurchase) {
    // Find spell by name
    int spell_id = -1;
    int required_int = 0;
    int gold_cost = 0;

    for (int i = 0; i < DEF_MAXMAGICTYPE; i++) {
        if (m_pMagicConfigList[i] == NULL) continue;
        if (strcmp(m_pMagicConfigList[i]->m_cName, pName) == 0) {
            spell_id = i;
            required_int = m_pMagicConfigList[i]->m_sIntLimit;
            gold_cost = m_pMagicConfigList[i]->m_iGoldCost;
            break;
        }
    }

    if (spell_id == -1) {
        // Spell not found
        return;
    }

    // Check if already learned
    if (m_cMagicMastery[spell_id] != 0) {
        // Already knows this spell
        return;
    }

    // Check INT requirement
    if (required_int > m_iInt) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAGICSTUDYFAIL,
                      1, NULL, NULL, NULL);  // Reason: INT too low
        return;
    }

    // Check if purchasable
    if (gold_cost == -1) {
        // Cannot be purchased (quest only)
        return;
    }

    // Check gold (if purchasing)
    if (bIsPurchase == TRUE) {
        DWORD current_gold = dwGetItemCount(iClientH, "Gold");
        if (gold_cost > current_gold) {
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAGICSTUDYFAIL,
                          2, NULL, NULL, NULL);  // Reason: Not enough gold
            return;
        }

        // Deduct gold
        SetItemCount(iClientH, "Gold", current_gold - gold_cost);
    }

    // Learn the spell
    m_cMagicMastery[spell_id] = 1;

    // Success notification
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_MAGICSTUDYSUCCESS,
                  spell_id, 0, NULL, NULL);
}
```

### Spell Availability

| Gold Cost | Meaning |
|-----------|---------|
| > 0 | Can be purchased from NPCs |
| -1 | Cannot be purchased (quest reward only) |
| -10 | Admin-only spell |

---

## 14. NPC Magic Casting

### NPC Spell Handler

```cpp
void NpcMagicHandler(int iNpcH, short dX, short dY, short sType) {
    if (m_pNpcList[iNpcH] == NULL) return;
    if (m_pMagicConfigList[sType] == NULL) return;

    // Get spell properties
    short sValue1 = m_pMagicConfigList[sType]->m_sValue1;
    short sValue2 = m_pMagicConfigList[sType]->m_sValue2;
    short sValue3 = m_pMagicConfigList[sType]->m_sValue3;
    // ... etc

    // NPC hit ratio (simpler than player)
    int iResult = m_pNpcList[iNpcH]->m_iMagicHitRatio;

    // Weather modifier
    int weather_bonus = iGetWhetherMagicBonusEffect(
        sType,
        m_pMapList[m_pNpcList[iNpcH]->m_cMapIndex]->m_cWhetherStatus
    );

    int element = m_pMagicConfigList[sType]->m_iAttribute;

    // Find target at location
    short sOwnerH;
    char cOwnerType;
    m_pMapList[map_index]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);

    // Apply spell effect based on type
    switch (m_pMagicConfigList[sType]->m_sType) {
        case DEF_MAGICTYPE_DAMAGE_SPOT:
            Effect_Damage_Spot(
                iNpcH, DEF_OWNERTYPE_NPC,
                sOwnerH, cOwnerType,
                sValue4, sValue5, sValue6 + weather_bonus,
                TRUE, element
            );
            break;

        case DEF_MAGICTYPE_HPUP_SPOT:
            Effect_HpUp_Spot(
                iNpcH, DEF_OWNERTYPE_NPC,
                sOwnerH, cOwnerType,
                sValue4, sValue5, sValue6
            );
            break;

        // ... other spell types
    }
}
```

### NPC Magic Properties

```cpp
// NPC magic is simpler:
// - Fixed hit ratio (m_iMagicHitRatio)
// - No mana cost
// - No INT requirement
// - Limited spell selection based on NPC type
// - Some NPCs cannot be held/paralyzed (magic level >= 6)
```

---

## 15. Weather Effects

### Weather Bonus Function

```cpp
int iGetWhetherMagicBonusEffect(short sType, char cWeatherStatus) {
    int bonus = 0;

    switch (cWeatherStatus) {
        case 0:  // Clear
            break;

        case 1:  // Light rain
        case 2:  // Medium rain
        case 3:  // Heavy rain
            // Lightning spells get BONUS in rain
            switch (sType) {
                case 10:  // Energy-Bolt
                case 37:  // Lightning-Arrow
                case 43:  // Lightning
                case 51:  // Lightning-Bolt
                case 56:  // Mass-Lightning-Arrow
                case 74:  // Lightning-Strike
                    bonus = 1;  // +1 damage bonus
                    break;
            }

            // Fire spells get PENALTY in rain
            switch (sType) {
                case 20:  // Fire-Ball
                case 30:  // Fire-Strike
                case 40:  // Fire-Wall
                case 41:  // Fire-Field
                case 61:  // Mass-Fire-Strike
                case 81:  // Meteor-Strike
                case 90:  // Hellfire
                case 92:  // Fiery-Shock-Wave
                    bonus = -1;  // -1 damage penalty
                    break;
            }
            break;
    }

    return bonus;
}
```

---

## 16. Special Spell Mechanics

### Invisibility (Type 13)

```cpp
// Two strength levels
sValue4 == 1  // Weak invisibility (detectable with Detect-Invisibility)
sValue4 == 2  // Strong invisibility (harder to detect)

// Effect application
target->m_iStatus |= 0x10;  // Set invisibility flag
target->m_cMagicEffectStatus[13] = strength;

// Duration: m_dwLastTime seconds (typically 60s)

// Breaking invisibility:
// - Taking any damage
// - Casting offensive spell
// - Attacking
// - Sometimes moving (depends on strength)

// Removal
target->m_iStatus &= ~0x10;  // Clear invisibility flag
target->m_cMagicEffectStatus[13] = 0;
```

### Hold/Paralyze (Type 12)

```cpp
// Application
target->m_cMagicEffectStatus[12] = strength;  // 1 or 2

// Effects while paralyzed:
// - Cannot move
// - Cannot attack
// - Cannot cast spells
// - Cannot use items

// Immunity:
// - NPCs with m_cMagicLevel >= 6 cannot be held
// - Bosses are typically immune

// Duration: 30-50 seconds
```

### Protection Spells (Type 11)

Three protection levels:

| Level | Spell | Effect |
|-------|-------|--------|
| 1 | Protection-From-Arrow | Physical damage reduction |
| 2 | Protection-From-Magic | Magic damage reduction, blocks some spells |
| 3 | Defense-Shield | General defense boost |
| 4 | Great-Defense-Shield | Greater defense boost |
| 5 | Absolute-Magic-Protection | 100% magic immunity |

```cpp
target->m_cMagicEffectStatus[11] = protection_level;
// Duration: 40-60 seconds
```

### Poison (Type 17)

```cpp
// Application
target->m_cMagicEffectStatus[17] = 1;  // Poisoned flag
target->m_iPoisonLevel = sValue5;       // Duration in ticks

// Damage per tick
// Applied every DEF_POISONTIME (12 seconds)
int poison_damage = base_poison_damage;

// Cure spell (ID 36) removes poison
if (spell_id == 36) {
    target->m_cMagicEffectStatus[17] = 0;
    target->m_iPoisonLevel = 0;
}
```

### Berserk (Type 18)

```cpp
// Application
target->m_cMagicEffectStatus[18] = 1;

// Effects:
// - Increased attack power
// - May reduce defense (trade-off)

// Duration: 40 seconds
```

### Summoning (Type 9)

```cpp
// sValue1 = NPC type to summon
// sValue2 = Number to summon

// Creates temporary NPC ally
// Duration: DEF_SUMMONTIME (5 minutes)
// Summoned creatures follow caster
// Die when duration expires or caster dies
```

### Resurrection (Type 32)

```cpp
// sValue4-6 = HP restored on revive

// Special mechanics:
// - Very high mana cost (200)
// - Long cast time (600 seconds in config, likely error)
// - Target must be dead
// - Revives with partial HP based on spell values

// Application
if (target_is_dead) {
    RevivePlayer(target_handle);
    int restore_hp = iDice(sValue4, sValue5) + sValue6;
    target->m_iHP = restore_hp;
}
```

### Cancellation (Type 29)

```cpp
// Removes all active magical effects from target

// Clears all effect status slots
for (int i = 0; i < DEF_MAXMAGICEFFECTS; i++) {
    target->m_cMagicEffectStatus[i] = 0;
}

// Removes:
// - Buffs (protection, berserk)
// - Debuffs (poison, paralyze, confusion)
// - Invisibility
// - All timed effects
```

### Inhibition/Silence (Type 31)

```cpp
// Application
target->m_bInhibition = TRUE;

// Effects:
// - Cannot cast any spells
// - Duration: 60 seconds

// Resistible via magic resistance check
```

---

## 17. Area of Effect Mechanics

### Rectangular AOE (Types 3, 5, 7, 14, 21, 25)

```cpp
// sValue2 = radius X (horizontal)
// sValue3 = radius Y (vertical)

// Area covered: (2*X+1) * (2*Y+1) tiles
// Example: radius 2,2 = 5x5 = 25 tiles

void ApplyAreaEffect(int center_x, int center_y, int radius_x, int radius_y) {
    for (int iy = center_y - radius_y; iy <= center_y + radius_y; iy++) {
        for (int ix = center_x - radius_x; ix <= center_x + radius_x; ix++) {
            // Apply effect to entity at (ix, iy)
            ProcessTileEffect(ix, iy);
        }
    }
}
```

### Linear Effects (Types 19, 26, 30)

```cpp
// Traces line from caster to target
// Applies effect at 2-tile increments
// Also hits tiles perpendicular to line

void ApplyLinearEffect(int start_x, int start_y, int end_x, int end_y) {
    int current_x, current_y;
    int error;

    for (int i = 2; i < 10; i++) {
        // Get point along line at distance i
        m_Misc.GetPoint2(start_x, start_y, end_x, end_y,
                        &current_x, &current_y, &error, i);

        // Check if we've reached target
        if (abs(current_x - end_x) <= 1 && abs(current_y - end_y) <= 1) {
            break;
        }

        // Apply effect at line position
        ProcessTileEffect(current_x, current_y);

        // Apply effect to adjacent tiles (perpendicular to line)
        ProcessTileEffect(current_x + 1, current_y);
        ProcessTileEffect(current_x - 1, current_y);
        ProcessTileEffect(current_x, current_y + 1);
        ProcessTileEffect(current_x, current_y - 1);
    }
}
```

### Dynamic Object Fields (Type 14)

Creates persistent ground effects:

```cpp
// Fire-Wall, Fire-Field, Poison-Cloud, Ice-Storm, etc.

// Creates dynamic object at target location
// Object persists for m_dwLastTime seconds
// Damages entities that enter or remain in area
// Can stack with other fields

struct dynamic_object {
    int type;           // Field type
    int owner_handle;   // Caster
    int map_index;
    int x, y;
    int damage_per_tick;
    DWORD expire_time;
};
```

---

## 18. Spell Duration & Effects

### Effect Status Array

```cpp
// Each entity has 100 effect slots
char m_cMagicEffectStatus[DEF_MAXMAGICEFFECTS];  // 100 slots

// Slot usage by magic type:
// [11] = Protection level (1-5)
// [12] = Hold/Paralyze level (1-2)
// [13] = Invisibility level (1-2)
// [16] = Confusion type
// [17] = Poison flag
// [18] = Berserk flag
// [20] = Polymorph form
// [31] = Inhibition flag
```

### Duration System

Spell durations use the delayed event system:

```cpp
// Register effect expiration
bRegisterDelayEvent(
    DEF_DELAYEVENTTYPE_MAGICRELEASE,  // Event type
    magic_type,                        // Spell type
    dwTime + (m_dwLastTime * 1000),   // Expire time (ms)
    target_handle,
    target_type,
    NULL, NULL, NULL,
    effect_strength,                   // Effect value to remove
    NULL, NULL
);

// When delay event fires:
void ProcessMagicRelease(int target_h, int magic_type, int value) {
    target->m_cMagicEffectStatus[magic_type] = 0;
    // Send notification to client
    SendNotifyMsg(NULL, target_h, DEF_NOTIFY_MAGICEFFECTOFF,
                  magic_type, value, NULL, NULL);
}
```

### Standard Durations

| Spell Type | Duration |
|------------|----------|
| Invisibility | 60 seconds |
| Hold-Person | 30 seconds |
| Paralyze | 50 seconds |
| Protection spells | 40-60 seconds |
| Defense-Shield | 60 seconds |
| Berserk | 40 seconds |
| Confusion | 10-20 seconds |
| Poison | 300-600 ticks |
| Dynamic fields | 30-60 seconds |

---

## 19. Combat Interaction

### Casting Restrictions

```cpp
// Cannot cast if:

// 1. In certain maps
if (bIsHeldenianMap && !IsAllowedSpell(spell_id)) {
    return FAIL;
}

// 2. Silenced
if (m_bInhibition == TRUE) {
    return FAIL;
}

// 3. Casting too fast (< 1 second between casts)
if (timeGetTime() - m_dwMagicFreqTime < 1000) {
    return FAIL;
}

// 4. Wrong weapon equipped
if (!IsStaffWeapon(equipped_weapon) && equipped_weapon != 0) {
    return FAIL;
}

// 5. Too hungry
if (m_iHungerStatus <= 10) {
    return FAIL;
}

// 6. Out of stamina
if (m_iSP <= 0) {
    // Some spells may fail or cost more
}

// 7. Insufficient mana
if (m_iMP < ManaCost(spell_id)) {
    return FAIL;
}
```

### PvP Restrictions

```cpp
// Civilian vs Civilian restrictions
if (caster_is_civilian && target_is_civilian) {
    // Cannot cast damaging spells on other civilians
    if (IsDamageSpell(spell_id)) {
        return FAIL;
    }

    // Healing and buffs are allowed
}

// Safe zone restrictions
if (IsInSafeZone(target)) {
    // Cannot cast offensive spells in safe zones
    if (IsOffensiveSpell(spell_id)) {
        return FAIL;
    }
}

// Same faction restrictions
if (caster_side == target_side && !IsInWarZone()) {
    // Cannot damage same faction outside war zones
    if (IsDamageSpell(spell_id)) {
        return FAIL;
    }
}
```

### Damage Modifiers Summary

| Condition | Modifier |
|-----------|----------|
| Fight Zone | +33% damage |
| Heldenian Map | +33% damage |
| Crusade Duty (Level <= 80) | +70% damage |
| Crusade Duty (Level <= 100) | +50% damage |
| Crusade Duty (Level > 100) | +33% damage |
| Hero Armor Set | +4 flat damage |
| Safe Zone Casting | +40% mana cost |

---

## 20. Constants & Limits

### System Limits

```cpp
#define DEF_MAXMAGICTYPE        100   // Maximum spell IDs (0-99)
#define DEF_MAXMAGICEFFECTS     100   // Maximum concurrent effects per entity
```

### Hit Ratio Bounds

```cpp
#define DEF_MINIMUMHITRATIO     15    // Minimum 15% hit chance
#define DEF_MAXIMUMHITRATIO     95    // Maximum 95% hit chance
```

### Timing Constants

```cpp
#define DEF_MPUPTIME            20000 // MP regeneration interval (20 seconds)
#define DEF_POISONTIME          12000 // Poison tick interval (12 seconds)
#define DEF_SUMMONTIME          300000 // Summon duration (5 minutes)
```

### Magic Circles

```cpp
// Circle 1: Spell ID 0-9   (base prob 300%)
// Circle 2: Spell ID 10-19 (base prob 250%)
// Circle 3: Spell ID 20-29 (base prob 200%)
// Circle 4: Spell ID 30-39 (base prob 150%)
// Circle 5: Spell ID 40-49 (base prob 100%)
// Circle 6: Spell ID 50-59 (base prob 80%)
// Circle 7: Spell ID 60-69 (base prob 70%)
// Circle 8: Spell ID 70-79 (base prob 60%)
// Circle 9: Spell ID 80-89 (base prob 50%)
// Circle 10: Spell ID 90-99 (base prob 40%)
```

---

## 21. Formula Summary

### Damage Formula (Complete)

```
BASE_DAMAGE = dice(sV1, sV2) + sV3

MAG_MODIFIER = (MAG <= 0) ? 1.0 : (MAG / 3.3)

SCALED_DAMAGE = BASE_DAMAGE + (BASE_DAMAGE * MAG_MODIFIER / 100)

TOTAL_DAMAGE = SCALED_DAMAGE + m_iAddMagicalDamage

// Zone bonuses (multiplicative)
if (fight_zone) TOTAL_DAMAGE += TOTAL_DAMAGE / 3
if (heldenian_map) TOTAL_DAMAGE += TOTAL_DAMAGE / 3
if (crusade_duty == 1) {
    if (level <= 80) TOTAL_DAMAGE += (TOTAL_DAMAGE * 7) / 10
    else if (level <= 100) TOTAL_DAMAGE += TOTAL_DAMAGE / 2
    else TOTAL_DAMAGE += TOTAL_DAMAGE / 3
}

// Elemental resistance
ELEMENT_REDUCTION = TOTAL_DAMAGE * (target.absorption[element] / 100)
FINAL_DAMAGE = TOTAL_DAMAGE - ELEMENT_REDUCTION

// Floor at 0
FINAL_DAMAGE = max(0, FINAL_DAMAGE)
```

### Healing Formula

```
HEAL_AMOUNT = dice(sV1, sV2) + sV3

MAX_HP = (3 * VIT) + (2 * LEVEL) + (STR / 2)

if (side_effect_maxhp_down != 0) {
    MAX_HP -= MAX_HP / side_effect_maxhp_down
}

NEW_HP = clamp(current_HP + HEAL_AMOUNT, 1, MAX_HP)
```

### Mana Cost Formula

```
BASE_COST = sValue1

// Safe zone penalty
if (safe_zone && !fight_zone) {
    BASE_COST += (BASE_COST / 2) - (BASE_COST / 10)  // +40%
}

// Mana save ratio
if (mana_save_ratio > 0) {
    SAVED = (BASE_COST * mana_save_ratio) / 100
    BASE_COST = max(1, BASE_COST - SAVED)
}

// Staff penalty
if (staff_type == 34) {
    BASE_COST += 20
}

FINAL_COST = BASE_COST
```

### Hit Ratio Formula

```
MAGIC_CIRCLE = (spell_id / 10) + 1

BASE_RATIO = (skill_mastery / 100) * circle_prob[MAGIC_CIRCLE]

// INT bonus
if (INT > 50) BASE_RATIO += (INT - 50) / 2

// Level comparison
PLAYER_MAGIC_LEVEL = player_level / 10

if (MAGIC_CIRCLE > PLAYER_MAGIC_LEVEL) {
    // Penalty for high circle
    DIFF = MAGIC_CIRCLE - PLAYER_MAGIC_LEVEL
    PENALTY = DIFF * level_penalty[MAGIC_CIRCLE]
    BASE_RATIO -= PENALTY
} else if (MAGIC_CIRCLE < PLAYER_MAGIC_LEVEL) {
    // Bonus for low circle
    DIFF = PLAYER_MAGIC_LEVEL - MAGIC_CIRCLE
    BASE_RATIO += 5 * DIFF
}

// Weather
switch (weather) {
    case 1: BASE_RATIO -= BASE_RATIO / 24  // ~4%
    case 2: BASE_RATIO -= BASE_RATIO / 12  // ~8%
    case 3: BASE_RATIO -= BASE_RATIO / 5   // 20%
}

// Special weapon
if (special_weapon_type == 10) {
    BASE_RATIO += special_weapon_value * 3
}

FINAL_RATIO = max(1, BASE_RATIO)
```

### Resistance Check Formula

```
TARGET_RESIST = skill_mastery[3] + m_iAddMR + m_iAddResistMagic

if (MAG > 50) TARGET_RESIST += (MAG - 50)

if (protection_level == 5) return RESIST_SUCCESS  // Absolute protection
if (hit_ratio < 10000 && protection_level == 2) return RESIST_SUCCESS

if (TARGET_RESIST < 1) TARGET_RESIST = 1

HIT_CHANCE = (hit_ratio / TARGET_RESIST) * 50

HIT_CHANCE = clamp(HIT_CHANCE, 15, 95)  // 15-95% range

if (HIT_CHANCE >= 100) return RESIST_FAIL

if (dice(1, 100) <= HIT_CHANCE) return RESIST_FAIL

return RESIST_SUCCESS
```

---

## Appendix: Message Types

### Client -> Server

| Message | Description |
|---------|-------------|
| `DEF_COMMONTYPE_REQ_USEMAGIC` | Request to cast spell |
| `DEF_COMMONTYPE_REQ_STUDYMAGIC` | Request to learn spell |

### Server -> Client

| Message | Description |
|---------|-------------|
| `DEF_NOTIFY_MAGICEFFECTON` | Spell effect activated |
| `DEF_NOTIFY_MAGICEFFECTOFF` | Spell effect expired |
| `DEF_NOTIFY_MAGICSTUDYSUCCESS` | Successfully learned spell |
| `DEF_NOTIFY_MAGICSTUDYFAIL` | Failed to learn spell |
| `DEF_NOTIFY_HP` | HP changed (healing) |
| `DEF_NOTIFY_MP` | MP changed |
| `DEF_NOTIFY_SP` | SP changed |
| `DEF_NOTIFY_KILLED` | Entity killed by spell |

---

## Implementation Notes

### Not Yet Implemented in Modern Code

1. **Summon System** - NPCs are not actually spawned
2. **Teleportation** - Spell exists but map transition logic incomplete
3. **Polymorph** - Shape change visuals not implemented
4. **Possession** - Complex feature, not implemented
5. **Scan** - Information reveal not implemented
6. **Spell Chaining** - No combo system
7. **Mana Shield** - No HP-to-mana conversion
8. **Spell Reflection** - No bounce mechanics

### Known Quirks

1. Resurrection has 600 second cast time in config (likely typo, should be 6 or 60)
2. Some spells have -1 gold cost (cannot be purchased, quest only)
3. Weather bonus is only +/- 1 damage (minimal effect)
4. NPCs with magic level >= 6 are immune to hold/paralyze
5. Warm effect gives 30 second complete ice immunity

### Legacy Code Patterns

- Uses Hungarian notation (`m_i`, `m_c`, `s`, `dw`)
- Heavy use of switch statements for spell types
- Damage dice use `iDice(count, sides)` function
- All timings stored in milliseconds
- Status effects stored in fixed-size arrays
