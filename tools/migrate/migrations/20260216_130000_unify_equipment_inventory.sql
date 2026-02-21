-- Migration: Add equip_slot column to items table for unified equipment/inventory
-- Description: Equipment items now live in inventory slots with an equip_slot flag
--              instead of being stored in a separate location=1 container.

-- up
ALTER TABLE items ADD COLUMN IF NOT EXISTS equip_slot SMALLINT DEFAULT NULL;

-- Migrate existing equipment items (location=1): copy their slot as equip_slot,
-- then assign new inventory slot indices that don't collide with existing inventory items.
UPDATE items SET equip_slot = slot WHERE location = 1;

-- Assign inventory slot indices for equipment items per character
WITH ranked AS (
    SELECT id, character_id,
           ROW_NUMBER() OVER (PARTITION BY character_id ORDER BY slot) as rn
    FROM items WHERE location = 1
),
max_slots AS (
    SELECT character_id, COALESCE(MAX(slot), -1) as max_slot
    FROM items WHERE location = 0
    GROUP BY character_id
)
UPDATE items SET
    slot = COALESCE(ms.max_slot, -1) + r.rn,
    location = 0
FROM ranked r
LEFT JOIN max_slots ms ON r.character_id = ms.character_id
WHERE items.id = r.id;

-- down
-- Reverse: move equipped items back to location=1 with equip_slot as their slot
UPDATE items SET slot = equip_slot, location = 1 WHERE equip_slot IS NOT NULL AND location = 0;
ALTER TABLE items DROP COLUMN IF EXISTS equip_slot;
