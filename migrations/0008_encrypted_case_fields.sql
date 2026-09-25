ALTER TABLE cases ADD COLUMN case_number_cipher BLOB;
ALTER TABLE cases ADD COLUMN case_number_nonce BLOB;
ALTER TABLE cases ADD COLUMN case_number_tag BLOB;

ALTER TABLE cases ADD COLUMN title_cipher BLOB;
ALTER TABLE cases ADD COLUMN title_nonce BLOB;
ALTER TABLE cases ADD COLUMN title_tag BLOB;

ALTER TABLE cases ADD COLUMN description_cipher BLOB;
ALTER TABLE cases ADD COLUMN description_nonce BLOB;
ALTER TABLE cases ADD COLUMN description_tag BLOB;
