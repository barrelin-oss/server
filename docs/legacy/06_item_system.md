# Legacy Item System Documentation

**Document Version:** 1.0
**System Complexity:** High (~3,000+ lines across Game.cpp)
**Primary Files:** `Item.h`, `Item.cpp`, `BuildItem.h`, `BuildItem.cpp`, `Game.cpp`

---

## Table of Contents

1. [Overview](#1-overview)
2. [Data Structures](#2-data-structures)
3. [Item Types](#3-item-types)
4. [Item Effect Types](#4-item-effect-types)
5. [Equipment Positions](#5-equipment-positions)
6. [Item Configuration File Format](#6-item-configuration-file-format)
7. [Item Creation and Initialization](#7-item-creation-and-initialization)
8. [Item Effect Calculation](#8-item-effect-calculation)
9. [Inventory Management](#9-inventory-management)
10. [Bank Storage](#10-bank-storage)
11. [Item Drop System](#11-item-drop-system)
12. [Item Trading and Shops](#12-item-trading-and-shops)
13. [Item Crafting](#13-item-crafting)
14. [Special Item Features](#14-special-item-features)
15. [Item Logging](#15-item-logging)
16. [Constants and Limits](#16-constants-and-limits)
17. [Key Functions Reference](#17-key-functions-reference)

---

## 1. Overview

The legacy item system manages all items in Helbreath, including weapons, armor, consumables, crafting materials, and quest items. Items are stored as instances of the `CItem` class, with templates loaded from configuration files at server startup.

### Architecture

```
Item.cfg ──────────────► m_pItemConfigList[5000]  (Templates)
                                   │
                                   ▼
                         _bInitItemAttr()
                                   │
                                   ▼
                         CItem instance (Runtime)
                                   │
                    ┌──────────────┼──────────────┐
                    ▼              ▼              ▼
             m_pItemList[]  m_pItemInBankList[]  Ground/Trade
              (Inventory)      (Bank Storage)    (Dynamic Objects)
```

---

## 2. Data Structures

### CItem Class (Item.h)

The core item instance structure:

```cpp
class CItem {
public:
    CItem();
    virtual ~CItem();

    // Identity
    char  m_cName[21];              // Item name (max 20 chars)
    short m_sIDnum;                 // Item template ID number

    // Type Classification
    char  m_cItemType;              // Item type (see DEF_ITEMTYPE_*)
    char  m_cEquipPos;              // Equipment position (see DEF_EQUIPPOS_*)

    // Primary Effect
    short m_sItemEffectType;        // Effect type (see DEF_ITEMEFFECTTYPE_*)
    short m_sItemEffectValue1;      // Attack dice throw (weapons) / Effect value 1
    short m_sItemEffectValue2;      // Attack dice range (weapons) / Effect value 2
    short m_sItemEffectValue3;      // Attack bonus (weapons) / Effect value 3
    short m_sItemEffectValue4;      // Large attack dice throw / Stat bonus type
    short m_sItemEffectValue5;      // Large attack dice range / Stat bonus value
    short m_sItemEffectValue6;      // Large attack bonus / Reserved

    // Durability
    WORD  m_wMaxLifeSpan;           // Maximum durability
    WORD  m_wCurLifeSpan;           // Current durability

    // Special Effects
    short m_sSpecialEffect;         // Special effect type
    short m_sSpecialEffectValue1;   // Hit ratio bonus (Small/Medium targets)
    short m_sSpecialEffectValue2;   // Hit ratio bonus (Large targets)

    // Visual
    short m_sSprite;                // Sprite sheet ID
    short m_sSpriteFrame;           // Frame within sprite sheet
    char  m_cApprValue;             // Appearance value for character display
    char  m_cItemColor;             // Item color/dye value

    // Economics
    DWORD m_wPrice;                 // Item price (negative = not for sale)
    WORD  m_wWeight;                // Item weight
    BOOL  m_bIsForSale;             // Can be sold in shops

    // Requirements
    short m_sLevelLimit;            // Minimum level to use
    char  m_cGenderLimit;           // Gender restriction (0=none, 1=male, 2=female)

    // Skill Association
    short m_sRelatedSkill;          // Associated weapon skill index
    char  m_cCategory;              // Item category for organization
    char  m_cSpeed;                 // Attack speed modifier

    // Instance Data (per-item, not template)
    DWORD m_dwCount;                // Stack count (for stackables like arrows, gold)

    // Touch Effects (first-time pickup)
    short m_sTouchEffectType;       // Effect when first touched
    short m_sTouchEffectValue1;     // Touch effect value 1
    short m_sTouchEffectValue2;     // Touch effect value 2
    short m_sTouchEffectValue3;     // Touch effect value 3

    // Special Item Effects
    short m_sItemSpecEffectValue1;  // Special ability effect value 1
    short m_sItemSpecEffectValue2;  // Custom item quality value
    short m_sItemSpecEffectValue3;  // Special ability effect value 3

    // Attribute Flags (bitfield)
    DWORD m_dwAttribute;            // See Attribute Flags section below
};
```

### Attribute Flags (m_dwAttribute)

The `m_dwAttribute` field is a 32-bit bitfield:

```
Bit Layout: aaaa bbbb cccc dddd eeee ffff xxxx xxx1
            │    │    │    │    │    │         │
            │    │    │    │    │    │         └─ Bit 0: Custom-Made Item flag
            │    │    │    │    │    └─────────── Bits 8-11: Sub effect value
            │    │    │    │    └──────────────── Bits 12-15: Sub effect type
            │    │    │    └───────────────────── Bits 16-19: Main effect value
            │    │    └────────────────────────── Bits 20-23: Main effect type
            │    └─────────────────────────────── Bits 24-27: Item attribute value
            └──────────────────────────────────── Bits 28-31: Additional damage
```

**Main Effect Types (bits 20-23):**
- 0: None
- 1: Super attack count bonus
- 2: Poison effect
- 3: Critical hit bonus
- 4: Reserved
- 5: Damage reduction
- 6: Light effect
- 7: Strong effect (+1 damage dice range)
- 8: Fire attribute
- 9: Ancient weapon (+2 damage dice range)
- 10: Magic damage

**Sub Effect Types (bits 12-15):**
- 0: None
- 1: Physical resist bonus (+value*7)
- 2: Attack rating bonus (+value*7)
- 3: Defense rating bonus (+value*7)
- 4: HP bonus (+value*7)
- 5: SP bonus (+value*7)
- 6: MP bonus (+value*7)
- 7: Magic resist bonus (+value*7)
- 8: Damage absorption (+value*3)
- 9: Magic damage absorption (+value*3)
- 10: Critical damage bonus
- 11: Experience bonus (+value*10%)
- 12: Gold bonus (+value*10%)

---

## 3. Item Types

### DEF_ITEMTYPE_* Constants

```cpp
#define DEF_ITEMTYPE_NOTUSED                -1  // Deprecated item (replaced on load)
#define DEF_ITEMTYPE_NONE                    0  // No type
#define DEF_ITEMTYPE_EQUIP                   1  // Equipment (weapons, armor)
#define DEF_ITEMTYPE_APPLY                   2  // Applied effect item
#define DEF_ITEMTYPE_USE_DEPLETE             3  // Use and deplete (potions)
#define DEF_ITEMTYPE_INSTALL                 4  // Installation item
#define DEF_ITEMTYPE_CONSUME                 5  // Consumable (stackable)
#define DEF_ITEMTYPE_ARROW                   6  // Arrow/ammunition
#define DEF_ITEMTYPE_EAT                     7  // Food item
#define DEF_ITEMTYPE_USE_SKILL               8  // Skill activation item
#define DEF_ITEMTYPE_USE_PERM                9  // Permanent use item
#define DEF_ITEMTYPE_USE_SKILL_ENABLEDIALOGBOX 10  // Skill with dialog
#define DEF_ITEMTYPE_USE_DEPLETE_DEST        11  // Use on target and deplete
#define DEF_ITEMTYPE_MATERIAL                12  // Crafting material
```

### Item Type Behaviors

| Type | Stackable | Equippable | Consumed on Use | Notes |
|------|-----------|------------|-----------------|-------|
| EQUIP (1) | No | Yes | No | Weapons, armor, accessories |
| APPLY (2) | No | No | Sometimes | Applied effects |
| USE_DEPLETE (3) | Yes | No | Yes | Potions, scrolls |
| CONSUME (5) | Yes | No | Yes | General consumables |
| ARROW (6) | Yes | No | Yes | Ammunition for bows |
| EAT (7) | Yes | No | Yes | Food for hunger |
| MATERIAL (12) | Yes | No | No | Crafting materials |

---

## 4. Item Effect Types

### DEF_ITEMEFFECTTYPE_* Constants

```cpp
// Combat Effects
#define DEF_ITEMEFFECTTYPE_NONE              0   // No effect
#define DEF_ITEMEFFECTTYPE_ATTACK            1   // Attack: value1 D value2 + value3
#define DEF_ITEMEFFECTTYPE_DEFENSE           2   // Defense value
#define DEF_ITEMEFFECTTYPE_ATTACK_ARROW      3   // Bow attack (uses arrow)

// Stat Recovery
#define DEF_ITEMEFFECTTYPE_HP                4   // HP recovery
#define DEF_ITEMEFFECTTYPE_MP                5   // MP recovery
#define DEF_ITEMEFFECTTYPE_SP                6   // SP recovery
#define DEF_ITEMEFFECTTYPE_HPSTOCK           7   // HP recovery (delayed, no visual)

// Utility
#define DEF_ITEMEFFECTTYPE_GET               8   // Picking up items
#define DEF_ITEMEFFECTTYPE_STUDYSKILL        9   // Learn skill
#define DEF_ITEMEFFECTTYPE_SHOWLOCATION      10  // Show location on map
#define DEF_ITEMEFFECTTYPE_MAGIC             11  // Cast magic effect
#define DEF_ITEMEFFECTTYPE_CHANGEATTR        12  // Change player attributes

// Enhanced Combat
#define DEF_ITEMEFFECTTYPE_ATTACK_MANASAVE   13  // Attack + mana save
#define DEF_ITEMEFFECTTYPE_ADDEFFECT         14  // Additional effect
#define DEF_ITEMEFFECTTYPE_MAGICDAMAGESAVE   15  // Magic damage absorption
#define DEF_ITEMEFFECTTYPE_ATTACK_MAXHPDOWN  19  // Attack + max HP reduction
#define DEF_ITEMEFFECTTYPE_ATTACK_DEFENSE    20  // Attack + defense bonus

// Special Items
#define DEF_ITEMEFFECTTYPE_OCCUPYFLAG        16  // Crusade occupation flag
#define DEF_ITEMEFFECTTYPE_DYE               17  // Dye item
#define DEF_ITEMEFFECTTYPE_STUDYMAGIC        18  // Learn magic
#define DEF_ITEMEFFECTTYPE_MATERIAL_ATTR     21  // Crafting material attribute
#define DEF_ITEMEFFECTTYPE_FIRMSTAMINAR      22  // Stamina protection
#define DEF_ITEMEFFECTTYPE_LOTTERY           23  // Lottery ticket
#define DEF_ITEMEFFECTTYPE_ATTACK_SPECABLTY  24  // Attack with special ability
#define DEF_ITEMEFFECTTYPE_DEFENSE_SPECABLTY 25  // Defense with special ability
#define DEF_ITEMEFFECTTYPE_ALTERITEMDROP     26  // Alter item drop (bag item)
#define DEF_ITEMEFFECTTYPE_CONSTRUCTIONKIT   27  // Construction kit
#define DEF_ITEMEFFECTTYPE_WARM              28  // Unfreeze potion
#define DEF_ITEMEFFECTTYPE_FARMING           30  // Farming seed
#define DEF_ITEMEFFECTTYPE_SLATES            31  // Ancient tablets
#define DEF_ITEMEFFECTTYPE_ARMORDYE          32  // Armor dye
#define DEF_ITEMEFFECTTYPE_CRITKOMM          33  // Critical candy
#define DEF_ITEMEFFECTTYPE_WEAPONDYE         34  // Weapon dye
```

### Effect Value Interpretation

For **ATTACK** type items (weapons):
- `m_sItemEffectValue1`: Attack dice throw count (Small/Medium targets)
- `m_sItemEffectValue2`: Attack dice range/sides (Small/Medium targets)
- `m_sItemEffectValue3`: Attack bonus (Small/Medium targets)
- `m_sItemEffectValue4`: Attack dice throw count (Large targets)
- `m_sItemEffectValue5`: Attack dice range/sides (Large targets)
- `m_sItemEffectValue6`: Attack bonus (Large targets)

**Damage Formula:** `iDice(DiceThrow, DiceRange) + AttackBonus`

For **DEFENSE** type items (armor):
- `m_sItemEffectValue1`: Defense value

For **ADDEFFECT** type items:
- `m_sItemEffectValue1`: Effect sub-type (1=magic resist, 2=mana save, etc.)
- `m_sItemEffectValue2`: Effect magnitude

---

## 5. Equipment Positions

### DEF_EQUIPPOS_* Constants

```cpp
#define DEF_MAXITEMEQUIPPOS     15      // Total equipment slots

#define DEF_EQUIPPOS_NONE        0      // Not equippable
#define DEF_EQUIPPOS_HEAD        1      // Helmet/hat
#define DEF_EQUIPPOS_BODY        2      // Armor/chest
#define DEF_EQUIPPOS_ARMS        3      // Gloves/bracers
#define DEF_EQUIPPOS_PANTS       4      // Leg armor
#define DEF_EQUIPPOS_LEGGINGS    5      // Boots
#define DEF_EQUIPPOS_NECK        6      // Necklace/amulet
#define DEF_EQUIPPOS_LHAND       7      // Left hand (shield)
#define DEF_EQUIPPOS_RHAND       8      // Right hand (one-hand weapon)
#define DEF_EQUIPPOS_TWOHAND     9      // Two-hand weapon
#define DEF_EQUIPPOS_RFINGER    10      // Right ring
#define DEF_EQUIPPOS_LFINGER    11      // Left ring
#define DEF_EQUIPPOS_BACK       12      // Cape/cloak
#define DEF_EQUIPPOS_RELEASEALL 13      // Special: unequip all
```

### Equipment Slot Conflicts

- `DEF_EQUIPPOS_RHAND` and `DEF_EQUIPPOS_TWOHAND` are mutually exclusive
- Two-handed weapons prevent shield usage (`DEF_EQUIPPOS_LHAND`)
- Only one item per slot (no stacking equipment)

### Player Equipment Tracking

```cpp
// In CClient class:
class CItem * m_pItemList[DEF_MAXITEMS];              // Inventory (50 slots)
BOOL  m_bIsItemEquipped[DEF_MAXITEMS];                // Equipped status per slot
short m_sItemEquipmentStatus[DEF_MAXITEMEQUIPPOS];    // Item index per equip pos
```

---

## 6. Item Configuration File Format

### Item.cfg Structure

Items are defined in `Item.cfg` with space/tab separated values:

```
Item = <ID> <Name> <Type> <EquipPos> <EffectType> <EV1> <EV2> <EV3> <EV4> <EV5> <EV6> <MaxLifeSpan> <SpecialEffect> <Sprite> <SpriteFrame> <Price> <Weight> <ApprValue> <Speed> <LevelLimit> <GenderLimit> <SpecialEffectValue1> <SpecialEffectValue2> <RelatedSkill> <Category> <ItemColor>
```

### Field Order (26 fields total)

| # | Field | Type | Description |
|---|-------|------|-------------|
| 1 | ID | short | Unique item template ID |
| 2 | Name | string | Item name (no spaces) |
| 3 | Type | char | DEF_ITEMTYPE_* value |
| 4 | EquipPos | char | DEF_EQUIPPOS_* value |
| 5 | EffectType | short | DEF_ITEMEFFECTTYPE_* value |
| 6 | EffectValue1 | short | Primary effect value 1 |
| 7 | EffectValue2 | short | Primary effect value 2 |
| 8 | EffectValue3 | short | Primary effect value 3 |
| 9 | EffectValue4 | short | Primary effect value 4 |
| 10 | EffectValue5 | short | Primary effect value 5 |
| 11 | EffectValue6 | short | Primary effect value 6 |
| 12 | MaxLifeSpan | WORD | Maximum durability |
| 13 | SpecialEffect | short | Special effect type |
| 14 | Sprite | short | Sprite ID |
| 15 | SpriteFrame | short | Sprite frame |
| 16 | Price | int | Base price (negative = not for sale) |
| 17 | Weight | WORD | Item weight |
| 18 | ApprValue | char | Appearance value |
| 19 | Speed | char | Attack speed |
| 20 | LevelLimit | short | Minimum level |
| 21 | GenderLimit | char | Gender restriction |
| 22 | SpecialEffectValue1 | short | Hit ratio bonus (S/M) |
| 23 | SpecialEffectValue2 | short | Hit ratio bonus (L) |
| 24 | RelatedSkill | short | Associated skill index |
| 25 | Category | char | Item category |
| 26 | ItemColor | char | Default color |

### Example Items

```
; Dagger: 1d6 damage, equip right hand
Item = 1 Dagger 1 8 1 1 6 0 1 6 0 300 0 1 0 25 6 1 0 0 0 0 0 7 1 0

; Excalibur: 3d11+5 damage, legendary sword
Item = 20 Excaliber 1 8 1 3 11 5 3 13 5 8000 0 1 19 -31000 60 5 0 0 0 -1 0 8 1 0

; Red Potion: HP recovery
Item = 91 RedPotion 3 0 4 30 0 0 0 0 0 0 0 3 0 15 1 0 0 0 0 0 0 0 11 0

; Gold: Currency
Item = 90 Gold 5 0 8 0 0 0 0 0 0 0 0 2 0 1 0 0 0 0 0 0 0 0 11 0
```

---

## 7. Item Creation and Initialization

### Template Loading

```cpp
// CGame::_bDecodeItemConfigFileContents()
// Parses Item.cfg and populates m_pItemConfigList[]

class CItem * m_pItemConfigList[DEF_MAXITEMTYPES];  // 5000 templates
```

### Item Instance Creation

```cpp
// CGame::_bInitItemAttr() - Initialize item from template name
BOOL CGame::_bInitItemAttr(class CItem * pItem, char * pItemName)
{
    // 1. Search m_pItemConfigList for matching name
    // 2. Copy all template values to instance
    // 3. Set instance-specific defaults (count=1, etc.)
    // 4. Return TRUE on success
}

// CGame::_bInitItemAttr() - Initialize item from template ID
BOOL CGame::_bInitItemAttr(class CItem * pItem, int iItemID)
{
    // Direct lookup by ID, then copy template
}
```

### Item Copy

```cpp
BOOL CGame::bCopyItemContents(CItem * pCopy, CItem * pOriginal)
{
    // Deep copy all item fields from original to copy
    // Used for trading, dropping, banking
}
```

---

## 8. Item Effect Calculation

### CalcTotalItemEffect Function

This is the core function that calculates all stat bonuses from equipped items:

```cpp
void CGame::CalcTotalItemEffect(int iClientH, int iEquipItemID, BOOL bNotify)
```

**Reset Phase:**
1. Clear all attack dice values
2. Reset hit ratio to 0
3. Reset defense ratio to `DEX * 2`
4. Clear all damage absorption values
5. Clear mana save, magic resist, physical/magical damage bonuses
6. Clear elemental absorption (Air, Earth, Fire, Water)
7. Clear custom item bonuses
8. Clear special weapon effects
9. Clear all stat bonuses (HP, SP, MP, AR, PR, DR, MR, etc.)
10. Clear special ability settings

**Scan Non-Equipped Items:**
- Check for `DEF_ITEMEFFECTTYPE_ALTERITEMDROP` items (bag effects)

**Scan Equipped Items:**

For each equipped item, apply effects based on `m_sItemEffectType`:

**ATTACK Types (1, 13, 19, 20, 24):**
```cpp
// Set attack dice values
m_cAttackDiceThrow_SM = m_sItemEffectValue1;  // Small/Medium
m_cAttackDiceRange_SM = m_sItemEffectValue2;
m_cAttackBonus_SM     = m_sItemEffectValue3;
m_cAttackDiceThrow_L  = m_sItemEffectValue4;  // Large
m_cAttackDiceRange_L  = m_sItemEffectValue5;
m_cAttackBonus_L      = m_sItemEffectValue6;

// Add skill-based hit ratio
m_iHitRatio += m_cSkillMastery[m_sRelatedSkill];

// Set weapon skill
m_sUsingWeaponSkill = m_sRelatedSkill;

// Process m_dwAttribute for additional effects
// - Bits 28-31: Additional physical/magical damage
// - Bits 20-23: Main effect type (super attack, poison, etc.)
// - Bits 12-15: Sub effect type (resistances, bonuses)
```

**DEFENSE Type (2):**
```cpp
// Add to armor absorption for equipment position
m_iDamageAbsorption_Armor[cEquipPos] += m_sItemEffectValue1;

// Defense bonus for shields
if (cEquipPos == DEF_EQUIPPOS_LHAND) {
    m_iDamageAbsorption_Shield += m_sSpecialEffect;
}
```

**ADDEFFECT Type (14):**
```cpp
switch (m_sItemEffectValue1) {
    case 1:  // Magic resist
        m_iAddResistMagic += m_sItemEffectValue2;
        break;
    case 2:  // Mana save (max 80%)
        m_iManaSaveRatio += m_sItemEffectValue2;
        break;
    case 3:  // Physical damage bonus
        m_iAddPhysicalDamage += m_sItemEffectValue2;
        break;
    case 4:  // Magical damage bonus
        m_iAddMagicalDamage += m_sItemEffectValue2;
        break;
    case 5:  // Lucky effect
        m_bIsLuckyEffect = TRUE;
        break;
    // ... more cases for elemental absorption
}
```

**Armor Stat Bonuses (EffectValue4/5):**
For body, arms, and leggings armor:
```cpp
// EffectValue4 determines stat type
switch (m_sItemEffectValue4) {
    case 10: iAddStr += m_sItemEffectValue5; break;  // Strength
    case 11: iAddDex += m_sItemEffectValue5; break;  // Dexterity
    case 12: iAddVit += m_sItemEffectValue5; break;  // Vitality
    case 13: iAddInt += m_sItemEffectValue5; break;  // Intelligence
    case 14: iAddMag += m_sItemEffectValue5; break;  // Magic
    case 15: iAddChr += m_sItemEffectValue5; break;  // Charisma
}
```

---

## 9. Inventory Management

### Inventory Structure

```cpp
// In CClient class:
class CItem * m_pItemList[DEF_MAXITEMS];      // 50 inventory slots
POINT m_ItemPosList[DEF_MAXITEMS];            // Visual position in UI
BOOL  m_bIsItemEquipped[DEF_MAXITEMS];        // Equipment status
char  m_cArrowIndex;                          // Current arrow slot (-1 = none)
```

### Adding Items

```cpp
BOOL CGame::bAddItem(int iClientH, CItem * pItem, char cMode)
```

**Modes:**
- `cMode = 0`: Normal add (check weight, stack if possible)
- `cMode = 1`: Force add (ignore weight limit)

**Process:**
1. Check if stackable (`DEF_ITEMTYPE_CONSUME` or `DEF_ITEMTYPE_ARROW`)
2. If stackable, find existing stack and add to it
3. If not stackable or no existing stack, find empty slot
4. Check weight limit (`iCalcTotalWeight()` vs `_iCalcMaxLoad()`)
5. Add item to slot or reject if full/overweight

### Dropping Items

```cpp
void CGame::DropItemHandler(int iClientH, short sItemIndex, int iAmount, char * pItemName, BOOL bByPlayer)
```

**Process:**
1. Validate item exists in inventory
2. For stackables, split if `iAmount < m_dwCount`
3. Create dynamic object on ground
4. Remove from inventory
5. Log item drop

### Getting Items from Ground

```cpp
int CGame::iClientMotion_GetItem_Handler(int iClientH, short sX, short sY, char cDir)
```

**Process:**
1. Check tile for dynamic object
2. Validate ownership (dropped items have delay)
3. Call `_bAddClientItemList()` to add to inventory
4. Remove dynamic object from map

### Weight Calculation

```cpp
int CGame::iCalcTotalWeight(int iClientH)
{
    int iWeight = 0;
    for (int i = 0; i < DEF_MAXITEMS; i++) {
        if (m_pItemList[i] != NULL) {
            iWeight += iGetItemWeight(m_pItemList[i], m_pItemList[i]->m_dwCount);
        }
    }
    return iWeight;
}

int CGame::_iCalcMaxLoad(int iClientH)
{
    // Max weight = (STR * 500) + 1000
    return (m_pClientList[iClientH]->m_iStr * 500) + 1000;
}
```

---

## 10. Bank Storage

### Bank Structure

```cpp
// In CClient class:
class CItem * m_pItemInBankList[DEF_MAXBANKITEMS];  // 200 bank slots
```

### Deposit to Bank

```cpp
BOOL CGame::bPlayerItemToBank(int iClientH, short sItemIndex)
BOOL CGame::bSetItemToBankItem(int iClientH, short sItemIndex)
BOOL CGame::bSetItemToBankItem(int iClientH, class CItem * pItem)
```

**Process:**
1. Verify item is not equipped
2. Find empty bank slot
3. Copy item to bank
4. Remove from inventory
5. Log deposit

### Retrieve from Bank

```cpp
BOOL CGame::bBankItemToPlayer(int iClientH, short sItemIndex)
void CGame::RequestRetrieveItemHandler(int iClientH, char * pData)
```

**Process:**
1. Find item in bank
2. Check inventory space and weight
3. Copy to inventory
4. Remove from bank
5. Log retrieval

---

## 11. Item Drop System

### NPC Death Drops

```cpp
void CGame::NpcDeadItemGenerator(int iNpcH, short sAttackerH, char cAttackerType)
```

**Drop Calculation:**

1. **Gold Drop Check** (Primary):
   - Roll `iDice(1, 10000)` vs `m_iPrimaryDropRate` (default 6500)
   - 60% of successful drops are gold
   - Gold amount: `iDice(1, m_iGoldDiceMax - m_iGoldDiceMin) + m_iGoldDiceMin`
   - Gold bonus from equipment applied

2. **Item Drop Check** (Secondary):
   - Roll `iDice(1, 10000)` vs `m_iSecondaryDropRate` (modified by player rating)
   - 90% chance: Consumable drops (potions, candy)
   - 10% chance: Valuable drops based on NPC level

**NPC Generation Levels:**

| Level | NPC Types |
|-------|-----------|
| 1 | Slime, Giant-Ant, Amphis, Rabbit, Cat |
| 2 | Skeleton, Orc, Scorpion, Zombie |
| 3 | Stone-Golem, Clay-Golem |
| 4 | Hellbound, Rudolph |
| 5 | Cyclops, Troll, Beholder, DireBoar |
| 6 | Ogre, WereWolf, Stalker, Dark-Elf, Ice-Golem, Minotaurus |
| 7 | Balrogs, Centaurus, Liche, Frost, Nizie |
| 8 | Demon, Unicorn, Hellclaw, Tigerworm, Gargoyle |
| 9 | MountainGiant |
| 10 | MasterMage-Orc, Ettin, Lizards |

**Consumable Drop Table:**

| Roll Range | Drop |
|------------|------|
| 1-3000 | Green Potion |
| 3001-4000 | Red Potion |
| 4001-5500 | Blue Potion |
| 5501-7000 | Big Green Potion |
| 7001-8500 | Big Red Potion |
| 8501-9200 | Big Blue Potion |
| 9201-9800 | Power Green or Candy |
| 9801-10000 | Super Power Green, Zemstones, Ancient Tablets, Energy Balls |
| 10001-12000 | Seasonal Candy (December only) |

### Multiple Item Drops

```cpp
BOOL CGame::bGetMultipleItemNamesWhenDeleteNpc(
    short sNpcType,
    int iProbability,
    int iMin, int iMax,
    short sBaseX, short sBaseY,
    int iItemSpreadType, int iSpreadRange,
    int *iItemIDs, POINT *BasePos, int *iNumItem)
```

Some NPCs drop multiple items at once using this function.

---

## 12. Item Trading and Shops

### Shop Purchase

```cpp
void CGame::RequestPurchaseItemHandler(int iClientH, char * pItemName, int iNum)
```

**Process:**
1. Check if in shop NPC range
2. Validate item is for sale (`m_bIsForSale == TRUE`)
3. Calculate total cost
4. Check player gold
5. Create item instance
6. Add to inventory
7. Deduct gold

### Shop Sale

```cpp
void CGame::ReqSellItemHandler(int iClientH, char cItemID, char cSellToWhom, int iNum, char * pItemName)
void CGame::ReqSellItemConfirmHandler(int iClientH, char cItemID, int iNum, char * pString)
```

**Sell Price:** `m_wPrice / 2` (50% of buy price)

### Item Repair

```cpp
void CGame::ReqRepairItemHandler(int iClientH, char cItemID, char cRepairWhom, char * pString)
void CGame::ReqRepairItemCofirmHandler(int iClientH, char cItemID, char * pString)
```

**Repair Cost:** Based on durability lost and item value

### Player Trading

```cpp
void CGame::ExchangeItemHandler(int iClientH, short sItemIndex, int iAmount, short dX, short dY, WORD wObjectID, char *pItemName)
void CGame::SetExchangeItem(int iClientH, int iItemIndex, int iAmount)
void CGame::ConfirmExchangeItem(int iClientH)
void CGame::CancelExchangeItem(int iClientH)
```

**Exchange Slots:**
```cpp
// In CClient class:
BOOL  m_bIsExchangeMode;
int   m_iExchangeH;                    // Trading partner
char  m_cExchangeItemName[4][21];      // Up to 4 items
char  m_cExchangeItemIndex[4];
int   m_iExchangeItemAmount[4];
BOOL  m_bIsExchangeConfirm;
int   iExchangeCount;
```

---

## 13. Item Crafting

### CBuildItem Class (BuildItem.h)

```cpp
class CBuildItem {
public:
    char  m_cName[21];              // Crafted item name
    short m_sItemID;                // Result item ID
    int   m_iSkillLimit;            // Required skill level
    int   m_iMaterialItemID[6];     // Material item IDs
    int   m_iMaterialItemCount[6];  // Required counts
    int   m_iMaterialItemValue[6];  // Material quality values
    int   m_iIndex[6];              // Material slot indices
    int   m_iMaxValue;              // Maximum quality achievable
    int   m_iAverageValue;          // Average quality
    int   m_iMaxSkill;              // Max skill increase
    WORD  m_wAttribute;             // Result item attribute flags
};
```

### Crafting Process

```cpp
void CGame::BuildItemHandler(int iClientH, char * pData)
```

**Process:**
1. Parse build request
2. Verify all materials present in inventory
3. Check skill requirement
4. Calculate success chance based on skill
5. On success: Create crafted item, remove materials
6. On failure: Some materials may be lost

---

## 14. Special Item Features

### Touch Effects

First-time pickup effects using `m_sTouchEffectType`:

```cpp
#define DEF_ITET_UNIQUE_OWNER    1  // Bind to first owner
#define DEF_ITET_ID              2  // Standard item ID
#define DEF_ITET_DATE            3  // Date-based expiration
```

### Unique/Bound Items

```cpp
void CGame::CheckUniqueItemEquipment(int iClientH)
```

Items with `DEF_ITET_UNIQUE_OWNER` become bound to the first player who picks them up.

### Duplicate Item Detection

```cpp
BOOL CGame::_bCheckDupItemID(CItem *pItem)
```

Checks for duplicate item IDs to prevent item duplication exploits.

### Rare Item Value Adjustment

```cpp
void CGame::_AdjustRareItemValue(CItem *pItem)
```

Adjusts rare item values based on random rolls.

### Item Upgrade

```cpp
BOOL CGame::bCheckIsItemUpgradeSuccess(int iClientH, int iItemIndex, int iSomH, BOOL bBonus)
void CGame::RequestItemUpgradeHandler(int iClientH, int iItemIndex)
```

---

## 15. Item Logging

### Log Types

```cpp
#define DEF_ITEMLOG_GIVE          1   // Item given to player
#define DEF_ITEMLOG_DROP          2   // Item dropped
#define DEF_ITEMLOG_GET           3   // Item picked up
#define DEF_ITEMLOG_DEPLETE       4   // Item consumed/depleted
#define DEF_ITEMLOG_NEWGENDROP    5   // New item generated (NPC drop)
#define DEF_ITEMLOG_DUPITEMID     6   // Duplicate item detected
#define DEF_ITEMLOG_BUY           7   // Item purchased
#define DEF_ITEMLOG_SELL          8   // Item sold
#define DEF_ITEMLOG_RETRIEVE      9   // Retrieved from bank
#define DEF_ITEMLOG_DEPOSIT      10   // Deposited to bank
#define DEF_ITEMLOG_EXCHANGE     11   // Traded with player
#define DEF_ITEMLOG_SKILLLEARN   12   // Skill learned from item
#define DEF_ITEMLOG_MAKE         13   // Crafted
#define DEF_ITEMLOG_SUMMONMONSTER 14  // Summoning item used
#define DEF_ITEMLOG_POISONED     15   // Poisoned item
#define DEF_ITEMLOG_MAGICLEARN   16   // Magic learned from item
#define DEF_ITEMLOG_REPAIR       17   // Item repaired
#define DEF_ITEMLOG_USE          32   // General item use
```

### Logging Functions

```cpp
BOOL CGame::_bItemLog(int iAction, int iClientH, char * cName, class CItem * pItem)
BOOL CGame::_bItemLog(int iAction, int iGiveH, int iRecvH, class CItem * pItem, BOOL bForceItemLog)
BOOL CGame::_bCheckGoodItem(class CItem * pItem)  // Check if item should be logged
```

---

## 16. Constants and Limits

### Server Limits

```cpp
#define DEF_MAXITEMTYPES        5000   // Maximum item templates
#define DEF_MAXITEMS              50   // Player inventory slots
#define DEF_MAXBANKITEMS         200   // Bank storage slots (v2.0: was 120)
#define DEF_MAXITEMEQUIPPOS       15   // Equipment positions
#define DEF_MAXNPCITEMS         1000   // NPC shop item limit
#define DEF_MAXDUPITEMID         100   // Duplicate item tracking
#define DEF_MAXBUILDITEMS        300   // Crafting recipes
```

### Default Values

- **Maximum Stack (Gold):** 99,999,999
- **Repair Cost Modifier:** 50% of item value
- **Sell Price:** 50% of buy price
- **Primary Drop Rate:** 6500/10000 (65% base drop chance)
- **Secondary Drop Rate:** 9000/10000 (90% item vs gold)

---

## 17. Key Functions Reference

### Item Lifecycle

| Function | Purpose |
|----------|---------|
| `_bDecodeItemConfigFileContents()` | Load Item.cfg |
| `_bInitItemAttr(CItem*, char*)` | Initialize by name |
| `_bInitItemAttr(CItem*, int)` | Initialize by ID |
| `bCopyItemContents()` | Deep copy item |
| `iGetItemWeight()` | Calculate weight |

### Inventory Operations

| Function | Purpose |
|----------|---------|
| `bAddItem()` | Add to inventory |
| `_bAddClientItemList()` | Add picked up item |
| `DropItemHandler()` | Drop item |
| `iClientMotion_GetItem_Handler()` | Pick up item |
| `ReleaseItemHandler()` | Remove from inventory |
| `_iGetItemSpaceLeft()` | Check available slots |

### Equipment

| Function | Purpose |
|----------|---------|
| `bEquipItemHandler()` | Equip item |
| `CalcTotalItemEffect()` | Recalculate all stats |
| `CheckUniqueItemEquipment()` | Handle bound items |

### Trading

| Function | Purpose |
|----------|---------|
| `RequestPurchaseItemHandler()` | Buy from shop |
| `ReqSellItemHandler()` | Sell to shop |
| `ReqRepairItemHandler()` | Repair item |
| `ExchangeItemHandler()` | Player trade |
| `SetExchangeItem()` | Set trade offer |
| `ConfirmExchangeItem()` | Confirm trade |

### Banking

| Function | Purpose |
|----------|---------|
| `bPlayerItemToBank()` | Deposit |
| `bBankItemToPlayer()` | Withdraw |
| `bSetItemToBankItem()` | Bank storage |
| `RequestRetrieveItemHandler()` | Retrieve request |

### Drops & Creation

| Function | Purpose |
|----------|---------|
| `NpcDeadItemGenerator()` | Generate NPC drops |
| `bGetItemNameWhenDeleteNpc()` | Get drop item |
| `bGetMultipleItemNamesWhenDeleteNpc()` | Multiple drops |
| `AdminOrder_CreateItem()` | GM create item |

### Crafting

| Function | Purpose |
|----------|---------|
| `_bDecodeBuildItemConfigFileContents()` | Load recipes |
| `BuildItemHandler()` | Process crafting |

### Validation

| Function | Purpose |
|----------|---------|
| `_bCheckDupItemID()` | Detect duplicates |
| `_AdjustRareItemValue()` | Rare item handling |
| `_bCheckItemReceiveCondition()` | Validate receipt |
| `_bCheckGoodItem()` | Log-worthy check |
| `bCheckAndConvertPlusWeaponItem()` | Weapon upgrade |

---

## Appendix A: Sample Item IDs

| ID | Name | Type | Notes |
|----|------|------|-------|
| 1 | Dagger | Weapon | Basic starting weapon |
| 20 | Excaliber | Weapon | Legendary sword |
| 79 | WoodShield | Shield | Basic shield |
| 90 | Gold | Currency | Stackable currency |
| 91 | RedPotion | Consumable | HP recovery |
| 92 | BigRedPotion | Consumable | Large HP recovery |
| 93 | BluePotion | Consumable | MP recovery |
| 94 | BigBluePotion | Consumable | Large MP recovery |
| 95 | GreenPotion | Consumable | SP recovery |
| 96 | BigGreenPotion | Consumable | Large SP recovery |
| 390 | PowerGreenPotion | Consumable | Super SP recovery |
| 650 | ZemstoneOfSacrifice | Material | Crafting material |
| 656 | XelimaStone | Material | Special stone |
| 657 | MerienStone | Material | Special stone |
| 780 | RedCandy | Consumable | Holiday item |
| 868-871 | AncientTablet | Material | Slate fragments |

---

## Appendix B: Related Files

- `Item.h` - CItem class definition
- `Item.cpp` - CItem constructor/destructor
- `BuildItem.h` - CBuildItem class definition
- `BuildItem.cpp` - CBuildItem constructor/destructor
- `Game.cpp` - All item logic functions (see line numbers in overview)
- `Client.h` - Player inventory structures
- `NpcItem.cpp` - NPC shop item handling
- `TempNpcItem.h/cpp` - Temporary NPC item structures
