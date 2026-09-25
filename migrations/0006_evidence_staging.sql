CREATE TABLE IF NOT EXISTS evidence_staging (
    id TEXT PRIMARY KEY,
    case_id TEXT NOT NULL,
    source_path BLOB,
    staging_path TEXT NOT NULL,
    state INTEGER NOT NULL,
    started_at TEXT NOT NULL,
    last_updated_at TEXT NOT NULL,
    FOREIGN KEY(case_id) REFERENCES cases(id)
);
