ALTER TABLE training_review_items
ADD COLUMN correction_instruction TEXT NOT NULL DEFAULT '';

ALTER TABLE training_review_items
ADD COLUMN target_output_text TEXT NOT NULL DEFAULT '';

ALTER TABLE training_review_items
ADD COLUMN correction_updated_utc TEXT NOT NULL DEFAULT '';
