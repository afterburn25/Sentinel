UPDATE model_foundations
SET
    source_model='Qwen/Qwen3.5-9B',
    trainable_source_path=
        CASE
            WHEN trainable_source_path='' OR trainable_source_path='Qwen3.5-9B'
                THEN 'Qwen/Qwen3.5-9B'
            ELSE trainable_source_path
        END,
    updated_utc=CURRENT_TIMESTAMP
WHERE
    name='SARA Foundation 1'
    AND source_model IN ('Qwen3.5-9B','Qwen/Qwen3.5-9B');
