-- schema.sql
-- PostgreSQL schema for Helbreath server authentication and persistence
--
-- FRESH INSTALL: psql -U hgserver -d helbreath -f schema.sql
-- EXISTING DB:   cd tools/migrate && npx tsx migrate.ts
--
-- This file represents the current schema state. Keep it in sync with
-- migration files in tools/migrate/migrations/ so fresh installs match
-- migrated databases. Never hand-edit an existing database — always
-- create a migration file instead.

-- Enable UUID extension for session tokens
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Accounts table
CREATE TABLE IF NOT EXISTS accounts (
    id              SERIAL PRIMARY KEY,
    username        VARCHAR(32) UNIQUE NOT NULL,
    password_hash   VARCHAR(255) NOT NULL DEFAULT '',
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    last_login      TIMESTAMP WITH TIME ZONE,
    is_banned       BOOLEAN DEFAULT FALSE,
    ban_reason      TEXT,
    ban_expires     TIMESTAMP WITH TIME ZONE,
    admin_level     SMALLINT DEFAULT 0,
    forum_member_id BIGINT,

    CONSTRAINT username_lowercase CHECK (username = LOWER(username)),
    CONSTRAINT username_format CHECK (username ~ '^[a-z0-9_]{3,32}$')
);
CREATE UNIQUE INDEX IF NOT EXISTS idx_accounts_forum_member_id ON accounts (forum_member_id) WHERE forum_member_id IS NOT NULL;

-- Characters table
CREATE TABLE IF NOT EXISTS characters (
    id              SERIAL PRIMARY KEY,
    account_id      INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name            VARCHAR(32) UNIQUE NOT NULL,
    level           SMALLINT DEFAULT 1 CHECK (level >= 1 AND level <= 255),
    class_type      SMALLINT NOT NULL CHECK (class_type >= 0),
    nation          SMALLINT NOT NULL CHECK (nation >= 0 AND nation <= 2),
    gender          SMALLINT NOT NULL CHECK (gender >= 1 AND gender <= 2),

    -- Location
    -- Note: pos_x/pos_y = -1 indicates "use map's initial spawn point"
    map_name        VARCHAR(32) DEFAULT 'default',
    pos_x           SMALLINT DEFAULT -1,
    pos_y           SMALLINT DEFAULT -1,

    -- Experience and progression
    experience      BIGINT DEFAULT 0 CHECK (experience >= 0),

    -- Resources
    hp              INTEGER DEFAULT 0,
    hp_max          INTEGER DEFAULT 0,
    mp              INTEGER DEFAULT 0,
    mp_max          INTEGER DEFAULT 0,
    sp              INTEGER DEFAULT 0,
    sp_max          INTEGER DEFAULT 0,
    gold            INTEGER DEFAULT 0 CHECK (gold >= 0),

    -- Base stats
    strength        SMALLINT DEFAULT 10 CHECK (strength >= 0),
    dexterity       SMALLINT DEFAULT 10 CHECK (dexterity >= 0),
    vitality        SMALLINT DEFAULT 10 CHECK (vitality >= 0),
    intelligence    SMALLINT DEFAULT 10 CHECK (intelligence >= 0),
    magic           SMALLINT DEFAULT 10 CHECK (magic >= 0),
    charisma        SMALLINT DEFAULT 10 CHECK (charisma >= 0),
    luck            SMALLINT DEFAULT 0 CHECK (luck >= 0),

    -- Appearance
    hair_style      SMALLINT DEFAULT 0,
    hair_color      SMALLINT DEFAULT 0,
    skin_color      SMALLINT DEFAULT 0,
    underwear_color SMALLINT DEFAULT 0,

    -- PK/Criminal status
    pk_count        INTEGER DEFAULT 0,
    pk_points       INTEGER DEFAULT 0,
    reward_gold     INTEGER DEFAULT 0,
    hunger_level    SMALLINT DEFAULT 100,

    -- Progression
    enemy_kill_count INTEGER DEFAULT 0,
    contribution    INTEGER DEFAULT 0,
    stat_points_available SMALLINT DEFAULT 0,

    -- JSON data for complex serialized data (JSONB for queryability)
    -- skills_data: [{"type":5,"level":45,"total_uses":15000,"uses":200}, ...]
    skills_data     JSONB DEFAULT '[]'::jsonb,
    quest_data      JSONB DEFAULT '[]'::jsonb,
    magic_data      JSONB DEFAULT '[]'::jsonb,

    -- Timestamps
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    last_played     TIMESTAMP WITH TIME ZONE,
    total_playtime  BIGINT DEFAULT 0,  -- In seconds

    CONSTRAINT name_format CHECK (name ~ '^[A-Za-z][A-Za-z0-9_]{2,31}$')
);

