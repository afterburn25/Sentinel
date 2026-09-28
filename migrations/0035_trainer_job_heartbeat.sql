ALTER TABLE trainer_jobs
ADD COLUMN heartbeat_utc TEXT;

ALTER TABLE trainer_jobs
ADD COLUMN worker_pid INTEGER NOT NULL DEFAULT 0;
