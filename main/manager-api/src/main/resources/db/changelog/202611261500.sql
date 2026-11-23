CREATE TABLE speaker_function_plugin (
    plugin_id VARCHAR(32) NOT NULL PRIMARY KEY,
    config_cipher TEXT NOT NULL,
    enabled BOOLEAN NOT NULL,
    version BIGINT NOT NULL
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

ALTER TABLE speaker_third_party_service
    ADD COLUMN jwt_issuer VARCHAR(128),
    ADD COLUMN jwt_project VARCHAR(128),
    ADD COLUMN jwt_key_id VARCHAR(128),
    ADD COLUMN default_city VARCHAR(64);

UPDATE speaker_third_party_service
SET endpoints = JSON_ARRAY(
    JSON_OBJECT('path', '/geo/v2/city/lookup', 'purpose', '城市查询'),
    JSON_OBJECT('path', '/weather/v1/current/{latitude}/{longitude}', 'purpose', '实况'),
    JSON_OBJECT('path', '/weather/v1/daily/{latitude}/{longitude}', 'purpose', '七天预报')
), version = version + 1
WHERE id = 'qweather';
