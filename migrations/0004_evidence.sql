CREATE TABLE IF NOT EXISTS evidence (
    id TEXT PRIMARY KEY,
    case_id TEXT NOT NULL,
    evidence_type INTEGER NOT NULL,
    original_filename BLOB NOT NULL,
    media_type TEXT,
    original_sha256 TEXT NOT NULL,
    container_sha256 TEXT NOT NULL,
    original_size INTEGER NOT NULL,
    stored_size INTEGER NOT NULL,
    storage_relative_path TEXT NOT NULL,
    parent_evidence_id TEXT,
    acquisition_time TEXT,
    imported_at TEXT NOT NULL,
    imported_by TEXT NOT NULL,
    status INTEGER NOT NULL,
    FOREIGN KEY(case_id) REFERENCES cases(id),
    FOREIGN KEY(parent_evidence_id) REFERENCES evidence(id)
);
