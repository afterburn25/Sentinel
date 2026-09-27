CREATE TABLE IF NOT EXISTS persona_response_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    persona_name TEXT NOT NULL,
    match_type TEXT NOT NULL DEFAULT 'contains'
        CHECK(match_type IN ('exact','contains')),
    trigger_text TEXT NOT NULL,
    response_text TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    priority INTEGER NOT NULL DEFAULT 100,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_response_rules_lookup
    ON persona_response_rules(persona_name, enabled, priority DESC, id ASC);

CREATE TABLE IF NOT EXISTS persona_learned_notes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    persona_name TEXT NOT NULL,
    conversation_id TEXT,
    source_kind TEXT NOT NULL DEFAULT 'persona_claim',
    note_text TEXT NOT NULL,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_learned_notes_persona
    ON persona_learned_notes(persona_name, created_utc DESC);
