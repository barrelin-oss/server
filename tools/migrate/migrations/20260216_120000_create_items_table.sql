-- up
CREATE TABLE items (
    id INTEGER PRIMARY KEY,
    character_id INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    template_id INTEGER NOT NULL,
    name VARCHAR(32) NOT NULL DEFAULT '',
    location SMALLINT NOT NULL DEFAULT 0,
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
CREATE INDEX idx_items_character ON items(character_id);
CREATE INDEX idx_items_template ON items(template_id);
CREATE INDEX idx_items_name ON items(name);

ALTER TABLE characters DROP COLUMN IF EXISTS inventory_data;
ALTER TABLE characters DROP COLUMN IF EXISTS equipment_data;
ALTER TABLE characters DROP COLUMN IF EXISTS bank_data;

-- down
ALTER TABLE characters ADD COLUMN inventory_data JSONB DEFAULT '[]'::jsonb;
ALTER TABLE characters ADD COLUMN equipment_data JSONB DEFAULT '[]'::jsonb;
ALTER TABLE characters ADD COLUMN bank_data JSONB DEFAULT '[]'::jsonb;
DROP TABLE IF EXISTS items;
