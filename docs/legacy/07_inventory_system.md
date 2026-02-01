# Legacy Inventory System Documentation

**System:** Inventory Management
**Complexity:** Medium (~1,000 lines across Game.cpp + Client.h)
**Primary Files:** `Client.h`, `Item.h`, inventory functions in `Game.cpp`

---

## Table of Contents

1. [Overview](#1-overview)
2. [Data Structures](#2-data-structures)
3. [Constants & Limits](#3-constants--limits)
4. [Equipment Positions](#4-equipment-positions)
5. [Item Types](#5-item-types)
6. [Item Effect Types](#6-item-effect-types)
7. [Core Functions](#7-core-functions)
8. [Weight System](#8-weight-system)
9. [Equipment Validation](#9-equipment-validation)
10. [Bank System](#10-bank-system)
11. [Trading/Exchange System](#11-tradingexchange-system)
12. [Network Protocol](#12-network-protocol)
13. [Item Logging](#13-item-logging)
14. [Special Mechanics](#14-special-mechanics)
15. [Integration Points](#15-integration-points)
16. [Message Flow Diagrams](#16-message-flow-diagrams)

---

## 1. Overview

The legacy inventory system manages:
- **Player Inventory:** 50 slots for carrying items
- **Bank Storage:** 200 slots for long-term storage
- **Equipment:** 15 equipment positions on the character
- **Weight/Burden:** Carrying capacity based on STR and level
- **Item Stacking:** Consumables and materials can stack
- **Trading:** Player-to-player item exchange

All inventory operations are validated server-side. The client sends requests, and the server confirms or denies based on rules (weight limits, slot availability, item requirements).

---

## 2. Data Structures

### 2.1 CClient Inventory Fields

Located in `Client.h`:

```cpp
class CClient {
    // Primary inventory storage
    class CItem * m_pItemList[DEF_MAXITEMS];           // 50-slot inventory array
    POINT m_ItemPosList[DEF_MAXITEMS];                  // Grid positions for UI

    // Bank storage
    class CItem * m_pItemInBankList[DEF_MAXBANKITEMS]; // 200-slot bank array

    // Equipment tracking
    BOOL  m_bIsItemEquipped[DEF_MAXITEMS];              // Is item[i] currently equipped?
    short m_sItemEquipmentStatus[DEF_MAXITEMEQUIPPOS];  // slot[pos] = inventory index
    char  m_cArrowIndex;                                // Active arrow slot (-1 = none)

    // Weight tracking
    int   m_iCurWeightLoad;                             // Current total weight carried

    // Exchange/trading state
    BOOL  m_bIsExchangeMode;                            // In trade mode?
    int   m_iExchangeH;                                 // Trade partner handle
    char  m_cExchangeName[11];                          // Trade partner name
    char  m_cExchangeItemName[4][21];                   // Items offered (names)
    char  m_cExchangeItemIndex[4];                      // Items offered (slots)
    int   m_iExchangeItemAmount[4];                     // Items offered (quantities)
    BOOL  m_bIsExchangeConfirm;                         // Trade confirmed?
    int   iExchangeCount;                               // Number of items in trade
};
```

### 2.2 CItem Class Structure

Located in `Item.h`:

```cpp
class CItem {
public:
    // Identity
    char  m_cName[21];              // Item name (20 chars max)
    short m_sIDnum;                 // Template ID (references item database)

    // Classification
    char  m_cItemType;              // Type category (equip, consume, etc.)
    char  m_cEquipPos;              // Which slot this can equip to
    char  m_cCategory;              // Sub-category

    // Effects (up to 6 effect parameters)
    short m_sItemEffectType;        // Primary effect type
    short m_sItemEffectValue1;      // Effect parameter 1
    short m_sItemEffectValue2;      // Effect parameter 2
    short m_sItemEffectValue3;      // Effect parameter 3
    short m_sItemEffectValue4;      // Effect parameter 4
    short m_sItemEffectValue5;      // Effect parameter 5
    short m_sItemEffectValue6;      // Effect parameter 6

    // Durability
    WORD  m_wMaxLifeSpan;           // Maximum durability
    WORD  m_wCurLifeSpan;           // Current durability

    // Special effects
    short m_sSpecialEffect;         // Special effect flag
    short m_sSpecialEffectValue1;   // Special effect param 1
    short m_sSpecialEffectValue2;   // Special effect param 2

    // Visual
    short m_sSprite;                // Sprite ID for rendering
    short m_sSpriteFrame;           // Sprite animation frame
    char  m_cApprValue;             // Appearance/quality value
    char  m_cItemColor;             // Dye/color value

    // Stats
    char  m_cSpeed;                 // Speed modifier
    DWORD m_wPrice;                 // Sale price (to NPC)
    WORD  m_wWeight;                // Weight per unit

    // Requirements
    short m_sLevelLimit;            // Minimum level to equip
    char  m_cGenderLimit;           // Gender restriction (0=any, 1=M, 2=F)
    short m_sRelatedSkill;          // Associated skill

    // Stack & usage
    DWORD m_dwCount;                // Stack quantity
    BOOL  m_bIsForSale;             // Can be sold to NPCs?

    // Touch effects (on pickup/obtain)
    short m_sTouchEffectType;       // Effect on obtain
    short m_sTouchEffectValue1;     // Touch param 1
    short m_sTouchEffectValue2;     // Touch param 2
    short m_sTouchEffectValue3;     // Touch param 3

    // Extended special effects
    short m_sItemSpecEffectValue1;  // Additional effect 1
    short m_sItemSpecEffectValue2;  // Additional effect 2
    short m_sItemSpecEffectValue3;  // Additional effect 3

    // Attribute bit flags
    DWORD m_dwAttribute;            // See section 14.4 for bit layout
};
```

### 2.3 Attribute Bit Layout

The `m_dwAttribute` field uses bit flags:

```
Bit Layout (32-bit DWORD):
┌─────────────────────────────────────────────────────────────┐
│ 31-28 │ 27-24 │ 23-20 │ 19-16 │ 15-12 │ 11-8 │ 7-4 │ 3-0   │
│  (a)  │  (b)  │  (c)  │  (d)  │  (e)  │  (f) │ (x) │ flags │
└─────────────────────────────────────────────────────────────┘

Bit 0:   Custom-Made Item flag (1 = custom, bypasses level req)
Bits 1-3:  Reserved flags
Bits 4-7:  Reserved (x)
Bits 8-11:  (f) Additional special effect type
Bits 12-15: (e) Additional special effect flag
Bits 16-19: (d) Special effect type
Bits 20-23: (c) Special effect flag
Bits 24-27: (b) Item attribute value
Bits 28-31: (a) Item attribute type
```

---

## 3. Constants & Limits

### 3.1 Capacity Constants

```cpp
#define DEF_MAXITEMS              50    // Inventory slot count
#define DEF_MAXBANKITEMS         200    // Bank storage slot count
#define DEF_MAXITEMEQUIPPOS       15    // Equipment position count
```

### 3.2 Weight Calculation Constants

```cpp
// Maximum carrying capacity formula:
// MaxWeight = (STR * 500) + (Level * 500)

// Special weight rules:
// - Gold (item ID 90): effective weight = weight / 20
// - Minimum weight contribution = 1 (if calculated weight <= 0)
```

### 3.3 Server Limits

```cpp
#define DEF_MAXITEMTYPES        5000    // Maximum item template definitions
#define DEF_MAXDYNAMICOBJECTS  60000    // Max items on ground (shared pool)
```

---

## 4. Equipment Positions

### 4.1 Position Constants

```cpp
#define DEF_EQUIPPOS_NONE         0    // Not equippable / not equipped
#define DEF_EQUIPPOS_HEAD         1    // Helmet, hat, headgear
#define DEF_EQUIPPOS_BODY         2    // Chest armor, robe
#define DEF_EQUIPPOS_ARMS         3    // Arm guards, shoulders
#define DEF_EQUIPPOS_PANTS        4    // Leg armor, pants
#define DEF_EQUIPPOS_LEGGINGS     5    // Boots, leg guards
#define DEF_EQUIPPOS_NECK         6    // Amulet, necklace
#define DEF_EQUIPPOS_LHAND        7    // Left hand (shield, off-hand)
#define DEF_EQUIPPOS_RHAND        8    // Right hand (primary weapon)
#define DEF_EQUIPPOS_TWOHAND      9    // Two-handed weapon (uses both hands)
#define DEF_EQUIPPOS_RFINGER     10    // Right finger (ring)
#define DEF_EQUIPPOS_LFINGER     11    // Left finger (ring)
#define DEF_EQUIPPOS_BACK        12    // Cape, backpack
#define DEF_EQUIPPOS_RELEASEALL  13    // Special: unequip all items
```

### 4.2 Equipment Slot Diagram

```
                    ┌───────────┐
                    │   HEAD    │  (1)
                    │  Helmet   │
                    └─────┬─────┘
                          │
              ┌───────────┼───────────┐
              │           │           │
        ┌─────┴─────┐ ┌───┴───┐ ┌─────┴─────┐
        │   NECK    │ │ BODY  │ │   BACK    │
        │  Amulet   │ │ Armor │ │   Cape    │
        │    (6)    │ │  (2)  │ │   (12)    │
        └───────────┘ └───┬───┘ └───────────┘
                          │
              ┌───────────┼───────────┐
              │           │           │
        ┌─────┴─────┐ ┌───┴───┐ ┌─────┴─────┐
        │  LHAND    │ │ ARMS  │ │  RHAND    │
        │  Shield   │ │  (3)  │ │  Weapon   │
        │    (7)    │ └───────┘ │    (8)    │
        └───────────┘           └───────────┘
                                     or
              ┌─────────────────────────────┐
              │         TWOHAND (9)         │
              │    Two-Handed Weapon        │
              └─────────────────────────────┘
                          │
              ┌───────────┼───────────┐
              │           │           │
        ┌─────┴─────┐ ┌───┴───┐ ┌─────┴─────┐
        │  LFINGER  │ │ PANTS │ │  RFINGER  │
        │   Ring    │ │  (4)  │ │   Ring    │
        │   (11)    │ └───────┘ │   (10)    │
        └───────────┘           └───────────┘
                          │
                    ┌─────┴─────┐
                    │ LEGGINGS  │
                    │   Boots   │
                    │    (5)    │
                    └───────────┘
```

### 4.3 Equipment Status Array

```cpp
// m_sItemEquipmentStatus[position] = inventory slot index
// Value of -1 means nothing equipped at that position

// Example state:
// m_sItemEquipmentStatus[DEF_EQUIPPOS_HEAD] = 3    // Helmet in inv slot 3
// m_sItemEquipmentStatus[DEF_EQUIPPOS_BODY] = 7    // Armor in inv slot 7
// m_sItemEquipmentStatus[DEF_EQUIPPOS_RHAND] = 12  // Sword in inv slot 12
// m_sItemEquipmentStatus[DEF_EQUIPPOS_LHAND] = -1  // No shield equipped
```

---

## 5. Item Types

### 5.1 Type Constants

```cpp
#define DEF_ITEMTYPE_NONE                      0   // Invalid/empty slot
#define DEF_ITEMTYPE_EQUIP                     1   // Equipment (armor, weapons)
#define DEF_ITEMTYPE_APPLY                     2   // Applied/passive effect
#define DEF_ITEMTYPE_USE_DEPLETE               3   // Consumable (depletes on use)
#define DEF_ITEMTYPE_INSTALL                   4   // Installation/placement
#define DEF_ITEMTYPE_CONSUME                   5   // Consumable (potions, etc.)
#define DEF_ITEMTYPE_ARROW                     6   // Ammunition (arrows, bolts)
#define DEF_ITEMTYPE_EAT                       7   // Food item
#define DEF_ITEMTYPE_USE_SKILL                 8   // Skill scroll (learn on use)
#define DEF_ITEMTYPE_USE_PERM                  9   // Permanent item (skill book)
#define DEF_ITEMTYPE_USE_SKILL_ENABLEDIALOGBOX 10  // Skill with dialog prompt
#define DEF_ITEMTYPE_USE_DEPLETE_DEST          11  // Depleting + destination
#define DEF_ITEMTYPE_MATERIAL                  12  // Crafting material
#define DEF_ITEMTYPE_NOTUSED                   -1  // Deleted/removed marker
```

### 5.2 Type Behavior Summary

| Type | Stackable | Equippable | Consumed on Use | Notes |
|------|-----------|------------|-----------------|-------|
| EQUIP (1) | No | Yes | No | Armor, weapons |
| APPLY (2) | No | No | No | Passive effects |
| USE_DEPLETE (3) | Yes | No | Yes | Count decreases |
| INSTALL (4) | No | No | Yes | Placed in world |
| CONSUME (5) | Yes | No | Yes | Potions, scrolls |
| ARROW (6) | Yes | No | Yes | Depletes on attack |
| EAT (7) | Yes | No | Yes | Food, hunger |
| USE_SKILL (8) | Yes | No | Yes | Teaches skill |
| USE_PERM (9) | No | No | No | Permanent book |
| MATERIAL (12) | Yes | No | No | Crafting only |

---

## 6. Item Effect Types

### 6.1 Effect Type Constants

```cpp
#define DEF_ITEMEFFECTTYPE_NONE                0   // No effect
#define DEF_ITEMEFFECTTYPE_ATTACK              1   // Attack power bonus
#define DEF_ITEMEFFECTTYPE_DEFENSE             2   // Defense ratio bonus
#define DEF_ITEMEFFECTTYPE_ATTACK_ARROW        3   // Arrow attack effects
#define DEF_ITEMEFFECTTYPE_HP                  4   // HP bonus/modification
#define DEF_ITEMEFFECTTYPE_MP                  5   // MP bonus/modification
#define DEF_ITEMEFFECTTYPE_SP                  6   // SP bonus/modification
#define DEF_ITEMEFFECTTYPE_HPSTOCK             7   // HP recovery over time
#define DEF_ITEMEFFECTTYPE_GET                 8   // Gold/item acquisition
#define DEF_ITEMEFFECTTYPE_STUDYSKILL          9   // Learn a skill
#define DEF_ITEMEFFECTTYPE_SHOWLOCATION        10  // Reveal map location
#define DEF_ITEMEFFECTTYPE_MAGIC               11  // Cast magic effect
#define DEF_ITEMEFFECTTYPE_CHANGEATTR          12  // Change attributes
#define DEF_ITEMEFFECTTYPE_ATTACK_MANASAVE     13  // Attack with mana saving
#define DEF_ITEMEFFECTTYPE_ADDEFFECT           14  // Additional effect
#define DEF_ITEMEFFECTTYPE_MAGICDAMAGESSAVE    15  // Magic damage reduction
#define DEF_ITEMEFFECTTYPE_OCCUPYFLAG          16  // War occupation flag
#define DEF_ITEMEFFECTTYPE_DYE                 17  // Dye/recolor item
#define DEF_ITEMEFFECTTYPE_STUDYMAGIC          18  // Learn magic spell
#define DEF_ITEMEFFECTTYPE_ATTACK_MAXHPDOWN    19  // Attack lowers max HP
#define DEF_ITEMEFFECTTYPE_ATTACK_DEFENSE      20  // Attack affects defense
#define DEF_ITEMEFFECTTYPE_MATERIAL_ATTR       21  // Material attribute
#define DEF_ITEMEFFECTTYPE_FIRMSTAMINAR        22  // Stamina resistance
#define DEF_ITEMEFFECTTYPE_LOTTERY             23  // Random reward
#define DEF_ITEMEFFECTTYPE_ATTACK_SPECABLTY    24  // Special attack ability
#define DEF_ITEMEFFECTTYPE_DEFENSE_SPECABLTY   25  // Special defense ability
#define DEF_ITEMEFFECTTYPE_ALTERITEMDROP       26  // Modify drop rates
#define DEF_ITEMEFFECTTYPE_CONSTRUCTIONKIT     27  // Construction material
#define DEF_ITEMEFFECTTYPE_WARM                28  // Unfreeze/warm effect
#define DEF_ITEMEFFECTTYPE_FARMING             30  // Farming tool
#define DEF_ITEMEFFECTTYPE_SLATES              31  // Ancient tablet
#define DEF_ITEMEFFECTTYPE_ARMORDYE            32  // Armor dye
#define DEF_ITEMEFFECTTYPE_CRITKOMM            33  // Critical hit bonus
#define DEF_ITEMEFFECTTYPE_WEAPONDYE           34  // Weapon dye
```

### 6.2 Touch Effect Types

Applied when item is obtained/picked up:

```cpp
#define DEF_ITET_UNIQUE_OWNER    1   // Binds to player (soulbound)
#define DEF_ITET_ID              2   // Binds by instance ID
#define DEF_ITET_DATE            3   // Binds by date obtained
```

---

## 7. Core Functions

### 7.1 Item Addition

```cpp
// Add item to player inventory
BOOL _bAddClientItemList(int iClientH, CItem * pItem, int * pDelayType);
```

**Parameters:**
- `iClientH`: Client handle (player index)
- `pItem`: Pointer to item to add
- `pDelayType`: Output - delay event type if applicable

**Behavior:**
1. Find first empty inventory slot
2. If stackable item and same item exists, increase count instead
3. Calculate weight impact
4. Check weight limit (fail if exceeded)
5. Add to `m_pItemList[slot]`
6. Update `m_iCurWeightLoad`
7. Return TRUE on success, FALSE on failure

### 7.2 Item Removal

```cpp
// Remove item from specific inventory slot
void _ClearItemByIndex(int iClientH, int iItemIndex);
```

**Behavior:**
1. If item is equipped, unequip first
2. Delete item object
3. Set `m_pItemList[index] = NULL`
4. Recalculate weight

### 7.3 Item Pickup

```cpp
// Handle client picking up ground item
int iClientMotion_GetItem_Handler(int iClientH, short sX, short sY, char cDir);
```

**Returns:**
- `0`: Item not available
- `1`: Success
- `2`: Position mismatch

**Behavior:**
1. Validate player state (alive, initialized)
2. Check position matches claimed pickup location
3. Find item on ground tile
4. Verify inventory has space
5. Check weight limit
6. Call `_bAddClientItemList()`
7. Remove item from ground
8. Log pickup event
9. Broadcast to nearby players
10. Send inventory update to client

### 7.4 Item Drop

```cpp
// Handle client dropping an item
void DropItemHandler(int iClientH, short sItemIndex, int iAmount,
                     char * pItemName, BOOL bByPlayer = TRUE);
```

**Parameters:**
- `iClientH`: Client handle
- `sItemIndex`: Inventory slot to drop from
- `iAmount`: Quantity to drop (for stackables)
- `pItemName`: Expected item name (validation)
- `bByPlayer`: TRUE if player-initiated

**Behavior:**
1. Validate slot index and item exists
2. Verify item name matches (anti-cheat)
3. If equipped, unequip first
4. For stackables with partial drop:
   - Create new item instance with dropped count
   - Reduce original stack count
5. For full item drop:
   - Remove from inventory
6. Place item on ground at player position
7. Log drop event
8. Update client inventory

### 7.5 Item Equip

```cpp
// Equip item from inventory
BOOL bEquipItemHandler(int iClientH, short sItemIndex, BOOL bNotify = TRUE);
```

**Returns:** TRUE if equipped, FALSE if failed

**Validation Sequence:**
1. Item exists at slot
2. Item type is `DEF_ITEMTYPE_EQUIP`
3. Durability > 0
4. Level requirement met (unless custom item)
5. Gender requirement met
6. STR >= item weight (for heavy items)
7. Attribute requirements met (armor-specific)

**On Success:**
1. Set `m_bIsItemEquipped[slot] = TRUE`
2. Set `m_sItemEquipmentStatus[equipPos] = slot`
3. Call `CalcTotalItemEffect()` to recalculate bonuses
4. Notify client if `bNotify = TRUE`

### 7.6 Item Unequip

```cpp
// Release equipped item
void ReleaseItemHandler(int iClientH, short sItemIndex, BOOL bNotify = TRUE);
```

**Behavior:**
1. Validate item is actually equipped
2. Set `m_bIsItemEquipped[slot] = FALSE`
3. Set `m_sItemEquipmentStatus[equipPos] = -1`
4. Recalculate combat bonuses
5. Notify client if `bNotify = TRUE`

### 7.7 Equipment Effect Calculation

```cpp
// Recalculate all bonuses from equipped items
void CalcTotalItemEffect(int iClientH, int iEquipItemID, BOOL bNotify = TRUE);
```

**Recalculates:**
- Attack dice throw/range
- Attack bonus (small/large creatures)
- Hit ratio
- Defense ratio
- Damage absorption
- Shield parrying
- Mana savings
- Magic resistance
- Physical/magical damage bonuses
- Elemental resistances (air, earth, fire, water)
- Critical hit modifiers
- Experience/gold drop modifiers
- Special abilities
- HP/SP/MP bonuses

**Two-Handed Logic:**
- If equipping two-handed weapon (slot 9)
- Automatically unequip shield (slot 7)
- Cannot dual-wield with two-hander

---

## 8. Weight System

### 8.1 Weight Calculation Functions

```cpp
// Get weight of single item stack
int iGetItemWeight(CItem * pItem, int iCount);
```

**Formula:**
```
weight = item->m_wWeight * count
if (item is Gold) weight = weight / 20
if (weight <= 0) weight = 1
```

```cpp
// Get max carrying capacity
int _iCalcMaxLoad(int iClientH);
```

**Formula:**
```
maxWeight = (STR * 500) + (Level * 500)
```

```cpp
// Recalculate total inventory weight
int iCalcTotalWeight(int iClientH);
```

**Behavior:**
1. Sum weight of all inventory items
2. Process ALTERITEMDROP effects (bonus weight)
3. Store result in `m_iCurWeightLoad`
4. Return total weight

### 8.2 Encumbrance Effects

| Weight Ratio | Effect |
|--------------|--------|
| 0-100% | No penalty |
| >100% | Cannot run |
| >100% | Cannot pick up items |
| >100% | Reduced movement speed |

### 8.3 Space Check

```cpp
// Calculate remaining inventory slots
int _iGetItemSpaceLeft(int iClientH);
```

**Returns:** Number of empty inventory slots (0-50)

---

## 9. Equipment Validation

### 9.1 Validation Order

When calling `bEquipItemHandler()`:

```
1. Basic Checks
   ├── Item exists?
   ├── Item type == EQUIP?
   └── Durability > 0?

2. Level Check
   └── (Player Level >= Item Level) OR (Custom Item)?

3. Gender Check
   └── Item allows player gender?

4. Strength Check
   └── Player STR >= Item Weight?

5. Attribute Checks (Armor Only)
   ├── Check m_sItemEffectValue4 for attribute type
   ├── Types: 10=STR, 11=DEX, 12=VIT, 13=INT, 14=MAG, 15=CHR
   └── Player stat >= m_sItemEffectValue5?
```

### 9.2 Failure Responses

| Failure | Server Action |
|---------|---------------|
| Level too low | Send `DEF_NOTIFY_ITEMRELEASED` |
| Wrong gender | Send `DEF_NOTIFY_ITEMRELEASED` |
| STR too low | Send `DEF_NOTIFY_ITEMRELEASED` |
| Stat requirement | Unequip + `DEF_NOTIFY_ITEMRELEASED` |
| No durability | Unequip + `DEF_NOTIFY_ITEMRELEASED` |

---

## 10. Bank System

### 10.1 Bank Functions

```cpp
// Deposit item from inventory to bank
BOOL bPlayerItemToBank(int iClientH, short sItemIndex);
```

**Behavior:**
1. Unequip item if equipped
2. Find first empty bank slot
3. Move item pointer to bank array
4. Shift inventory to close gap
5. Return TRUE on success

```cpp
// Withdraw item from bank to inventory
BOOL bBankItemToPlayer(int iClientH, short sItemIndex);
```

**Behavior:**
1. Find first empty inventory slot
2. Check weight limit
3. Move item pointer to inventory
4. Return TRUE on success

### 10.2 Bank Limits

- Maximum bank slots: 200 (`DEF_MAXBANKITEMS`)
- Bank accessed via NPC interaction (Warehouse NPC)
- Gold can be deposited separately
- Bank contents persist across sessions

### 10.3 Bank NPC Interaction

```
Player → "Use" Warehouse NPC
Server → Opens bank UI
Player → Select deposit/withdraw
Server → Validates and transfers
Server → Sends updated inventory/bank contents
```

---

## 11. Trading/Exchange System

### 11.1 Exchange State

```cpp
// Per-client trading state
BOOL  m_bIsExchangeMode;              // Currently trading?
int   m_iExchangeH;                   // Trading partner handle
char  m_cExchangeName[11];            // Partner name
char  m_cExchangeItemName[4][21];     // Offered item names
char  m_cExchangeItemIndex[4];        // Offered item slots
int   m_iExchangeItemAmount[4];       // Offered quantities
BOOL  m_bIsExchangeConfirm;           // Confirmed trade?
int   iExchangeCount;                 // Item count in trade
```

### 11.2 Trading Flow

```
1. Player A initiates trade with Player B
   └── Both enter exchange mode

2. Players add items (up to 4 each)
   ├── Item validated (not bound, tradeable)
   └── Shown to partner

3. Both players confirm
   └── m_bIsExchangeConfirm = TRUE

4. Server validates and swaps
   ├── Check inventory space for received items
   ├── Check weight limits
   ├── Transfer items
   └── Log transaction

5. Exit exchange mode
   └── Clear exchange state
```

### 11.3 Trade Restrictions

- Maximum 4 items per player per trade
- Bound items (UNIQUE_OWNER) cannot be traded
- Quest items typically cannot be traded
- Must have space for incoming items
- Weight limit applies to received items

---

## 12. Network Protocol

### 12.1 Client → Server Messages

```cpp
// Inventory operations
#define DEF_COMMONTYPE_ITEMDROP                0x0A01  // Drop item
#define DEF_COMMONTYPE_EQUIPITEM               0x0A02  // Equip item
#define DEF_COMMONTYPE_REQ_LISTCONTENTS        0x0A03  // Request contents
#define DEF_COMMONTYPE_REQ_PURCHASEITEM        0x0A04  // Buy from shop
#define DEF_COMMONTYPE_GIVEITEMTOCHAR          0x0A05  // Give to player
#define DEF_COMMONTYPE_RELEASEITEM             0x0A0A  // Unequip item
#define DEF_COMMONTYPE_SETITEM                 0x0A0C  // Move item in inv
#define DEF_COMMONTYPE_REQ_USEITEM             0x0A11  // Use consumable
#define DEF_COMMONTYPE_REQ_SELLITEM            0x0A13  // Sell to shop
#define DEF_COMMONTYPE_REQ_REPAIRITEM          0x0A14  // Repair item
#define DEF_COMMONTYPE_REQ_SELLITEMCONFIRM     0x0A15  // Confirm sale
#define DEF_COMMONTYPE_REQ_REPAIRITEMCONFIRM   0x0A16  // Confirm repair
#define DEF_COMMONTYPE_REQ_CREATEPORTION       0x0A19  // Craft potion
#define DEF_COMMONTYPE_BUILDITEM               0x0A23  // Craft item

// Trading
#define DEF_COMMONTYPE_EXCHANGEITEMTOCHAR      0x0A1E  // Start trade
#define DEF_COMMONTYPE_SETEXCHANGEITEM         0x0A1F  // Add trade item
#define DEF_COMMONTYPE_CONFIRMEXCHANGEITEM     0x0A20  // Confirm trade
#define DEF_COMMONTYPE_CANCELEXCHANGEITEM      0x0A21  // Cancel trade
```

### 12.2 Server → Client Messages

```cpp
// Inventory notifications
#define DEF_NOTIFY_ITEMOBTAINED                0x0B01  // Item received
#define DEF_NOTIFY_CANNOTCARRYMOREITEM         0x0B05  // Inventory full
#define DEF_NOTIFY_ITEMPURCHASED               0x0B06  // Purchase success
#define DEF_NOTIFY_DROPITEMFIN_COUNTCHANGED    ...     // Stack updated
#define DEF_NOTIFY_ITEMRELEASED                ...     // Item unequipped
#define DEF_NOTIFY_GIZONITEMUPGRADELEFT        ...     // Upgrade available
```

### 12.3 Message Payload Examples

**Drop Item Request:**
```
[MessageType: 0x0A01]
[SlotIndex: 2 bytes]
[Amount: 4 bytes]
[ItemName: 20 bytes]
```

**Equip Item Request:**
```
[MessageType: 0x0A02]
[SlotIndex: 2 bytes]
```

**Item Obtained Notification:**
```
[MessageType: 0x0B01]
[ItemID: 4 bytes]
[ItemName: 20 bytes]
[Quantity: 4 bytes]
[SlotIndex: 1 byte]
[ItemData: variable]
```

---

## 13. Item Logging

### 13.1 Log Types

```cpp
enum ItemLogType {
    LOG_GIVE        = 1,    // Item given (GM command)
    LOG_DROP        = 2,    // Item dropped on ground
    LOG_GET         = 3,    // Item picked up
    LOG_DEPLETE     = 4,    // Item consumed/used
    LOG_NEWGEN_DROP = 5,    // New item generated (drop)
    LOG_DUP_ITEMID  = 6,    // Duplicate ID detected
    LOG_BUY         = 7,    // Purchased from shop
    LOG_SELL        = 8,    // Sold to shop
    LOG_RETRIEVE    = 9,    // Retrieved from bank
    LOG_DEPOSIT     = 10,   // Deposited to bank
    LOG_EXCHANGE    = 11,   // Traded with player
    LOG_SKILL_LEARN = 12,   // Skill scroll used
    LOG_MAKE        = 13,   // Crafted item
    LOG_SUMMON      = 14,   // Summon item used
    LOG_POISONED    = 15,   // Poisoned item
    LOG_MAGIC_LEARN = 16,   // Magic scroll used
    LOG_REPAIR      = 17,   // Item repaired
    LOG_USE         = 32,   // Generic use
};
```

### 13.2 Logging Function

```cpp
BOOL _bItemLog(int iLogType, int iClientH, int iNpcH, CItem * pItem, BOOL bFlag);
```

**Logged Data:**
- Player name/account
- Item name/ID
- Quantity
- Action type
- Timestamp
- Location (map, coordinates)
- NPC involved (if applicable)

---

## 14. Special Mechanics

### 14.1 Durability System

- Equipment has `m_wCurLifeSpan` / `m_wMaxLifeSpan`
- Durability decreases with use:
  - Weapons: On attack
  - Armor: When taking damage
- Zero durability = cannot equip
- Repair at Blacksmith NPC (costs gold)
- Repair cannot exceed max durability

### 14.2 Two-Handed Weapons

```cpp
// When equipping two-handed weapon:
if (equipPos == DEF_EQUIPPOS_TWOHAND) {
    // Automatically unequip shield
    if (m_sItemEquipmentStatus[DEF_EQUIPPOS_LHAND] != -1) {
        ReleaseItemHandler(iClientH,
            m_sItemEquipmentStatus[DEF_EQUIPPOS_LHAND], TRUE);
    }
}
```

### 14.3 Arrow Slot

```cpp
char m_cArrowIndex;  // Special tracking for equipped arrows
```

- Arrows equipped in a special slot
- Consumed when firing ranged attacks
- `-1` when no arrows equipped
- Tracks which inventory slot has arrows

### 14.4 Custom Items

Items with bit 0 set in `m_dwAttribute`:
- Bypass level requirements
- May have enhanced stats
- Created via special means (crafting, events)

### 14.5 Bound Items

Touch effect `DEF_ITET_UNIQUE_OWNER`:
- Cannot be traded
- Cannot be dropped
- Bound to character on pickup
- Character ID stored in touch values

### 14.6 Stack Limits

- Consumables: Stack up to 20,000
- Materials: Stack up to 20,000
- Arrows: Stack up to 9,999
- Gold: Stacks (separate tracking)
- Equipment: No stacking (count = 1)

---

## 15. Integration Points

### 15.1 Combat System

```cpp
// Combat reads equipped weapon stats
CalcTotalItemEffect() →
    m_iAttackDiceThrow    // Attack dice
    m_iAttackDiceRange    // Dice range
    m_iAttackBonus        // Flat bonus

// Armor provides defense
CalcTotalItemEffect() →
    m_iDefenseRatio       // Defense rating
    m_iDamageAbsorption   // Damage reduction
```

### 15.2 Magic System

```cpp
// Magic items provide bonuses
CalcTotalItemEffect() →
    m_iMagicDamageSaveRatio  // Magic resistance
    m_iManaSaveRatio         // Mana cost reduction
```

### 15.3 Skill System

```cpp
// Skill scrolls teach skills
if (item->m_sItemEffectType == DEF_ITEMEFFECTTYPE_STUDYSKILL) {
    // Learn skill from m_sItemEffectValue1
    // Consume item
}
```

### 15.4 Crafting System

```cpp
// Crafting consumes materials
// Type DEF_ITEMTYPE_MATERIAL used
// BuildItem recipes reference item IDs
// Potions use Portion recipes
```

### 15.5 Trading System

```cpp
// Exchange validates items
if (item->m_sTouchEffectType == DEF_ITET_UNIQUE_OWNER) {
    // Cannot trade bound items
}
```

### 15.6 Quest System

```cpp
// Quest items tracked
// Some quests require specific items
// Quest rewards add items
```

---

## 16. Message Flow Diagrams

### 16.1 Item Pickup Flow

```
┌────────┐                    ┌────────┐
│ Client │                    │ Server │
└───┬────┘                    └───┬────┘
    │                             │
    │ ──── Motion: Get Item ────> │
    │      (x, y, direction)      │
    │                             │
    │                             ├── Validate position
    │                             ├── Find item on tile
    │                             ├── Check inv space
    │                             ├── Check weight
    │                             ├── Add to inventory
    │                             ├── Remove from ground
    │                             ├── Log pickup
    │                             │
    │ <── Notify: Item Obtained ──│
    │     (item data, slot)       │
    │                             │
    │ <── Broadcast: Object Remove│
    │     (to nearby players)     │
    │                             │
```

### 16.2 Equip Item Flow

```
┌────────┐                    ┌────────┐
│ Client │                    │ Server │
└───┬────┘                    └───┬────┘
    │                             │
    │ ──── Equip Item Request ──> │
    │      (slot index)           │
    │                             │
    │                             ├── Validate item exists
    │                             ├── Check item type
    │                             ├── Check durability
    │                             ├── Check level req
    │                             ├── Check gender
    │                             ├── Check STR
    │                             ├── Check attr requirements
    │                             │
    │                     ┌───────┴───────┐
    │                     │   Success?    │
    │                     └───────┬───────┘
    │                       Yes   │   No
    │                     ┌───────┴───────┐
    │                     │               │
    │                     ▼               ▼
    │               Set equipped    Send Release
    │               Calc bonuses    Notification
    │               Update client
    │                     │
    │ <─── Notify: Equipped ──────│
    │      (new stats)            │
    │                             │
```

### 16.3 Trading Flow

```
┌──────────┐             ┌────────┐             ┌──────────┐
│ Player A │             │ Server │             │ Player B │
└────┬─────┘             └───┬────┘             └────┬─────┘
     │                       │                       │
     │ ── Exchange Request ─>│                       │
     │    (target: B)        │                       │
     │                       │                       │
     │                       │<── Accept Exchange ───│
     │                       │                       │
     │<── Exchange Started ──│── Exchange Started ──>│
     │                       │                       │
     │ ── Add Item ─────────>│                       │
     │                       │── Item Added ────────>│
     │                       │                       │
     │                       │<──────── Add Item ────│
     │<──── Item Added ──────│                       │
     │                       │                       │
     │ ── Confirm ──────────>│                       │
     │                       │<────────── Confirm ───│
     │                       │                       │
     │                       ├── Validate both sides │
     │                       ├── Check space/weight  │
     │                       ├── Transfer items      │
     │                       ├── Log transaction     │
     │                       │                       │
     │<─ Exchange Complete ──│── Exchange Complete ─>│
     │   (received items)    │   (received items)    │
     │                       │                       │
```

---

## Appendix A: Quick Reference

### Common Item Operations

| Operation | Function | Message Type |
|-----------|----------|--------------|
| Pickup item | `iClientMotion_GetItem_Handler` | Motion message |
| Drop item | `DropItemHandler` | `0x0A01` |
| Equip item | `bEquipItemHandler` | `0x0A02` |
| Unequip item | `ReleaseItemHandler` | `0x0A0A` |
| Use item | (varies by type) | `0x0A11` |
| Move in inventory | (varies) | `0x0A0C` |
| Bank deposit | `bPlayerItemToBank` | (NPC interaction) |
| Bank withdraw | `bBankItemToPlayer` | (NPC interaction) |
| Trade start | (exchange handlers) | `0x0A1E` |
| Trade confirm | (exchange handlers) | `0x0A20` |

### Slot Count Summary

| Container | Slots | Constant |
|-----------|-------|----------|
| Inventory | 50 | `DEF_MAXITEMS` |
| Bank | 200 | `DEF_MAXBANKITEMS` |
| Equipment | 15 | `DEF_MAXITEMEQUIPPOS` |
| Trade offer | 4 | Hard-coded |

---

*Document generated from legacy source analysis. Last updated: 2026-02-01*
