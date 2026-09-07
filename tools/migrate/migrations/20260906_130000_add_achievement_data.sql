-- up

-- Achievements: {"counters": {"kills_total": 12, "kills_type:14": 3}, "unlocked": [{"id": 1, "at": 1700000000}]}
ALTER TABLE characters ADD COLUMN IF NOT EXISTS achievement_data JSONB DEFAULT '{}'::jsonb;

-- down
ALTER TABLE characters DROP COLUMN IF EXISTS achievement_data;
