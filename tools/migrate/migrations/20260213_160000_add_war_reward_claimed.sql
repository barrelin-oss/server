-- up
ALTER TABLE war_participants ADD COLUMN reward_claimed BOOLEAN NOT NULL DEFAULT FALSE;
CREATE INDEX IF NOT EXISTS idx_war_participants_unclaimed ON war_participants(character_id) WHERE reward_claimed = FALSE;

-- down
DROP INDEX IF EXISTS idx_war_participants_unclaimed;
ALTER TABLE war_participants DROP COLUMN IF EXISTS reward_claimed;
