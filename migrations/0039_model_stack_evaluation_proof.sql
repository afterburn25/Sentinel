ALTER TABLE model_foundations
ADD COLUMN approved_evaluation_run_id TEXT NOT NULL DEFAULT '';

ALTER TABLE persona_lora_bindings
ADD COLUMN approved_evaluation_run_id TEXT NOT NULL DEFAULT '';

UPDATE model_foundations
SET approved_evaluation_run_id='LEGACY_ACTIVE_1.0.15'
WHERE status='ACTIVE' AND approved_evaluation_run_id='';

UPDATE persona_lora_bindings
SET approved_evaluation_run_id='LEGACY_ACTIVE_1.0.15'
WHERE active=1 AND approved_evaluation_run_id='';
