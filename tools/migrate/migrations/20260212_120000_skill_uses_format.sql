-- up

-- Migrate skill data from exp-based to use-count format.
-- The skills_data JSONB column stores an array of skill objects per character.
-- Replace "exp" key with "total_uses":0 and "uses":0, preserving level.
UPDATE characters
SET skills_data = (
    SELECT COALESCE(jsonb_agg(
        CASE
            WHEN elem ? 'exp' THEN (elem - 'exp') || '{"total_uses":0,"uses":0}'::jsonb
            ELSE elem
        END
    ), '[]'::jsonb)
    FROM jsonb_array_elements(skills_data) AS elem
)
WHERE skills_data IS NOT NULL AND skills_data != '[]'::jsonb;

-- down

-- Revert: replace total_uses/uses with exp:0
UPDATE characters
SET skills_data = (
    SELECT COALESCE(jsonb_agg(
        CASE
            WHEN elem ? 'total_uses' THEN (elem - 'total_uses' - 'uses') || '{"exp":0}'::jsonb
            ELSE elem
        END
    ), '[]'::jsonb)
    FROM jsonb_array_elements(skills_data) AS elem
)
WHERE skills_data IS NOT NULL AND skills_data != '[]'::jsonb;