-- Guilds table
CREATE TABLE IF NOT EXISTS guilds (
    id              SERIAL PRIMARY KEY,
    name            VARCHAR(32) UNIQUE NOT NULL,
    tag             VARCHAR(8) NOT NULL DEFAULT '',
    motd            TEXT NOT NULL DEFAULT '',
    nation          SMALLINT NOT NULL CHECK (nation >= 0 AND nation <= 2),
    leader_id       INTEGER REFERENCES characters(id) ON DELETE SET NULL,

    -- Guild progression
    level           INTEGER NOT NULL DEFAULT 1,
    experience      BIGINT NOT NULL DEFAULT 0,

    -- Guild stats
    total_kills     BIGINT NOT NULL DEFAULT 0,
    total_deaths    BIGINT NOT NULL DEFAULT 0,
    gold_bank       BIGINT NOT NULL DEFAULT 0,
    warehouse_data  BYTEA,

    -- Rank configuration (JSON array of {name, permissions})
    rank_configs    JSONB NOT NULL DEFAULT '[]'::jsonb,

    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    CONSTRAINT guild_name_format CHECK (name ~ '^[A-Za-z][A-Za-z0-9 _]{2,31}$')
);

-- Guild members junction table
CREATE TABLE IF NOT EXISTS guild_members (
    guild_id        INTEGER NOT NULL REFERENCES guilds(id) ON DELETE CASCADE,
    character_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    rank            SMALLINT DEFAULT 0 CHECK (rank >= 0 AND rank <= 10),
    joined_at       TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    contribution    BIGINT DEFAULT 0,
    note            TEXT NOT NULL DEFAULT '',

    PRIMARY KEY (guild_id, character_id)
);

-- Friend requests table: pending requests (not yet accepted)
CREATE TABLE IF NOT EXISTS friend_requests (
    requester_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    requestee_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    PRIMARY KEY (requester_id, requestee_id),
    CONSTRAINT friend_request_not_self CHECK (requester_id != requestee_id)
);

CREATE INDEX IF NOT EXISTS idx_friend_requests_requestee ON friend_requests(requestee_id);

-- Friends table: accepted friendships (bidirectional)
-- Normalized ordering: lower character_id first to avoid duplicate rows
CREATE TABLE IF NOT EXISTS friends (
    character_a     INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    character_b     INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    PRIMARY KEY (character_a, character_b),
    CONSTRAINT friends_ordered CHECK (character_a < character_b),
    CONSTRAINT friends_not_self CHECK (character_a != character_b)
);

CREATE INDEX IF NOT EXISTS idx_friends_char_a ON friends(character_a);
CREATE INDEX IF NOT EXISTS idx_friends_char_b ON friends(character_b);

-- Friend blocks table: unidirectional blocking
CREATE TABLE IF NOT EXISTS friend_blocks (
    blocker_id      INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    blocked_id      INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    PRIMARY KEY (blocker_id, blocked_id),
    CONSTRAINT block_not_self CHECK (blocker_id != blocked_id)
);

CREATE INDEX IF NOT EXISTS idx_friend_blocks_blocker ON friend_blocks(blocker_id);

-- Session tokens for authentication
CREATE TABLE IF NOT EXISTS sessions (
    id              UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    account_id      INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    token           VARCHAR(255) UNIQUE NOT NULL,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    expires_at      TIMESTAMP WITH TIME ZONE NOT NULL,
    ip_address      INET,
    user_agent      TEXT,

    CONSTRAINT session_not_expired CHECK (expires_at > NOW())
);

