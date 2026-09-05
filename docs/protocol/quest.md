# Quest Protocol

Quests come from the legacy `Quest.cfg` (converted to `bin/game_configs/quests.yaml`)
and are handed out by the city hall officers, exactly as in the original game where
only City Hall (`iWho = 4`) gave quests: **Kennedy** for Aresden, **William** for
Elvine. Every quest is faction-bound and level-banded (`min_level`/`max_level`), and
hunting quests are repeatable after turn-in.

Two objective kinds exist in the data:

| Legacy type | Objective | Example |
|---|---|---|
| 1 (hunt) | `kill` — kill N of one NPC type | Hunt Giant-Ant x22 (level 11-20, Aresden) |
| 7 (go place) | `visit` — reach map coordinates within a radius | Scout elvine (218, 90) |

Rewards are experience, gold (legacy item 90) and items. Reputation
(`contribution`) is recorded on the template but no system consumes it yet.

## Flow

1. Talk to the officer: `npc_dialog_request` then `dialog_choice_request` on the
   option whose action is `open_quests` (or send `quest_list_request` directly).
2. `quest_accept_request` for one of the quests marked `available`.
3. Kill the targets. After every qualifying kill the server pushes `quest_update`.
4. When every objective is `complete`, return to the officer and send
   `quest_complete_request`, or pick the dialog option `claim_rewards`, which turns in
   every finished quest from that officer at once.

Requests that name an NPC go through the same interaction check as shops: the NPC
must exist, be alive and be friendly. A quest can only be accepted from, and turned
in to, its own giver (`wrong_npc` otherwise).

## Quest object

Used by every response and by `quest_update`.

```json
{
  "quest_id": 1,
  "name": "Hunt Giant-Ant x22",
  "description": "Hunt 22 Giant-Ant for Aresden (level 11-20), around aresden",
  "min_level": 11,
  "max_level": 20,
  "repeatable": true,
  "giver_npc_id": 84,
  "status": "active",
  "objectives": [
    { "description": "Kill 22 Giant-Ant", "type": "kill", "target_npc_id": 6,
      "target_name": "Giant-Ant", "current": 5, "required": 22, "complete": false }
  ],
  "rewards": { "experience": 100, "gold": 250, "items": [] }
}
```

| Field | Notes |
|---|---|
| `status` | `available`, `active`, `complete` (all objectives done, not yet turned in), `turned_in`, `failed`, `abandoned` |
| `objectives[].type` | `kill`, `kill_player`, `collect`, `deliver`, `visit`, `talk`, `other` |
| `objectives[].current` / `required` | Progress; `current` is 0 for quests not yet accepted |
| `visit` objectives | carry `map_id`, `x`, `y`, `radius` instead of `target_npc_id` |

## Messages

### `quest_list_request` → `quest_list_response`

```json
{ "type": "quest_list_request", "seq": 10, "data": { "npc_entity_id": 5012 } }
```
```json
{ "type": "quest_list_response", "seq": 10,
  "data": { "success": true, "npc_entity_id": 5012, "quests": [ /* quest objects */ ] } }
```

The list holds the quests this officer offers to this player right now (level and
faction already filtered) plus the ones already taken from this officer, with their
progress. Errors: `quests_unavailable`, plus the NPC interaction errors.

### `quest_accept_request` → `quest_accept_response`

```json
{ "type": "quest_accept_request", "seq": 11, "data": { "npc_entity_id": 5012, "quest_id": 1 } }
```
```json
{ "type": "quest_accept_response", "seq": 11, "data": { "success": true, "quest_id": 1 } }
```

On success the server also pushes a `quest_update` with the fresh state. Errors:
`quest_not_found`, `wrong_npc`, `level_out_of_range`, `wrong_faction`,
`already_active`, `quest_log_full`, `missing_prerequisite`, `cannot_accept`.

### `quest_abandon_request` → `quest_abandon_response`

```json
{ "type": "quest_abandon_request", "seq": 12, "data": { "quest_id": 1 } }
```

Errors: `not_active`.

### `quest_complete_request` → `quest_complete_response`

```json
{ "type": "quest_complete_request", "seq": 13, "data": { "npc_entity_id": 5012, "quest_id": 1 } }
```
```json
{ "type": "quest_complete_response", "seq": 13,
  "data": { "success": true, "quest_id": 1, "rewards": { "experience": 100, "gold": 250, "items": [] } } }
```

Rewards are applied before the response: experience through the normal
`experience_update`, gold through `gold_update` (reason `quest_reward`), items into
the inventory (skipped with a server warning when the inventory is full). Errors:
`quest_not_found`, `wrong_npc`, `objectives_incomplete`, `not_active`.

The dialog action `claim_rewards` turns in every finished quest from that officer and
sends one `quest_complete_response` per quest with `seq` 0.

### `quest_journal_request` → `quest_journal_response`

```json
{ "type": "quest_journal_request", "seq": 14, "data": {} }
```
```json
{ "type": "quest_journal_response", "seq": 14, "data": { "quests": [ /* active quest objects */ ] } }
```

### `quest_update` (server push, `seq` 0)

```json
{ "type": "quest_update", "seq": 0, "data": { /* quest object */ } }
```

Sent after a quest is accepted and after every kill that counts toward one of the
player's active quests. Party members do not share kill progress: only the killer
advances, as in the original.

## Server configuration

Quests load from `bin/game_configs/quests.yaml` at startup after the NPC registry and
maps. Rows whose NPC type or map cannot be resolved are skipped with a warning. The
officers spawn from `mapdata/*.yaml` with the HelbreathX-local spot types `20`
(Kennedy) and `21` (William).
