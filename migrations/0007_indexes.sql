CREATE INDEX IF NOT EXISTS idx_cases_modified ON cases(modified_at);
CREATE INDEX IF NOT EXISTS idx_evidence_case ON evidence(case_id);
CREATE INDEX IF NOT EXISTS idx_evidence_original_hash ON evidence(original_sha256);
CREATE INDEX IF NOT EXISTS idx_evidence_parent ON evidence(parent_evidence_id);
CREATE INDEX IF NOT EXISTS idx_evidence_imported_at ON evidence(imported_at);
CREATE INDEX IF NOT EXISTS idx_audit_timestamp ON audit_records(timestamp);
CREATE INDEX IF NOT EXISTS idx_audit_actor ON audit_records(actor_id);
CREATE INDEX IF NOT EXISTS idx_audit_target ON audit_records(target_type,target_id);
