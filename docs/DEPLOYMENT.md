# Deployment Guide

This guide covers building, configuring, and running the Helbreath game server.

---

## Prerequisites

| Dependency | Version | Purpose |
|------------|---------|---------|
| C++ compiler | C++20 support (GCC 11+, Clang 14+, MSVC 19.29+) | Build |
| CMake | 3.20+ | Build system |
| vcpkg | Latest | C++ package manager |
| PostgreSQL | 13+ | Database |
| Node.js | 18+ | Database migration tool (optional) |

### vcpkg Dependencies (installed automatically)

| Package | Purpose |
|---------|---------|
| spdlog | Logging |
| nlohmann-json | JSON protocol |
| yaml-cpp | Config and game data |
| libpqxx | PostgreSQL client |
| libsodium | Password hashing (Argon2id) |
| ixwebsocket | WebSocket server |
| openssl | TLS/SSL |
| zlib | Compression |
| gtest | Unit testing |

---

## Building

### Linux (GCC/Clang)

```bash
# Clone and enter repo
git clone <repo-url>
cd server

# Configure (Debug)
cmake --preset default

# Build
cmake --build --preset default

# Or manually:
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
```

### Windows (MSVC)

```bash
cmake --preset default
cmake --build --preset default
```

### Release Build

```bash
cmake --preset release
cmake --build --preset release
```

Release enables LTO (link-time optimization) for better performance.

### Build Output

Everything goes to `bin/`:

```
bin/
├── hgserver              # Server executable
├── hgserver_tests        # Test executable
├── server.yaml           # Server configuration (you create this)
├── game_configs/         # Game data YAML files
├── mapdata/              # Map binary + config files
├── GameData/             # Legacy state files
└── logs/                 # Log output (created at runtime)
```

### Running Tests

```bash
./bin/hgserver_tests
```

---

## Database Setup

### 1. Create the Database

```bash
# Create the role and database
sudo -u postgres createuser hgserver
sudo -u postgres createdb -O hgserver helbreath

# Enable the UUID extension (requires superuser)
sudo -u postgres psql -d helbreath -c 'CREATE EXTENSION IF NOT EXISTS "uuid-ossp";'

# Set a password
sudo -u postgres psql -c "ALTER USER hgserver PASSWORD 'your_password_here';"
```

### 2. Initialize the Schema

**Option A: Fresh install (recommended for new deployments)**

```bash
psql -U hgserver -d helbreath -f src/database/schema.sql
```

**Option B: Migration tool (recommended for existing deployments)**

```bash
cd tools/migrate
npm install        # First time only
npx tsx migrate.ts migrate
```

The migration tool reads database credentials from `bin/server.yaml` (override with `HBSERVER_CFG` env var). It tracks applied migrations in a `migrations` table and runs pending ones in order.

Migration commands:

| Command | Description |
|---------|-------------|
| `npx tsx migrate.ts migrate` | Apply pending migrations |
| `npx tsx migrate.ts status` | Show applied/pending migrations |
| `npx tsx migrate.ts rollback` | Roll back the last batch |
| `npx tsx migrate.ts fresh` | Drop all tables and re-run everything |
| `npx tsx migrate.ts create <name>` | Scaffold a new migration file |

### Database Tables

| Table | Purpose |
|-------|---------|
| `accounts` | User accounts, password hashes, admin levels, bans |
| `characters` | Player characters (stats, inventory, equipment as JSONB) |
| `guilds` | Guild info (name, faction, leader, warehouse) |
| `guild_members` | Guild membership and rank |
| `friends` | Accepted friendships (bidirectional) |
| `friend_requests` | Pending friend requests |
| `friend_blocks` | Unidirectional player blocks |
| `sessions` | Active auth sessions |
| `login_history` | Login audit log |
| `item_log` | Item transaction log |
| `chat_log` | Chat moderation log |
| `war_history` | Completed war event records |
| `war_participants` | Per-player war statistics and rewards |

---

## Configuration

Copy the example config into `bin/` and edit it:

```bash
cp server.yaml.example bin/server.yaml
```

### Minimal Configuration

The only setting that **must** be changed is the database password:

```yaml
database:
  password: your_actual_password
```

Everything else has sensible defaults for a single-server deployment.

### Full Configuration Reference

```yaml
# Server Identity
server:
  name: HGServer           # Server name shown to clients
  port: 2848               # Game server port

# Self-contained mode runs auth + game in one process with PostgreSQL.
# Set false only if using legacy external auth/gate/log servers.
self_contained: true

# Database (required when self_contained: true)
database:
  host: localhost
  port: 5432
  name: helbreath
  user: hgserver
  password: your_password_here   # REQUIRED - no default
  pool_size: 10                  # Connection pool size

# WebSocket server
websocket:
  bind: 0.0.0.0             # Bind address
  port: 2848                 # WebSocket port
  max_connections: 2000      # Max simultaneous connections

# Authentication
auth:
  max_characters: 4          # Characters per account
  allow_registration: true   # Open registration
  session_timeout: 3600      # Session expiry (seconds)

# Auto-save
auto_save:
  enabled: true
  interval_seconds: 300      # Save all players every 5 minutes

# Logging (levels: trace, debug, info, warn, error, critical, off)
logging:
  console_level: info        # Console output level
  file_level: debug          # File output level
  directory: logs            # Log directory (relative to working dir)
  file: hgserver.log         # Log filename
  max_size_mb: 10            # Max log file size before rotation
  max_files: 3               # Number of rotated log files to keep

# Legacy binary protocol (for old clients)
legacy:
  enabled: false
  port: 2849

# Forum auth integration (optional)
forum_auth:
  enabled: false
  login_url: ""
  validate_url: ""
  api_key: ""
```

