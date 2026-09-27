CREATE TABLE IF NOT EXISTS subjects (
    id TEXT PRIMARY KEY,
    case_id TEXT,
    display_name TEXT NOT NULL DEFAULT '',
    status INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS subject_identities (
    id TEXT PRIMARY KEY,
    subject_id TEXT NOT NULL,
    identity_type TEXT NOT NULL,
    provider TEXT NOT NULL DEFAULT '',
    external_id TEXT NOT NULL,
    display_value TEXT NOT NULL DEFAULT '',
    link_state INTEGER NOT NULL DEFAULT 0,
    confidence REAL NOT NULL DEFAULT 0.0,
    source_event_id TEXT,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    confirmed_utc TEXT,
    confirmed_by TEXT,
    UNIQUE(identity_type, provider, external_id),
    FOREIGN KEY(subject_id) REFERENCES subjects(id)
);

CREATE TABLE IF NOT EXISTS channel_accounts (
    id TEXT PRIMARY KEY,
    channel_type TEXT NOT NULL,
    provider TEXT NOT NULL,
    external_account_id TEXT NOT NULL DEFAULT '',
    display_name TEXT NOT NULL DEFAULT '',
    address TEXT NOT NULL DEFAULT '',
    jurisdiction TEXT NOT NULL DEFAULT '',
    compliance_status TEXT NOT NULL DEFAULT '',
    capability_mask INTEGER NOT NULL DEFAULT 0,
    enabled INTEGER NOT NULL DEFAULT 1,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(provider, external_account_id, address)
);

CREATE TABLE IF NOT EXISTS channel_assignments (
    id TEXT PRIMARY KEY,
    operation_case_id TEXT,
    persona_name TEXT NOT NULL,
    channel_account_id TEXT NOT NULL,
    valid_from_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    valid_until_utc TEXT,
    enabled INTEGER NOT NULL DEFAULT 1,
    FOREIGN KEY(channel_account_id) REFERENCES channel_accounts(id)
);

CREATE TABLE IF NOT EXISTS channel_conversations (
    id TEXT PRIMARY KEY,
    subject_id TEXT,
    persona_name TEXT NOT NULL,
    channel_account_id TEXT NOT NULL,
    channel_type TEXT NOT NULL,
    provider TEXT NOT NULL,
    provider_conversation_id TEXT NOT NULL,
    external_peer_id TEXT NOT NULL DEFAULT '',
    automation_profile_id TEXT,
    state INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(provider, channel_account_id, provider_conversation_id),
    FOREIGN KEY(subject_id) REFERENCES subjects(id),
    FOREIGN KEY(channel_account_id) REFERENCES channel_accounts(id)
);

CREATE TABLE IF NOT EXISTS channel_events (
    id TEXT PRIMARY KEY,
    channel_conversation_id TEXT,
    provider TEXT NOT NULL,
    channel_type TEXT NOT NULL,
    provider_account_id TEXT NOT NULL DEFAULT '',
    provider_conversation_id TEXT NOT NULL DEFAULT '',
    provider_message_id TEXT NOT NULL DEFAULT '',
    event_type TEXT NOT NULL,
    direction INTEGER NOT NULL,
    sender_external_id TEXT NOT NULL DEFAULT '',
    recipient_external_id TEXT NOT NULL DEFAULT '',
    provider_timestamp TEXT NOT NULL DEFAULT '',
    raw_payload TEXT NOT NULL,
    payload_sha256 TEXT NOT NULL,
    received_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(channel_conversation_id) REFERENCES channel_conversations(id)
);

CREATE TABLE IF NOT EXISTS normalized_messages (
    id TEXT PRIMARY KEY,
    event_id TEXT,
    channel_conversation_id TEXT NOT NULL,
    subject_id TEXT,
    persona_name TEXT NOT NULL DEFAULT '',
    direction INTEGER NOT NULL,
    sender_external_id TEXT NOT NULL DEFAULT '',
    recipient_external_id TEXT NOT NULL DEFAULT '',
    body TEXT NOT NULL DEFAULT '',
    delivery_state INTEGER NOT NULL DEFAULT 0,
    automation_mode INTEGER NOT NULL DEFAULT 0,
    model_run_id TEXT,
    policy_decision_id TEXT,
    approval_id TEXT,
    provider_message_id TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(event_id) REFERENCES channel_events(id),
    FOREIGN KEY(channel_conversation_id) REFERENCES channel_conversations(id),
    FOREIGN KEY(subject_id) REFERENCES subjects(id)
);

CREATE TABLE IF NOT EXISTS message_attachments (
    id TEXT PRIMARY KEY,
    message_id TEXT NOT NULL,
    media_type TEXT NOT NULL DEFAULT '',
    mime_type TEXT NOT NULL DEFAULT '',
    provider_url TEXT NOT NULL DEFAULT '',
    local_path TEXT NOT NULL DEFAULT '',
    sha256 TEXT NOT NULL DEFAULT '',
    original_name TEXT NOT NULL DEFAULT '',
    size_bytes INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(message_id) REFERENCES normalized_messages(id)
);

CREATE TABLE IF NOT EXISTS memory_facts (
    id TEXT PRIMARY KEY,
    subject_id TEXT,
    persona_name TEXT NOT NULL DEFAULT '',
    fact_type TEXT NOT NULL,
    fact_text TEXT NOT NULL,
    source_message_id TEXT,
    confidence REAL NOT NULL DEFAULT 0.0,
    locked INTEGER NOT NULL DEFAULT 0,
    active INTEGER NOT NULL DEFAULT 1,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(subject_id) REFERENCES subjects(id),
    FOREIGN KEY(source_message_id) REFERENCES normalized_messages(id)
);

CREATE TABLE IF NOT EXISTS channel_migrations (
    id TEXT PRIMARY KEY,
    subject_id TEXT,
    from_conversation_id TEXT,
    to_channel_type TEXT NOT NULL,
    requested_by INTEGER NOT NULL DEFAULT 0,
    detected_message_id TEXT,
    destination_identifier TEXT NOT NULL DEFAULT '',
    status INTEGER NOT NULL DEFAULT 0,
    detected_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    confirmed_by TEXT,
    confirmed_utc TEXT,
    to_conversation_id TEXT,
    FOREIGN KEY(subject_id) REFERENCES subjects(id),
    FOREIGN KEY(from_conversation_id) REFERENCES channel_conversations(id),
    FOREIGN KEY(to_conversation_id) REFERENCES channel_conversations(id)
);

CREATE TABLE IF NOT EXISTS automation_profiles (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    default_mode INTEGER NOT NULL DEFAULT 1,
    rules_json TEXT NOT NULL DEFAULT '{}',
    enabled INTEGER NOT NULL DEFAULT 1,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE TABLE IF NOT EXISTS automation_decisions (
    id TEXT PRIMARY KEY,
    message_id TEXT,
    profile_id TEXT,
    requested_action TEXT NOT NULL,
    decision INTEGER NOT NULL,
    reason TEXT NOT NULL DEFAULT '',
    requires_approval INTEGER NOT NULL DEFAULT 1,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(message_id) REFERENCES normalized_messages(id),
    FOREIGN KEY(profile_id) REFERENCES automation_profiles(id)
);

CREATE TABLE IF NOT EXISTS model_runs (
    id TEXT PRIMARY KEY,
    message_id TEXT,
    model_name TEXT NOT NULL,
    purpose TEXT NOT NULL,
    input_hash TEXT NOT NULL,
    output_hash TEXT NOT NULL DEFAULT '',
    latency_ms INTEGER NOT NULL DEFAULT 0,
    status INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(message_id) REFERENCES normalized_messages(id)
);

CREATE TABLE IF NOT EXISTS policy_decisions (
    id TEXT PRIMARY KEY,
    message_id TEXT,
    policy_name TEXT NOT NULL,
    action TEXT NOT NULL,
    allowed INTEGER NOT NULL,
    requires_supervisor INTEGER NOT NULL DEFAULT 0,
    reason TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(message_id) REFERENCES normalized_messages(id)
);

CREATE INDEX IF NOT EXISTS idx_subject_identities_subject ON subject_identities(subject_id);
CREATE INDEX IF NOT EXISTS idx_channel_conversations_subject ON channel_conversations(subject_id, updated_utc DESC);
CREATE INDEX IF NOT EXISTS idx_channel_events_conversation ON channel_events(channel_conversation_id, received_utc);
CREATE UNIQUE INDEX IF NOT EXISTS idx_channel_events_provider_message_unique
ON channel_events(provider, provider_message_id, event_type)
WHERE provider_message_id <> '';
CREATE INDEX IF NOT EXISTS idx_normalized_messages_conversation ON normalized_messages(channel_conversation_id, created_utc);
CREATE INDEX IF NOT EXISTS idx_memory_facts_subject ON memory_facts(subject_id, active, updated_utc DESC);
CREATE INDEX IF NOT EXISTS idx_channel_migrations_subject ON channel_migrations(subject_id, detected_utc DESC);
