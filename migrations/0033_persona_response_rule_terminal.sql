ALTER TABLE persona_response_rules
ADD COLUMN terminal INTEGER NOT NULL DEFAULT 1
CHECK(terminal IN (0,1));
