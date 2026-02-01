-- schema.sql
-- PostgreSQL schema for Helbreath server authentication and persistence
-- Run this file to set up the database: psql -U hgserver -d helbreath -f schema.sql
--
-- MIGRATION: If upgrading from BYTEA to JSONB columns, run:
--   ALTER TABLE characters
--     ALTER COLUMN skills_data TYPE JSONB USING COALESCE(skills_data::text::jsonb, '[]'::jsonb),
--     ALTER COLUMN inventory_data TYPE JSONB USING COALESCE(inventory_data::text::jsonb, '[]'::jsonb),
--     ALTER COLUMN equipment_data TYPE JSONB USING COALESCE(equipment_data::text::jsonb, '[]'::jsonb),
--     ALTER COLUMN bank_data TYPE JSONB USING COALESCE(bank_data::text::jsonb, '[]'::jsonb),
--     ALTER COLUMN quest_data TYPE JSONB USING COALESCE(quest_data::text::jsonb, '[]'::jsonb),
--     ALTER COLUMN magic_data TYPE JSONB USING COALESCE(magic_data::text::jsonb, '[]'::jsonb);
--
-- MIGRATION: If upgrading pos_x/pos_y defaults from 0 to -1 (map initial point), run:
--   ALTER TABLE characters ALTER COLUMN pos_x SET DEFAULT -1;
--   ALTER TABLE characters ALTER COLUMN pos_y SET DEFAULT -1;
--   -- Optionally reset existing characters at (0,0) to use initial spawn:
--   -- UPDATE characters SET pos_x = -1, pos_y = -1 WHERE pos_x = 0 AND pos_y = 0;

-- Enable UUID extension for session tokens
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";

-- Accounts table
CREATE TABLE IF NOT EXISTS accounts (
    id              SERIAL PRIMARY KEY,
    username        VARCHAR(32) UNIQUE NOT NULL,
    password_hash   VARCHAR(255) NOT NULL,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    last_login      TIMESTAMP WITH TIME ZONE,
    is_banned       BOOLEAN DEFAULT FALSE,
    ban_reason      TEXT,
    ban_expires     TIMESTAMP WITH TIME ZONE,
    admin_level     SMALLINT DEFAULT 0,

    CONSTRAINT username_lowercase CHECK (username = LOWER(username)),
    CONSTRAINT username_format CHECK (username ~ '^[a-z0-9_]{3,32}$')
);

-- Characters table
CREATE TABLE IF NOT EXISTS characters (
    id              SERIAL PRIMARY KEY,
    account_id      INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    name            VARCHAR(32) UNIQUE NOT NULL,
    level           SMALLINT DEFAULT 1 CHECK (level >= 1 AND level <= 255),
    class_type      SMALLINT NOT NULL CHECK (class_type >= 0),
    nation          SMALLINT NOT NULL CHECK (nation >= 0 AND nation <= 2),
    gender          SMALLINT NOT NULL CHECK (gender >= 0 AND gender <= 1),

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
    reward_gold     INTEGER DEFAULT 0,
    hunger_level    SMALLINT DEFAULT 100,

    -- JSON data for complex serialized data (JSONB for queryability)
    -- skills_data: [{"type":5,"level":45,"exp":1200}, ...]
    -- inventory_data: [{"slot":0,"item_id":123,"count":1}, ...]
    -- equipment_data: [{"slot":5,"item_id":123,"durability":80,"max_durability":100}, ...]
    skills_data     JSONB DEFAULT '[]'::jsonb,
    inventory_data  JSONB DEFAULT '[]'::jsonb,
    equipment_data  JSONB DEFAULT '[]'::jsonb,
    bank_data       JSONB DEFAULT '[]'::jsonb,
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
    nation          SMALLINT NOT NULL CHECK (nation >= 0 AND nation <= 2),
    leader_id       INTEGER REFERENCES characters(id) ON DELETE SET NULL,

    -- Guild stats
    gold            BIGINT DEFAULT 0,
    warehouse_data  BYTEA,

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

    PRIMARY KEY (guild_id, character_id)
);

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
    timestamp       TIMESTAMP WITH TIME ZONE DEFAULT NOW(),
    details         JSONB
);

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
CREATE INDEX IF NOT EXISTS idx_characters_inventory ON characters USING GIN (inventory_data);
CREATE INDEX IF NOT EXISTS idx_characters_equipment ON characters USING GIN (equipment_data);

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
