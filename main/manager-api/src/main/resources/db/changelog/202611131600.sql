ALTER TABLE dc_ota_firmware
  MODIFY github_release_id BIGINT NULL,
  MODIFY github_asset_id BIGINT NULL,
  ADD COLUMN source_kind VARCHAR(32) NOT NULL DEFAULT 'github_release',
  ADD COLUMN source_name VARCHAR(255) NULL;
