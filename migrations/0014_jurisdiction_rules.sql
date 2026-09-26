CREATE TABLE IF NOT EXISTS jurisdiction_rule_profiles (
    id TEXT PRIMARY KEY,
    country_code TEXT NOT NULL DEFAULT 'US',
    region_code TEXT NOT NULL,
    name TEXT NOT NULL,
    version TEXT NOT NULL,
    effective_from TEXT NOT NULL DEFAULT '',
    effective_until TEXT,
    review_status INTEGER NOT NULL DEFAULT 0,
    reviewed_by TEXT,
    reviewed_utc TEXT,
    rules_json TEXT NOT NULL DEFAULT '{}',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(country_code, region_code, version)
);

CREATE TABLE IF NOT EXISTS jurisdiction_rule_sources (
    id TEXT PRIMARY KEY,
    profile_id TEXT NOT NULL,
    authority_type TEXT NOT NULL,
    citation TEXT NOT NULL,
    source_url TEXT NOT NULL DEFAULT '',
    note TEXT NOT NULL DEFAULT '',
    checked_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(profile_id) REFERENCES jurisdiction_rule_profiles(id)
);

CREATE TABLE IF NOT EXISTS operation_jurisdiction (
    operation_key TEXT PRIMARY KEY,
    country_code TEXT NOT NULL DEFAULT 'US',
    region_code TEXT NOT NULL,
    profile_id TEXT,
    selected_by TEXT NOT NULL DEFAULT '',
    selected_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY(profile_id) REFERENCES jurisdiction_rule_profiles(id)
);

CREATE INDEX IF NOT EXISTS idx_jurisdiction_profiles_region
ON jurisdiction_rule_profiles(country_code, region_code, review_status, effective_from DESC);

CREATE INDEX IF NOT EXISTS idx_jurisdiction_sources_profile
ON jurisdiction_rule_sources(profile_id);
