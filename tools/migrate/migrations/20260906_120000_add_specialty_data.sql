-- up

-- Monster specialties (mastery per monster type): [{"type": 14, "kills": 320}, ...]
ALTER TABLE characters ADD COLUMN IF NOT EXISTS specialty_data JSONB DEFAULT '[]'::jsonb;

-- down
ALTER TABLE characters DROP COLUMN IF EXISTS specialty_data;
