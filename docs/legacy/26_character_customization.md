# System #26: Character Customization System

## Table of Contents

1. [Overview](#overview)
2. [Character Creation Attributes](#character-creation-attributes)
   - [Sex Selection](#sex-selection)
   - [Skin Color](#skin-color)
   - [Hair Style](#hair-style)
   - [Hair Color](#hair-color)
   - [Underwear Type](#underwear-type)
3. [Visual Appearance Encoding](#visual-appearance-encoding)
   - [m_sType - Character/Entity Type](#m_stype---characterentity-type)
   - [m_sAppr1 - Hair and Underwear](#m_sappr1---hair-and-underwear)
   - [m_sAppr2 - Weapon and Shield](#m_sappr2---weapon-and-shield)
   - [m_sAppr3 - Armor (Body/Arms/Pants/Head)](#m_sappr3---armor-bodyarmspantshead)
   - [m_sAppr4 - Cape/Leggings/Effects](#m_sappr4---capeleggingseffects)
   - [m_iApprColor - Item Color Values](#m_iapprcolor---item-color-values)
4. [Status Effects Visual Flags](#status-effects-visual-flags)
5. [Polymorph System](#polymorph-system)
6. [Dye System](#dye-system)
7. [Equipment Appearance Updates](#equipment-appearance-updates)
8. [Data Storage and Persistence](#data-storage-and-persistence)
9. [Network Synchronization](#network-synchronization)
10. [Related Functions](#related-functions)
11. [Constants Reference](#constants-reference)

---

## Overview

The Helbreath character customization system controls all visual aspects of player characters, including base appearance (sex, skin, hair), equipped items, status effects, and transformation (polymorph) states. The system uses a combination of individual attribute fields and packed bit-field values to efficiently transmit appearance data to clients.

**Key Data Structures:**
- `CClient` class - Player character state
- `CNpc` class - NPC state (limited appearance data)
- `CItem` class - Item appearance values

**Primary Files:**
- `Client.h` / `Client.cpp` - Character appearance fields
- `Game.cpp` - Appearance calculation and synchronization
- `Item.h` - Equipment appearance constants

---

## Character Creation Attributes

### Sex Selection

```cpp
// Client.h:90
char m_cSex;  // Character sex value
```

**Valid Values:**
| Value | Description |
|-------|-------------|
| 1 | Male |
| 2 | Female |

**Effect on m_sType:**
```cpp
// Game.cpp:7271-7276
if (m_pClientList[iClientH]->m_cSex == 1) {
    sTmpType = 1;  // Male base type
}
else if (m_pClientList[iClientH]->m_cSex == 2) {
    sTmpType = 4;  // Female base type
}
```

The base type is then modified by skin color to produce the final `m_sType` value (1-6 for normal players).

### Skin Color

```cpp
// Client.h:90
char m_cSkin;  // Skin color value
```

**Valid Values:**
| Value | Description | Male m_sType | Female m_sType |
|-------|-------------|--------------|----------------|
| 1 | Light skin | 1 | 4 |
| 2 | Medium skin | 2 | 5 |
| 3 | Dark skin | 3 | 6 |

**Type Calculation:**
```cpp
// Game.cpp:7278-7287
switch (m_pClientList[iClientH]->m_cSkin) {
case 1:
    break;  // No modification
case 2:
    sTmpType += 1;
    break;
case 3:
    sTmpType += 2;
    break;
}
```

**Skin Change via Items:**
```cpp
// Game.cpp:27403-27417 - DEF_ITEMEFFECTTYPE_CHANGEATTR case 3
m_pClientList[iClientH]->m_cSkin++;
if (m_pClientList[iClientH]->m_cSkin > 3)
    m_pClientList[iClientH]->m_cSkin = 1;  // Wraps around

// Recalculate type based on sex and new skin
if (m_pClientList[iClientH]->m_cSex == 1)      sTemp = 1;
else if (m_pClientList[iClientH]->m_cSex == 2) sTemp = 4;

switch (m_pClientList[iClientH]->m_cSkin) {
case 2: sTemp += 1; break;
case 3: sTemp += 2; break;
}
m_pClientList[iClientH]->m_sType = sTemp;
```

### Hair Style

```cpp
// Client.h:90
char m_cHairStyle;  // Hair style index
```

**Valid Values:** 0-7 (8 different styles)

**Storage in m_sAppr1:**
- Bits 8-11 (upper nibble of low byte): `m_cHairStyle << 8`

**Hair Style Change via Items:**
```cpp
// Game.cpp:27394-27400 - DEF_ITEMEFFECTTYPE_CHANGEATTR case 2
m_pClientList[iClientH]->m_cHairStyle++;
if (m_pClientList[iClientH]->m_cHairStyle > 7)
    m_pClientList[iClientH]->m_cHairStyle = 0;

sTemp = (m_pClientList[iClientH]->m_cHairStyle << 8) |
        (m_pClientList[iClientH]->m_cHairColor << 4) |
        (m_pClientList[iClientH]->m_cUnderwear);
m_pClientList[iClientH]->m_sAppr1 = sTemp;
```

### Hair Color

```cpp
// Client.h:90
char m_cHairColor;  // Hair color index
```

**Valid Values:** 0-15 (16 different colors)

**Storage in m_sAppr1:**
- Bits 4-7 (upper nibble of lower nibble): `m_cHairColor << 4`

**Hair Color Change via Items:**
```cpp
// Game.cpp:27385-27391 - DEF_ITEMEFFECTTYPE_CHANGEATTR case 1
m_pClientList[iClientH]->m_cHairColor++;
if (m_pClientList[iClientH]->m_cHairColor > 15)
    m_pClientList[iClientH]->m_cHairColor = 0;

sTemp = (m_pClientList[iClientH]->m_cHairStyle << 8) |
        (m_pClientList[iClientH]->m_cHairColor << 4) |
        (m_pClientList[iClientH]->m_cUnderwear);
m_pClientList[iClientH]->m_sAppr1 = sTemp;
```

### Underwear Type

```cpp
// Client.h:90
char m_cUnderwear;  // Underwear/base clothing type
```

**Valid Values:** 0-15 (stored in lower 4 bits)

**Storage in m_sAppr1:**
- Bits 0-3 (lower nibble): `m_cUnderwear`

---

## Visual Appearance Encoding

The appearance system uses packed bit-fields to efficiently transmit visual data. Each field encodes specific equipment or appearance aspects.

### m_sType - Character/Entity Type

```cpp
// Client.h:78-79
short m_sType;          // Current visual type
short m_sOriginalType;  // Base type (restored after polymorph)
```

**Player Types (1-6):**
| m_sType | Sex | Skin |
|---------|-----|------|
| 1 | Male | Light |
| 2 | Male | Medium |
| 3 | Male | Dark |
| 4 | Female | Light |
| 5 | Female | Medium |
| 6 | Female | Dark |

**Admin Types:**
```cpp
// Game.cpp:7290-7291
if (m_pClientList[iClientH]->m_iAdminUserLevel >= 10)
    sTmpType = m_pClientList[iClientH]->m_iAdminUserLevel;
```

Admin levels 10+ override the normal type, allowing for special admin appearances.

### m_sAppr1 - Hair and Underwear

```cpp
// Client.h:80
short m_sAppr1;
```

**Bit Layout:**
```
Bits 15-12: Unused (0)
Bits 11-8:  Hair Style (0-7)
Bits 7-4:   Hair Color (0-15)
Bits 3-0:   Underwear Type (0-15)
```

**Assembly:**
```cpp
// Game.cpp:7293
sTmpAppr1 = (m_pClientList[iClientH]->m_cHairStyle << 8) |
            (m_pClientList[iClientH]->m_cHairColor << 4) |
            (m_pClientList[iClientH]->m_cUnderwear);
m_pClientList[iClientH]->m_sAppr1 = sTmpAppr1;
```

### m_sAppr2 - Weapon and Shield

```cpp
// Client.h:81
short m_sAppr2;
```

**Bit Layout:**
```
Bits 15-12: Invisibility/Combat Mode Flag (0xF000)
            0x0 = Visible/Normal
            0xF = Invisible/Combat Mode Active
Bits 11-4:  Weapon Appearance Value (0x0FF0)
Bits 3-0:   Shield Appearance Value (0x000F)
```

**Weapon Type Extraction:**
```cpp
// Game.cpp:10740
sAttackerWeapon = ((m_pClientList[sAttackerH]->m_sAppr2 & 0x0FF0) >> 4);
```

**Shield Equip:**
```cpp
// Game.cpp:12470-12474 - DEF_EQUIPPOS_LHAND
sTemp = m_pClientList[iClientH]->m_sAppr2;
sTemp = sTemp & 0xFFF0;  // Clear shield bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue));
m_pClientList[iClientH]->m_sAppr2 = sTemp;
```

**Weapon Equip (Right Hand/Two-Hand):**
```cpp
// Game.cpp:12482-12486 - DEF_EQUIPPOS_RHAND
sTemp = m_pClientList[iClientH]->m_sAppr2;
sTemp = sTemp & 0xF00F;  // Clear weapon bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue) << 4);
m_pClientList[iClientH]->m_sAppr2 = sTemp;
```

**Invisibility Toggle:**
```cpp
// Game.cpp:16745-16756 - SetInvisibilityFlag effect
sAppr2 = (short)((m_pClientList[iClientH]->m_sAppr2 & 0xF000) >> 12);

if (sAppr2 == 0) {
    // Make visible in combat mode
    m_pClientList[iClientH]->m_sAppr2 = (0xF000 | m_pClientList[iClientH]->m_sAppr2);
}
else {
    // Remove combat mode flag
    m_pClientList[iClientH]->m_sAppr2 = (0x0FFF & m_pClientList[iClientH]->m_sAppr2);
}
```

**Checking Combat Mode:**
```cpp
// Game.cpp:35417
if ((m_pClientList[iClientH]->m_sAppr2 & 0xF000) == 0) return;  // Not in combat mode
```

### m_sAppr3 - Armor (Body/Arms/Pants/Head)

```cpp
// Client.h:82
short m_sAppr3;
```

**Bit Layout:**
```
Bits 15-12: Body Armor (0xF000) - Upper armor value
Bits 11-8:  Pants (0x0F00)
Bits 7-4:   Head/Helmet (0x00F0)
Bits 3-0:   Arms/Gloves (0x000F)
```

**Body Armor Equip:**
```cpp
// Game.cpp:12436-12450 - DEF_EQUIPPOS_BODY
sTemp = m_pClientList[iClientH]->m_sAppr3;
sTemp = sTemp & 0x0FFF;  // Clear body bits

if (m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue < 100) {
    sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue) << 12);
    m_pClientList[iClientH]->m_sAppr3 = sTemp;
}
else {
    // Extended armor (value >= 100)
    sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue - 100) << 12);
    m_pClientList[iClientH]->m_sAppr3 = sTemp;
    // Set extension flag in m_sAppr4
    sTemp = m_pClientList[iClientH]->m_sAppr4;
    sTemp = sTemp | 0x080;
    m_pClientList[iClientH]->m_sAppr4 = sTemp;
}
```

**Pants Equip:**
```cpp
// Game.cpp:12412-12416 - DEF_EQUIPPOS_PANTS
sTemp = m_pClientList[iClientH]->m_sAppr3;
sTemp = sTemp & 0xF0FF;  // Clear pants bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue) << 8);
m_pClientList[iClientH]->m_sAppr3 = sTemp;
```

**Head/Helmet Equip:**
```cpp
// Game.cpp:12400-12404 - DEF_EQUIPPOS_HEAD
sTemp = m_pClientList[iClientH]->m_sAppr3;
sTemp = sTemp & 0xFF0F;  // Clear head bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue) << 4);
m_pClientList[iClientH]->m_sAppr3 = sTemp;
```

**Arms/Gloves Equip:**
```cpp
// Game.cpp:12458-12462 - DEF_EQUIPPOS_ARMS
sTemp = m_pClientList[iClientH]->m_sAppr3;
sTemp = sTemp & 0xFFF0;  // Clear arms bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue));
m_pClientList[iClientH]->m_sAppr3 = sTemp;
```

### m_sAppr4 - Cape/Leggings/Effects

```cpp
// Client.h:83
short m_sAppr4;
```

**Bit Layout:**
```
Bits 15-12: Leggings/Boots (0xF000)
Bits 11-8:  Cape/Mantle (0x0F00)
Bit 7:      Extended Body Armor Flag (0x0080)
Bits 6-4:   Reserved
Bits 3-2:   Attack Special Effect (0x000C)
            0x00 = None
            0x04 = Effect 1 (sparkle)
            0x08 = Effect 3
            0x0C = Effect 2
Bits 1-0:   Defense Special Effect (0x0003)
            0x00 = None
            0x01 = GM Effect
            0x02 = Green Effect
            0x03 = Ice Element
```

**Leggings Equip:**
```cpp
// Game.cpp:12424-12428 - DEF_EQUIPPOS_LEGGINGS
sTemp = m_pClientList[iClientH]->m_sAppr4;
sTemp = sTemp & 0x0FFF;  // Clear leggings bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue) << 12);
m_pClientList[iClientH]->m_sAppr4 = sTemp;
```

**Cape Equip:**
```cpp
// Game.cpp:12524-12528 - DEF_EQUIPPOS_BACK
sTemp = m_pClientList[iClientH]->m_sAppr4;
sTemp = sTemp & 0xF0FF;  // Clear cape bits
sTemp = sTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cApprValue) << 8);
m_pClientList[iClientH]->m_sAppr4 = sTemp;
```

**Special Attack Effect:**
```cpp
// Game.cpp:12548-12563 - DEF_ITEMEFFECTTYPE_ATTACK_SPECABLTY
m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 & 0xFFF3;
switch (m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_sSpecialEffect) {
case 0: break;
case 1:
    m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 | 0x0004;
    break;
case 2:
    m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 | 0x000C;
    break;
case 3:
    m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 | 0x0008;
    break;
}
```

**Special Defense Effect:**
```cpp
// Game.cpp:12566-12587 - DEF_ITEMEFFECTTYPE_DEFENSE_SPECABLTY
m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 & 0xFFFC;
switch(m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_sSpecialEffect){
case 0:
    break;
case 50:
case 51:
case 52:
    m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 | 0x0002;  // Green
    break;
default:
    if(m_pClientList[iClientH]->m_iAdminUserLevel > 0)
        m_pClientList[iClientH]->m_sAppr4 = m_pClientList[iClientH]->m_sAppr4 | 0x0001;  // GM
    // Effect values:
    // 0x0001 GM
    // 0x0002 Green
    // 0x0003 ice element
    // 0x0004 sparkle
    // 0x0005 sparkle green gm
    // 0x0006 sparkle green
    break;
}
```

### m_iApprColor - Item Color Values

```cpp
// Client.h:84
int m_iApprColor;  // v1.4 Color value table
```

**Bit Layout (32-bit):**
```
Bits 31-28: Weapon Color (0xF0000000)
Bits 27-24: Shield Color (0x0F000000)
Bits 23-20: Body Armor Color (0x00F00000)
Bits 19-16: Cape Color (0x000F0000)
Bits 15-12: Arms Color (0x0000F000)
Bits 11-8:  Pants Color (0x00000F00)
Bits 7-4:   Leggings Color (0x000000F0)
Bits 3-0:   Head/Helmet Color (0x0000000F)
```

**Color Assignment by Equipment Slot:**

| Equipment Slot | Mask | Shift |
|---------------|------|-------|
| Head | 0xFFFFFFF0 | 0 |
| Pants | 0xFFFFF0FF | 8 |
| Leggings | 0xFFFFFF0F | 4 |
| Body | 0xFF0FFFFF | 20 |
| Arms | 0xFFFF0FFF | 12 |
| Cape | 0xFFF0FFFF | 16 |
| Shield (LHand) | 0xF0FFFFFF | 24 |
| Weapon (RHand/TwoHand) | 0x0FFFFFFF | 28 |

**Example - Head Color:**
```cpp
// Game.cpp:12406-12409
iTemp = m_pClientList[iClientH]->m_iApprColor;
iTemp = iTemp & 0xFFFFFFF0;  // Clear head color
iTemp = iTemp | ((m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cItemColor));
m_pClientList[iClientH]->m_iApprColor = iTemp;
```

---

## Status Effects Visual Flags

The `m_iStatus` field contains bit flags for various visual status effects.

```cpp
// Client.h:85
int m_iStatus;
```

**Status Flag Bit Map:**

| Bit | Hex Value | Effect | Function |
|-----|-----------|--------|----------|
| 4 | 0x00000010 | Invisibility | SetInvisibilityFlag() |
| 5 | 0x00000020 | Berserk | SetBerserkFlag() |
| 6 | 0x00000040 | Frozen/Ice | SetIceFlag() |
| 7 | 0x00000080 | Poisoned | SetPoisonFlag() |
| 16 | 0x00010000 | Unknown status | - |
| 17 | 0x00020000 | Hero Item Aura | SetHeroFlag() |
| 20 | 0x00100000 | Inhibit Casting | SetInhibitionCastingFlag() |
| 21 | 0x00200000 | Illusion Movement | SetIllusionMovementFlag() |
| 22 | 0x00400000 | Damage Protection | - |
| 23 | 0x00800000 | Unknown | - |
| 24 | 0x01000000 | Illusion | SetIllusionFlag() |
| 25 | 0x02000000 | Defense Shield | SetDefenseShieldFlag() |
| 26 | 0x04000000 | Magic Protection | SetMagicProtectionFlag() |
| 27 | 0x08000000 | Arrow Protection | SetProtectionFromArrowFlag() |

**Weapon Speed (Bits 0-3):**
```cpp
// Game.cpp:12493-12499
iTemp = m_pClientList[iClientH]->m_iStatus;
iTemp = iTemp & 0xFFFFFFF0;  // Clear speed bits
sSpeed = (m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_cSpeed);
sSpeed -= (m_pClientList[iClientH]->m_iStr / 13);
if (sSpeed < 0) sSpeed = 0;
iTemp = iTemp | (int)sSpeed;
m_pClientList[iClientH]->m_iStatus = iTemp;
```

---

## Polymorph System

Players can be transformed into NPC appearances using admin commands or special abilities.

### Admin Polymorph Command

```cpp
// Game.h:374
void AdminOrder_Polymorph(int iClientH, char * pData, DWORD dwMsgSize);

// Game.h:972
int m_iAdminLevelPolymorph;  // Minimum admin level required
```

**Function Implementation:**
```cpp
// Game.cpp:34388-34661
void CGame::AdminOrder_Polymorph(int iClientH, char *pData, DWORD dwMsgSize)
{
    // Check admin level
    if (m_pClientList[iClientH]->m_iAdminUserLevel < m_iAdminLevelPolymorph) {
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_ADMINUSERLEVELLOW, NULL, NULL, NULL, NULL);
        return;
    }

    // Parse token and set m_sType
    if (memcmp(token, "off", 3) == 0)
        m_pClientList[iClientH]->m_sType = m_pClientList[iClientH]->m_sOriginalType;
    // ... various creature types ...

    SendEventToNearClient_TypeA(iClientH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
                                 DEF_OBJECTNULLACTION, NULL, NULL, NULL);
}
```

### Polymorph Type Table

| Type ID | Creature Name | Type ID | Creature Name |
|---------|---------------|---------|---------------|
| 10 | Slime | 46 | TK |
| 11 | Skeleton | 47 | BG |
| 12 | Stone-Golem | 48 | Stalker |
| 13 | Cyclops | 49 | Hellclaw |
| 14 | Orc | 50 | Tigerworm |
| 15 | ShopKeeper | 51 | CP |
| 16 | Giant-Ant | 52 | Gagoyle |
| 17 | Scorpion | 53 | Beholder |
| 18 | Zombie | 54 | Dark-Elf |
| 19 | Gandlf | 55 | Rabbit |
| 20 | Howard | 56 | Cat |
| 21 | Gaurd | 57 | Giant-Frog |
| 22 | Amphis | 58 | Mountain-Giant |
| 23 | Clay-Golem | 59 | Ettin |
| 24 | Tom | 60 | Cannibal-Plant |
| 25 | William | 61 | Rudolph |
| 26 | Kennedy | 62 | DireBoar |
| 27 | Hellbound | 63 | Frost |
| 28 | Troll | 64 | Crops |
| 29 | Orge | 65 | Ice-Golem |
| 30 | Liche | 67 | McGaffin |
| 31 | Demon | 68 | Perry |
| 32 | Unicorn | 69 | Devlin |
| 33 | WereWolf | 70 | Barlog |
| 34 | Dummy | 71 | Centaurus |
| 35 | Energy-Sphere | 72 | Claw-Turtle |
| 36 | AGT | 74 | Giant-Crayfish |
| 37 | CGT | 75 | Giant-Lizard |
| 38 | MS | 76 | Giant-Plant |
| 39 | DT | 77 | MasterMage-Orc |
| 40 | ESG | 78 | Minotaurs |
| 41 | GMG | 79 | Nizie |
| 42 | ManaStone | 80 | Tentocle |
| 43 | LWB | 82 | Sor |
| 44 | GHK | 83 | ATK |
| 45 | GHC | 84-91 | Various |

### Zombie Transformation (Death Effect)

```cpp
// Game.cpp:17357
m_pClientList[sOwnerH]->m_sType = 18;  // Transform to zombie on death spell
```

---

## Dye System

Items can be colored using dye items with various effect types.

### Dye Effect Types

```cpp
// Item.h:63
#define DEF_ITEMEFFECTTYPE_DYE              17  // General dye
#define DEF_ITEMEFFECTTYPE_ARMORDYE         32  // Armor dyes
#define DEF_ITEMEFFECTTYPE_WEAPONDYE        34  // Weapon dyes
```

### Dye Application

**General Dye (DEF_ITEMEFFECTTYPE_DYE):**
```cpp
// Game.cpp:35939-35958
case DEF_ITEMEFFECTTYPE_DYE:
    // Valid for categories 11 (shields?) and 12 (accessories?)
    if ((m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 11) ||
        (m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 12)) {
        m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cItemColor =
            m_pClientList[iClientH]->m_pItemList[sItemIndex]->m_sItemEffectValue1;
        SendNotifyMsg(NULL, iClientH, DEF_NOTIFY_ITEMCOLORCHANGE, sDestItemID,
                      m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cItemColor, NULL, NULL);
        return TRUE;
    }
```

**Armor Dye (DEF_ITEMEFFECTTYPE_ARMORDYE):**
```cpp
// Game.cpp:35960-35976
case DEF_ITEMEFFECTTYPE_ARMORDYE:
    // Valid for categories 6 (body armor), 15 (special armor), 13 (leggings)
    if ((m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 6) ||
        (m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 15) ||
        (m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 13)) {
        // Apply color...
    }
```

**Weapon Dye (DEF_ITEMEFFECTTYPE_WEAPONDYE):**
```cpp
// Game.cpp:35978-35994
case DEF_ITEMEFFECTTYPE_WEAPONDYE:
    // Valid for categories 1 (swords), 3 (axes/maces), 8 (bows/wands)
    if ((m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 1) ||
        (m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 3) ||
        (m_pClientList[iClientH]->m_pItemList[sDestItemID]->m_cCategory == 8)) {
        // Apply color...
    }
```

### Armor Dye Item IDs

```cpp
// Game.cpp:54750-54762
case 6:  iItemID=881; break;  // ArmorDye(Indigo)
case 7:  iItemID=882; break;  // ArmorDye(CrimsonRed)
case 8:  iItemID=883; break;  // ArmorDye(Gold)
case 9:  iItemID=884; break;  // ArmorDye(Aqua)
case 10: iItemID=885; break;  // ArmorDye(Pink)
case 11: iItemID=886; break;  // ArmorDye(Violet)
case 12: iItemID=887; break;  // ArmorDye(Blue)
case 13: iItemID=888; break;  // ArmorDye(Khaki)
case 14: iItemID=889; break;  // ArmorDye(Yellow)
case 15: iItemID=890; break;  // ArmorDye(Red)
case 16: iItemID=971; break;  // ArmorDye(Green)
case 17: iItemID=972; break;  // ArmorDye(Black)
case 18: iItemID=973; break;  // ArmorDye(Knight)
```

---

## Equipment Appearance Updates

### bEquipItemHandler

When an item is equipped, the appearance values are updated:

```cpp
// Game.cpp:12398-12594
switch (cEquipPos) {
    case DEF_EQUIPPOS_HEAD:
        // Update m_sAppr3 bits 7-4
        // Update m_iApprColor bits 3-0
        break;

    case DEF_EQUIPPOS_PANTS:
        // Update m_sAppr3 bits 11-8
        // Update m_iApprColor bits 11-8
        break;

    case DEF_EQUIPPOS_LEGGINGS:
        // Update m_sAppr4 bits 15-12
        // Update m_iApprColor bits 7-4
        break;

    case DEF_EQUIPPOS_BODY:
        // Update m_sAppr3 bits 15-12
        // Update m_iApprColor bits 23-20
        // Handle extended armor (m_sAppr4 bit 7)
        break;

    case DEF_EQUIPPOS_ARMS:
        // Update m_sAppr3 bits 3-0
        // Update m_iApprColor bits 15-12
        break;

    case DEF_EQUIPPOS_LHAND:  // Shield
        // Update m_sAppr2 bits 3-0
        // Update m_iApprColor bits 27-24
        break;

    case DEF_EQUIPPOS_RHAND:  // One-hand weapon
    case DEF_EQUIPPOS_TWOHAND:  // Two-hand weapon
        // Update m_sAppr2 bits 11-4
        // Update m_iApprColor bits 31-28
        // Update m_iStatus weapon speed bits 3-0
        break;

    case DEF_EQUIPPOS_BACK:  // Cape
        // Update m_sAppr4 bits 11-8
        // Update m_iApprColor bits 19-16
        break;
}
```

### ReleaseItemHandler

When an item is unequipped, the appearance bits are cleared:

```cpp
// Game.cpp:15796-15920
switch (cEquipPos) {
    case DEF_EQUIPPOS_RHAND:
        sTemp = m_pClientList[iClientH]->m_sAppr2;
        sTemp = sTemp & 0xF00F;  // Clear weapon bits
        m_pClientList[iClientH]->m_sAppr2 = sTemp;

        iTemp = m_pClientList[iClientH]->m_iApprColor;
        iTemp = iTemp & 0x0FFFFFFF;  // Clear weapon color
        m_pClientList[iClientH]->m_iApprColor = iTemp;

        // Reset weapon speed
        iTemp = m_pClientList[iClientH]->m_iStatus;
        iTemp = iTemp & 0xFFFFFFF0;
        m_pClientList[iClientH]->m_iStatus = iTemp;
        break;
    // ... other slots ...
}
```

---

## Data Storage and Persistence

### Save Format (Player Data File)

Character appearance is saved to player data files with the following fields:

```cpp
// Game.cpp:7553-7709
strcat(pData, "sex-status       = ");
itoa(m_pClientList[iClientH]->m_cSex, cTxt, 10);
// ...

strcat(pData, "skin-status      = ");
itoa(m_pClientList[iClientH]->m_cSkin, cTxt, 10);
// ...

strcat(pData, "hairstyle-status = ");
itoa(m_pClientList[iClientH]->m_cHairStyle, cTxt, 10);
// ...

strcat(pData, "haircolor-status = ");
itoa(m_pClientList[iClientH]->m_cHairColor, cTxt, 10);
// ...

strcat(pData, "underwear-status = ");
itoa(m_pClientList[iClientH]->m_cUnderwear, cTxt, 10);
// ...

strcat(pData, "appr1 = ");
itoa(m_pClientList[iClientH]->m_sAppr1, cTxt, 10);
// ...

strcat(pData, "appr2 = ");
itoa(m_pClientList[iClientH]->m_sAppr2, cTxt, 10);
// ...

strcat(pData, "appr3 = ");
itoa(m_pClientList[iClientH]->m_sAppr3, cTxt, 10);
// ...

strcat(pData, "appr4 = ");
itoa(m_pClientList[iClientH]->m_sAppr4, cTxt, 10);
// ...

strcat(pData, "appr-color = ");
itoa(m_pClientList[iClientH]->m_iApprColor, cTxt, 10);
```

### Load Format (Parsing)

```cpp
// Game.cpp:5750-5798
case 6:  // sex-status
    m_pClientList[iClientH]->m_cSex = atoi(token);
    break;

case 7:  // skin-status
    m_pClientList[iClientH]->m_cSkin = atoi(token);
    break;

case 8:  // hairstyle-status
    m_pClientList[iClientH]->m_cHairStyle = atoi(token);
    break;

case 9:  // haircolor-status
    m_pClientList[iClientH]->m_cHairColor = atoi(token);
    break;

case 10: // underwear-status
    m_pClientList[iClientH]->m_cUnderwear = atoi(token);
    break;
```

### Item Color Storage

```cpp
// Item.h:126
char m_cItemColor;  // Item color value stored per item

// Game.cpp:5600-5608 - Loading item color
// m_cItemColor
m_pClientList[iClientH]->m_pItemList[iItemIndex]->m_cItemColor = atoi(token);

// Game.cpp:7748 - Saving item color
itoa(m_pClientList[iClientH]->m_pItemList[i]->m_cItemColor, cTxt, 10);
```

---

## Network Synchronization

### SendEventToNearClient_TypeA

Used to broadcast appearance changes to nearby players:

```cpp
// Game.h:674
void SendEventToNearClient_TypeA(short sOwnerH, char cOwnerType, DWORD dwMsgID,
                                  WORD wMsgType, short sV1, short sV2, short sV3);
```

**Appearance Update Trigger:**
```cpp
// Game.cpp:12593
SendEventToNearClient_TypeA(iClientH, DEF_OWNERTYPE_PLAYER, MSGID_EVENT_MOTION,
                             DEF_OBJECTNULLACTION, NULL, NULL, NULL);
```

### Appearance Data in Messages

When sending object data to clients, appearance values are included:

```cpp
// Game.cpp:1343-1363 (Client object data)
*sp = m_pClientList[wObjectID]->m_sAppr1;
sp++;
*sp = m_pClientList[wObjectID]->m_sAppr2;
sp++;
*sp = m_pClientList[wObjectID]->m_sAppr3;
sp++;
*sp = m_pClientList[wObjectID]->m_sAppr4;
sp++;
*ip = m_pClientList[wObjectID]->m_iApprColor;
ip++;
sTemp = m_pClientList[wObjectID]->m_iStatus;
```

---

## Related Functions

### Status Flag Functions

| Function | Purpose | Status Bit |
|----------|---------|------------|
| `SetInvisibilityFlag()` | Toggle invisibility aura | 0x00000010 |
| `SetBerserkFlag()` | Toggle berserk aura | 0x00000020 |
| `SetIceFlag()` | Toggle frozen effect | 0x00000040 |
| `SetPoisonFlag()` | Toggle poison aura | 0x00000080 |
| `SetHeroFlag()` | Toggle hero item aura | 0x00020000 |
| `SetInhibitionCastingFlag()` | Toggle casting inhibition | 0x00100000 |
| `SetIllusionMovementFlag()` | Toggle illusion movement | 0x00200000 |
| `SetIllusionFlag()` | Toggle illusion aura | 0x01000000 |
| `SetDefenseShieldFlag()` | Toggle defense aura | 0x02000000 |
| `SetMagicProtectionFlag()` | Toggle magic protection | 0x04000000 |
| `SetProtectionFromArrowFlag()` | Toggle arrow protection | 0x08000000 |

### Equipment Functions

| Function | Purpose |
|----------|---------|
| `bEquipItemHandler()` | Handle item equip, update appearance |
| `ReleaseItemHandler()` | Handle item unequip, clear appearance |
| `CalcTotalItemEffect()` | Recalculate item bonuses |
| `_cCheckHeroItemEquipped()` | Check for hero armor set |

### Admin Functions

| Function | Purpose |
|----------|---------|
| `AdminOrder_Polymorph()` | Transform into creature |
| `AdminOrder_SetInvi()` | Toggle invisibility |

---

## Constants Reference

### Equipment Positions (Item.h)

```cpp
#define DEF_MAXITEMEQUIPPOS     15
#define DEF_EQUIPPOS_NONE       0
#define DEF_EQUIPPOS_HEAD       1
#define DEF_EQUIPPOS_BODY       2
#define DEF_EQUIPPOS_ARMS       3
#define DEF_EQUIPPOS_PANTS      4
#define DEF_EQUIPPOS_LEGGINGS   5
#define DEF_EQUIPPOS_NECK       6
#define DEF_EQUIPPOS_LHAND      7   // Shield
#define DEF_EQUIPPOS_RHAND      8   // One-hand weapon
#define DEF_EQUIPPOS_TWOHAND    9   // Two-hand weapon
#define DEF_EQUIPPOS_RFINGER    10
#define DEF_EQUIPPOS_LFINGER    11
#define DEF_EQUIPPOS_BACK       12  // Cape
#define DEF_EQUIPPOS_RELEASEALL 13
```

### Item Effect Types (Item.h)

```cpp
#define DEF_ITEMEFFECTTYPE_CHANGEATTR       12  // Change appearance attributes
#define DEF_ITEMEFFECTTYPE_DYE              17  // General dye
#define DEF_ITEMEFFECTTYPE_ATTACK_SPECABLTY 24  // Attack special ability (visual)
#define DEF_ITEMEFFECTTYPE_DEFENSE_SPECABLTY 25 // Defense special ability (visual)
#define DEF_ITEMEFFECTTYPE_ARMORDYE         32  // Armor dye
#define DEF_ITEMEFFECTTYPE_WEAPONDYE        34  // Weapon dye
```

### Change Attribute Effect Values

Used with `DEF_ITEMEFFECTTYPE_CHANGEATTR` (`sItemEffectValue1`):

| Value | Effect |
|-------|--------|
| 1 | Change hair color |
| 2 | Change hair style |
| 3 | Change skin color |
| 4 | Change sex (requires no armor equipped) |

---

## Summary

The Helbreath character customization system uses a sophisticated bit-packing scheme to efficiently transmit visual appearance data:

1. **Base Appearance**: Sex, skin, hair stored in individual fields and packed into `m_sType` and `m_sAppr1`
2. **Equipment Appearance**: Packed into `m_sAppr2`, `m_sAppr3`, `m_sAppr4` with per-slot nibbles
3. **Equipment Colors**: Stored in 32-bit `m_iApprColor` with 4 bits per equipment slot
4. **Status Effects**: Stored as bit flags in `m_iStatus` for visual auras
5. **Transformation**: Uses `m_sType` to change character model entirely

All appearance changes are synchronized to nearby clients via `SendEventToNearClient_TypeA()` using the `MSGID_EVENT_MOTION` / `DEF_OBJECTNULLACTION` message type.
