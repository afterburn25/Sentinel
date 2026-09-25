CREATE TABLE IF NOT EXISTS audit_records (
    id TEXT PRIMARY KEY,
    sequence INTEGER NOT NULL UNIQUE,
    timestamp TEXT NOT NULL,
    actor_id TEXT NOT NULL,
    action INTEGER NOT NULL,
    target_type TEXT NOT NULL,
    target_id TEXT,
    metadata BLOB,
    previous_hash TEXT NOT NULL,
    record_hash TEXT NOT NULL
);
