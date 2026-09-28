CREATE TABLE IF NOT EXISTS trainer_dialogue_sessions (
    id TEXT PRIMARY KEY,
    persona_name TEXT NOT NULL,
    training_mode TEXT NOT NULL
        CHECK(training_mode IN ('BEHAVIOR','CORRECTION','PERSONA_LORA','FOUNDATION_SFT','PREFERENCE')),
    title TEXT NOT NULL DEFAULT '',
    active INTEGER NOT NULL DEFAULT 1,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_trainer_dialogue_active
    ON trainer_dialogue_sessions(persona_name,training_mode)
    WHERE active=1;

CREATE TABLE IF NOT EXISTS trainer_dialogue_turns (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id TEXT NOT NULL,
    role TEXT NOT NULL
        CHECK(role IN ('OPERATOR','SARA','SYSTEM')),
    message_text TEXT NOT NULL,
    payload TEXT NOT NULL DEFAULT '',
    applied INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    applied_utc TEXT,
    FOREIGN KEY(session_id) REFERENCES trainer_dialogue_sessions(id)
);

CREATE INDEX IF NOT EXISTS idx_trainer_dialogue_turns_recent
    ON trainer_dialogue_turns(session_id,id DESC);
