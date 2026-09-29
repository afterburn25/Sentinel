CREATE TABLE IF NOT EXISTS identity_research_tasks (
    id TEXT PRIMARY KEY,
    subject_id TEXT NOT NULL,
    research_type INTEGER NOT NULL CHECK(research_type BETWEEN 0 AND 4),
    provider TEXT NOT NULL DEFAULT 'manual/authorized',
    query_text TEXT NOT NULL,
    purpose TEXT NOT NULL,
    status INTEGER NOT NULL DEFAULT 0 CHECK(status BETWEEN 0 AND 3),
    result_summary TEXT NOT NULL DEFAULT '',
    result_reference TEXT NOT NULL DEFAULT '',
    provenance TEXT NOT NULL DEFAULT '',
    promoted_lead_id TEXT NOT NULL DEFAULT '',
    reviewed_by TEXT NOT NULL DEFAULT '',
    review_notes TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_utc TEXT,
    FOREIGN KEY(subject_id) REFERENCES subjects(id)
);

CREATE INDEX IF NOT EXISTS idx_identity_research_subject
    ON identity_research_tasks(subject_id,status,updated_utc DESC);
