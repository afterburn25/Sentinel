CREATE TABLE IF NOT EXISTS persona_conversation_logs (
    id TEXT PRIMARY KEY,
    conversation_id TEXT NOT NULL DEFAULT '',
    persona_name TEXT NOT NULL,
    model_name TEXT NOT NULL DEFAULT '',
    event_kind TEXT NOT NULL,
    input_text TEXT NOT NULL DEFAULT '',
    output_text TEXT NOT NULL DEFAULT '',
    persona_summary TEXT NOT NULL DEFAULT '',
    recalled_memory TEXT NOT NULL DEFAULT '',
    context_json TEXT NOT NULL DEFAULT '{}',
    start_delay_ms INTEGER NOT NULL DEFAULT 0,
    typing_delay_ms INTEGER NOT NULL DEFAULT 0,
    policy_status TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_conversation_logs_conversation
ON persona_conversation_logs(conversation_id, created_utc);

CREATE INDEX IF NOT EXISTS idx_persona_conversation_logs_persona
ON persona_conversation_logs(persona_name, created_utc DESC);