### Legacy INI Format

The server also supports `GServer.cfg` (INI-style). See `GServer.cfg.example`. The config loader auto-detects format by file extension. Override the config path with `--config`:

```bash
./hgserver --config GServer.cfg
```

---

## Game Data Files

The server requires game data files in specific directories relative to the working directory (`bin/`).

### Map Data (`mapdata/`)

| File Type | Count | Description |
|-----------|-------|-------------|
| `*.amd` | 76 | Binary tile data (original Helbreath format) |
| `*.yaml` | 94 | Map configuration (spawners, teleports, safe zones, etc.) |

All `.amd` files in `mapdata/` are loaded automatically on startup. Each map's `.yaml` config is loaded alongside it.

### Game Configs (`game_configs/`)

| File | Description |
|------|-------------|
| `items.yaml` | Item definitions (stats, requirements, effects) |
| `npcs.yaml` | NPC/monster templates (stats, AI, combat) |
| `magic.yaml` | Spell definitions (cost, damage, targeting) |
| `skills.yaml` | Skill definitions and leveling tier tables |
| `loot_tables.yaml` | NPC loot drops (pools, chances, boss multi-drops) |
| `spawn_tables.yaml` | Random mob spawn rules |
| `shops.yaml` | NPC shop inventories and pricing |
| `dialogs.yaml` | NPC dialog trees |
| `teleports.yaml` | NPC-triggered teleport destinations |
| `build_recipes.yaml` | Manufacturing (smithing) recipes |
| `craft_recipes.yaml` | Gem crafting recipes |
| `recipes.yaml` | Alchemy recipes |
| `mining.yaml` | Mineral node types |
| `fishing.yaml` | Fish types and difficulty |
| `crusade.yaml` | Crusade war configuration |
| `crusade_structures.yaml` | Crusade structure definitions |
| `notice.txt` | Server notice / MOTD |

---

## Running the Server

```bash
cd bin
./hgserver
```

### Command-Line Options

| Flag | Description |
|------|-------------|
| `--config <file>` | Config file path (default: `server.yaml`) |
| `--hash-password <pw>` | Hash a password and exit (utility) |
| `--verify-password <pw> <hash>` | Verify a password against a hash and exit |
| `--dump-loot-tables` | Print loot probabilities for all NPCs and exit |
| `--help` / `-h` | Show help |

### Startup Sequence

1. Load and parse configuration
2. Initialize logging
3. Connect to PostgreSQL (connection pool)
4. Load map data from `mapdata/` (all `.amd` + `.yaml` files)
5. Load game configs from `game_configs/`
6. Register NPC spawn points
7. Load guilds and friend lists from database
8. Start WebSocket server on configured port
9. Register auto-save scheduler task
10. Enter main game loop (10 ticks/second)

### Shutdown

The server handles `SIGINT` and `SIGTERM` gracefully:

1. Saves all online players to database
2. Stops the WebSocket server
3. Cleans up all subsystems
4. Exits

---

## Production Recommendations

### Logging

For production, reduce log verbosity to avoid disk I/O overhead:

```yaml
logging:
  console_level: warn
  file_level: info
```

### Database

- Use a dedicated PostgreSQL instance or managed service
- Tune `pool_size` to match expected concurrent players (default 10 handles ~200 players comfortably; increase for larger populations)
- Run `cleanup_expired_sessions()` periodically (e.g., via `pg_cron`) to prune stale sessions
- Back up the database regularly — character data, guilds, and war history live here

### Firewall

Only one port needs to be exposed:

| Port | Protocol | Purpose |
|------|----------|---------|
| 2848 | TCP (WebSocket) | Client connections |

PostgreSQL (5432) should **not** be exposed publicly.

### Systemd Service

Example unit file (`/etc/systemd/system/hgserver.service`):

```ini
[Unit]
Description=Helbreath Game Server
After=network.target postgresql.service
Requires=postgresql.service

[Service]
Type=simple
User=hgserver
WorkingDirectory=/opt/hgserver/bin
ExecStart=/opt/hgserver/bin/hgserver
Restart=on-failure
RestartSec=5

# Logging to journald (server also writes to logs/)
StandardOutput=journal
StandardError=journal

# Resource limits
LimitNOFILE=65536

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now hgserver
sudo journalctl -u hgserver -f    # Follow logs
```

### Reverse Proxy (Optional)

To terminate TLS in front of the WebSocket server, use nginx:

```nginx
upstream hgserver {
    server 127.0.0.1:2848;
}

server {
    listen 443 ssl;
    server_name game.example.com;

    ssl_certificate     /etc/letsencrypt/live/game.example.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/game.example.com/privkey.pem;

    location / {
        proxy_pass http://hgserver;
        proxy_http_version 1.1;
        proxy_set_header Upgrade $http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
        proxy_read_timeout 86400s;
        proxy_send_timeout 86400s;
    }
}
```

---

## Quick Start Checklist

```
1. [ ] Build:       cmake --preset default && cmake --build --preset default
2. [ ] PostgreSQL:  createuser hgserver && createdb -O hgserver helbreath
3. [ ] Schema:      psql -U hgserver -d helbreath -f src/database/schema.sql
4. [ ] Config:      cp server.yaml.example bin/server.yaml  (set database.password)
5. [ ] Verify data: bin/mapdata/*.amd exists, bin/game_configs/*.yaml exists
6. [ ] Run:         cd bin && ./hgserver
7. [ ] Connect:     WebSocket client to ws://localhost:2848
```
