CREATE TABLE IF NOT EXISTS conversations (
    id TEXT PRIMARY KEY,
    case_id TEXT,
    provider TEXT NOT NULL,
    external_ref TEXT,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS conversation_messages (
    id TEXT PRIMARY KEY,
    conversation_id TEXT NOT NULL,
    direction INTEGER NOT NULL,
    sender TEXT NOT NULL,
    body TEXT NOT NULL,
    delivery_state INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(conversation_id) REFERENCES conversations(id)
);

CREATE TABLE IF NOT EXISTS supervisor_approvals (
    id TEXT PRIMARY KEY,
    action TEXT NOT NULL,
    action_hash TEXT NOT NULL,
    requested_by TEXT NOT NULL,
    status INTEGER NOT NULL DEFAULT 0,
    reviewed_by TEXT,
    note TEXT,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
