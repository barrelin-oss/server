# System #27: Economy & Gold System

## Table of Contents

1. [Overview](#overview)
2. [Gold as an Item](#gold-as-an-item)
   - [Gold Item Properties](#gold-item-properties)
   - [Gold Weight Calculation](#gold-weight-calculation)
3. [Gold Functions](#gold-functions)
   - [dwGetItemCount](#dwgetitemcount)
   - [SetItemCount](#setitemcount)
4. [Reward Gold System](#reward-gold-system)
   - [PK Capture Rewards](#pk-capture-rewards)
   - [Enemy Kill Rewards](#enemy-kill-rewards)
   - [Collecting Reward Gold](#collecting-reward-gold)
5. [Shop Transactions](#shop-transactions)
   - [Buying Items](#buying-items)
   - [Selling Items](#selling-items)
   - [Charisma Discount](#charisma-discount)
6. [Item Repair Costs](#item-repair-costs)
7. [Magic Learning Costs](#magic-learning-costs)
8. [Service Fees](#service-fees)
   - [Arena Reservation](#arena-reservation)
   - [Guild Summoning](#guild-summoning)
9. [NPC Gold Drops](#npc-gold-drops)
   - [Gold Drop Configuration](#gold-drop-configuration)
   - [Gold Drop Calculation](#gold-drop-calculation)
   - [Gold Bonus Equipment](#gold-bonus-equipment)
10. [City Economic Tracking](#city-economic-tracking)
11. [Player-to-Player Trading](#player-to-player-trading)
12. [Quest Rewards](#quest-rewards)
13. [Special Gold Items](#special-gold-items)
14. [Starter Gold](#starter-gold)
15. [Constants Reference](#constants-reference)
16. [Related Functions](#related-functions)

---

## Overview

The Helbreath economy system uses Gold as the primary currency. Gold is implemented as a stackable item (ID 90) that players carry in their inventory. The system manages various economic interactions including NPC shops, item repairs, service fees, player trading, and war rewards.

**Key Characteristics:**
- Gold is a physical item with weight (reduced by 1/20th for gold specifically)
- Maximum gold per stack: Limited by `DWORD` (4,294,967,295)
- Maximum reward gold: 99,999,999 (`DEF_MAXREWARDGOLD`)
- City treasuries track economic activity per faction

**Primary Files:**
- `Game.cpp` - All gold handling functions
- `Client.h` - Player gold-related fields
- `Game.h` - Constants and definitions

---

## Gold as an Item

### Gold Item Properties

Gold is item ID 90 and behaves like other stackable consumables:

```cpp
// Game.cpp:24806-24807 - Creating gold item
ZeroMemory(cItemName, sizeof(cItemName));
wsprintf(cItemName, "Gold");
_bInitItemAttr(pItem, cItemName);
```

**Item Attributes:**
| Property | Value |
|----------|-------|
| Item ID | 90 |
| Item Name | "Gold" |
| Item Type | Stackable |
| m_dwCount | Amount of gold |

### Gold Weight Calculation

Gold has special weight handling - its weight is divided by 20 to allow carrying large amounts:

```cpp
// Game.cpp:41112-41124
int CGame::iGetItemWeight(CItem *pItem, int iCount)
{
    int iWeight;

    // Calculate weight based on quantity. For Gold, divide weight by 20
    iWeight = (pItem->m_wWeight);
    if (iCount < 0) iCount = 1;
    iWeight = iWeight * iCount;
    if (pItem->m_sIDnum == 90) iWeight = iWeight / 20;  // Gold special case
    if (iWeight <= 0) iWeight = 1;

    return iWeight;
}
```

This means 20 gold weighs the same as 1 unit of a normal item with the same base weight.

---

## Gold Functions

### dwGetItemCount

Retrieves the amount of a named item (typically Gold) the player has:

```cpp
// Game.cpp:15341-15358
DWORD CGame::dwGetItemCount(int iClientH, char * pName)
{
    register int i;
    char cTmpName[21];

    if (m_pClientList[iClientH] == NULL) return NULL;

    ZeroMemory(cTmpName, sizeof(cTmpName));
    strcpy(cTmpName, pName);

    for (i = 0; i < DEF_MAXITEMS; i++)
    if ((m_pClientList[iClientH]->m_pItemList[i] != NULL) &&
        (memcmp(m_pClientList[iClientH]->m_pItemList[i]->m_cName, cTmpName, 20) == 0)) {
        return m_pClientList[iClientH]->m_pItemList[i]->m_dwCount;
    }

    return 0;
}
```

**Usage:**
```cpp
dwGoldCount = dwGetItemCount(iClientH, "Gold");
```

### SetItemCount

Sets the count of a named item. If count reaches 0, the item is deleted:

```cpp
// Game.cpp:15360-15390
int CGame::SetItemCount(int iClientH, char * pItemName, DWORD dwCount)
{
    register int i;
    char cTmpName[21];
    WORD wWeight;

    if (m_pClientList[iClientH] == NULL) return -1;

    ZeroMemory(cTmpName, sizeof(cTmpName));
    strcpy(cTmpName, pItemName);

    for (i = 0; i < DEF_MAXITEMS; i++)
    if ((m_pClientList[iClientH]->m_pItemList[i] != NULL) &&
        (memcmp(m_pClientList[iClientH]->m_pItemList[i]->m_cName, cTmpName, 20) == 0)) {

        wWeight = iGetItemWeight(m_pClientList[iClientH]->m_pItemList[i], 1);

        // If count is 0, delete the item from inventory
        if (dwCount == 0) {
            ItemDepleteHandler(iClientH, i, FALSE);
        }
        else {
            // Update item count and notify client
            m_pClientList[iClientH]->m_pItemList[i]->m_dwCount = dwCount;
            SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SETITEMCOUNT, i, dwCount, (char)TRUE, NULL);
        }

        return wWeight;
    }

    return -1;
}
```

**Usage:**
```cpp
// Deduct gold
SetItemCount(iClientH, "Gold", dwGoldCount - iCost);
```

There's also an overloaded version that takes an item index instead of name:

```cpp
// Game.cpp:15393-15413
int CGame::SetItemCount(int iClientH, int iItemIndex, DWORD dwCount)
```

---

## Reward Gold System

Reward Gold (`m_iRewardGold`) is a separate tracking system for gold earned through PvP combat. This gold is stored on the character but must be "collected" to become actual gold items.

### PK Capture Rewards

When a player kills a PK (player killer), they earn reward gold based on the PK's level:

```cpp
// Game.cpp:24401-24413 - PKCapture reward
if (m_pClientList[sAttackerH]->m_iPKCount != 0) {
    // PK killing a PK gets nothing
}
else {
    // Reward gold accumulates (no experience for killing PKs)
    m_pClientList[sAttackerH]->m_iRewardGold += iGetExpLevel(m_pClientList[sVictumH]->m_iExp) * 3;

    if (m_pClientList[sAttackerH]->m_iRewardGold > DEF_MAXREWARDGOLD)
        m_pClientList[sAttackerH]->m_iRewardGold = DEF_MAXREWARDGOLD;
    if (m_pClientList[sAttackerH]->m_iRewardGold < 0)
        m_pClientList[sAttackerH]->m_iRewardGold = 0;

    SendNotifyMsg(NULL, sAttackerH, DEF_NOTIFY_PKCAPTURED, ...);
}
```

**Reward Formula:** `victim_level * 3` gold per PK kill

### Enemy Kill Rewards

When killing enemy faction players, reward gold is based on the victim's level:

```cpp
// Game.cpp:24467-24471
m_pClientList[iAttackerH]->m_iRewardGold += iDice(1, (iGetExpLevel(m_pClientList[iClientH]->m_iExp)));
if (m_pClientList[iAttackerH]->m_iRewardGold > DEF_MAXREWARDGOLD)
    m_pClientList[iAttackerH]->m_iRewardGold = DEF_MAXREWARDGOLD;
if (m_pClientList[iAttackerH]->m_iRewardGold < 0)
    m_pClientList[iAttackerH]->m_iRewardGold = 0;
```

**Reward Formula:** `iDice(1, victim_level)` gold per enemy kill (1 to victim_level random)

### Collecting Reward Gold

Players collect accumulated reward gold through `GetRewardMoneyHandler`:

```cpp
// Game.cpp:24783-24907
void CGame::GetRewardMoneyHandler(int iClientH)
{
    int iRet, iEraseReq, iWeightLeft, iRewardGoldLeft;
    // ...

    // Calculate remaining carry capacity
    iWeightLeft = _iCalcMaxLoad(iClientH) - iCalcTotalWeight(iClientH);
    if (iWeightLeft <= 0) return;

    // Reserve half capacity for buying items later
    iWeightLeft = iWeightLeft / 2;
    if (iWeightLeft <= 0) return;

    pItem = new class CItem;
    wsprintf(cItemName, "Gold");
    _bInitItemAttr(pItem, cItemName);

    // Calculate maximum gold that can be collected based on weight
    if ((iWeightLeft / iGetItemWeight(pItem, 1)) >= m_pClientList[iClientH]->m_iRewardGold) {
        // Can collect all reward gold
        pItem->m_dwCount = m_pClientList[iClientH]->m_iRewardGold;
        iRewardGoldLeft = 0;
    }
    else {
        // Collect what weight allows
        pItem->m_dwCount = (iWeightLeft / iGetItemWeight(pItem, 1));
        iRewardGoldLeft = m_pClientList[iClientH]->m_iRewardGold - pItem->m_dwCount;
    }

    if (_bAddClientItemList(iClientH, pItem, &iEraseReq) == TRUE) {
        m_pClientList[iClientH]->m_iRewardGold = iRewardGoldLeft;
        // Send item obtained notification...
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_REWARDGOLD, NULL, NULL, NULL, NULL);
    }
    // ...
}
```

**Key Points:**
- Player can only collect up to half their remaining weight capacity
- Gold not collected remains in reward pool
- Weight-limited collection prevents inventory overflow

---

## Shop Transactions

### Buying Items

When buying from NPC shops, the cost is calculated and gold is deducted:

```cpp
// Game.cpp:13036-13150
// Calculate cost
pItem->m_dwCount = dwItemCount;
iCost = pItem->m_wPrice * pItem->m_dwCount;

// Get player's gold
dwGoldCount = dwGetItemCount(iClientH, "Gold");

// Calculate charisma-based discount
iDiscountRatio = ((m_pClientList[iClientH]->m_iCharisma - 10) / 4);
// ...

// Check if player has enough gold
if (dwGoldCount < (DWORD)(iCost - iDiscountCost)) {
    // Not enough gold
    *wp = DEF_NOTIFY_NOTENOUGHGOLD;
    return;
}

// Deduct gold after successful purchase
iGoldWeight = SetItemCount(iClientH, "Gold", dwGoldCount - wTempPrice);

// Add to city funds
m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += wTempPrice;
```

### Selling Items

When selling to NPCs, players receive half the base price with various adjustments:

```cpp
// Game.cpp:30505-30512 - Basic sell price calculation
iPrice = (m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2) * iNum;

// Neutral players get half price
if (bNeutral == TRUE) iPrice = iPrice / 2;

// Minimum and maximum caps
if (iPrice <= 0) iPrice = 1;
if (iPrice > 1000000) iPrice = 1000000;
```

**Enhanced Items:**
Items with special attributes (custom, upgraded) get bonus value:

```cpp
// Game.cpp:30560-30631 - Enhanced item calculation
d1 = (double)iPrice * dwMul1;  // Base modifier
// Additional price for special attributes...
iPrice = iPrice + (dwAddPrice1 - (dwAddPrice1/3)) + (dwAddPrice2 - (dwAddPrice2/3));
```

**Gold Creation on Sale:**
When selling items, gold is created and added to inventory:

```cpp
// Game.cpp:30847-30854
pItemGold = new class CItem;
wsprintf(cItemName, "Gold");
_bInitItemAttr(pItemGold, cItemName);
pItemGold->m_dwCount = iPrice;

if (_bAddClientItemList(iClientH, pItemGold, &iEraseReq) == TRUE) {
    // Gold added successfully
}
```

### Charisma Discount

Player's Charisma stat affects shop prices:

```cpp
// Game.cpp:13043-13056
iDiscountRatio = ((m_pClientList[iClientH]->m_iCharisma - 10) / 4);
dTmp1 = (double)iCost;
dTmp2 = (dTmp1 * (iDiscountRatio)) / 100;
iDiscountCost = (int)dTmp2;

// Maximum discount is half price
if (iDiscountCost >= (iCost/2)) iDiscountCost = (iCost/2) - 1;
```

**Discount Formula:** `(CHR - 10) / 4` percent discount, up to 50% maximum

---

## Item Repair Costs

Repair cost is based on item's base price and current durability:

```cpp
// Game.cpp:31001-31012 - Repair price calculation
// Base repair cost is half the item's price
sPrice = m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2;

// For items with durability
d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice;
// Adjusted based on remaining lifespan...
sPrice = (m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2) - (short)d3;
```

When repair is executed:

```cpp
// Game.cpp:31131-31137
// Deduct gold
iGoldWeight = SetItemCount(iClientH, "Gold", dwGoldCount - sPrice);

// Add to city funds
m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += sPrice;
```

---

## Magic Learning Costs

Each spell has a gold cost defined in the magic configuration:

```cpp
// Game.cpp:20851-20858 - Loading magic gold cost
// m_iGoldCost
m_pMagicConfigList[iMagicConfigListIndex]->m_iGoldCost = atoi(token);
```

When learning magic:

```cpp
// Game.cpp:21092-21124
dwGoldCount = dwGetItemCount(iClientH, "Gold");

// Check if spell is purchasable (negative cost = not for sale)
if (m_pMagicConfigList[iRet]->m_iGoldCost < 0) bMagic = FALSE;

// Check if player has enough gold
if ((DWORD)iCost > dwGoldCount) bMagic = FALSE;

// Deduct gold if purchasing
if (bIsPurchase == TRUE) SetItemCount(iClientH, "Gold", dwGoldCount - iCost);
```

---

## Service Fees

### Arena Reservation

Reserving an arena (fightzone) costs 1500 gold:

```cpp
// Game.cpp:23843-23864
dwGoldCount = dwGetItemCount(iClientH, "Gold");

if (dwGoldCount < 1500) {
    // Player doesn't have enough gold for arena entry fee
    wResult = DEF_MSGTYPE_REJECT;
    iResult = -2;  // -2 indicates insufficient gold
}
else {
    // Reserve successful
    wResult = DEF_MSGTYPE_CONFIRM;

    // Deduct arena reservation fee
    SetItemCount(iClientH, "Gold", dwGoldCount - 1500);
    iCalcTotalWeight(iClientH);

    // Record reservation...
}
```

**Fixed Cost:** 1500 gold per arena reservation

### Guild Summoning

Guild summoning cost is configurable in server settings:

```cpp
// Game.cpp:5182-5183 - Loading summon guild cost
m_iSummonGuildCost = atoi(token);
wsprintf(cTxt, "(*) Summoning guild costs (%d) gold", m_iSummonGuildCost);
```

When summoning guild:

```cpp
// Game.cpp:48381-48389
dwGoldCount = dwGetItemCount(iClientH, "Gold");

// Check if player has enough gold
if (m_iSummonGuildCost > dwGoldCount) {
    return;
}
else {
    // Deduct gold
    SetItemCount(iClientH, "Gold", dwGoldCount - m_iSummonGuildCost);
}
```

**Alternative Cost (Non-Crusade):**
```cpp
// Game.cpp:9267-9268
if (dwGetItemCount(iClientH, "Gold") >= 100000) {
    SetItemCount(iClientH, "Gold", dwGetItemCount(iClientH, "Gold") - 50000);
    // Allow guild summon...
}
```

---

## NPC Gold Drops

### Gold Drop Configuration

Each NPC type has configurable gold drop ranges in NPC.cfg:

```cpp
// Game.cpp:16047-16066 - Loading NPC gold dice values
case 9:
    // m_iGoldDiceMin
    m_pNpcConfigList[iNpcConfigListIndex]->m_iGoldDiceMin = atoi(token);
    cReadModeB = 10;
    break;

case 10:
    // m_iGoldDiceMax
    m_pNpcConfigList[iNpcConfigListIndex]->m_iGoldDiceMax = atoi(token);
    cReadModeB = 11;
    break;
```

Values are copied to spawned NPCs:

```cpp
// Game.cpp:16358-16359
pNpc->m_iGoldDiceMin = m_pNpcConfigList[i]->m_iGoldDiceMin;
pNpc->m_iGoldDiceMax = m_pNpcConfigList[i]->m_iGoldDiceMax;
```

### Gold Drop Calculation

When an NPC dies, gold drop is calculated:

```cpp
// Game.cpp:46718-46763
void CGame::NpcDeadItemGenerator(int iNpcH, short sAttackerH, char cAttackerType)
{
    // ...

    // Some NPCs don't drop gold
    switch (m_pNpcList[iNpcH]->m_sType) {
    case 21: // Guard
    case 34: // Dummy
    case 64: // Crop
        return;
    }

    // 35% chance to drop, 60% of drops are gold
    if (iDice(1,10000) >= m_iPrimaryDropRate) {
        if (iDice(1,10000) <= 6000) {
            iItemID = 90; // Gold

            pItem = new class CItem;
            _bInitItemAttr(pItem, iItemID);

            // Calculate gold amount from dice range
            pItem->m_dwCount = (DWORD)(iDice(1, (m_pNpcList[iNpcH]->m_iGoldDiceMax -
                                                  m_pNpcList[iNpcH]->m_iGoldDiceMin)) +
                                       m_pNpcList[iNpcH]->m_iGoldDiceMin);
            // ...
        }
    }
}
```

**Gold Amount Formula:** `iDice(1, (max - min)) + min`
- Effectively: random value between `min` and `max`

### Gold Bonus Equipment

Players can have equipment that grants bonus gold from drops (`m_iAddGold`):

```cpp
// Game.cpp:46757-46763
// v1.42 Gold bonus
if ((cAttackerType == DEF_OWNERTYPE_PLAYER) && (m_pClientList[sAttackerH]->m_iAddGold != NULL)) {
    dTmp1 = (double)m_pClientList[sAttackerH]->m_iAddGold;
    dTmp2 = (double)pItem->m_dwCount;
    dTmp3 = (dTmp1/100.0f) * dTmp2;
    pItem->m_dwCount += (int)dTmp3;
}
```

**Bonus Formula:** `base_gold + (base_gold * m_iAddGold / 100)`

This bonus is set from equipment special effects:

```cpp
// Game.cpp:32017 - Effect type 12 adds gold bonus
case 12: m_pClientList[iClientH]->m_iAddGold += (int)dwSWEValue * 10; break;
```

---

## City Economic Tracking

Each faction (Aresden/Elvine) has economic tracking:

```cpp
// Game.cpp:242-248 - City status initialization
m_stCityStatus[1].iCrimes = 0;
m_stCityStatus[1].iFunds  = 0;
m_stCityStatus[1].iWins   = 0;

m_stCityStatus[2].iCrimes = 0;
m_stCityStatus[2].iFunds  = 0;
m_stCityStatus[2].iWins   = 0;
```

**Fund Sources:**
- Shop purchases add to city funds
- Item repairs add to city funds

```cpp
// Game.cpp:13155 - Shop purchase adds to city funds
m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += wTempPrice;

// Game.cpp:31137 - Repair cost adds to city funds
m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += sPrice;
```

---

## Player-to-Player Trading

Players can trade items (including gold) directly through the exchange system:

### Exchange Mode

```cpp
// Client.h - Exchange-related fields
BOOL  m_bIsExchangeMode;
int   m_iExchangeH;  // Exchange partner handle
char  m_cExchangeItemIndex[8];
int   m_iExchangeItemAmount[8];
char  m_cExchangeItemName[8][21];
```

### Initiating Trade

```cpp
// Game.cpp:37000-37064
void CGame::ExchangeItemHandler(int iClientH, short sItemIndex, int iAmount, ...)
{
    if (m_pClientList[iClientH]->m_bIsExchangeMode == TRUE) return;

    // Check if target player can trade
    if ((m_pClientList[sOwnerH]->m_bIsExchangeMode == TRUE) || ...) {
        // Target busy
        return;
    }

    // Initialize exchange mode for both players
    m_pClientList[iClientH]->m_bIsExchangeMode = TRUE;
    m_pClientList[sOwnerH]->m_bIsExchangeMode = TRUE;

    // Clear exchange item arrays...
    // Set initial exchange item...
}
```

### Adding Items to Trade

```cpp
// Game.cpp:37093-37140
void CGame::SetExchangeItem(int iClientH, int iItemIndex, int iAmount)
{
    if ((m_pClientList[iClientH]->m_bIsExchangeMode == TRUE) &&
        (m_pClientList[iClientH]->m_iExchangeH != NULL)) {

        // Add item to exchange list
        m_pClientList[iClientH]->m_cExchangeItemIndex[iExchangeCount] = (char)iItemIndex;
        m_pClientList[iClientH]->m_iExchangeItemAmount[iExchangeCount] = iAmount;
        memcpy(m_pClientList[iClientH]->m_cExchangeItemName[iExchangeCount],
               m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_cName, 20);
        // ...
    }
}
```

### Confirming Trade

```cpp
// Game.cpp:37163-37300
void CGame::ConfirmExchangeItem(int iClientH)
{
    // Validate both players still in exchange mode
    // Validate all items still exist and match names
    // Check weight capacity for both players
    // Transfer items between players
    // Clear exchange mode
}
```

### Canceling Trade

```cpp
// Game.cpp:46326-46334
void CGame::CancelExchangeItem(int iClientH)
{
    int iExH;

    // Clear both players' exchange status
    iExH = m_pClientList[iClientH]->m_iExchangeH;
    _ClearExchangeStatus(iExH);
    _ClearExchangeStatus(iClientH);
}
```

---

## Quest Rewards

Quests can reward items including gold:

```cpp
// Client.h - Quest reward fields
int  m_iQuestRewardType;    // Item type for reward
int  m_iQuestRewardAmount;  // Amount of reward

// Game.cpp:37517-37526 - Quest reward handling
if ((m_pClientList[iClientH]->m_iQuestRewardType > 0) &&
    (m_pItemConfigList[m_pClientList[iClientH]->m_iQuestRewardType] != NULL)) {
    pItem = new class CItem;
    _bInitItemAttr(pItem, m_pItemConfigList[m_pClientList[iClientH]->m_iQuestRewardType]->m_cName);
    pItem->m_dwCount = m_pClientList[iClientH]->m_iQuestRewardAmount;
    // Give item to player...
}
```

---

## Special Gold Items

Special gold-containing items that drop rarely:

```cpp
// Game.cpp:46657-46659 - Bag of Gold drops
case 1: if (iDice(1,(2 * fProbC)) == 2) iItemID = 740; break; // BagOfGold-medium
case 2: if (iDice(1,(2 * fProbC)) == 2) iItemID = 741; break; // BagOfGold-large
case 3: if (iDice(1,(2 * fProbC)) == 2) iItemID = 742; break; // BagOfGold-largest
```

| Item ID | Name | Description |
|---------|------|-------------|
| 90 | Gold | Basic gold currency |
| 740 | BagOfGold-medium | Rare drop, medium gold amount |
| 741 | BagOfGold-large | Rare drop, large gold amount |
| 742 | BagOfGold-largest | Rare drop, largest gold amount |

---

## Starter Gold

New characters receive starter gold based on their level:

```cpp
// Game.cpp:32864-32878
// Beginner gold. Levels 1-5 get 100 gold
if (_bInitItemAttr(pItem, "Gold") == FALSE) {
    // ...
}
pItem->m_dwCount = 100;

// Beginner gold. Levels 5-20 get 300 gold
if (_bInitItemAttr(pItem, "Gold") == FALSE) {
    // ...
}
pItem->m_dwCount = 300;
```

| Level Range | Starter Gold |
|-------------|--------------|
| 1-5 | 100 |
| 5-20 | 300 |

---

## Constants Reference

### Maximum Values

```cpp
// Game.h:91
#define DEF_MAXREWARDGOLD    99999999   // Maximum reward gold
```

### Notification Types

| Constant | Purpose |
|----------|---------|
| `DEF_NOTIFY_NOTENOUGHGOLD` | Insufficient gold for purchase |
| `DEF_NOTIFY_REWARDGOLD` | Reward gold update notification |
| `DEF_NOTIFY_SETITEMCOUNT` | Item count changed |
| `DEF_NOTIFY_ITEMOBTAINED` | Item received notification |
| `DEF_NOTIFY_SELLITEMPRICE` | Item sell price notification |
| `DEF_NOTIFY_REPAIRITEMPRICE` | Repair cost notification |

### Fixed Costs

| Service | Cost |
|---------|------|
| Arena Reservation | 1500 gold |
| Guild Summon (Non-Crusade) | 50,000 gold |
| Guild Summon (Crusade) | Configurable (`m_iSummonGuildCost`) |

---

## Related Functions

### Gold Management

| Function | Purpose |
|----------|---------|
| `dwGetItemCount()` | Get count of named item (Gold) |
| `SetItemCount()` | Set item count (deduct/add gold) |
| `iGetItemWeight()` | Calculate item weight (special for gold) |
| `iCalcTotalWeight()` | Calculate total inventory weight |
| `_iCalcMaxLoad()` | Calculate maximum carry capacity |

### Transaction Handlers

| Function | Purpose |
|----------|---------|
| `GetRewardMoneyHandler()` | Collect accumulated reward gold |
| `NpcDeadItemGenerator()` | Handle NPC gold drops |
| `ExchangeItemHandler()` | Initiate player trade |
| `SetExchangeItem()` | Add item to trade |
| `ConfirmExchangeItem()` | Complete trade |
| `CancelExchangeItem()` | Cancel trade |
| `FightzoneReserveHandler()` | Arena reservation with gold cost |

### Shop Functions

| Function | Purpose |
|----------|---------|
| `RequestSellItemListHandler()` | Process item sale |
| `RequestItemRepairHandler()` | Process item repair |
| `GetMagicAbilityHandler()` | Learn magic (if purchasable) |

---

## Summary

The Helbreath economy system features:

1. **Gold as Physical Item**: Gold (ID 90) is a stackable item with special weight reduction (1/20th)

2. **Dual Gold Tracking**:
   - Inventory gold (`m_dwCount` of Gold item)
   - Reward gold (`m_iRewardGold` - earned from PvP)

3. **Shop Economy**:
   - Buy at full price (minus charisma discount)
   - Sell at half price (with modifiers for special items)
   - Repairs cost half item price

4. **NPC Drops**: Gold drops configured per NPC type with dice ranges

5. **City Funds**: Shop and repair transactions contribute to faction treasury

6. **Player Trading**: Direct item exchange including gold

7. **Service Fees**: Fixed costs for arena, guild services

8. **Bonus System**: Equipment can provide gold bonus percentage on drops
