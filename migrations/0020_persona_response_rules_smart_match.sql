PRAGMA foreign_keys=OFF;

ALTER TABLE persona_response_rules RENAME TO persona_response_rules_old;

CREATE TABLE persona_response_rules (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    persona_name TEXT NOT NULL,
    match_type TEXT NOT NULL DEFAULT 'smart'
        CHECK(match_type IN ('exact','contains','smart')),
    trigger_text TEXT NOT NULL,
    response_text TEXT NOT NULL,
    enabled INTEGER NOT NULL DEFAULT 1,
    priority INTEGER NOT NULL DEFAULT 100,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    response_mode TEXT NOT NULL DEFAULT 'persona_variation'
);

INSERT INTO persona_response_rules(
    id,persona_name,match_type,trigger_text,response_text,
    enabled,priority,created_utc,updated_utc,response_mode)
SELECT
    id,persona_name,match_type,trigger_text,response_text,
    enabled,priority,created_utc,updated_utc,
    COALESCE(response_mode,'persona_variation')
FROM persona_response_rules_old;

DROP TABLE persona_response_rules_old;

CREATE INDEX IF NOT EXISTS idx_persona_response_rules_lookup
    ON persona_response_rules(persona_name,enabled,priority DESC,id ASC);

CREATE INDEX IF NOT EXISTS idx_persona_response_rules_mode
    ON persona_response_rules(persona_name,response_mode,enabled);

PRAGMA foreign_keys=ON;
