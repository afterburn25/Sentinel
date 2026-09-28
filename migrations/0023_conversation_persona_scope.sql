ALTER TABLE simulation_conversations
ADD COLUMN persona_summary TEXT NOT NULL DEFAULT '';

UPDATE simulation_conversations
SET persona_summary = persona_name
WHERE persona_summary = '';

UPDATE simulation_conversations
SET persona_name = trim(
    CASE
        WHEN instr(persona_name, ',') > 0
            THEN substr(persona_name, 1, instr(persona_name, ',') - 1)
        ELSE persona_name
    END
)
WHERE persona_name <> '';
