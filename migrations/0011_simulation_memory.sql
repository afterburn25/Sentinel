CREATE TABLE IF NOT EXISTS simulation_conversations (
    id TEXT PRIMARY KEY,
    title TEXT NOT NULL,
    persona_name TEXT NOT NULL,
    scenario TEXT NOT NULL,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS simulation_messages (
    row_id INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    speaker INTEGER NOT NULL,
    body TEXT NOT NULL,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(conversation_id) REFERENCES simulation_conversations(id)
);

CREATE INDEX IF NOT EXISTS idx_simulation_messages_conversation
ON simulation_messages(conversation_id,row_id);

CREATE INDEX IF NOT EXISTS idx_simulation_conversations_updated
ON simulation_conversations(updated_utc DESC);
