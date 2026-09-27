CREATE TABLE IF NOT EXISTS conversation_media (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    speaker INTEGER NOT NULL,
    stored_path TEXT NOT NULL,
    original_name TEXT NOT NULL DEFAULT '',
    sha256 TEXT NOT NULL DEFAULT '',
    caption TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_conversation_media_conversation
    ON conversation_media(conversation_id,id);
