ALTER TABLE simulation_conversations
ADD COLUMN persona_summary TEXT NOT NULL DEFAULT '';

UPDATE simulation_conversations
SET persona_summary = persona_name
WHERE persona_summary = '';

UPDATE simulation_conversations
SET persona_name = trim(substr(persona_name,1,instr(persona_name,',')-1))
WHERE instr(persona_name,',') > 0;

CREATE INDEX IF NOT EXISTS idx_simulation_conversations_persona_updated
ON simulation_conversations(persona_name COLLATE NOCASE,updated_utc DESC);
