ALTER TABLE persona_response_rules
ADD COLUMN response_mode TEXT NOT NULL DEFAULT 'persona_variation';

CREATE INDEX IF NOT EXISTS idx_persona_response_rules_mode
    ON persona_response_rules(persona_name, response_mode, enabled);
