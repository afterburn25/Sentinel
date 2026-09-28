CREATE TABLE IF NOT EXISTS investigation_subjects (
    id TEXT PRIMARY KEY,
    case_id TEXT NOT NULL,
    display_name TEXT NOT NULL,
    legal_name TEXT NOT NULL DEFAULT '',
    aliases TEXT NOT NULL DEFAULT '',
    usernames TEXT NOT NULL DEFAULT '',
    contact_identifiers TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    identity_status INTEGER NOT NULL DEFAULT 0
        CHECK(identity_status IN (0,1,2)),
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(case_id) REFERENCES cases(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_investigation_subjects_case
    ON investigation_subjects(case_id, updated_utc DESC);

CREATE TABLE IF NOT EXISTS subject_identity_leads (
    id TEXT PRIMARY KEY,
    subject_id TEXT NOT NULL,
    source_type TEXT NOT NULL DEFAULT '',
    source_reference TEXT NOT NULL DEFAULT '',
    lead_value TEXT NOT NULL DEFAULT '',
    confidence INTEGER NOT NULL DEFAULT 0
        CHECK(confidence >= 0 AND confidence <= 100),
    status INTEGER NOT NULL DEFAULT 0
        CHECK(status IN (0,1,2)),
    provenance TEXT NOT NULL DEFAULT '',
    reviewer TEXT NOT NULL DEFAULT '',
    review_notes TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    reviewed_utc TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(subject_id) REFERENCES investigation_subjects(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS idx_subject_identity_leads_subject
    ON subject_identity_leads(subject_id, created_utc DESC);

CREATE INDEX IF NOT EXISTS idx_subject_identity_leads_status
    ON subject_identity_leads(subject_id, status, confidence DESC);
