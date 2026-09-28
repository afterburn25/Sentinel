CREATE TABLE IF NOT EXISTS foundation_activation_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    from_foundation_id TEXT NOT NULL DEFAULT '',
    to_foundation_id TEXT NOT NULL,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_foundation_activation_history_target
    ON foundation_activation_history(to_foundation_id,id DESC);
