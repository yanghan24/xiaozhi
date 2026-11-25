UPDATE speaker_third_party_service
SET endpoints = JSON_ARRAY_APPEND(endpoints, '$', JSON_OBJECT('path', '/weather/v1/current', 'purpose', '实时天气')),
    version = version + 1
WHERE id = 'qweather' AND NOT JSON_CONTAINS(endpoints, '{"path": "/weather/v1/current"}');

UPDATE speaker_third_party_service
SET endpoints = JSON_ARRAY_APPEND(endpoints, '$', JSON_OBJECT('path', '/airquality/v1/current', 'purpose', '实时空气质量')),
    version = version + 1
WHERE id = 'qweather' AND NOT JSON_CONTAINS(endpoints, '{"path": "/airquality/v1/current"}');
