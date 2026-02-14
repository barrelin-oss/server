-- up
ALTER TABLE item_log ADD COLUMN IF NOT EXISTS gold_amount INTEGER DEFAULT 0;
ALTER TABLE item_log ADD COLUMN IF NOT EXISTS map_name VARCHAR(32);
ALTER TABLE item_log ADD COLUMN IF NOT EXISTS pos_x SMALLINT;
ALTER TABLE item_log ADD COLUMN IF NOT EXISTS pos_y SMALLINT;
CREATE INDEX IF NOT EXISTS idx_item_log_action ON item_log(action_type);
CREATE INDEX IF NOT EXISTS idx_item_log_item_name ON item_log(item_name);
CREATE INDEX IF NOT EXISTS idx_item_log_character ON item_log(character_id);
CREATE INDEX IF NOT EXISTS idx_item_log_timestamp ON item_log(timestamp);

-- down
DROP INDEX IF EXISTS idx_item_log_timestamp;
DROP INDEX IF EXISTS idx_item_log_character;
DROP INDEX IF EXISTS idx_item_log_item_name;
DROP INDEX IF EXISTS idx_item_log_action;
ALTER TABLE item_log DROP COLUMN IF EXISTS pos_y;
ALTER TABLE item_log DROP COLUMN IF EXISTS pos_x;
ALTER TABLE item_log DROP COLUMN IF EXISTS map_name;
ALTER TABLE item_log DROP COLUMN IF EXISTS gold_amount;
