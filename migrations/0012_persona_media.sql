CREATE TABLE IF NOT EXISTS persona_media (
    id TEXT PRIMARY KEY,
    persona_name TEXT NOT NULL,
    original_name TEXT NOT NULL,
    stored_path TEXT NOT NULL,
    sha256 TEXT NOT NULL,
    tags TEXT NOT NULL DEFAULT 'casual',
    approved INTEGER NOT NULL DEFAULT 0,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_media_persona
ON persona_media(persona_name, approved, created_utc DESC);
