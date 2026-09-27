CREATE TABLE IF NOT EXISTS model_foundations (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    parent_id TEXT,
    source_model TEXT NOT NULL,
    trainable_source_path TEXT,
    runtime_gguf_path TEXT,
    version INTEGER NOT NULL DEFAULT 1,
    status TEXT NOT NULL DEFAULT 'DRAFT'
        CHECK(status IN ('DRAFT','TRAINING','CANDIDATE','APPROVED','ACTIVE','RETIRED')),
    notes TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS persona_lora_bindings (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    persona_name TEXT NOT NULL,
    foundation_id TEXT NOT NULL,
    lora_name TEXT NOT NULL,
    lora_path TEXT NOT NULL,
    weight REAL NOT NULL DEFAULT 1.0,
    active INTEGER NOT NULL DEFAULT 1,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(foundation_id) REFERENCES model_foundations(id)
);

CREATE UNIQUE INDEX IF NOT EXISTS idx_persona_lora_active
    ON persona_lora_bindings(persona_name,foundation_id,lora_name);

CREATE TABLE IF NOT EXISTS trainer_jobs (
    id TEXT PRIMARY KEY,
    training_mode TEXT NOT NULL
        CHECK(training_mode IN ('BEHAVIOR','CORRECTION','PERSONA_LORA','FOUNDATION_SFT','PREFERENCE')),
    target_name TEXT NOT NULL,
    persona_name TEXT NOT NULL DEFAULT '',
    foundation_id TEXT NOT NULL DEFAULT '',
    dataset_path TEXT NOT NULL DEFAULT '',
    base_model_path TEXT NOT NULL DEFAULT '',
    output_path TEXT NOT NULL DEFAULT '',
    config_json TEXT NOT NULL DEFAULT '{}',
    state TEXT NOT NULL DEFAULT 'DRAFT'
        CHECK(state IN ('DRAFT','QUEUED','RUNNING','COMPLETED','FAILED','CANCELLED')),
    progress INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    started_utc TEXT,
    completed_utc TEXT,
    error_text TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_trainer_jobs_recent
    ON trainer_jobs(created_utc DESC);
