CREATE TABLE IF NOT EXISTS training_review_items (
    id TEXT PRIMARY KEY,
    source_log_id TEXT NOT NULL UNIQUE,
    conversation_id TEXT NOT NULL DEFAULT '',
    persona_name TEXT NOT NULL DEFAULT '',
    model_name TEXT NOT NULL DEFAULT '',
    input_text TEXT NOT NULL DEFAULT '',
    output_text TEXT NOT NULL DEFAULT '',
    persona_summary TEXT NOT NULL DEFAULT '',
    recalled_memory TEXT NOT NULL DEFAULT '',
    context_json TEXT NOT NULL DEFAULT '{}',
    status INTEGER NOT NULL DEFAULT 0 CHECK(status IN (0,1,2)),
    reviewer TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    reviewed_utc TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_training_review_status
ON training_review_items(status, created_utc);

CREATE INDEX IF NOT EXISTS idx_training_review_persona
ON training_review_items(persona_name, status, created_utc DESC);
