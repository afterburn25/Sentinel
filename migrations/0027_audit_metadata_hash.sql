-- Bind audit metadata into all new record hashes while preserving
-- verification of immutable audit-v1 records created before this migration.
ALTER TABLE audit_records
ADD COLUMN metadata_sha256 TEXT NOT NULL DEFAULT '';
