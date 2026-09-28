CREATE TABLE IF NOT EXISTS operation_supervisor_state (
    operation_key TEXT PRIMARY KEY,
    investigator_takeover INTEGER NOT NULL DEFAULT 0
        CHECK(investigator_takeover IN (0,1)),
    takeover_actor TEXT NOT NULL DEFAULT '',
    takeover_note TEXT NOT NULL DEFAULT '',
    takeover_utc TEXT NOT NULL DEFAULT '',
    released_by TEXT NOT NULL DEFAULT '',
    released_utc TEXT NOT NULL DEFAULT '',
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
