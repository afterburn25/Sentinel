CREATE TABLE IF NOT EXISTS identity_research_providers (
    id TEXT PRIMARY KEY,
    display_name TEXT NOT NULL,
    access_mode INTEGER NOT NULL DEFAULT 0 CHECK(access_mode BETWEEN 0 AND 2),
    supported_types_mask INTEGER NOT NULL DEFAULT 31,
    endpoint_hint TEXT NOT NULL DEFAULT '',
    credential_reference TEXT NOT NULL DEFAULT '',
    enabled INTEGER NOT NULL DEFAULT 1,
    notes TEXT NOT NULL DEFAULT '',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT OR IGNORE INTO identity_research_providers(
    id,display_name,access_mode,supported_types_mask,enabled,notes
) VALUES
(
    'manual/authorized',
    'Manual / Authorized Source',
    0,
    31,
    1,
    'Default investigator-performed research. No automatic lookup.'
),
(
    'agency-public-records',
    'Agency Public Records Connector',
    2,
    1,
    0,
    'Disabled template. Configure only an agency-approved API and external secret reference.'
),
(
    'agency-social-profile',
    'Authorized Social/Profile Connector',
    2,
    6,
    0,
    'Disabled template for lawful public-profile and username sources.'
),
(
    'agency-contact-resolution',
    'Authorized Contact Connector',
    2,
    8,
    0,
    'Disabled template for legally permitted contact-identifier research.'
),
(
    'agency-image-reference',
    'Authorized Image Reference Connector',
    2,
    16,
    0,
    'Disabled template for agency-approved visual/reference-match sources.'
);
