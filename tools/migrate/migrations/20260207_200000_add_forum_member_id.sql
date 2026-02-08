-- Migration: add_forum_member_id
-- Description: Add forum_member_id column to accounts table for external forum authentication

-- up
ALTER TABLE accounts ADD COLUMN IF NOT EXISTS forum_member_id BIGINT;
CREATE UNIQUE INDEX IF NOT EXISTS idx_accounts_forum_member_id ON accounts (forum_member_id) WHERE forum_member_id IS NOT NULL;

-- down
DROP INDEX IF EXISTS idx_accounts_forum_member_id;
ALTER TABLE accounts DROP COLUMN IF EXISTS forum_member_id;
