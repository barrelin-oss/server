# NPCs & Interaction

[← Back to Protocol Index](../JSON_PROTOCOL.md)

## NPC Messages

### `npc_spawn`

An NPC entered visibility range.

**Server Broadcast:**
```json
{
  "type": "npc_spawn",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "template_id": 100,
    "sprite_id": 14,
    "name": "Orc Warrior",
    "x": 105,
    "y": 150,
    "direction": 4,
    "hp": 200,
    "max_hp": 200,
    "level": 25,
    "category": "monster",
    "hostility": "enemy"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Unique NPC entity ID |
| `template_id` | uint32 | NPC template/type ID |
| `sprite_id` | int16 | Legacy sprite type for client rendering |
| `name` | string | NPC display name |
| `x` | int16 | X coordinate |
| `y` | int16 | Y coordinate |
| `direction` | uint8 | Facing direction (0-7) |
| `hp` | int32 | Current HP |
| `max_hp` | int32 | Maximum HP |
| `level` | int16 | NPC level |
| `category` | string | NPC category: `monster`, `boss`, `guard`, `merchant`, `quest`, `trainer`, `banker`, `warehouse`, `pet`, `summon` |
| `hostility` | string | Hostility relative to receiving player: `enemy`, `friendly`, `neutral` |

---

### `npc_despawn`

An NPC left visibility range.

**Server Broadcast:**
```json
{
  "type": "npc_despawn",
  "seq": 0,
  "data": {
    "entity_id": 5001
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | NPC entity ID |

---

### `npc_move`

An NPC moved.

**Server Broadcast:**
```json
{
  "type": "npc_move",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "x": 106,
    "y": 151,
    "direction": 2
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | NPC entity ID |
| `x` | int16 | New X coordinate |
| `y` | int16 | New Y coordinate |
| `direction` | uint8 | Facing direction (0-7) |

---

### `npc_attack`

An NPC attacked something. Includes positions for client-side rendering of the attack animation. For ranged NPCs, additional fields indicate projectile type.

**Server Broadcast (melee):**
```json
{
  "type": "npc_attack",
  "seq": 0,
  "data": {
    "attacker_id": 5001,
    "target_id": 1001,
    "damage": 25,
    "is_critical": false,
    "attacker_x": 106,
    "attacker_y": 151,
    "target_x": 105,
    "target_y": 151
  }
}
```

**Server Broadcast (ranged NPC):**
```json
{
  "type": "npc_attack",
  "seq": 0,
  "data": {
    "attacker_id": 5001,
    "target_id": 1001,
    "damage": 18,
    "is_critical": false,
    "attacker_x": 106,
    "attacker_y": 151,
    "target_x": 100,
    "target_y": 151,
    "is_ranged": true,
    "projectile_type": "arrow"
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `attacker_id` | uint32 | Yes | NPC entity ID |
| `target_id` | uint32 | Yes | Target entity ID |
| `damage` | int32 | Yes | Damage dealt |
| `is_critical` | bool | Yes | Whether it was a critical hit |
| `attacker_x` | int16 | Yes | NPC X position |
| `attacker_y` | int16 | Yes | NPC Y position |
| `target_x` | int16 | Yes | Target X position |
| `target_y` | int16 | Yes | Target Y position |
| `is_ranged` | bool | No | Present and `true` for ranged NPCs (attack_range > 1) |
| `projectile_type` | string | No | `"arrow"` (only when `is_ranged`) |

---

### `npc_death`

An NPC died.

**Server Broadcast:**
```json
{
  "type": "npc_death",
  "seq": 0,
  "data": {
    "entity_id": 5001,
    "killer_id": 1001,
    "x": 106,
    "y": 151
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | NPC that died |
| `killer_id` | uint32 | Entity that killed (0 if environmental/unknown) |
| `x` | int16 | Death location X |
| `y` | int16 | Death location Y |

---

## Entity Info Messages

### `entity_info_request`

Request detailed information about an entity (player or NPC).

**Request:**
```json
{
  "type": "entity_info_request",
  "seq": 155,
  "data": {
    "entity_id": 1221
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `entity_id` | uint32 | Yes | Entity ID to query |

---

### `entity_info_response`

Server responds with detailed entity information.

**Success Response (Player):**
```json
{
  "type": "entity_info_response",
  "seq": 155,
  "data": {
    "success": true,
    "entity": {
      "entity_id": 1221,
      "entity_type": "player",
      "name": "DarkKnight",
      "level": 45,
      "hp": 1250,
      "hp_max": 2000,
      "x": 128,
      "y": 256,
      "direction": 4,
      "faction": "aresden",
      "class_type": 0,
      "pk_count": 12,
      "guild_name": "BloodGuard"
    }
  }
}
```

**Success Response (NPC):**
```json
{
  "type": "entity_info_response",
  "seq": 156,
  "data": {
    "success": true,
    "entity": {
      "entity_id": 50001,
      "entity_type": "npc",
      "name": "Orc Warrior",
      "level": 25,
      "hp": 450,
      "hp_max": 800,
      "x": 95,
      "y": 142,
      "direction": 2,
      "template_id": 10
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "entity_info_response",
  "seq": 157,
  "data": {
    "success": false,
    "error": "entity_not_found"
  }
}
```

#### Entity Info Response Object

**Common Fields (all entities):**

| Field | Type | Description |
|-------|------|-------------|
| `entity_id` | uint32 | Entity ID |
| `entity_type` | string | `"player"` or `"npc"` |
| `name` | string | Entity display name |
| `level` | int16 | Entity level |
| `hp` | int32 | Current hit points |
| `hp_max` | int32 | Maximum hit points |
| `x` | int16 | X coordinate |
| `y` | int16 | Y coordinate |
| `direction` | int16 | Facing direction (0-7) |

**Player-specific Fields (only when `entity_type` = `"player"`):**

| Field | Type | Description |
|-------|------|-------------|
| `faction` | string | `"aresden"`, `"elvine"`, or `"neutral"` |
| `class_type` | int16 | Class (0=Warrior, 1=Mage, 2=Archer) |
| `pk_count` | int32 | Total player kill count |
| `guild_name` | string | Guild name (only if player is in a guild) |

**NPC-specific Fields (only when `entity_type` = `"npc"`):**

| Field | Type | Description |
|-------|------|-------------|
| `template_id` | uint32 | NPC template ID |
| `npc_type` | string | NPC type (e.g., `"monster"`, `"vendor"`, `"guard"`) |

**Possible Errors:**

| Error Code | Description |
|------------|-------------|
| `entity_not_found` | No entity with the given ID exists |
| `invalid_request` | Missing or invalid entity_id field |
| `not_in_game` | Client is not in-game |
| `internal_error` | Required subsystems unavailable |

---

## Interact Messages

### `player_interact_request`

Request to interact with an NPC or object.

**Request:**
```json
{
  "type": "player_interact_request",
  "seq": 210,
  "data": {
    "x": 100,
    "y": 150,
    "target_type": "npc",
    "target_id": 5001,
    "timestamp": 1706500000000
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `x` | int16 | Yes | Current X coordinate (for validation) |
| `y` | int16 | Yes | Current Y coordinate (for validation) |
| `target_type` | string/int | No | Target type |
| `target_id` | uint32 | Yes | Target NPC or object ID |
| `timestamp` | uint64 | No | Client timestamp in milliseconds |

---

### `player_interact_response`

Server responds with interaction result. The response includes structured `interaction_data` whose contents depend on the `interaction_type`.

**Success Response (Shop):**
```json
{
  "type": "player_interact_response",
  "seq": 210,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5001,
      "interaction_type": "shop",
      "interaction_data": {
        "npc_name": "Blacksmith",
        "items": [
          {
            "template_id": 10,
            "name": "Short Sword",
            "price": 500,
            "category": "weapon"
          },
          {
            "template_id": 301,
            "name": "Leather Armor",
            "price": 1200,
            "category": "armor"
          }
        ]
      }
    }
  }
}
```

**Success Response (Bank):**
```json
{
  "type": "player_interact_response",
  "seq": 211,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5002,
      "interaction_type": "bank",
      "interaction_data": {
        "npc_name": "Banker",
        "items": [
          {
            "slot": 0,
            "item_id": 101,
            "name": "Long Sword",
            "count": 1,
            "durability": 45,
            "max_durability": 50
          }
        ]
      }
    }
  }
}
```

**Success Response (Dialog):**
```json
{
  "type": "player_interact_response",
  "seq": 212,
  "data": {
    "success": true,
    "result": {
      "success": true,
      "target_id": 5003,
      "interaction_type": "dialog",
      "interaction_data": {
        "npc_name": "Elder",
        "node_id": "start",
        "text": "Greetings, traveler. How may I help you?",
        "options": [
          { "label": "Tell me about this town", "action": "goto_node", "next_node": "town_info" },
          { "label": "Open shop", "action": "open_shop" },
          { "label": "Goodbye", "action": "close" }
        ]
      }
    }
  }
}
```

**Failure Response:**
```json
{
  "type": "player_interact_response",
  "seq": 210,
  "data": {
    "success": false,
    "error": "Target out of range"
  }
}
```

#### Interact Result Object

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether interaction succeeded |
| `target_id` | uint32 | Target entity ID |
| `interaction_type` | string | Type: `"dialog"`, `"shop"`, `"bank"` |
| `interaction_data` | object | Type-specific data (see below) |

#### Shop Interaction Data

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | NPC display name |
| `items` | array | Array of shop item objects |

**Shop Item Object:**

| Field | Type | Description |
|-------|------|-------------|
| `template_id` | uint32 | Item template ID |
| `name` | string | Item display name |
| `price` | int32 | Buy price in gold |
| `category` | string | Item category (e.g., `"weapon"`, `"armor"`, `"potion"`) |

#### Bank Interaction Data

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | NPC display name |
| `items` | array | Array of bank item objects (same format as Inventory Item Object) |

#### Dialog Interaction Data

| Field | Type | Description |
|-------|------|-------------|
| `npc_name` | string | NPC display name |
| `node_id` | string | Current dialog node ID |
| `text` | string | Dialog text to display |
| `options` | array | Array of dialog option objects |

**Dialog Option Object:**

| Field | Type | Description |
|-------|------|-------------|
| `label` | string | Display text for the option |
| `action` | string | Action type: `"goto_node"`, `"close"`, `"open_shop"`, `"open_bank"` |
| `next_node` | string | Next dialog node ID (only for `"goto_node"` action) |

---

## NPC Interaction Messages

These messages handle shop transactions, bank operations, and dialog choices after the initial `player_interact_request`/`player_interact_response` exchange has opened the interaction.

### `shop_buy_request`

Request to buy an item from a shop NPC.

**Request:**
```json
{
  "type": "shop_buy_request",
  "seq": 220,
  "data": {
    "npc_entity_id": 5001,
    "item_template_id": 10,
    "count": 1
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `npc_entity_id` | uint32 | Yes | - | Entity ID of the shop NPC |
| `item_template_id` | uint32 | Yes | - | Template ID of the item to buy |
| `count` | int16 | No | 1 | Number of items to buy |

---

### `shop_buy_response`

Server confirms or rejects the purchase.

**Success Response:**
```json
{
  "type": "shop_buy_response",
  "seq": 220,
  "data": {
    "success": true,
    "item_name": "Short Sword",
    "count": 1,
    "price_paid": 500,
    "gold_remaining": 14500
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_buy_response",
  "seq": 220,
  "data": {
    "success": false,
    "error": "Not enough gold"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether purchase succeeded |
| `item_name` | string | Name of item purchased (on success) |
| `count` | int16 | Number of items purchased (on success) |
| `price_paid` | int32 | Total gold spent (on success) |
| `gold_remaining` | int32 | Player's gold after purchase (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Not enough gold`
- `Inventory full`
- `Item not sold here`
- `NPC not found`
- `Not in range`

---

### `shop_sell_request`

Request a price quote for selling an item to a shop NPC. This does not complete the sale -- the client must follow up with `shop_sell_confirm_request` to finalize.

**Request:**
```json
{
  "type": "shop_sell_request",
  "seq": 221,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 3,
    "count": 1
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `npc_entity_id` | uint32 | Yes | - | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | - | Inventory slot of the item to sell |
| `count` | int16 | No | - | Number of items to sell (for stackable items; omit for all) |

---

### `shop_sell_response`

Server responds with the offered price for the item.

**Success Response:**
```json
{
  "type": "shop_sell_response",
  "seq": 221,
  "data": {
    "success": true,
    "item_name": "Short Sword",
    "offered_price": 250,
    "durability": 45
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_sell_response",
  "seq": 221,
  "data": {
    "success": false,
    "error": "Item cannot be sold"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether price quote succeeded |
| `item_name` | string | Name of item (on success) |
| `offered_price` | int32 | Gold the NPC will pay (on success) |
| `durability` | int16 | Current durability of the item (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Item cannot be sold`
- `Invalid inventory slot`
- `NPC not found`
- `Not in range`

---

### `shop_sell_confirm_request`

Confirm the sale of an item to a shop NPC after receiving a price quote via `shop_sell_response`.

**Request:**
```json
{
  "type": "shop_sell_confirm_request",
  "seq": 222,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 3,
    "count": 1
  }
}
```

| Field | Type | Required | Default | Description |
|-------|------|----------|---------|-------------|
| `npc_entity_id` | uint32 | Yes | - | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | - | Inventory slot of the item to sell |
| `count` | int16 | No | - | Number of items to sell (for stackable items; omit for all) |

---

### `shop_sell_confirm_response`

Server confirms or rejects the finalized sale.

**Success Response:**
```json
{
  "type": "shop_sell_confirm_response",
  "seq": 222,
  "data": {
    "success": true,
    "gold_received": 250,
    "gold_total": 14750
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_sell_confirm_response",
  "seq": 222,
  "data": {
    "success": false,
    "error": "Item no longer in slot"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether sale succeeded |
| `gold_received` | int32 | Gold earned from the sale (on success) |
| `gold_total` | int32 | Player's total gold after sale (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Item no longer in slot`
- `Item cannot be sold`
- `NPC not found`
- `Not in range`

---

### `shop_repair_request`

Request a cost quote for repairing an item at a shop NPC. This does not complete the repair -- the client must follow up with `shop_repair_confirm_request` to finalize.

**Request:**
```json
{
  "type": "shop_repair_request",
  "seq": 223,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | Inventory slot of the item to repair |

---

### `shop_repair_response`

Server responds with the repair cost estimate.

**Success Response:**
```json
{
  "type": "shop_repair_response",
  "seq": 223,
  "data": {
    "success": true,
    "item_name": "Long Sword",
    "repair_cost": 350,
    "durability": 25
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_repair_response",
  "seq": 223,
  "data": {
    "success": false,
    "error": "Item does not need repair"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether repair quote succeeded |
| `item_name` | string | Name of item (on success) |
| `repair_cost` | int32 | Gold cost to repair (on success) |
| `durability` | int16 | Current durability before repair (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Item does not need repair`
- `Item cannot be repaired`
- `Invalid inventory slot`
- `NPC not found`
- `Not in range`

---

### `shop_repair_confirm_request`

Confirm the repair of an item after receiving a cost quote via `shop_repair_response`.

**Request:**
```json
{
  "type": "shop_repair_confirm_request",
  "seq": 224,
  "data": {
    "npc_entity_id": 5001,
    "inventory_slot": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the shop NPC |
| `inventory_slot` | int16 | Yes | Inventory slot of the item to repair |

---

### `shop_repair_confirm_response`

Server confirms or rejects the finalized repair.

**Success Response:**
```json
{
  "type": "shop_repair_confirm_response",
  "seq": 224,
  "data": {
    "success": true,
    "new_durability": 50,
    "gold_spent": 350,
    "gold_remaining": 14150
  }
}
```

**Failure Response:**
```json
{
  "type": "shop_repair_confirm_response",
  "seq": 224,
  "data": {
    "success": false,
    "error": "Not enough gold"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether repair succeeded |
| `new_durability` | int16 | Item's durability after repair (on success) |
| `gold_spent` | int32 | Gold spent on repair (on success) |
| `gold_remaining` | int32 | Player's gold after repair (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Not enough gold`
- `Item no longer in slot`
- `Item cannot be repaired`
- `NPC not found`
- `Not in range`

---

### `bank_deposit_request`

Request to deposit an item from inventory into the bank.

**Request:**
```json
{
  "type": "bank_deposit_request",
  "seq": 230,
  "data": {
    "npc_entity_id": 5002,
    "inventory_slot": 5
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the bank NPC |
| `inventory_slot` | int16 | Yes | Inventory slot of the item to deposit |

---

### `bank_deposit_response`

Server confirms or rejects the deposit.

**Success Response:**
```json
{
  "type": "bank_deposit_response",
  "seq": 230,
  "data": {
    "success": true,
    "item_name": "Health Potion"
  }
}
```

**Failure Response:**
```json
{
  "type": "bank_deposit_response",
  "seq": 230,
  "data": {
    "success": false,
    "error": "Bank is full"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether deposit succeeded |
| `item_name` | string | Name of item deposited (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Bank is full`
- `Invalid inventory slot`
- `NPC not found`
- `Not in range`

---

### `bank_withdraw_request`

Request to withdraw an item from the bank into inventory.

**Request:**
```json
{
  "type": "bank_withdraw_request",
  "seq": 231,
  "data": {
    "npc_entity_id": 5002,
    "bank_slot": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the bank NPC |
| `bank_slot` | int16 | Yes | Bank slot of the item to withdraw |

---

### `bank_withdraw_response`

Server confirms or rejects the withdrawal.

**Success Response:**
```json
{
  "type": "bank_withdraw_response",
  "seq": 231,
  "data": {
    "success": true,
    "item_name": "Health Potion"
  }
}
```

**Failure Response:**
```json
{
  "type": "bank_withdraw_response",
  "seq": 231,
  "data": {
    "success": false,
    "error": "Inventory full"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether withdrawal succeeded |
| `item_name` | string | Name of item withdrawn (on success) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Inventory full`
- `Invalid bank slot`
- `NPC not found`
- `Not in range`

---

### `dialog_choice_request`

Send a dialog choice to an NPC during a dialog interaction.

**Request:**
```json
{
  "type": "dialog_choice_request",
  "seq": 240,
  "data": {
    "npc_entity_id": 5003,
    "node_id": "start",
    "choice_index": 0
  }
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `npc_entity_id` | uint32 | Yes | Entity ID of the dialog NPC |
| `node_id` | string | Yes | Current dialog node ID |
| `choice_index` | int16 | Yes | Index of the chosen option in the `options` array |

---

### `dialog_choice_response`

Server responds with the result of the dialog choice.

**Success Response (goto_node -- continues dialog):**
```json
{
  "type": "dialog_choice_response",
  "seq": 240,
  "data": {
    "success": true,
    "action": "goto_node",
    "node_id": "town_info",
    "text": "This town was founded many years ago by the great king...",
    "options": [
      { "label": "Tell me more", "action": "goto_node", "next_node": "town_history" },
      { "label": "Back", "action": "goto_node", "next_node": "start" },
      { "label": "Goodbye", "action": "close" }
    ]
  }
}
```

**Success Response (open_shop -- transitions to shop):**
```json
{
  "type": "dialog_choice_response",
  "seq": 241,
  "data": {
    "success": true,
    "action": "open_shop"
  }
}
```

**Success Response (open_bank -- transitions to bank):**
```json
{
  "type": "dialog_choice_response",
  "seq": 242,
  "data": {
    "success": true,
    "action": "open_bank"
  }
}
```

**Success Response (close -- ends dialog):**
```json
{
  "type": "dialog_choice_response",
  "seq": 243,
  "data": {
    "success": true,
    "action": "close"
  }
}
```

**Success Response (not_implemented):**
```json
{
  "type": "dialog_choice_response",
  "seq": 244,
  "data": {
    "success": true,
    "action": "not_implemented"
  }
}
```

**Failure Response:**
```json
{
  "type": "dialog_choice_response",
  "seq": 240,
  "data": {
    "success": false,
    "error": "Invalid choice index"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `success` | bool | Whether the choice was processed |
| `action` | string | Result action: `"goto_node"`, `"close"`, `"open_shop"`, `"open_bank"`, `"not_implemented"` |
| `node_id` | string | New dialog node ID (only for `"goto_node"`) |
| `text` | string | Dialog text for the new node (only for `"goto_node"`) |
| `options` | array | Array of dialog option objects (only for `"goto_node"`, see Dialog Option Object) |
| `error` | string | Error message (on failure) |

**Possible Errors:**
- `Invalid choice index`
- `Invalid node`
- `NPC not found`
- `Not in range`
- `No active dialog`

---
