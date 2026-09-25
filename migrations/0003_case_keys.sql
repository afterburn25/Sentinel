CREATE TABLE IF NOT EXISTS case_keys (
    case_id TEXT PRIMARY KEY,
    wrapped_key BLOB NOT NULL,
    nonce BLOB NOT NULL,
    tag BLOB NOT NULL,
    key_version INTEGER NOT NULL,
    created_at TEXT NOT NULL,
    FOREIGN KEY(case_id) REFERENCES cases(id)
);
