UPDATE speaker_third_party_service
SET endpoints = JSON_ARRAY_APPEND(endpoints, '$', JSON_OBJECT(
        'path', '/airquality/v1/current/{latitude}/{longitude}', 'purpose', '实时空气质量')),
    version = version + 1
WHERE id = 'qweather'
  AND NOT JSON_CONTAINS(endpoints, '{"path": "/airquality/v1/current"}')
  AND NOT JSON_CONTAINS(endpoints, '{"path": "/airquality/v1/current/{latitude}/{longitude}"}');
