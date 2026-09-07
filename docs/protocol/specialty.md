# Specialties (monster mastery)

Every kill of a monster type counts towards the player's specialty in that monster. Each level
unlocks the next bonus of the monster's ladder (`bin/game_configs/specialties.yaml`), applied only
against that monster: flat or percent damage, flat or percent damage reduction, flat or percent hit
ratio, drop rate. Kills are credited to the killer. Level L needs `base_kills * L * (L + 1) / 2`
kills. Progress is saved in `characters.specialty_data`.

## Specialty object

```json
{
  "npc_type": 14,
  "npc_name": "Orc",
  "kills": 320,
  "level": 2,
  "max_level": 8,
  "next_level_kills": 900,
  "unlocked": ["damage", "damage_reduction"],
  "bonuses": {
    "damage": 2,
    "damage_pct": 0.0,
    "damage_reduction_pct": 1,
    "damage_reduction_mult_pct": 0.0,
    "hit_ratio": 0,
    "hit_ratio_pct": 0.0,
    "drop_rate_pct": 0.0
  }
}
```

`bonuses` are the totals the player currently enjoys against that monster. Percent fields are
already in percent (3.0 = +3%). `next_level_kills` is 0 once the ladder is complete.

## Messages

### `specialty_list_request` → `specialty_list_response`

Client → server: `{}`. Server → client: `{"success": true, "specialties": [specialty, ...]}`, every
monster the player has killed at least once that has a ladder, most kills first.

### `specialty_update` (server → client, push)

Sent when a kill raises a specialty level. Data is one specialty object. The server also writes a
system chat line: `Orc specialty level 2: +1% damage reduction`.
