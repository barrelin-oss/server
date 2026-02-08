-- up
-- Add missing character persistence columns for data that was previously lost on disconnect

-- PK points (criminal/murderer status threshold, decays over time)
ALTER TABLE characters ADD COLUMN IF NOT EXISTS pk_points INTEGER DEFAULT 0;

-- Enemy kill count (EK system)
ALTER TABLE characters ADD COLUMN IF NOT EXISTS enemy_kill_count INTEGER DEFAULT 0;

-- Contribution points
ALTER TABLE characters ADD COLUMN IF NOT EXISTS contribution INTEGER DEFAULT 0;

-- Unspent stat points
ALTER TABLE characters ADD COLUMN IF NOT EXISTS stat_points_available SMALLINT DEFAULT 0;

-- down
ALTER TABLE characters DROP COLUMN IF EXISTS pk_points;
ALTER TABLE characters DROP COLUMN IF EXISTS enemy_kill_count;
ALTER TABLE characters DROP COLUMN IF EXISTS contribution;
ALTER TABLE characters DROP COLUMN IF EXISTS stat_points_available;