-- Login history for security auditing
CREATE TABLE IF NOT EXISTS login_history (
    id              SERIAL PRIMARY KEY,
    account_id      INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    login_time      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    ip_address      INET,
    success         BOOLEAN NOT NULL,
    failure_reason  TEXT
);

-- Item log for tracking item transactions
CREATE TABLE IF NOT EXISTS item_log (
    id              SERIAL PRIMARY KEY,
    character_id    INTEGER REFERENCES characters(id) ON DELETE SET NULL,
    item_name       VARCHAR(64) NOT NULL,
    item_id         INTEGER,
    action_type     SMALLINT NOT NULL,
    quantity        INTEGER DEFAULT 1,
    other_char_id   INTEGER,
    gold_amount     INTEGER DEFAULT 0,
    map_name        VARCHAR(32),
    pos_x           SMALLINT,
    pos_y           SMALLINT,
    timestamp       TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    details         JSONB
);
CREATE INDEX IF NOT EXISTS idx_item_log_action ON item_log(action_type);
CREATE INDEX IF NOT EXISTS idx_item_log_item_name ON item_log(item_name);
CREATE INDEX IF NOT EXISTS idx_item_log_character ON item_log(character_id);
CREATE INDEX IF NOT EXISTS idx_item_log_timestamp ON item_log(timestamp);

-- Chat log for moderation
CREATE TABLE IF NOT EXISTS chat_log (
    id              SERIAL PRIMARY KEY,
    character_id    INTEGER REFERENCES characters(id) ON DELETE SET NULL,
    character_name  VARCHAR(32) NOT NULL,
    channel         SMALLINT NOT NULL,
    message         TEXT NOT NULL,
    timestamp       TIMESTAMP WITH TIME ZONE DEFAULT NOW()
);

-- Indexes for common queries
CREATE INDEX IF NOT EXISTS idx_characters_account ON characters(account_id);
CREATE INDEX IF NOT EXISTS idx_characters_name_lower ON characters(LOWER(name));
CREATE INDEX IF NOT EXISTS idx_characters_nation ON characters(nation);
CREATE INDEX IF NOT EXISTS idx_guild_members_character ON guild_members(character_id);
CREATE INDEX IF NOT EXISTS idx_sessions_account ON sessions(account_id);
CREATE INDEX IF NOT EXISTS idx_sessions_token ON sessions(token);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at);
CREATE INDEX IF NOT EXISTS idx_login_history_account ON login_history(account_id);
CREATE INDEX IF NOT EXISTS idx_login_history_time ON login_history(login_time DESC);
CREATE INDEX IF NOT EXISTS idx_item_log_character ON item_log(character_id);
CREATE INDEX IF NOT EXISTS idx_item_log_time ON item_log(timestamp DESC);
CREATE INDEX IF NOT EXISTS idx_chat_log_character ON chat_log(character_id);
CREATE INDEX IF NOT EXISTS idx_chat_log_time ON chat_log(timestamp DESC);

-- GIN indexes for JSONB columns (enables fast containment queries)
-- Example: SELECT * FROM characters WHERE skills_data @> '[{"type":5}]'
CREATE INDEX IF NOT EXISTS idx_characters_skills ON characters USING GIN (skills_data);

