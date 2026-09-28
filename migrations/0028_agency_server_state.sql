CREATE TABLE IF NOT EXISTS agency_server_config (
    singleton_id INTEGER PRIMARY KEY CHECK(singleton_id=1),
    endpoint TEXT NOT NULL DEFAULT '',
    agency_id TEXT NOT NULL DEFAULT '',
    workstation_id TEXT NOT NULL DEFAULT 'local-workstation',
    enabled INTEGER NOT NULL DEFAULT 0 CHECK(enabled IN (0,1)),
    updated_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

INSERT OR IGNORE INTO agency_server_config(
    singleton_id,endpoint,agency_id,workstation_id,enabled
) VALUES(1,'','','local-workstation',0);

CREATE TABLE IF NOT EXISTS agency_sync_queue (
    id TEXT PRIMARY KEY,
    item_type INTEGER NOT NULL,
    local_reference TEXT NOT NULL DEFAULT '',
    attempts INTEGER NOT NULL DEFAULT 0,
    complete INTEGER NOT NULL DEFAULT 0 CHECK(complete IN (0,1)),
    created_utc TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    completed_utc TEXT
);

CREATE INDEX IF NOT EXISTS idx_agency_sync_queue_pending
ON agency_sync_queue(complete,created_utc);
