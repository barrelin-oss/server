# Guild System

[← Back to Protocol Index](../JSON_PROTOCOL.md)

Player-facing guild management messages. All require being in-game.

### `guild_create_request`

**Direction:** Client → Server

Create a new guild. The requesting player becomes guild master.

```json
{
  "type": "guild_create_request",
  "seq": 1,
  "data": {
    "name": "BloodGuard",
    "tag": "BG"
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Guild name (3-20 chars) |
| `tag` | string | Short guild tag (2-4 chars) |

### `guild_create_response`

**Direction:** Server → Client

```json
{
  "type": "guild_create_response",
  "seq": 1,
  "data": {
    "success": true,
    "guild_name": "BloodGuard",
    "tag": "BG"
  }
}
```

Errors: `name_taken`, `invalid_name`, `already_in_guild`, `insufficient_gold`

### `guild_disband_request`

**Direction:** Client → Server

Disband the player's guild. Must be guild master.

```json
{
  "type": "guild_disband_request",
  "seq": 1,
  "data": {}
}
```

### `guild_disband_response`

**Direction:** Server → Client

```json
{
  "type": "guild_disband_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `not_in_guild`, `insufficient_permissions`

### `guild_leave_request`

**Direction:** Client → Server

Leave the current guild. Guild masters cannot leave (must disband).

```json
{
  "type": "guild_leave_request",
  "seq": 1,
  "data": {}
}
```

### `guild_leave_response`

**Direction:** Server → Client

```json
{
  "type": "guild_leave_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `not_in_guild`

### `guild_kick_request`

**Direction:** Client → Server

Kick a member from the guild. Requires kick permission and higher rank.

```json
{
  "type": "guild_kick_request",
  "seq": 1,
  "data": {
    "target_name": "Bob"
  }
}
```

### `guild_kick_response`

**Direction:** Server → Client

```json
{
  "type": "guild_kick_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `player_not_found`, `not_in_guild`, `cannot_kick_self`, `cannot_kick_higher_rank`, `insufficient_permissions`

### `guild_invite_request`

**Direction:** Client → Server

Invite a player to the guild. Creates a pending invite — the target must accept via `guild_invite_respond_request`. Also available as `/ginvite <player>` chat command.

```json
{
  "type": "guild_invite_request",
  "seq": 1,
  "data": {
    "target_name": "Bob"
  }
}
```

### `guild_invite_response`

**Direction:** Server → Client

Confirms the invite was sent (not that the player joined).

```json
{
  "type": "guild_invite_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `player_not_found`, `not_in_guild`, `guild_full`, `already_in_guild`, `insufficient_permissions`

### `guild_invite_received`

**Direction:** Server → Client (unsolicited push to invite target)

Notifies a player they've been invited to a guild. The player should show this as a prompt and use `guild_invite_respond_request` or `/gaccept`/`/gdecline` to respond. Invites expire after 60 seconds.

```json
{
  "type": "guild_invite_received",
  "seq": 0,
  "data": {
    "guild_name": "Knights",
    "guild_tag": "KNT",
    "inviter_name": "Alice"
  }
}
```

### `guild_invite_respond_request`

**Direction:** Client → Server

Accept or decline a pending guild invite. Also available as `/gaccept` and `/gdecline` chat commands.

```json
{
  "type": "guild_invite_respond_request",
  "seq": 1,
  "data": {
    "accept": true
  }
}
```

### `guild_invite_respond_response`

**Direction:** Server → Client

```json
{
  "type": "guild_invite_respond_response",
  "seq": 1,
  "data": {
    "success": true,
    "accepted": true,
    "guild_name": "Knights",
    "guild_tag": "KNT"
  }
}
```

On decline: `{"success": true, "accepted": false}`

Errors: `guild_not_found` (no pending invite or expired), `already_in_guild`, `guild_full`

### `guild_promote_request`

**Direction:** Client → Server

Promote a guild member by one rank.

```json
{
  "type": "guild_promote_request",
  "seq": 1,
  "data": {
    "target_name": "Bob"
  }
}
```

### `guild_promote_response`

**Direction:** Server → Client

```json
{
  "type": "guild_promote_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `player_not_found`, `insufficient_permissions`, `cannot_promote_higher`

### `guild_demote_request`

**Direction:** Client → Server

Demote a guild member by one rank.

```json
{
  "type": "guild_demote_request",
  "seq": 1,
  "data": {
    "target_name": "Bob"
  }
}
```

### `guild_demote_response`

**Direction:** Server → Client

```json
{
  "type": "guild_demote_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `player_not_found`, `insufficient_permissions`

### `guild_set_motd_request`

**Direction:** Client → Server

Set the guild's message of the day.

```json
{
  "type": "guild_set_motd_request",
  "seq": 1,
  "data": {
    "motd": "Welcome to BloodGuard! Rally at 8pm."
  }
}
```

### `guild_set_motd_response`

**Direction:** Server → Client

```json
{
  "type": "guild_set_motd_response",
  "seq": 1,
  "data": {
    "success": true
  }
}
```

Errors: `not_in_guild`, `insufficient_permissions`

### `guild_info_request`

**Direction:** Client → Server

Request full info about the player's current guild including member list.

```json
{
  "type": "guild_info_request",
  "seq": 1,
  "data": {}
}
```

### `guild_info_response`

**Direction:** Server → Client

```json
{
  "type": "guild_info_response",
  "seq": 1,
  "data": {
    "success": true,
    "guild_name": "BloodGuard",
    "tag": "BG",
    "motd": "Welcome!",
    "member_count": 3,
    "master_name": "Alice",
    "members": [
      {"name": "Alice", "rank": 0, "rank_name": "Guild Master", "is_online": true},
      {"name": "Bob", "rank": 3, "rank_name": "Member", "is_online": true},
      {"name": "Charlie", "rank": 4, "rank_name": "Recruit", "is_online": false}
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| `guild_name` | string | Guild name |
| `tag` | string | Guild tag |
| `motd` | string | Message of the day |
| `member_count` | int | Number of members |
| `master_name` | string | Guild master's name |
| `members` | array | Member list with rank info and online status |

### `guild_update`

**Direction:** Server → Client (broadcast)

Unsolicited broadcast sent to online guild members when guild state changes.

```json
{
  "type": "guild_update",
  "seq": 0,
  "data": {
    "action": "member_joined",
    "guild_name": "BloodGuard",
    "player_name": "NewMember"
  }
}
```

| Action | Description | Extra fields |
|--------|-------------|--------------|
| `member_joined` | New member added to guild | `player_name` |
| `member_left` | Member left the guild | `player_name` |
| `member_kicked` | Member was kicked | `player_name` |
| `you_were_kicked` | Sent to kicked player only | — |
| `member_promoted` | Member was promoted | `player_name` |
| `member_demoted` | Member was demoted | `player_name` |
| `guild_disbanded` | Guild was disbanded | — |
| `motd_changed` | MOTD was updated | `motd` |

### Guild Chat Commands

Guild operations are also available as `/` chat commands:

| Command | Description | Permission |
|---------|-------------|------------|
| `/gcreate <name> [tag]` | Create a guild | Not in a guild |
| `/gdisband` | Disband your guild | Guild master only |
| `/ginvite <player>` | Invite a player (pending accept) | Invite permission |
| `/gkick <player>` | Kick a member | Kick permission |
| `/gaccept` | Accept a pending guild invite | Has pending invite |
| `/gdecline` | Decline a pending guild invite | Has pending invite |
| `/gquit` | Leave your guild | Non-guild-master |

Commands are sent via `command_request` or typed in chat with `/` prefix.

### Enter Game Guild Data

The `enter_game_response` character object includes guild state:

| Field | Type | Description |
|-------|------|-------------|
| `guild_name` | string | Guild name (empty if no guild) |
| `guild_tag` | string | Guild tag (empty if no guild) |
| `guild_rank` | uint8 | Rank (0=guild_master, 1=officer, 2=veteran, 3=member, 4=recruit) |
