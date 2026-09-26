CREATE TABLE IF NOT EXISTS persona_profiles (
    name TEXT PRIMARY KEY,
    age INTEGER NOT NULL,
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
    communication_level TEXT NOT NULL DEFAULT 'Age-appropriate',
    slang_level TEXT NOT NULL DEFAULT 'Moderate',
    grammar_quality TEXT NOT NULL DEFAULT 'Casual',
    typo_frequency TEXT NOT NULL DEFAULT 'Occasional',
    emoji_level TEXT NOT NULL DEFAULT 'Occasional',
    vocabulary_level TEXT NOT NULL DEFAULT 'Age-appropriate',
    capitalization_style TEXT NOT NULL DEFAULT 'Casual',
    message_length TEXT NOT NULL DEFAULT 'Short to medium',
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_profiles_updated
ON persona_profiles(updated_utc DESC);
