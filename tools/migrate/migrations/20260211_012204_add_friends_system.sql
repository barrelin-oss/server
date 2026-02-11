-- up

-- Friend requests table: pending requests (not yet accepted)
CREATE TABLE IF NOT EXISTS friend_requests (
    requester_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    requestee_id    INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    PRIMARY KEY (requester_id, requestee_id),
    CONSTRAINT friend_request_not_self CHECK (requester_id != requestee_id)
);

CREATE INDEX IF NOT EXISTS idx_friend_requests_requestee ON friend_requests(requestee_id);

-- Friends table: accepted friendships (bidirectional)
-- Normalized ordering: lower character_id first to avoid duplicate rows
CREATE TABLE IF NOT EXISTS friends (
    character_a     INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    character_b     INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    PRIMARY KEY (character_a, character_b),
    CONSTRAINT friends_ordered CHECK (character_a < character_b),
    CONSTRAINT friends_not_self CHECK (character_a != character_b)
);

CREATE INDEX IF NOT EXISTS idx_friends_char_a ON friends(character_a);
CREATE INDEX IF NOT EXISTS idx_friends_char_b ON friends(character_b);

-- Friend blocks table: unidirectional blocking
CREATE TABLE IF NOT EXISTS friend_blocks (
    blocker_id      INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    blocked_id      INTEGER NOT NULL REFERENCES characters(id) ON DELETE CASCADE,
    created_at      TIMESTAMP WITH TIME ZONE DEFAULT NOW(),

    PRIMARY KEY (blocker_id, blocked_id),
    CONSTRAINT block_not_self CHECK (blocker_id != blocked_id)
);

CREATE INDEX IF NOT EXISTS idx_friend_blocks_blocker ON friend_blocks(blocker_id);

-- down

DROP TABLE IF EXISTS friend_blocks;
DROP TABLE IF EXISTS friends;
DROP TABLE IF EXISTS friend_requests;
