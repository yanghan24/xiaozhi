CREATE TABLE speaker_third_party_service (
    id VARCHAR(48) CHARACTER SET ascii COLLATE ascii_bin PRIMARY KEY,
    display_name VARCHAR(128) NOT NULL,
    base_url VARCHAR(1024) NOT NULL,
    endpoints JSON NOT NULL,
    auth_mode VARCHAR(16) NOT NULL,
    auth_name VARCHAR(64) NOT NULL DEFAULT '',
    credential_cipher TEXT NULL,
    enabled BOOLEAN NOT NULL DEFAULT FALSE,
    version BIGINT NOT NULL DEFAULT 0
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

INSERT INTO speaker_third_party_service (id, display_name, base_url, endpoints, auth_mode, auth_name) VALUES
('amap', '高德', 'https://restapi.amap.com',
 JSON_ARRAY(JSON_OBJECT('path', '/v3/ip', 'purpose', 'IP 定位'), JSON_OBJECT('path', '/v3/weather/weatherInfo', 'purpose', '天气')), 'query', 'key'),
('qweather', '和风天气', '',
 JSON_ARRAY(JSON_OBJECT('path', '/geo/v2/city/lookup', 'purpose', '城市查询'), JSON_OBJECT('path', '/v7/weather/now', 'purpose', '实况'), JSON_OBJECT('path', '/v7/weather/7d', 'purpose', '七天预报')), 'query', 'key'),
('pconline', '太平洋电脑网', 'https://whois.pconline.com.cn',
 JSON_ARRAY(JSON_OBJECT('path', '/ipJson.jsp', 'purpose', 'IP 定位')), 'none', ''),
('air-quality', '空气质量聚合', '', JSON_ARRAY(), 'none', ''),
('open-meteo', '自建 Open-Meteo', '',
 JSON_ARRAY(JSON_OBJECT('path', '/v1/forecast', 'purpose', '天气预报'), JSON_OBJECT('path', '/v1/air-quality', 'purpose', '空气质量')), 'none', '');
