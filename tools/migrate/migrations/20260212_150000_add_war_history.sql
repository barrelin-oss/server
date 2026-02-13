-- up

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
    reward_contribution INTEGER DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_war_participants_war ON war_participants(war_id);
CREATE INDEX IF NOT EXISTS idx_war_participants_char ON war_participants(character_id);
CREATE UNIQUE INDEX IF NOT EXISTS idx_war_participants_unique ON war_participants(war_id, character_id);

-- down

DROP TABLE IF EXISTS war_participants;
DROP TABLE IF EXISTS war_history;
