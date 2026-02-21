# Item Protocol Reconciliation

## Problem

An audit of every item-related handler against the wire protocol revealed three categories of issues:

1. **Compact attribute keys** — `item_attribute::to_json()` uses abbreviated keys (`mt`, `mv`, `st`, `sv`, `cm`, `cq`) instead of readable names.
2. **Missing inventory slot updates** — Most handlers silently modify inventory without sending `inventory_slot_update` to the client. The client has no way to know which slot changed.
3. **No gold change notifications** — Gold changes (shop transactions, NPC loot) are either embedded in response fields or completely silent. No dedicated gold update message exists.

## Design

### Part 1: Attribute Key Rename

Rename keys in `item_attribute::to_json()` and `item_attribute::from_json()`:

| Current | New |
|---------|-----|
| `upgrade` | `upgrade` (no change) |
| `mt` | `main_type` |
| `mv` | `main_value` |
| `st` | `sub_type` |
| `sv` | `sub_value` |
| `cm` | `custom_made` |
| `cq` | `custom_quality` |

All messages that include item attributes are automatically fixed since they all call `item_attribute::to_json()`.

**Files:** `src/item/item_attribute.cpp`

### Part 2: Server-Authoritative Inventory Updates

Every handler that modifies an inventory slot sends `inventory_slot_update` to the owning player. The update contains the full `inventory_item_msg` (or null if cleared).

#### Equip — Slot Update Sequence

Unequips always come before the equip. Number of updates depends on what's already equipped:

| Scenario | Updates (in order) |
|----------|-------------------|
| Equip into empty slot | 1: equip new |
| Equip replacing occupied slot | 2: unequip old, equip new |
| Equip 2H, only shield occupied | 2: unequip shield, equip weapon |
| Equip 2H, only weapon occupied | 2: unequip old weapon, equip new |
| Equip 2H, both weapon + shield occupied | 3: unequip shield, unequip old weapon, equip new |
| Unequip | 1: clear equipped_slot |

Each update is an `inventory_slot_update` with the item's `equipped_slot` field set or cleared accordingly.

#### Other Handlers

| Handler | Slot Updates Sent |
|---------|-------------------|
| **Pickup** | Already sends — no change |
| **Drop** | Already sends null — no change |
| **Use Item** | Send update (decremented count, or null if fully consumed) |
| **Shop Buy** | Send update for new item's slot |
| **Shop Sell Confirm** | Send null for cleared slot |
| **Shop Repair Confirm** | Send update with new durability |
| **Bank Deposit** | Send null for cleared inventory slot + `bank_slot_update` for new bank slot |
| **Bank Withdraw** | Send `bank_slot_update` null for cleared bank slot + update for new inventory slot |
| **Manufacturing** | Send null for each consumed material slot + update for result slot |
| **Alchemy** | Send null for each consumed material slot + update for result slot |
| **Item Upgrade** | Send null for consumed stone slot + update for upgraded item slot (with new attribute) |
| **Admin Give** | Send update to target player for new item's slot |
| **Admin Remove** | Send null to target player for cleared slot |

#### New Message: `bank_slot_update`

Mirrors `inventory_slot_update` but for the bank container:

```json
{
  "type": "bank_slot_update",
  "seq": 0,
  "data": {
    "slot": 3,
    "item": { /* inventory_item_msg or null */ }
  }
}
```

### Part 3: Dedicated `gold_update` Message

New message type sent whenever gold changes:

```json
{
  "type": "gold_update",
  "seq": 0,
  "data": {
    "gold": 5000,
    "change": -500,
    "reason": "shop_buy"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `gold` | int64 | New gold total after change |
| `change` | int64 | Amount changed (positive = gained, negative = spent) |
| `reason` | string | Why gold changed |

**Reason values:** `shop_buy`, `shop_sell`, `shop_repair`, `npc_loot`, `admin`, `trade`

**Sent in these handlers:**
- Shop buy (negative)
- Shop sell confirm (positive)
- Shop repair confirm (negative)
- NPC gold loot (positive)
- Admin operations (either direction)
- Trade completion (both players)

Existing `gold_remaining`/`gold_total` fields in shop responses stay for backward compatibility.

## Files Affected

### Part 1 (attribute keys)
- `src/item/item_attribute.cpp` — to_json() and from_json()
- `tests/test_item_attribute.cpp` — update expected JSON keys
- `tests/test_item_persistence.cpp` — update expected JSON keys
- `tests/test_crafting_attribute.cpp` — update expected JSON keys
- `docs/protocol/items.md` — update attribute object docs

### Part 2 (slot updates)
- `src/bridge/handlers/game_handlers_equipment.cpp` — equip, unequip, use_item, upgrade, ability
- `src/bridge/handlers/game_handlers_shop.cpp` — shop buy/sell/repair, bank deposit/withdraw, drop (already done)
- `src/bridge/handlers/game_handlers.cpp` — crafting handlers (if here)
- `src/crafting/manufacturing_system.cpp` — may need to return consumed slot indices
- `src/bridge/handlers/admin_web_handlers.cpp` — give/remove item
- `src/network/json_protocol.h` — add `bank_slot_update` message type
- `src/network/json_protocol.cpp` — add `bank_slot_update` builder + type map entry
- `docs/protocol/items.md` — update all handler sections

### Part 3 (gold_update)
- `src/network/json_protocol.h` — add `gold_update` message type + data struct
- `src/network/json_protocol.cpp` — add to_json, builder, type map
- `src/bridge/handlers/game_handlers_shop.cpp` — send after gold changes
- `src/bridge/handlers/game_handlers_npc.cpp` — send after NPC gold loot
- `src/bridge/handlers/admin_web_handlers.cpp` — send after admin gold ops
- `docs/protocol/items.md` — add gold_update section
