-- Reconcile the temporary recovery subject tables with the canonical
-- subject / subject_identities tables that already existed in 1.0.15.

ALTER TABLE subjects ADD COLUMN legal_name TEXT NOT NULL DEFAULT '';
ALTER TABLE subjects ADD COLUMN aliases TEXT NOT NULL DEFAULT '';
ALTER TABLE subjects ADD COLUMN usernames TEXT NOT NULL DEFAULT '';
ALTER TABLE subjects ADD COLUMN contact_identifiers TEXT NOT NULL DEFAULT '';
ALTER TABLE subjects ADD COLUMN notes TEXT NOT NULL DEFAULT '';

ALTER TABLE subject_identities ADD COLUMN source_reference TEXT NOT NULL DEFAULT '';
ALTER TABLE subject_identities ADD COLUMN provenance TEXT NOT NULL DEFAULT '';
ALTER TABLE subject_identities ADD COLUMN reviewed_by TEXT NOT NULL DEFAULT '';
ALTER TABLE subject_identities ADD COLUMN review_notes TEXT NOT NULL DEFAULT '';
ALTER TABLE subject_identities ADD COLUMN reviewed_utc TEXT NOT NULL DEFAULT '';

INSERT OR IGNORE INTO subjects(
    id,case_id,display_name,status,created_utc,updated_utc,
    legal_name,aliases,usernames,contact_identifiers,notes)
SELECT
    id,case_id,display_name,identity_status,created_utc,updated_utc,
    legal_name,aliases,usernames,contact_identifiers,notes
FROM investigation_subjects;

INSERT OR IGNORE INTO subject_identities(
    id,subject_id,identity_type,provider,external_id,display_value,
    link_state,confidence,source_event_id,created_utc,confirmed_utc,confirmed_by,
    source_reference,provenance,reviewed_by,review_notes,reviewed_utc)
SELECT
    id,
    subject_id,
    CASE WHEN source_type='' THEN 'investigator-lead' ELSE source_type END,
    'sara-case',
    id,
    lead_value,
    status,
    confidence/100.0,
    '',
    created_utc,
    CASE WHEN status=1 THEN reviewed_utc ELSE NULL END,
    CASE WHEN status=1 THEN reviewer ELSE NULL END,
    source_reference,
    provenance,
    reviewer,
    review_notes,
    reviewed_utc
FROM subject_identity_leads;

DROP TABLE subject_identity_leads;
DROP TABLE investigation_subjects;
