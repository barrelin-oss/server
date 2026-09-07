# Achievements

Counters the game bumps (monsters killed, per type, elites, quests turned in, gold looted from
monsters and chests, treasure chests opened, character level, specialty levels gained) unlock the
achievements of `bin/game_configs/achievements.yaml`. Each is worth points and may carry a title.
Progress is saved in `characters.achievement_data`.

## Achievement object

```json
{
  "id": 101,
  "name": "Hunter",
  "description": "Kill 1,000 monsters",
  "category": "pvm",
  "title": "",
  "kind": "kills_total",
  "param": 0,
  "target": 1000,
  "points": 10,
  "current": 412,
  "unlocked_at": 0
}
```

`current` is capped at `target`; `unlocked_at` is the unix time of the unlock, 0 while locked.

## Messages

### `achievement_list_request` → `achievement_list_response`

Client → server: `{}`. Server → client:
`{"success": true, "points": 35, "achievements": [achievement, ...]}` with every definition.

### `achievement_unlocked` (server → client, push)

One achievement object, sent the moment it unlocks. The server also writes a system chat line:
`Achievement unlocked: Hunter (+10 points)`.
