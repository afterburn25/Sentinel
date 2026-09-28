CREATE TABLE IF NOT EXISTS persona_response_rule_matches (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    persona_name TEXT NOT NULL,
    conversation_id TEXT NOT NULL DEFAULT '',
    rule_id INTEGER NOT NULL,
    match_type TEXT NOT NULL DEFAULT '',
    match_score INTEGER NOT NULL DEFAULT 0,
    trigger_text TEXT NOT NULL DEFAULT '',
    input_text TEXT NOT NULL DEFAULT '',
    response_mode TEXT NOT NULL DEFAULT '',
    output_text TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_rule_matches_recent
    ON persona_response_rule_matches(persona_name,id DESC);

CREATE INDEX IF NOT EXISTS idx_persona_rule_matches_rule
    ON persona_response_rule_matches(persona_name,rule_id,id DESC);
