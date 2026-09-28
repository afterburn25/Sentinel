CREATE TABLE IF NOT EXISTS persona_lora_activation_history (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    persona_name TEXT NOT NULL,
    from_binding_id INTEGER NOT NULL,
    to_binding_id INTEGER NOT NULL,
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

CREATE INDEX IF NOT EXISTS idx_persona_lora_activation_history
    ON persona_lora_activation_history(persona_name,to_binding_id,id DESC);
