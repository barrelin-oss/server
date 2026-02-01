# Legacy Trading and Shops System Documentation

## Table of Contents

1. [Overview](#overview)
2. [Data Structures](#data-structures)
3. [NPC Shop System](#npc-shop-system)
4. [Item Purchase System](#item-purchase-system)
5. [Item Sale System](#item-sale-system)
6. [Item Repair System](#item-repair-system)
7. [Player-to-Player Trading](#player-to-player-trading)
8. [Price Calculation](#price-calculation)
9. [Network Protocol](#network-protocol)
10. [Helper Functions](#helper-functions)
11. [Constants Reference](#constants-reference)
12. [Logging System](#logging-system)
13. [Flow Diagrams](#flow-diagrams)

---

## Overview

The Helbreath trading system consists of two major components:

1. **NPC Shop System** - Players can buy items from NPCs, sell items to NPCs, and repair equipment at NPCs
2. **Player-to-Player Trading** - Direct item exchange between two players with confirmation system

All trading functions are located in `Game.cpp` within the monolithic `CGame` class.

---

## Data Structures

### Client Exchange State (Client.h lines 229-238)

```cpp
// Player-to-Player Trading State
BOOL  m_bIsExchangeMode;            // Is In Exchange Mode?
int   m_iExchangeH;                 // Client ID to Exchanging with
char  m_cExchangeName[11];          // Name of Client to Exchanging with
char  m_cExchangeItemName[4][21];   // Name of Item to exchange (up to 4 items)
char  m_cExchangeItemIndex[4];      // ItemID to Exchange (up to 4 items)
int   m_iExchangeItemAmount[4];     // Amount to exchange with (up to 4 items)
BOOL  m_bIsExchangeConfirm;         // Has the user hit confirm?
int   iExchangeCount;               // Keeps track of items which are on list
```

### Item Structure (Item.h)

Key fields relevant to trading:

```cpp
class CItem {
    char  m_cName[21];              // Item name
    DWORD m_wPrice;                 // Base item price
    WORD  m_wCurLifeSpan;           // Current durability
    WORD  m_wMaxLifeSpan;           // Maximum durability
    char  m_cCategory;              // Item category (determines sellable NPC)
    BOOL  m_bIsForSale;             // Can be purchased from NPC shop
    DWORD m_dwCount;                // Stack count (for consumables)
    char  m_cItemType;              // Item type (consumable, equipment, etc.)
    DWORD m_dwAttribute;            // Special attributes (affects sell price)
};
```

### Item Categories

Item categories determine which NPC can buy/repair the item:

| Category Range | Type | Sellable To |
|---------------|------|-------------|
| 1-10 | Weapons/Equipment | Blacksmith (NPC 24) |
| 11-12 | Clothing/Boots | Shopkeeper (NPC 15), Blacksmith (NPC 24) |
| 11-50 | General goods | Shopkeeper (NPC 15) |
| 43-50 | Tools (fishing rod, pickaxe) | Shopkeeper (NPC 15) |

### Item Types

```cpp
#define DEF_ITEMTYPE_NONE        0
#define DEF_ITEMTYPE_EQUIP       1
#define DEF_ITEMTYPE_APPLY       2
#define DEF_ITEMTYPE_USE_DEPLETE 3
#define DEF_ITEMTYPE_INSTALL     4
#define DEF_ITEMTYPE_CONSUME     5   // Stackable consumables
#define DEF_ITEMTYPE_ARROW       6   // Stackable arrows
#define DEF_ITEMTYPE_EAT         7
#define DEF_ITEMTYPE_USE_SKILL   8
#define DEF_ITEMTYPE_USE_PERM    9
#define DEF_ITEMTYPE_MATERIAL    12
```

---

## NPC Shop System

### NPC Types for Trading

| NPC ID | Name | Functions |
|--------|------|-----------|
| 15 | ShopKeeper-W | Buy/Sell general goods, repair clothing |
| 24 | Blacksmith (Tom) | Buy/Sell weapons, repair equipment |

### Shop NPC Identification

In `Game.cpp` line 34429:
```cpp
else if (memcmp(token, "ShopKeeper", 10) == 0)
```

NPCs are identified by their type number in switch statements throughout the code.

### Location Restrictions

Players can only buy items in their home territory:
- Aresden players: Can buy in "aresden" or "arefarm" maps
- Elvine players: Can buy in "elvine" or "elvfarm" maps
- Neutral players: Can buy anywhere but pay half price

From `RequestPurchaseItemHandler` (lines 12983-12998):
```cpp
if (memcmp(m_pClientList[iClientH]->m_cLocation, "NONE", 4) != 0) {
    if (memcmp(m_pClientList[iClientH]->m_cLocation, "are", 3) == 0) {
        if ((memcmp(m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->m_cLocationName, "aresden", 7) == 0) ||
            (memcmp(m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->m_cLocationName, "arefarm", 7) == 0)) {
            // OK to purchase
        }
        else return;
    }
    // Similar for Elvine...
}
```

---

## Item Purchase System

### Function: RequestPurchaseItemHandler

**Location:** Game.cpp line 12966

**Signature:**
```cpp
void CGame::RequestPurchaseItemHandler(int iClientH, char * pItemName, int iNum)
```

**Parameters:**
- `iClientH` - Client handle requesting purchase
- `pItemName` - Name of item to purchase
- `iNum` - Number of items to purchase

**Process Flow:**

1. **Validation:**
   - Check client exists and is initialized
   - Verify player is in their home territory
   - Check `m_pIsProcessingAllowed` flag

2. **Special Item Handling:**
   - "10Arrows" -> Creates "Arrow" with count 10
   - "100Arrows" -> Creates "Arrow" with count 100

3. **Item Creation:**
   ```cpp
   pItem = new class CItem;
   if (_bInitItemAttr(pItem, cItemName) == FALSE) {
       delete pItem;
   }
   ```

4. **Purchase Restrictions:**
   - Item must have `m_bIsForSale == TRUE`

5. **Cost Calculation with Charisma Discount:**
   ```cpp
   iCost = pItem->m_wPrice * pItem->m_dwCount;

   iDiscountRatio = ((m_pClientList[iClientH]->m_iCharisma - 10) / 4);

   dTmp1 = (double)(iDiscountRatio);
   dTmp2 = dTmp1 / 100.0f;
   dTmp1 = (double)iCost;
   dTmp3 = dTmp1 * dTmp2;
   iDiscountCost = (int)dTmp3;

   // Discount capped at 50% of base cost
   if (iDiscountCost >= (iCost/2)) iDiscountCost = (iCost/2)-1;
   ```

6. **Gold Check:**
   ```cpp
   dwGoldCount = dwGetItemCount(iClientH, "Gold");
   if (dwGoldCount < (DWORD)(iCost - iDiscountCost)) {
       // Send DEF_NOTIFY_NOTENOUGHGOLD
       return;
   }
   ```

7. **Add Item to Inventory:**
   ```cpp
   if (_bAddClientItemList(iClientH, pItem, &iEraseReq) == TRUE) {
       // Send DEF_NOTIFY_ITEMPURCHASED with item details
       SetItemCount(iClientH, "Gold", dwGoldCount - wTempPrice);
       // Add to city funds
       m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += wTempPrice;
   }
   ```

### Purchase Response Packet (DEF_NOTIFY_ITEMPURCHASED)

Packet structure (48 bytes total):
```cpp
DWORD  MSGID_NOTIFY           // Message ID
WORD   DEF_NOTIFY_ITEMPURCHASED // 0x0B06
char   count                  // Always 1
char   itemName[20]           // Item name
DWORD  itemCount              // Stack count
char   itemType               // Item type
char   equipPos               // Equipment position
char   equipped               // Always 0 (not equipped)
short  levelLimit             // Level requirement
char   genderLimit            // Gender requirement
WORD   curLifeSpan            // Current durability
WORD   weight                 // Item weight
short  sprite                 // Sprite ID
short  spriteFrame            // Sprite frame
char   itemColor              // Item color
WORD   finalPrice             // Actual price paid
```

---

## Item Sale System

### Two-Phase Sale Process

Selling items uses a two-phase confirmation system:

1. **Phase 1 - Price Quote:** Player requests to sell, server calculates and returns price
2. **Phase 2 - Confirm Sale:** Player confirms, server completes transaction

### Function: ReqSellItemHandler (Phase 1)

**Location:** Game.cpp line 30466

**Signature:**
```cpp
void CGame::ReqSellItemHandler(int iClientH, char cItemID, char cSellToWhom, int iNum, char * pItemName)
```

**Parameters:**
- `iClientH` - Client handle
- `cItemID` - Item slot index (0-49)
- `cSellToWhom` - NPC type (15=ShopKeeper, 24=Blacksmith)
- `iNum` - Number of items to sell
- `pItemName` - Item name (for validation)

**Process:**

1. **Validation:**
   ```cpp
   if ((cItemID < 0) || (cItemID >= 50)) return;
   if (m_pClientList[iClientH]->m_pItemList[cItemID] == NULL) return;
   if (iNum <= 0) return;
   if (m_pClientList[iClientH]->m_pItemList[cItemID]->m_dwCount < iNum) return;
   ```

2. **Neutral Player Check:**
   ```cpp
   bNeutral = FALSE;
   if (memcmp(m_pClientList[iClientH]->m_cLocation, "NONE", 4) == 0) bNeutral = TRUE;
   ```

3. **Price Calculation by NPC Type:**

   **ShopKeeper (15) - General Goods (Category 11-50):**
   ```cpp
   // Simple half price
   iPrice = (m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2) * iNum;
   ```

   **Blacksmith (24) - Equipment (Category 1-10):**
   ```cpp
   // Price based on remaining durability
   sRemainLife = m_pClientList[iClientH]->m_pItemList[cItemID]->m_wCurLifeSpan;

   if (sRemainLife == 0) {
       // Cannot sell broken items
       SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_CANNOTSELLITEM, cItemID, 2, ...);
   }
   else {
       d1 = (double)sRemainLife;
       d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wMaxLifeSpan;
       d3 = (d1 / d2) * 0.5f;  // 50% of durability ratio
       d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice;
       d3 = d3 * d2;  // Prorated price
       iPrice = (int)d3 * iNum;

       // Add special attribute bonuses (see Price Calculation section)
   }
   ```

4. **Neutral Penalty:**
   ```cpp
   if (bNeutral == TRUE) iPrice = iPrice/2;  // 25% of base price for neutrals
   ```

5. **Price Limits:**
   ```cpp
   if (iPrice <= 0) iPrice = 1;
   if (iPrice > 1000000) iPrice = 1000000;
   ```

6. **Weight Check:**
   ```cpp
   if (m_pClientList[iClientH]->m_iCurWeightLoad + iGetItemWeight(m_pGold, iPrice) >
       (DWORD)_iCalcMaxLoad(iClientH)) {
       SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_CANNOTSELLITEM, cItemID, 4, ...);
   }
   ```

7. **Send Price Quote:**
   ```cpp
   SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_SELLITEMPRICE, cItemID, sRemainLife, iPrice, itemName, iNum);
   ```

### Function: ReqSellItemConfirmHandler (Phase 2)

**Location:** Game.cpp line 30651

**Signature:**
```cpp
void CGame::ReqSellItemConfirmHandler(int iClientH, char cItemID, int iNum, char * pString)
```

**Process:**

1. Recalculate price (same as Phase 1)
2. Send DEF_NOTIFY_ITEMSOLD to confirm
3. Log the sale:
   ```cpp
   _bItemLog(DEF_ITEMLOG_SELL, iClientH, (int) -1, m_pClientList[iClientH]->m_pItemList[cItemID]);
   ```

4. Remove item from inventory:
   - For consumables/arrows: Reduce count
     ```cpp
     SetItemCount(iClientH, cItemID, m_pClientList[iClientH]->m_pItemList[cItemID]->m_dwCount - iNum);
     ```
   - For equipment: Delete item
     ```cpp
     ItemDepleteHandler(iClientH, cItemID, FALSE);
     ```

5. Give gold to player:
   ```cpp
   pItemGold = new class CItem;
   _bInitItemAttr(pItemGold, "Gold");
   pItemGold->m_dwCount = iPrice;
   _bAddClientItemList(iClientH, pItemGold, &iEraseReq);
   ```

### Bulk Sale Handler: RequestSellItemListHandler

**Location:** Game.cpp line 40559

**Signature:**
```cpp
void CGame::RequestSellItemListHandler(int iClientH, char * pData)
```

Allows selling up to 12 items at once:

```cpp
struct {
    char cIndex;
    int  iAmount;
} stTemp[12];

for (i = 0; i < 12; i++) {
    // Read each item from packet
    // Call ReqSellItemConfirmHandler for each
    ReqSellItemConfirmHandler(iClientH, cIndex, iAmount, NULL);
}
```

---

## Item Repair System

### Two-Phase Repair Process

Similar to selling, repairs use confirmation:

1. **Phase 1 - Price Quote:** Request repair price
2. **Phase 2 - Confirm Repair:** Execute repair

### Function: ReqRepairItemHandler (Phase 1)

**Location:** Game.cpp line 30973

**Signature:**
```cpp
void CGame::ReqRepairItemHandler(int iClientH, char cItemID, char cRepairWhom, char * pString)
```

**NPC Restrictions:**
- Weapons (Category 1-10): Only Blacksmith (24) can repair
- Clothing/Tools (Category 11-12, 43-50): Only ShopKeeper (15) can repair

**Repair Cost Calculation:**

```cpp
sRemainLife = m_pClientList[iClientH]->m_pItemList[cItemID]->m_wCurLifeSpan;

if (sRemainLife == 0) {
    // Completely broken = half original price
    sPrice = m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2;
}
else {
    // Partial damage = prorated cost
    d1 = (double)sRemainLife;
    d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wMaxLifeSpan;
    d3 = (d1 / d2) * 0.5f;
    d2 = (double)m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice;
    d3 = d3 * d2;

    sPrice = (m_pClientList[iClientH]->m_pItemList[cItemID]->m_wPrice / 2) - (short)d3;
}

SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_REPAIRITEMPRICE, cItemID, sRemainLife, sPrice, itemName);
```

### Function: ReqRepairItemCofirmHandler (Phase 2)

**Location:** Game.cpp line 31052

**Signature:**
```cpp
void CGame::ReqRepairItemCofirmHandler(int iClientH, char cItemID, char * pString)
```

**Process:**

1. Recalculate repair cost
2. Check gold:
   ```cpp
   dwGoldCount = dwGetItemCount(iClientH, "Gold");
   if (dwGoldCount < (DWORD)sPrice) {
       // Send DEF_NOTIFY_NOTENOUGHGOLD
       return;
   }
   ```

3. Restore durability:
   ```cpp
   m_pClientList[iClientH]->m_pItemList[cItemID]->m_wCurLifeSpan =
       m_pClientList[iClientH]->m_pItemList[cItemID]->m_wMaxLifeSpan;
   ```

4. Notify client:
   ```cpp
   SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_ITEMREPAIRED, cItemID, newLifeSpan, NULL, NULL);
   ```

5. Deduct gold and add to city funds:
   ```cpp
   SetItemCount(iClientH, "Gold", dwGoldCount - sPrice);
   m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += sPrice;
   ```

---

## Player-to-Player Trading

### Exchange System Overview

The exchange system allows two players to trade items directly. Features:
- Up to 4 items per player per trade
- Both players must confirm before exchange completes
- Weight validation before exchange
- Item name verification to prevent manipulation

### Function: ExchangeItemHandler (Initiate Trade)

**Location:** Game.cpp line 36989

**Signature:**
```cpp
void CGame::ExchangeItemHandler(int iClientH, short sItemIndex, int iAmount,
                                 short dX, short dY, WORD wObjectID, char *pItemName)
```

**Process:**

1. **Validation:**
   ```cpp
   if (m_pClientList[iClientH]->m_bIsExchangeMode == TRUE) return;  // Already trading
   if (wObjectID >= DEF_MAXCLIENTS) return;
   if ((m_bAdminSecurity == TRUE) && (m_pClientList[iClientH]->m_iAdminUserLevel > 0)) return;
   ```

2. **Find Trade Partner:**
   ```cpp
   m_pMapList[m_pClientList[iClientH]->m_cMapIndex]->GetOwner(&sOwnerH, &cOwnerType, dX, dY);
   ```

3. **Partner Validation:**
   ```cpp
   if ((m_pClientList[sOwnerH]->m_bIsExchangeMode == TRUE) ||  // Partner already trading
       (m_pClientList[sOwnerH]->m_sAppr2 & 0xF000) ||           // Partner in combat mode
       (m_pMapList[m_pClientList[sOwnerH]->m_cMapIndex]->m_bIsFightZone == TRUE)) {  // Fight zone
       _ClearExchangeStatus(iClientH);
       return;
   }
   ```

4. **Initialize Exchange Mode for Both Players:**
   ```cpp
   // Initiator setup
   m_pClientList[iClientH]->m_bIsExchangeMode = TRUE;
   m_pClientList[iClientH]->m_iExchangeH = sOwnerH;
   strcpy(m_pClientList[iClientH]->m_cExchangeName, m_pClientList[sOwnerH]->m_cCharName);

   // Clear item arrays
   m_pClientList[iClientH]->iExchangeCount = 0;
   for(int i=0; i<4 ; i++){
       ZeroMemory(m_pClientList[iClientH]->m_cExchangeItemName[i], 21);
       m_pClientList[iClientH]->m_cExchangeItemIndex[i] = -1;
       m_pClientList[iClientH]->m_iExchangeItemAmount[i] = 0;
   }

   // Store first item
   m_pClientList[iClientH]->m_cExchangeItemIndex[0] = (char)sItemIndex;
   m_pClientList[iClientH]->m_iExchangeItemAmount[0] = iAmount;
   memcpy(m_pClientList[iClientH]->m_cExchangeItemName[0],
          m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cName, 20);
   m_pClientList[iClientH]->iExchangeCount++;

   // Partner setup (similar)
   m_pClientList[sOwnerH]->m_bIsExchangeMode = TRUE;
   m_pClientList[sOwnerH]->m_iExchangeH = iClientH;
   // ...
   ```

5. **Notify Both Players:**
   ```cpp
   // Notify initiator (index + 1000 indicates self)
   SendNotifyMsg(iClientH, iClientH, DEF_NOTIFY_OPENEXCHANGEWINDOW,
                 sItemIndex+1000, sprite, spriteFrame, itemName, amount, ...);

   // Notify partner
   SendNotifyMsg(iClientH, sOwnerH, DEF_NOTIFY_OPENEXCHANGEWINDOW,
                 sItemIndex, sprite, spriteFrame, itemName, amount, ...);
   ```

### Function: SetExchangeItem (Add Item to Trade)

**Location:** Game.cpp line 37093

**Signature:**
```cpp
void CGame::SetExchangeItem(int iClientH, int iItemIndex, int iAmount)
```

**Process:**

1. **Limit Check:**
   ```cpp
   if (m_pClientList[iClientH]->iExchangeCount > 4) return;  // Max 4 items
   ```

2. **Duplicate Item Check:**
   ```cpp
   for(int i=0; i<m_pClientList[iClientH]->iExchangeCount; i++){
       if (m_pClientList[iClientH]->m_cExchangeItemIndex[i] == (char)iItemIndex) {
           _ClearExchangeStatus(iExH);
           _ClearExchangeStatus(iClientH);
           return;
       }
   }
   ```

3. **Add Item:**
   ```cpp
   m_pClientList[iClientH]->m_cExchangeItemIndex[m_pClientList[iClientH]->iExchangeCount] = (char)iItemIndex;
   m_pClientList[iClientH]->m_iExchangeItemAmount[m_pClientList[iClientH]->iExchangeCount] = iAmount;
   memcpy(m_pClientList[iClientH]->m_cExchangeItemName[m_pClientList[iClientH]->iExchangeCount],
          m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_cName, 20);
   m_pClientList[iClientH]->iExchangeCount++;
   ```

4. **Notify Both Players:**
   ```cpp
   SendNotifyMsg(iClientH, iClientH, DEF_NOTIFY_SETEXCHANGEITEM, iItemIndex+1000, ...);
   SendNotifyMsg(iClientH, iExH, DEF_NOTIFY_SETEXCHANGEITEM, iItemIndex, ...);
   ```

### Function: ConfirmExchangeItem (Complete Trade)

**Location:** Game.cpp line 37163

**Signature:**
```cpp
void CGame::ConfirmExchangeItem(int iClientH)
```

**Process:**

1. **Set Confirm Flag:**
   ```cpp
   m_pClientList[iClientH]->m_bIsExchangeConfirm = TRUE;
   ```

2. **Check if Partner Also Confirmed:**
   ```cpp
   if (m_pClientList[iExH]->m_bIsExchangeConfirm == TRUE) {
       // Both confirmed - execute trade
   }
   ```

3. **Validate All Items Still Exist:**
   ```cpp
   for(int i=0; i<m_pClientList[iClientH]->iExchangeCount; i++){
       if ((m_pClientList[iClientH]->m_pItemList[m_pClientList[iClientH]->m_cExchangeItemIndex[i]] == NULL) ||
           (memcmp(m_pClientList[iClientH]->m_pItemList[...], m_pClientList[iClientH]->m_cExchangeItemName[i], 20) != 0)) {
           _ClearExchangeStatus(iClientH);
           _ClearExchangeStatus(iExH);
           return;
       }
   }
   ```

4. **Calculate Weight Exchange:**
   ```cpp
   iWeightLeftA = _iCalcMaxLoad(iClientH) - iCalcTotalWeight(iClientH);
   iWeightLeftB = _iCalcMaxLoad(iExH) - iCalcTotalWeight(iExH);

   // Calculate weight of items being traded
   for(i=0; i<m_pClientList[iClientH]->iExchangeCount; i++){
       iItemWeightA += iGetItemWeight(...);
   }

   // Check if each player can receive the other's items
   if ((iWeightLeftA < iItemWeightB) || (iWeightLeftB < iItemWeightA)) {
       _ClearExchangeStatus(iClientH);
       _ClearExchangeStatus(iExH);
       return;
   }
   ```

5. **Execute Exchange:**

   For consumables/arrows, create new item copies:
   ```cpp
   if ((itemType == DEF_ITEMTYPE_CONSUME) || (itemType == DEF_ITEMTYPE_ARROW)) {
       pItemA[i] = new class CItem;
       _bInitItemAttr(pItemA[i], itemName);
       pItemA[i]->m_dwCount = exchangeAmount;
   }
   else {
       // Non-consumables transfer directly
       pItemA[i] = (class CItem *)m_pClientList[iClientH]->m_pItemList[index];
   }
   ```

   Transfer items:
   ```cpp
   // Give B's items to A
   for(i=0; i<m_pClientList[iExH]->iExchangeCount; i++){
       bAddItem(iClientH, pItemB[i], NULL);
       _bItemLog(DEF_ITEMLOG_EXCHANGE, iExH, iClientH, pItemBcopy[i]);

       if (consumable) {
           // Reduce count
           SetItemCount(iExH, itemIndex, originalCount - exchangeAmount);
       }
       else {
           // Remove item completely
           ReleaseItemHandler(iExH, itemIndex, TRUE);
           SendNotifyMsg(NULL, iExH, DEF_NOTIFY_GIVEITEMFIN_ERASEITEM, ...);
           m_pClientList[iExH]->m_pItemList[itemIndex] = NULL;
       }
   }
   // Similar for A's items to B
   ```

6. **Clear Exchange State:**
   ```cpp
   m_pClientList[iClientH]->m_bIsExchangeMode = FALSE;
   m_pClientList[iClientH]->m_bIsExchangeConfirm = FALSE;
   ZeroMemory(m_pClientList[iClientH]->m_cExchangeName, 11);
   m_pClientList[iClientH]->m_iExchangeH = NULL;
   m_pClientList[iClientH]->iExchangeCount = 0;
   // Similar for partner
   ```

7. **Notify Completion:**
   ```cpp
   SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_EXCHANGEITEMCOMPLETE, NULL, NULL, NULL, NULL);
   SendNotifyMsg(NULL, iExH, DEF_NOTIFY_EXCHANGEITEMCOMPLETE, NULL, NULL, NULL, NULL);
   ```

### Function: CancelExchangeItem

**Location:** Game.cpp line 46326

**Signature:**
```cpp
void CGame::CancelExchangeItem(int iClientH)
```

Simply clears exchange state for both parties:
```cpp
iExH = m_pClientList[iClientH]->m_iExchangeH;
_ClearExchangeStatus(iExH);
_ClearExchangeStatus(iClientH);
```

### Function: _ClearExchangeStatus

**Location:** Game.cpp line 46305

**Signature:**
```cpp
void CGame::_ClearExchangeStatus(int iToH)
```

Cleans up exchange state:
```cpp
if (m_pClientList[iToH]->m_cExchangeName != FALSE)
    SendNotifyMsg(NULL, iToH, DEF_NOTIFY_CANCELEXCHANGEITEM, ...);

m_pClientList[iToH]->m_dwInitCCTime = FALSE;
m_pClientList[iToH]->m_iAlterItemDropIndex = 0;
m_pClientList[iToH]->m_iExchangeH = NULL;
m_pClientList[iToH]->m_bIsExchangeMode = FALSE;
ZeroMemory(m_pClientList[iToH]->m_cExchangeName, 11);
```

---

## Price Calculation

### Special Weapon/Equipment Attributes (SWE)

Equipment with special attributes sells for more. The attribute is stored in `m_dwAttribute`:

```cpp
// Extract attribute type and value from m_dwAttribute
dwSWEType  = (m_dwAttribute & 0x00F00000) >> 20;  // Primary effect type
dwSWEValue = (m_dwAttribute & 0x000F0000) >> 16;  // Primary effect value

// Secondary effect (for rare items)
dwSWEType2  = (m_dwAttribute & 0x0000F000) >> 12;
dwSWEValue2 = (m_dwAttribute & 0x00000F00) >> 8;
```

### Primary Effect Multipliers (dwSWEType)

| Type | Description | Multiplier |
|------|-------------|------------|
| 1 | Critical Hit | 4x |
| 2 | Poison | 6x |
| 3 | Justice | 15x |
| 5 | Agile | 3x |
| 6 | Light | 2x |
| 7 | Sharp | 5x |
| 8 | Strong | 2x |
| 9 | Ancient | 20x |

### Secondary Effect Multipliers (dwSWEType2)

| Type | Description | Multiplier |
|------|-------------|------------|
| 1, 12 | Poison Resist, More Gold | 2x |
| 2-7 | Hit Rate, Defense, HP/SP/MP Regen, Magic Resist | 4x |
| 8-11 | Physical/Magic Absorb, Combo Damage, More EXP | 6x |

### Effect Value Modifiers (dwSWEValue)

| Value | Percentage |
|-------|------------|
| 1 | 10% |
| 2 | 20% |
| 3 | 30% |
| 4 | 35% |
| 5 | 40% |
| 6 | 50% |
| 7 | 100% |
| 8 | 200% |
| 9 | 300% |
| 10 | 400% |
| 11 | 500% |
| 12 | 700% |
| 13 | 900% |

### Price Calculation Formula

```cpp
// Base price from durability
d1 = (double)sRemainLife;
d2 = (double)wMaxLifeSpan;
d3 = (d1 / d2) * 0.5f;  // Durability ratio * 50%
iPrice = (int)(d3 * wPrice);

// Primary attribute bonus
if (dwSWEType != 0) {
    d1 = (double)iPrice * dwMul1;  // Apply type multiplier
    d2 = valuePercentage;           // From dwSWEValue
    d3 = d1 * (d2 / 100.0f);
    dwAddPrice1 = (int)(d1 + d3);
}

// Secondary attribute bonus (similar calculation)

// Final price = base + 77% of attribute bonuses
iPrice = iPrice + (dwAddPrice1 - (dwAddPrice1/3)) + (dwAddPrice2 - (dwAddPrice2/3));

// Neutral penalty: 50%
if (bNeutral) iPrice = iPrice / 2;

// Clamp to range
if (iPrice <= 0) iPrice = 1;
if (iPrice > 1000000) iPrice = 1000000;
```

### Charisma Discount (Purchases Only)

```cpp
// Discount percentage = (Charisma - 10) / 4
iDiscountRatio = ((m_pClientList[iClientH]->m_iCharisma - 10) / 4);

// Convert to actual discount
iDiscountCost = iCost * (iDiscountRatio / 100.0f);

// Cap at 50% discount
if (iDiscountCost >= (iCost/2)) iDiscountCost = (iCost/2) - 1;

// Final price
finalPrice = iCost - iDiscountCost;
```

---

## Network Protocol

### Client -> Server Messages

| Message Type | Hex Value | Handler Function | Parameters |
|--------------|-----------|------------------|------------|
| DEF_COMMONTYPE_REQ_PURCHASEITEM | 0x0A04 | RequestPurchaseItemHandler | itemName, count |
| DEF_COMMONTYPE_REQ_SELLITEM | 0x0A13 | ReqSellItemHandler | itemID, npcType, count, itemName |
| DEF_COMMONTYPE_REQ_SELLITEMCONFIRM | 0x0A15 | ReqSellItemConfirmHandler | itemID, count, itemName |
| DEF_COMMONTYPE_REQ_REPAIRITEM | 0x0A14 | ReqRepairItemHandler | itemID, npcType, itemName |
| DEF_COMMONTYPE_REQ_REPAIRITEMCONFIRM | 0x0A16 | ReqRepairItemCofirmHandler | itemID, itemName |
| DEF_COMMONTYPE_EXCHANGEITEMTOCHAR | 0x0A1E | ExchangeItemHandler | itemIndex, amount, targetX, targetY, objectID, itemName |
| DEF_COMMONTYPE_SETEXCHANGEITEM | 0x0A1F | SetExchangeItem | itemIndex, amount |
| DEF_COMMONTYPE_CONFIRMEXCHANGEITEM | 0x0A20 | ConfirmExchangeItem | none |
| DEF_COMMONTYPE_CANCELEXCHANGEITEM | 0x0A21 | CancelExchangeItem | none |
| MSGID_REQUEST_SELLITEMLIST | 0x2900AD30 | RequestSellItemListHandler | 12 item entries |

### Server -> Client Notifications

| Notification | Hex Value | Purpose |
|--------------|-----------|---------|
| DEF_NOTIFY_ITEMPURCHASED | 0x0B06 | Item successfully purchased |
| DEF_NOTIFY_NOTENOUGHGOLD | 0x0B08 | Insufficient gold |
| DEF_NOTIFY_CANNOTSELLITEM | 0x0B2C | Cannot sell item (reason in param) |
| DEF_NOTIFY_SELLITEMPRICE | 0x0B2D | Price quote for selling |
| DEF_NOTIFY_CANNOTREPAIRITEM | 0x0B2E | Cannot repair item |
| DEF_NOTIFY_REPAIRITEMPRICE | 0x0B2F | Price quote for repair |
| DEF_NOTIFY_ITEMREPAIRED | 0x0B30 | Item successfully repaired |
| DEF_NOTIFY_ITEMSOLD | 0x0B31 | Item successfully sold |
| DEF_NOTIFY_OPENEXCHANGEWINDOW | 0x0B5E | Open trade window |
| DEF_NOTIFY_SETEXCHANGEITEM | 0x0B5F | Item added to trade |
| DEF_NOTIFY_CANCELEXCHANGEITEM | 0x0B60 | Trade cancelled |
| DEF_NOTIFY_EXCHANGEITEMCOMPLETE | 0x0B61 | Trade completed |
| DEF_NOTIFY_CANNOTCARRYMOREITEM | 0x0B05 | Inventory full |
| DEF_NOTIFY_GIVEITEMFIN_ERASEITEM | 0x0B1D | Item removed (trade/give) |
| DEF_NOTIFY_ITEMOBTAINED | 0x0B01 | Item added to inventory |

### Cannot Sell Reasons

Sent with DEF_NOTIFY_CANNOTSELLITEM, second parameter:
| Code | Reason |
|------|--------|
| 1 | Wrong NPC type for this item category |
| 2 | Item is broken (durability = 0) |
| 4 | Receiving gold would exceed weight limit |

### Cannot Repair Reasons

Sent with DEF_NOTIFY_CANNOTREPAIRITEM, second parameter:
| Code | Reason |
|------|--------|
| 1 | Item type cannot be repaired |
| 2 | Wrong NPC type for this item category |

---

## Helper Functions

### dwGetItemCount

**Location:** Game.cpp line 15341

```cpp
DWORD CGame::dwGetItemCount(int iClientH, char * pName)
```

Returns total count of an item by name in player's inventory. Used primarily for gold checking.

### SetItemCount

**Location:** Game.cpp line 15360 (by name) and 15393 (by index)

```cpp
int CGame::SetItemCount(int iClientH, char * pItemName, DWORD dwCount)
int CGame::SetItemCount(int iClientH, int iItemIndex, DWORD dwCount)
```

Sets the count of a stackable item. If count becomes 0, removes the item.

### iCalcTotalWeight

**Location:** Game.cpp line 31146

```cpp
int CGame::iCalcTotalWeight(int iClientH)
```

Recalculates total inventory weight and stores in `m_iCurWeightLoad`.

### _iCalcMaxLoad

Returns maximum weight capacity based on strength.

### iGetItemWeight

Returns weight of item considering stack count.

### _bAddClientItemList

**Location:** Game.cpp

```cpp
BOOL CGame::_bAddClientItemList(int iClientH, CItem * pItem, int * pDelReq)
```

Adds item to inventory. Handles stacking for consumables. Returns TRUE on success.

### bAddItem

**Location:** Game.cpp line 37394

```cpp
BOOL CGame::bAddItem(int iClientH, CItem * pItem, char cMode)
```

High-level function to add item and send notification to client.

### _iGetItemSpaceLeft

**Location:** Game.cpp line 37383

```cpp
int CGame::_iGetItemSpaceLeft(int iClientH)
```

Returns number of empty inventory slots.

---

## Constants Reference

### Item Log Types (Game.h)

```cpp
#define DEF_ITEMLOG_GIVE            1
#define DEF_ITEMLOG_DROP            2
#define DEF_ITEMLOG_GET             3
#define DEF_ITEMLOG_DEPLETE         4
#define DEF_ITEMLOG_NEWGENDROP      5
#define DEF_ITEMLOG_DUPITEMID       6
#define DEF_ITEMLOG_BUY             7
#define DEF_ITEMLOG_SELL            8
#define DEF_ITEMLOG_RETRIEVE        9
#define DEF_ITEMLOG_DEPOSIT         10
#define DEF_ITEMLOG_EXCHANGE        11
#define DEF_ITEMLOG_SKILLLEARN      12
#define DEF_ITEMLOG_MAKE            13
#define DEF_ITEMLOG_SUMMONMONSTER   14
#define DEF_ITEMLOG_POISONED        15
#define DEF_ITEMLOG_MAGICLEARN      16
#define DEF_ITEMLOG_REPAIR          17
#define DEF_ITEMLOG_USE             32
#define DEF_ITEMLOG_UPGRADEFAIL     29
#define DEF_ITEMLOG_UPGRADESUCCESS  30
```

### Limits

```cpp
#define DEF_MAXITEMS        50      // Max inventory slots
#define DEF_MAXBANKITEMS    200     // Max bank slots
#define DEF_MAXCLIENTS      2000    // Max connected clients
```

---

## Logging System

### Item Transaction Logging

All trading transactions are logged via `_bItemLog`:

**Location:** Game.cpp line 43941

```cpp
BOOL CGame::_bItemLog(int iAction, int iGiveH, int iRecvH, class CItem * pItem, BOOL bForceItemLog)
```

**Log Format:**
```
(IP) PC(CharName)  Action  ItemName(count attr1 attr2 attr3 dwAttribute)  MapName(X Y)  [PC(OtherCharName)]
```

**Examples:**
```
(192.168.1.1) PC(Player1)  Exchange  Sword(1 0 0 0 0x100)  aresden(100 200)  PC(Player2)
(192.168.1.1) PC(Player1)  Sell  Potion(10 0 0 0 0x0)  elvine(50 75)
(192.168.1.1) PC(Player1)  Buy  Arrow(100 0 0 0 0x0)  aresden(120 180)
```

Logs are sent to the Log Server via:
```cpp
bSendMsgToLS(MSGID_GAMEITEMLOG, iGiveH, NULL, cTxt);
```

---

## Flow Diagrams

### NPC Shop Purchase Flow

```
Client                          Server
  |                               |
  |-- DEF_COMMONTYPE_REQ_PURCHASEITEM -->
  |      (itemName, count)        |
  |                               |-- Validate location
  |                               |-- Check m_bIsForSale
  |                               |-- Calculate discount
  |                               |-- Check gold
  |                               |-- Add item to inventory
  |                               |-- Deduct gold
  |                               |-- Add to city funds
  |<-- DEF_NOTIFY_ITEMPURCHASED --|
  |      (item details)           |
```

### NPC Shop Sale Flow

```
Client                          Server
  |                               |
  |-- DEF_COMMONTYPE_REQ_SELLITEM ---->
  |      (itemID, npcType, count) |
  |                               |-- Validate item
  |                               |-- Calculate price
  |<-- DEF_NOTIFY_SELLITEMPRICE --|
  |      (itemID, durability, price)
  |                               |
  |-- DEF_COMMONTYPE_REQ_SELLITEMCONFIRM -->
  |      (itemID, count)          |
  |                               |-- Remove item
  |                               |-- Give gold
  |<-- DEF_NOTIFY_ITEMSOLD -------|
  |<-- DEF_NOTIFY_ITEMOBTAINED ---|
  |      (Gold details)           |
```

### Player Trade Flow

```
Player A                      Server                      Player B
   |                            |                            |
   |-- EXCHANGEITEMTOCHAR ----->|                            |
   |   (itemIndex, target pos)  |                            |
   |                            |-- Validate both players    |
   |                            |-- Set exchange mode        |
   |<-- OPENEXCHANGEWINDOW -----|-- OPENEXCHANGEWINDOW ----->|
   |                            |                            |
   |-- SETEXCHANGEITEM -------->|                            |
   |                            |-- Add item to A's list     |
   |<-- SETEXCHANGEITEM --------|-- SETEXCHANGEITEM -------->|
   |                            |                            |
   |                            |<-- SETEXCHANGEITEM --------|
   |                            |-- Add item to B's list     |
   |<-- SETEXCHANGEITEM --------|-- SETEXCHANGEITEM -------->|
   |                            |                            |
   |-- CONFIRMEXCHANGEITEM ---->|                            |
   |                            |-- Set A confirmed          |
   |                            |<-- CONFIRMEXCHANGEITEM ----|
   |                            |-- Set B confirmed          |
   |                            |-- Validate all items       |
   |                            |-- Check weights            |
   |                            |-- Execute transfer         |
   |<-- EXCHANGEITEMCOMPLETE ---|-- EXCHANGEITEMCOMPLETE --->|
   |<-- GIVEITEMFIN_ERASEITEM --|-- GIVEITEMFIN_ERASEITEM -->|
   |<-- ITEMOBTAINED -----------|-- ITEMOBTAINED ----------->|
```

### Exchange Cancel Flow

```
Player A/B                    Server                      Other Player
   |                            |                            |
   |-- CANCELEXCHANGEITEM ----->|                            |
   |                            |-- _ClearExchangeStatus(A)  |
   |                            |-- _ClearExchangeStatus(B)  |
   |<-- CANCELEXCHANGEITEM -----|-- CANCELEXCHANGEITEM ----->|
```

---

## Security Considerations

### Admin Security

Admins are blocked from trading to prevent item spawning exploits:
```cpp
if ((m_bAdminSecurity == TRUE) && (m_pClientList[iClientH]->m_iAdminUserLevel > 0)) return;
```

### Item Name Verification

Items are verified by name to prevent slot manipulation:
```cpp
if (memcmp(storedItemName, currentItemName, 20) != 0) {
    _ClearExchangeStatus(...);
    return;
}
```

### Processing Lock

A flag prevents processing during certain states:
```cpp
if (m_pClientList[iClientH]->m_pIsProcessingAllowed == FALSE) return;
```

### Weight Validation

Both purchase and trade operations verify weight capacity before completion.

### Exchange Mode Lock

Players in exchange mode cannot:
- Start another exchange
- Be teleported
- Perform certain actions that would clear exchange state

---

## City Fund System

Purchases and repairs contribute to city funds:

```cpp
// On purchase
m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += wTempPrice;

// On repair
m_stCityStatus[m_pClientList[iClientH]->m_cSide].iFunds += sPrice;
```

The `m_cSide` value is:
- 0 = Neutral
- 1 = Aresden
- 2 = Elvine

City funds can be used for city upgrades, war preparations, etc.

---

## Related Files

| File | Purpose |
|------|---------|
| Game.cpp | All trading function implementations |
| Game.h | Function declarations, constants |
| Client.h | Exchange state variables |
| Item.h | Item structure, type definitions |
| NetMessages.h | Network message constants |
