CREATE TABLE product_asr_settings (
    id INT NOT NULL PRIMARY KEY,
    provider VARCHAR(16) NOT NULL,
    credential_cipher TEXT NULL,
    revision BIGINT NOT NULL DEFAULT 0
);
INSERT INTO product_asr_settings(id,provider,revision) VALUES (1,'market',0);
