CREATE TABLE IF NOT EXISTS persona_profiles (
    name TEXT PRIMARY KEY COLLATE NOCASE,
    age INTEGER NOT NULL DEFAULT 21,
    location TEXT NOT NULL DEFAULT '',
    gender TEXT NOT NULL DEFAULT 'Unspecified',
    pronouns TEXT NOT NULL DEFAULT 'Unspecified',
    occupation TEXT NOT NULL DEFAULT 'Unspecified',
    education TEXT NOT NULL DEFAULT 'Unspecified',
    relationship_status TEXT NOT NULL DEFAULT 'Unspecified',
    family_context TEXT NOT NULL DEFAULT 'Unspecified',
    personality TEXT NOT NULL DEFAULT 'Balanced',
    social_style TEXT NOT NULL DEFAULT 'Balanced',
    confidence_level TEXT NOT NULL DEFAULT 'Medium',
    background TEXT NOT NULL DEFAULT '',
    interests TEXT NOT NULL DEFAULT '',
    writing_style TEXT NOT NULL DEFAULT 'Casual',
    intelligence_level TEXT NOT NULL DEFAULT 'Average',
    slang_level TEXT NOT NULL DEFAULT 'Medium',
    grammar_quality TEXT NOT NULL DEFAULT 'Natural',
    typo_tendency TEXT NOT NULL DEFAULT 'Low',
    emoji_tendency TEXT NOT NULL DEFAULT 'Medium',
    mood TEXT NOT NULL DEFAULT 'Neutral',
    locked_facts TEXT NOT NULL DEFAULT '',
    min_delay_ms INTEGER NOT NULL DEFAULT 3000,
    max_delay_ms INTEGER NOT NULL DEFAULT 8500,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_profiles_updated
ON persona_profiles(updated_utc DESC);
