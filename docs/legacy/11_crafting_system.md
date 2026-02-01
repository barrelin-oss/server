# Legacy Crafting System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Constants and Limits](#constants-and-limits)
3. [Data Structures](#data-structures)
   - [CBuildItem Class](#cbuilditem-class)
   - [CPortion Class](#cportion-class)
4. [Crafting Skills](#crafting-skills)
   - [Skill Index 12: Alchemy](#skill-index-12-alchemy)
   - [Skill Index 13: Manufacturing](#skill-index-13-manufacturing)
5. [Item Manufacturing System](#item-manufacturing-system)
   - [Recipe Loading](#recipe-loading-builditemcfg)
   - [Manufacturing Handler](#manufacturing-handler-builditemhandler)
   - [Success Calculation](#manufacturing-success-calculation)
   - [Crafted Item Attributes](#crafted-item-attributes)
6. [Potion Mixing System](#potion-mixing-system)
   - [Recipe Loading](#recipe-loading-portioncfg)
   - [Potion Handler](#potion-handler-reqcreateportionhandler)
   - [Success Calculation](#potion-success-calculation)
7. [Item Upgrade System](#item-upgrade-system)
   - [Upgrade Handler](#upgrade-handler)
   - [Success Probability](#upgrade-success-probability)
   - [Gizon Points](#gizon-item-upgrade-points)
8. [Item Repair System](#item-repair-system)
9. [Network Messages](#network-messages)
10. [Configuration File Formats](#configuration-file-formats)
11. [Related Functions Reference](#related-functions-reference)

---

## Overview

The Helbreath crafting system consists of three main subsystems:

1. **Item Manufacturing** - Crafting weapons, armor, and materials using the Manufacturing skill (skill index 13)
2. **Potion Mixing** - Creating potions and consumables using the Alchemy skill (skill index 12)
3. **Item Upgrading** - Enhancing items using special stones and Gizon points

The crafting system is entirely managed within the monolithic `CGame` class in `Game.cpp`. Recipe data is stored in arrays of `CBuildItem` and `CPortion` objects loaded from configuration files at server startup.

### Key Characteristics

- **Maximum Build Item Recipes**: 300 (`DEF_MAXBUILDITEMS`)
- **Maximum Potion Recipes**: 500 (`DEF_MAXPORTIONTYPES`)
- **Materials per Recipe**: Up to 6 different materials for item manufacturing
- **Ingredients per Potion**: 12-slot ingredient array (6 items x 2 values each)
- **Skill-based Success**: Success rate directly tied to player's skill mastery level

---

## Constants and Limits

### Defined in Game.h

```cpp
#define DEF_MAXBUILDITEMS           300     // Maximum number of build item recipes
#define DEF_MAXPORTIONTYPES         500     // Maximum number of potion recipes
#define DEF_MAXITEMTYPES            5000    // Maximum item types in the game
#define DEF_MAXSKILLTYPE            60      // Maximum skill types
```

### Item Type Constants (Item.h)

```cpp
#define DEF_ITEMTYPE_MATERIAL       12      // Crafting material item type
#define DEF_ITEMTYPE_CONSUME        5       // Consumable item type (potions)
```

### Item Effect Types Related to Crafting (Item.h)

```cpp
#define DEF_ITEMEFFECTTYPE_MATERIAL_ATTR    21  // Material attribute effect
```

### Touch Effect Types for Crafted Items (Item.h)

```cpp
#define DEF_ITET_UNIQUE_OWNER       1       // Unique owner binding
#define DEF_ITET_ID                 2       // Item ID tracking
#define DEF_ITET_DATE               3       // Date-limited item
```

### Item Logging Constants (Game.h)

```cpp
#define DEF_ITEMLOG_MAKE            13      // Item crafted/manufactured
#define DEF_ITEMLOG_UPGRADESUCCESS  ...     // Upgrade successful
#define DEF_ITEMLOG_UPGRADEFAIL     ...     // Upgrade failed
```

---

## Data Structures

### CBuildItem Class

**File**: `BuildItem.h`, `BuildItem.cpp`

The `CBuildItem` class represents a single manufacturing recipe for crafting items like weapons, armor, and materials.

```cpp
class CBuildItem
{
public:
    CBuildItem();
    virtual ~CBuildItem();

    char  m_cName[21];              // Name of the item to be crafted
    short m_sItemID;                // Item ID number (resolved at load time)

    int  m_iSkillLimit;             // Minimum Manufacturing skill required

    // Material requirements (up to 6 different materials)
    int  m_iMaterialItemID[6];      // Item IDs of required materials
    int  m_iMaterialItemCount[6];   // Quantity of each material needed
    int  m_iMaterialItemValue[6];   // Weight/value contribution of each material
    int  m_iIndex[6];               // Resolved indices (internal use)

    int  m_iMaxValue;               // Maximum achievable quality value
    int  m_iAverageValue;           // Average expected quality value
    int  m_iMaxSkill;               // Maximum skill level for gaining XP
    WORD m_wAttribute;              // Attribute flags for the crafted item
};
```

#### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `m_cName` | char[21] | Item name string (max 20 chars + null terminator) |
| `m_sItemID` | short | Item ID number from Item.cfg, resolved during loading |
| `m_iSkillLimit` | int | Minimum Manufacturing skill to attempt crafting |
| `m_iMaterialItemID[6]` | int[6] | Array of required material item IDs |
| `m_iMaterialItemCount[6]` | int[6] | Number of each material required |
| `m_iMaterialItemValue[6]` | int[6] | Quality weight of each material slot |
| `m_iIndex[6]` | int[6] | Internal index tracking, initialized to -1 |
| `m_iMaxValue` | int | Sum of (material_value * 100) for all slots |
| `m_iAverageValue` | int | Baseline quality for neutral crafting result |
| `m_iMaxSkill` | int | Skill cap for experience gain from this recipe |
| `m_wAttribute` | WORD | 16-bit attribute flags applied to crafted item |

#### Constructor Initialization

```cpp
CBuildItem::CBuildItem()
{
    ZeroMemory(m_cName, sizeof(m_cName));
    m_sItemID = -1;
    m_iSkillLimit = 0;

    for (int i = 0; i < 6; i++) {
        m_iMaterialItemID[i]    = NULL;
        m_iMaterialItemCount[i] = NULL;
        m_iMaterialItemValue[i] = NULL;
        m_iIndex[i]             = -1;
    }

    m_iMaxValue     = 0;
    m_iAverageValue = 0;
    m_iMaxSkill     = 0;
    m_wAttribute    = 0;
}
```

---

### CPortion Class

**File**: `Portion.h`, `Portion.cpp`

The `CPortion` class represents a potion recipe for the Alchemy system.

```cpp
class CPortion
{
public:
    CPortion();
    virtual ~CPortion();

    char  m_cName[21];              // Name of the potion to be created
    short m_sArray[12];             // Ingredient array (6 items x 2 values)

    int   m_iSkillLimit;            // Minimum Alchemy skill required
    int   m_iDifficulty;            // Difficulty modifier for success rate
};
```

#### Field Descriptions

| Field | Type | Description |
|-------|------|-------------|
| `m_cName` | char[21] | Potion name string (max 20 chars + null terminator) |
| `m_sArray[12]` | short[12] | Ingredient definition: [ItemID1, Count1, ItemID2, Count2, ...] |
| `m_iSkillLimit` | int | Minimum Alchemy skill to attempt mixing |
| `m_iDifficulty` | int | Subtracted from skill level for success calculation |

#### Ingredient Array Structure

The `m_sArray[12]` array stores up to 6 different ingredient items:

```
Index 0:  Item 1 ID number
Index 1:  Item 1 count required
Index 2:  Item 2 ID number
Index 3:  Item 2 count required
Index 4:  Item 3 ID number
Index 5:  Item 3 count required
Index 6:  Item 4 ID number
Index 7:  Item 4 count required
Index 8:  Item 5 ID number
Index 9:  Item 5 count required
Index 10: Item 6 ID number
Index 11: Item 6 count required
```

A value of -1 indicates an empty slot (no ingredient required).

#### Constructor Initialization

```cpp
CPortion::CPortion()
{
    ZeroMemory(m_cName, sizeof(m_cName));
    m_iSkillLimit = 0;
    m_iDifficulty = 0;

    for (int i = 0; i < 12; i++)
        m_sArray[i] = -1;
}
```

---

## Crafting Skills

### Skill Index 12: Alchemy

**Purpose**: Potion mixing and consumable creation

**Skill Progression**:
- Maximum skill level: 100
- Stat dependency: INT * 2 (skill cannot exceed INT * 2)
- Experience gain: Random 1 to (difficulty / 3) per successful craft

**Skill Check Enforcement**:
```cpp
// From SkillCheck function (line ~54700)
while ((m_pClientList[sTargetH]->m_iInt*2) < m_pClientList[sTargetH]->m_cSkillMastery[12]) {
    m_pClientList[sTargetH]->m_cSkillMastery[12]--;
}
```

### Skill Index 13: Manufacturing

**Purpose**: Item crafting (weapons, armor, materials)

**Skill Progression**:
- Maximum skill level: 100
- Stat dependency: STR * 2 (skill cannot exceed STR * 2)
- Experience gain: Random 1 to (skill_limit / 4) per successful craft

**Skill Check Enforcement**:
```cpp
// From SkillCheck function (line ~54700)
while ((m_pClientList[sTargetH]->m_iStr*2) < m_pClientList[sTargetH]->m_cSkillMastery[13]) {
    m_pClientList[sTargetH]->m_cSkillMastery[13]--;
}
```

---

## Item Manufacturing System

### Recipe Loading (BuildItem.cfg)

**Function**: `CGame::_bDecodeBuildItemConfigFileContents`
**Location**: Game.cpp, line ~38633

Recipes are loaded from the BuildItem.cfg configuration file during server initialization.

```cpp
BOOL CGame::_bDecodeBuildItemConfigFileContents(char *pData, DWORD dwMsgSize)
```

#### Loading Process

1. Parse config file token by token
2. When "BuildItem" keyword is found, allocate new CBuildItem
3. Read 23 values in sequence for each recipe:
   - Item name
   - Skill limit
   - 6 material slots (ID, count, value for each)
   - Average value
   - Max skill
   - Attribute flags
4. Resolve item name to item ID
5. Calculate max value: `sum(material_value[i] * 100)`

#### State Machine

```cpp
cReadModeB:
1  = Item name
2  = Skill limit
3  = Material 1 ID
4  = Material 1 count
5  = Material 1 value
6  = Material 2 ID
7  = Material 2 count
8  = Material 2 value
... (continues for all 6 materials)
21 = Average value
22 = Max skill
23 = Attribute flags
```

#### Validation

```cpp
// Item existence check
pItem = new class CItem;
if (_bInitItemAttr(pItem, m_pBuildItemList[iIndex]->m_cName) == TRUE) {
    m_pBuildItemList[iIndex]->m_sItemID = pItem->m_sIDnum;
    // Calculate max value
    for (i = 0; i < 6; i++)
        m_pBuildItemList[iIndex]->m_iMaxValue +=
            (m_pBuildItemList[iIndex]->m_iMaterialItemValue[i] * 100);
}
else {
    // Item doesn't exist - recipe invalid
    delete m_pBuildItemList[iIndex];
    m_pBuildItemList[iIndex] = NULL;
    return FALSE;
}
```

---

### Manufacturing Handler (BuildItemHandler)

**Function**: `CGame::BuildItemHandler`
**Location**: Game.cpp, line ~39003

Handles player requests to craft items.

```cpp
void CGame::BuildItemHandler(int iClientH, char *pData)
```

#### Input Parameters

The function receives a packet containing:
- **Offset 11**: Item name (20 bytes)
- **Offset 31-36**: 6 element item indices from player inventory (1 byte each)

```cpp
// Parse packet
cp = (char *)(pData + 11);
ZeroMemory(cName, sizeof(cName));
memcpy(cName, cp, 20);
cp += 20;

// Get material indices
ZeroMemory(cElementItemID, sizeof(cElementItemID));
cElementItemID[0] = *cp; cp++;
cElementItemID[1] = *cp; cp++;
cElementItemID[2] = *cp; cp++;
cElementItemID[3] = *cp; cp++;
cElementItemID[4] = *cp; cp++;
cElementItemID[5] = *cp; cp++;
```

#### Processing Flow

1. **Validate client exists**
2. **Sort element indices** - Move -1 (empty) slots to end
3. **Initial skill check** - Roll d100, must be <= skill level
4. **Validate inventory items** - All element indices must be valid
5. **Find matching recipe** - Search `m_pBuildItemList` by name
6. **Check skill requirement** - `recipe.m_iSkillLimit <= player_skill`
7. **Validate materials** - Check all required materials are present
8. **Calculate quality value** - Based on material quality
9. **Create crafted item** - Apply attributes and quality
10. **Consume materials** - Remove used materials from inventory
11. **Grant experience** - If skill cap not reached

### Manufacturing Success Calculation

```cpp
// Initial success roll
iPlayerSkillLevel = m_pClientList[iClientH]->m_cSkillMastery[13];
iResult = iDice(1, 100);

if (iResult > iPlayerSkillLevel) {
    // Failure - crafting attempt failed
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_BUILDITEMFAIL, NULL, NULL, NULL, NULL);
    return;
}
```

**Success Probability**: `skill_level / 100` (linear)

### Material Validation

```cpp
for (x = 0; x < 6; x++) {
    if (m_pBuildItemList[i]->m_iMaterialItemCount[x] == 0) {
        iMatch++;  // Empty slot, auto-match
    }
    else {
        for (z = 0; z < 6; z++) {
            if ((cElementItemID[z] != -1) && (bItemFlag[z] == FALSE)) {
                // Check if player has the required material
                if ((player_item->m_sIDnum == recipe->m_iMaterialItemID[x]) &&
                    (player_item->m_dwCount >= recipe->m_iMaterialItemCount[x]) &&
                    (iItemCount[cElementItemID[z]] > 0)) {

                    // Calculate quality contribution
                    iTemp = player_item->m_sItemSpecEffectValue2;
                    if (iTemp > m_pClientList[iClientH]->m_cSkillMastery[13]) {
                        // Penalty for material above skill level
                        iTemp = iTemp - (iTemp - player_skill) / 2;
                    }

                    iTotalValue += (iTemp * recipe->m_iMaterialItemValue[x]);
                    iMatch++;
                }
            }
        }
    }
}

if (iMatch != 6) {
    // Not all materials present
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_BUILDITEMFAIL, NULL, NULL, NULL, NULL);
    return;
}
```

### Crafted Item Attributes

#### Quality Calculation

```cpp
// Calculate quality percentage
dV2 = (double)m_pBuildItemList[i]->m_iMaxValue;
dV3 = (iTotalValue > 0) ? (double)iTotalValue : 1.0f;
dV1 = (dV3 / dV2) * 100.0f;
iTotalValue = (int)dV1;

// Calculate result value relative to average
iResultValue = (iTotalValue - m_pBuildItemList[i]->m_iAverageValue);
```

#### Custom-Made Flag

```cpp
// Set custom-made flag (bit 0 of attribute)
dwTemp = pItem->m_dwAttribute;
dwTemp = dwTemp & 0xFFFFFFFE;
dwTemp = dwTemp | 0x00000001;
pItem->m_dwAttribute = dwTemp;
```

#### For Material Items

```cpp
if (pItem->m_cItemType == DEF_ITEMTYPE_MATERIAL) {
    // Material items get quality rating
    iTemp = iDice(1, (iPlayerSkillLevel/2)+1) - 1;
    pItem->m_sItemSpecEffectValue2 = (iPlayerSkillLevel/2) + iTemp;

    // Add unique ID
    pItem->m_sTouchEffectType   = DEF_ITET_ID;
    pItem->m_sTouchEffectValue1 = iDice(1,100000);
    pItem->m_sTouchEffectValue2 = iDice(1,100000);
    pItem->m_sTouchEffectValue3 = timeGetTime();
}
```

#### For Weapons/Armor

```cpp
else {
    // Apply attribute flags from recipe
    dwTemp = pItem->m_dwAttribute;
    dwTemp = dwTemp & 0x0000FFFF;
    dwTemp2 = (WORD)m_pBuildItemList[i]->m_wAttribute;
    dwTemp2 = dwTemp2 << 16;
    dwTemp = dwTemp | dwTemp2;
    pItem->m_dwAttribute = dwTemp;

    // Calculate quality modifier
    if (iResultValue > 0) {
        // Above average quality
        dV2 = (double)iResultValue;
        dV3 = (double)(100 - m_pBuildItemList[i]->m_iAverageValue);
        dV1 = (dV2/dV3) * 100.0f;
        pItem->m_sItemSpecEffectValue2 = (int)dV1;
    }
    else if (iResultValue < 0) {
        // Below average quality
        dV2 = (double)iResultValue;
        dV3 = (double)(m_pBuildItemList[i]->m_iAverageValue);
        dV1 = (dV2/dV3) * 100.0f;
        pItem->m_sItemSpecEffectValue2 = (int)dV1;
    }
    else {
        pItem->m_sItemSpecEffectValue2 = 0;
    }

    // Apply quality to max lifespan
    dV2 = (double)pItem->m_sItemSpecEffectValue2;
    dV3 = (double)pItem->m_wMaxLifeSpan;
    dV1 = (dV2/100.0f) * dV3;
    iTemp = (int)pItem->m_wMaxLifeSpan + (int)dV1;

    // Cap at 2x base lifespan
    if (wTemp <= pItem->m_wMaxLifeSpan * 2) {
        pItem->m_wMaxLifeSpan = wTemp;
        pItem->m_sItemSpecEffectValue1 = (short)wTemp;
        pItem->m_wCurLifeSpan = pItem->m_wMaxLifeSpan;
    }

    // Custom items are color 2
    pItem->m_cItemColor = 2;
}
```

#### Experience Gain

```cpp
// Grant experience if skill cap not reached
if (m_pBuildItemList[i]->m_iMaxSkill > m_pClientList[iClientH]->m_cSkillMastery[13])
    CalculateSSN_SkillIndex(iClientH, 13, 1);

// Grant character experience
GetExp(iClientH, iDice(1, (m_pBuildItemList[i]->m_iSkillLimit/4)));
```

---

## Potion Mixing System

### Recipe Loading (Portion.cfg)

**Function**: `CGame::_bDecodePortionConfigFileContents`
**Location**: Game.cpp, line ~35114

```cpp
BOOL CGame::_bDecodePortionConfigFileContents(char *pData, DWORD dwMsgSize)
```

#### Loading Process

1. Parse config file token by token
2. When "potion" keyword is found, begin reading recipe
3. Read values in sequence:
   - Recipe number (index in array)
   - Potion name
   - 12 ingredient array values (6 items x 2 each)
   - Skill limit
   - Difficulty

#### State Machine

```cpp
cReadModeB:
1  = Recipe number
2  = Potion name
3  = m_sArray[0]  (Item 1 ID)
4  = m_sArray[1]  (Item 1 count)
5  = m_sArray[2]  (Item 2 ID)
...
14 = m_sArray[11] (Item 6 count)
15 = Skill limit
16 = Difficulty
```

---

### Potion Handler (ReqCreatePortionHandler)

**Function**: `CGame::ReqCreatePortionHandler`
**Location**: Game.cpp, line ~34845

```cpp
void CGame::ReqCreatePortionHandler(int iClientH, char *pData)
```

#### Input Parameters

The function receives a packet containing:
- **Offset 11-16**: 6 ingredient item indices from player inventory (1 byte each)

```cpp
cp = (char *)(pData + 11);
cI[0] = *cp; cp++;
cI[1] = *cp; cp++;
cI[2] = *cp; cp++;
cI[3] = *cp; cp++;
cI[4] = *cp; cp++;
cI[5] = *cp; cp++;
```

#### Processing Flow

1. **Validate indices** - Must be valid inventory positions
2. **Consolidate duplicates** - Count same items together
3. **Sort by item ID** - Descending order (bubble sort)
4. **Build ingredient array** - Format: [ID, count, ID, count, ...]
5. **Find matching recipe** - Compare with all potion recipes
6. **Check skill requirement** - Must meet minimum skill
7. **Roll for success** - Based on skill minus difficulty
8. **Consume ingredients** - Remove used items
9. **Create potion** - Add to player inventory

### Ingredient Matching

```cpp
// Build the ingredient array from player items
j = 0;
for (i = 0; i < 6; i++) {
    if (sItemIndex[i] != -1)
        sItemArray[j] = m_pClientList[iClientH]->m_pItemList[sItemIndex[i]]->m_sIDnum;
    else
        sItemArray[j] = sItemIndex[i];
    sItemArray[j+1] = sItemNumber[i];
    j += 2;
}

// Find matching recipe
for (i = 0; i < DEF_MAXPORTIONTYPES; i++) {
    if (m_pPortionConfigList[i] != NULL) {
        bFlag = FALSE;
        for (j = 0; j < 12; j++) {
            if (m_pPortionConfigList[i]->m_sArray[j] != sItemArray[j])
                bFlag = TRUE;
        }

        if (bFlag == FALSE) {
            // Match found!
            memcpy(cPortionName, m_pPortionConfigList[i]->m_cName, 20);
            iSkillLimit = m_pPortionConfigList[i]->m_iSkillLimit;
            iDifficulty = m_pPortionConfigList[i]->m_iDifficulty;
        }
    }
}
```

### Potion Success Calculation

```cpp
// Check skill requirement
iSkillLevel = m_pClientList[iClientH]->m_cSkillMastery[12];  // Alchemy skill
if (iSkillLimit > iSkillLevel) {
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_LOWPORTIONSKILL, NULL, NULL, NULL, cPortionName);
    return;
}

// Calculate effective skill level
iSkillLevel -= iDifficulty;
if (iSkillLevel <= 0) iSkillLevel = 1;

// Roll for success
iResult = iDice(1, 100);
if (iResult > iSkillLevel) {
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_PORTIONFAIL, NULL, NULL, NULL, cPortionName);
    return;
}
```

**Success Probability**: `(skill - difficulty) / 100`

### Material Consumption

```cpp
for (i = 0; i < 6; i++) {
    if (sItemIndex[i] != -1) {
        if (m_pClientList[iClientH]->m_pItemList[sItemIndex[i]]->m_cItemType == DEF_ITEMTYPE_CONSUME)
            // Reduce count for consumable items
            SetItemCount(iClientH, sItemIndex[i],
                m_pClientList[iClientH]->m_pItemList[sItemIndex[i]]->m_dwCount - sItemNumber[i]);
        else
            // Delete non-consumable items
            ItemDepleteHandler(iClientH, sItemIndex[i], FALSE);
    }
}
```

---

## Item Upgrade System

### Upgrade Handler

**Function**: `CGame::RequestItemUpgradeHandler`
**Location**: Game.cpp, line ~50409

```cpp
void CGame::RequestItemUpgradeHandler(int iClientH, int iItemIndex)
```

The upgrade system uses special stones (Stone of Xelima, Stone of Merien) to enhance items.

#### Upgrade Materials

| Item ID | Name | Use |
|---------|------|-----|
| 656 | Stone of Xelima | Weapon upgrades |
| 657 | Stone of Merien | Armor/shield upgrades |

#### Upgrade Level Storage

Item upgrade level is stored in the highest 4 bits of `m_dwAttribute`:

```cpp
// Extract current upgrade level
iValue = (m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_dwAttribute & 0xF0000000) >> 28;

// Maximum upgrade level: 15
if (iValue >= 15 || iValue < 0) {
    SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_ITEMUPGRADEFAIL, 1, NULL, NULL, NULL);
    return;
}

// Apply new upgrade level
iValue += 1;
if (iValue > 15) iValue = 15;
dwTemp = m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_dwAttribute;
dwTemp = dwTemp & 0x0FFFFFFF;  // Clear upgrade bits
m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_dwAttribute = dwTemp | (iValue << 28);
```

### Upgrade Success Probability

**Function**: `CGame::bCheckIsItemUpgradeSuccess`
**Location**: Game.cpp, line ~48808

```cpp
BOOL CGame::bCheckIsItemUpgradeSuccess(int iClientH, int iItemIndex, int iSomH, BOOL bBonus)
```

#### Base Probability Table

| Current Level | Base Probability | Cumulative Success |
|---------------|------------------|-------------------|
| +0 | 30% | 30% |
| +1 | 25% | 7.5% |
| +2 | 20% | 1.5% |
| +3 | 15% | 0.225% |
| +4 | 10% | 0.0225% |
| +5 | 10% | 0.00225% |
| +6 | 8% | 0.00018% |
| +7 | 8% | 0.0000144% |
| +8 | 5% | 0.00000072% |
| +9 | 3% | 0.0000000216% |
| +10+ | 1% | ~0% |

```cpp
switch (iValue) {
case 0: iProb = 30; break;
case 1: iProb = 25; break;
case 2: iProb = 20; break;
case 3: iProb = 15; break;
case 4: iProb = 10; break;
case 5: iProb = 10; break;
case 6: iProb =  8; break;
case 7: iProb =  8; break;
case 8: iProb =  5; break;
case 9: iProb =  3; break;
default: iProb = 1; break;
}
```

#### Custom Item Bonus

Custom-made items (crafted items) get bonus success chance based on quality:

```cpp
if (((m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_dwAttribute & 0x00000001) != NULL) &&
    (m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_sItemSpecEffectValue2 > 100)) {

    if (iProb > 20)
        iProb += (quality / 10);
    else if (iProb > 7)
        iProb += (quality / 20);
    else
        iProb += (quality / 40);
}
```

#### Bonus Mode

When `bBonus == TRUE`, probability is doubled:

```cpp
if (bBonus == TRUE) iProb *= 2;
```

#### Final Roll

```cpp
iProb *= 100;  // Scale to 10000
iResult = iDice(1, 10000);

if (iProb >= iResult) {
    _bItemLog(DEF_ITEMLOG_UPGRADESUCCESS, iClientH, (int)-1, item);
    return TRUE;
}
return FALSE;
```

### Upgrade Level Caps

| Item Type | Custom-Made Cap | Normal Cap |
|-----------|-----------------|------------|
| Weapons | +10 | +7 |
| Shields | +10 | +7 |

### Gizon Item Upgrade Points

Special system using "Gizon Points" (`m_iGizonItemUpgradeLeft`) for upgrading hero items.

#### Cost Formula

```cpp
sItemUpgrade = (iValue * (iValue + 6) / 8) + 2;
```

| Level | Cost |
|-------|------|
| +0 to +1 | 2 |
| +1 to +2 | 3 |
| +2 to +3 | 4 |
| +3 to +4 | 5 |
| +4 to +5 | 7 |
| +5 to +6 | 9 |
| ... | ... |

#### Special Hero Items

Items 703, 709, 718, 727, 736, 737, 745 (various Dark Knight weapons) can be upgraded using Gizon points and transform into enhanced versions.

---

## Item Repair System

### Repair Handler

**Function**: `CGame::ReqRepairItemHandler`
**Location**: Game.cpp, line ~30973

```cpp
void CGame::ReqRepairItemHandler(int iClientH, char cItemID, char cRepairWhom, char * pString)
```

### Repair Pricing

Repair cost is calculated based on remaining durability:

```cpp
if (sRemainLife == 0) {
    // Completely broken: 50% of original price
    sPrice = m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2;
}
else {
    d1 = (double)sRemainLife;
    d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wMaxLifeSpan;
    d3 = (d1 / d2) * 0.5f;
    d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice;
    d3 = d3 * d2;

    sPrice = (original_price / 2) - (short)d3;
}
```

### NPC Repair Restrictions

| Item Category | Who Can Repair |
|---------------|----------------|
| Weapons (1-10) | Blacksmith (NPC 24) |
| Fishing/Tools (43-50) | Shop Owner (NPC 15) |
| Clothing (11-12) | Shop Owner (NPC 15) |

---

## Network Messages

### Request Messages (Client to Server)

| Message Type | Hex | Description |
|--------------|-----|-------------|
| `DEF_COMMONTYPE_BUILDITEM` | 0x0A23 | Request item manufacturing |
| `DEF_COMMONTYPE_REQ_CREATEPORTION` | 0x0A19 | Request potion creation |

### Configuration Messages (Log Server)

| Message ID | Hex | Description |
|------------|-----|-------------|
| `MSGID_BUILDITEMCONFIGURATIONCONTENTS` | 0x0FA40002 | BuildItem.cfg contents |
| `MSGID_PORTIONCONFIGURATIONCONTENTS` | 0x0FA314DE | Portion.cfg contents |

### Notification Messages (Server to Client)

| Message Type | Hex | Description |
|--------------|-----|-------------|
| `DEF_NOTIFY_BUILDITEMSUCCESS` | 0x0B70 | Item crafted successfully |
| `DEF_NOTIFY_BUILDITEMFAIL` | 0x0B71 | Item crafting failed |
| `DEF_NOTIFY_PORTIONSUCCESS` | 0x0B56 | Potion created successfully |
| `DEF_NOTIFY_PORTIONFAIL` | 0x0B55 | Potion creation failed |
| `DEF_NOTIFY_NOMATCHINGPORTION` | 0x0B53 | No recipe matches ingredients |
| `DEF_NOTIFY_LOWPORTIONSKILL` | 0x0B54 | Alchemy skill too low |
| `DEF_NOTIFY_ITEMUPGRADEFAIL` | 0x0BA8 | Item upgrade failed |
| `DEF_NOTIFY_GIZONITEMUPGRADELEFT` | 0x0BA4 | Gizon points remaining |

---

## Configuration File Formats

### BuildItem.cfg Format

```
BuildItem = <ItemName>
            <SkillLimit>
            <Material1_ID> <Material1_Count> <Material1_Value>
            <Material2_ID> <Material2_Count> <Material2_Value>
            <Material3_ID> <Material3_Count> <Material3_Value>
            <Material4_ID> <Material4_Count> <Material4_Value>
            <Material5_ID> <Material5_Count> <Material5_Value>
            <Material6_ID> <Material6_Count> <Material6_Value>
            <AverageValue>
            <MaxSkill>
            <AttributeFlags>
```

**Example**:
```
BuildItem = IronSword
            30
            100 5 10
            101 3 15
            0 0 0
            0 0 0
            0 0 0
            0 0 0
            50
            60
            0
```

**Field Descriptions**:
- `ItemName`: Name matching Item.cfg entry
- `SkillLimit`: Minimum Manufacturing skill (0-100)
- `Material_ID`: Item ID from Item.cfg (0 = empty slot)
- `Material_Count`: Quantity required
- `Material_Value`: Quality weight contribution
- `AverageValue`: Expected quality baseline (0-100)
- `MaxSkill`: Skill cap for XP gain
- `AttributeFlags`: 16-bit attribute flags

### Portion.cfg Format

```
potion = <RecipeNumber> <PotionName>
         <Item1_ID> <Item1_Count>
         <Item2_ID> <Item2_Count>
         <Item3_ID> <Item3_Count>
         <Item4_ID> <Item4_Count>
         <Item5_ID> <Item5_Count>
         <Item6_ID> <Item6_Count>
         <SkillLimit>
         <Difficulty>
```

**Example**:
```
potion = 1 RedPotion
         200 2
         201 1
         -1 0
         -1 0
         -1 0
         -1 0
         10
         5
```

**Field Descriptions**:
- `RecipeNumber`: Index in potion array (0-499)
- `PotionName`: Name matching Item.cfg entry
- `Item_ID`: Ingredient item ID (-1 = empty slot)
- `Item_Count`: Quantity required (0 = not used)
- `SkillLimit`: Minimum Alchemy skill
- `Difficulty`: Subtracted from skill for success roll

---

## Related Functions Reference

### Core Crafting Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `BuildItemHandler` | Game.cpp:39003 | Handle manufacturing requests |
| `_bDecodeBuildItemConfigFileContents` | Game.cpp:38633 | Load BuildItem.cfg |
| `ReqCreatePortionHandler` | Game.cpp:34845 | Handle potion creation requests |
| `_bDecodePortionConfigFileContents` | Game.cpp:35114 | Load Portion.cfg |

### Upgrade Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `RequestItemUpgradeHandler` | Game.cpp:50409 | Handle item upgrade requests |
| `bCheckIsItemUpgradeSuccess` | Game.cpp:48808 | Calculate upgrade success |

### Repair Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `ReqRepairItemHandler` | Game.cpp:30973 | Calculate repair price |
| `ReqRepairItemCofirmHandler` | Game.cpp:31052 | Execute repair |

### Support Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `_bInitItemAttr` | Game.cpp:8262 | Initialize item from template |
| `CalculateSSN_SkillIndex` | Game.cpp | Skill experience calculation |
| `SetItemCount` | Game.cpp | Modify item stack count |
| `ItemDepleteHandler` | Game.cpp | Remove item from inventory |
| `bAddItem` | Game.cpp | Add item to inventory |
| `GetExp` | Game.cpp | Grant character experience |

### Skill Functions

| Function | Location | Purpose |
|----------|----------|---------|
| `SkillCheck` | Game.cpp:54696 | Validate skill vs stat limits |

---

## Item Attribute Bit Layout

The `m_dwAttribute` field stores various item properties:

```
Bit Layout: aaaa bbbb cccc dddd eeee ffff xxxx xxx1

Bits 31-28 (a): Upgrade level (0-15)
Bits 27-24 (b): Item attribute type
Bits 23-20 (c): Special weapon effect flag
Bits 19-16 (d): Special weapon effect value
Bits 15-12 (e): Additional effect flag
Bits 11-8  (f): Additional effect value
Bits 7-1   (x): Reserved
Bit  0     (1): Custom-Made item flag
```

### Reading Upgrade Level

```cpp
int iValue = (item->m_dwAttribute & 0xF0000000) >> 28;
```

### Setting Upgrade Level

```cpp
dwTemp = item->m_dwAttribute;
dwTemp = dwTemp & 0x0FFFFFFF;  // Clear bits
item->m_dwAttribute = dwTemp | (iValue << 28);
```

### Checking Custom-Made Flag

```cpp
if ((item->m_dwAttribute & 0x00000001) != 0) {
    // This is a custom-made item
}
```

---

## Summary

The legacy crafting system is a complex but well-structured system that provides:

1. **Item Manufacturing**: Skill-based crafting with quality variance based on material quality
2. **Potion Mixing**: Exact recipe matching with difficulty-adjusted success rates
3. **Item Upgrading**: Progressive enhancement with exponentially decreasing success rates

Key design patterns:
- Recipe data loaded from config files at startup
- Success rates tied directly to skill levels
- Quality of crafted items influenced by material quality
- Custom items marked with special flag for tracking
- Upgrade levels stored in item attribute bits

The system integrates tightly with the inventory system (item consumption/addition) and the skill system (experience gain and level checks).
