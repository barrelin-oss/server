-- up

-- Add bank pagination columns to items table
ALTER TABLE items ADD COLUMN IF NOT EXISTS bank_page SMALLINT;
ALTER TABLE items ADD COLUMN IF NOT EXISTS bank_slot SMALLINT;

-- Create character_equipment table (linked model: slots reference items in inventory)
CREATE TABLE IF NOT EXISTS character_equipment (
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    slot SMALLINT NOT NULL,
    item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
    PRIMARY KEY (character_id, slot)
);

-- Migrate existing equipment data:
-- Items with location=1 (equipment) and equip_slot set need to be
-- inserted into character_equipment and their location changed to 0 (inventory)
INSERT INTO character_equipment (character_id, slot, item_id)
SELECT character_id, equip_slot, id
FROM items
WHERE equip_slot IS NOT NULL
ON CONFLICT DO NOTHING;

-- Move all equipment items back to inventory location
UPDATE items SET location = 0 WHERE location = 1;

-- Migrate bank items: set bank_page/bank_slot from existing slot column
-- Existing bank items (location=2) use flat slot index. Convert to page/slot.
-- Default: 12 slots per page
UPDATE items SET
    bank_page = slot / 12,
    bank_slot = slot % 12
WHERE location = 2;

-- down
DROP TABLE IF EXISTS character_equipment;
ALTER TABLE items DROP COLUMN IF EXISTS bank_page;
ALTER TABLE items DROP COLUMN IF EXISTS bank_slot;