-- Items table: persistent item instances (replaces JSONB inventory/equipment/bank columns)
CREATE TABLE IF NOT EXISTS items (
    id INTEGER PRIMARY KEY,
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    template_id INTEGER NOT NULL,
    name VARCHAR(32) NOT NULL DEFAULT '',
    location SMALLINT NOT NULL DEFAULT 0,  -- 0=inventory, 1=equipment, 2=bank, 3=mail
    slot SMALLINT NOT NULL DEFAULT 0,
    count SMALLINT NOT NULL DEFAULT 1,
    durability SMALLINT NOT NULL DEFAULT 0,
    max_durability SMALLINT NOT NULL DEFAULT 0,
    color SMALLINT NOT NULL DEFAULT 0,
    bound_to INTEGER REFERENCES characters(id) ON DELETE SET NULL,
    upgrade_level SMALLINT NOT NULL DEFAULT 0,
    main_enchant_type SMALLINT NOT NULL DEFAULT 0,
    main_enchant_value SMALLINT NOT NULL DEFAULT 0,
    sub_enchant_type SMALLINT NOT NULL DEFAULT 0,
    sub_enchant_value SMALLINT NOT NULL DEFAULT 0,
    custom_made BOOLEAN NOT NULL DEFAULT FALSE,
    custom_quality SMALLINT NOT NULL DEFAULT 0,
    pos_x SMALLINT NOT NULL DEFAULT 0,
    pos_y SMALLINT NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_items_character ON items(character_id);
CREATE INDEX IF NOT EXISTS idx_items_template ON items(template_id);
CREATE INDEX IF NOT EXISTS idx_items_name ON items(name);

-- ==========================================
-- War History
-- ==========================================

-- War history: one row per completed war event
CREATE TABLE IF NOT EXISTS war_history (
    id              SERIAL PRIMARY KEY,
    war_type        SMALLINT NOT NULL,          -- 0=crusade, 1=heldenian, 2=apocalypse
    started_at      TIMESTAMP WITH TIME ZONE NOT NULL,
    ended_at        TIMESTAMP WITH TIME ZONE NOT NULL,
    duration_seconds INTEGER NOT NULL DEFAULT 0,
    winner_faction  SMALLINT NOT NULL DEFAULT 0, -- 0=neutral/draw, 1=aresden, 2=elvine
    aresden_score   INTEGER DEFAULT 0,
    elvine_score    INTEGER DEFAULT 0,
    metadata        JSONB DEFAULT '{}'::jsonb    -- Extra per-war-type data
);

CREATE INDEX IF NOT EXISTS idx_war_history_type ON war_history(war_type);
CREATE INDEX IF NOT EXISTS idx_war_history_started ON war_history(started_at DESC);

-- War participants: per-player stats for each war
CREATE TABLE IF NOT EXISTS war_participants (
    id              SERIAL PRIMARY KEY,
    war_id          INTEGER NOT NULL REFERENCES war_history(id) ON DELETE CASCADE,
    character_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    faction         SMALLINT NOT NULL DEFAULT 0,
    duty            SMALLINT NOT NULL DEFAULT 0,  -- 0=none, 1=fighter, 2=constructor, 3=commander
    kills           INTEGER DEFAULT 0,
    deaths          INTEGER DEFAULT 0,
    assists         INTEGER DEFAULT 0,
    damage_dealt    INTEGER DEFAULT 0,
    healing_done    INTEGER DEFAULT 0,
    contribution    INTEGER DEFAULT 0,
    reward_exp      BIGINT DEFAULT 0,
    reward_gold     INTEGER DEFAULT 0,
    reward_contribution INTEGER DEFAULT 0,
    reward_claimed  BOOLEAN NOT NULL DEFAULT FALSE
);

CREATE INDEX IF NOT EXISTS idx_war_participants_war ON war_participants(war_id);
CREATE INDEX IF NOT EXISTS idx_war_participants_char ON war_participants(character_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_war_participants_unique ON war_participants(war_id, character_id);
CREATE INDEX IF NOT EXISTS idx_war_participants_unclaimed ON war_participants(character_id) WHERE reward_claimed = FALSE;

-- Function to clean up expired sessions
CREATE OR REPLACE FUNCTION cleanup_expired_sessions()
RETURNS void AS $$
BEGIN
    DELETE FROM sessions WHERE expires_at < NOW();
END;
$$ LANGUAGE plpgsql;

-- Function to update last_login on successful authentication
CREATE OR REPLACE FUNCTION update_last_login()
RETURNS TRIGGER AS $$
BEGIN
    IF NEW.success = TRUE THEN
        UPDATE accounts SET last_login = NOW() WHERE id = NEW.account_id;
    END IF;
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

-- Trigger to auto-update last_login
DROP TRIGGER IF EXISTS trigger_update_last_login ON login_history;
CREATE TRIGGER trigger_update_last_login
    AFTER INSERT ON login_history
    FOR EACH ROW
    EXECUTE FUNCTION update_last_login();

-- Grant permissions (adjust as needed for your setup)
-- GRANT ALL PRIVILEGES ON ALL TABLES IN SCHEMA public TO hgserver;
-- GRANT ALL PRIVILEGES ON ALL SEQUENCES IN SCHEMA public TO hgserver;
