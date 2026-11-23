-- Use the original firmware catalogue for the exact current first-batch board.
INSERT INTO sys_dict_data(id,dict_type_id,dict_label,dict_value,remark,sort,creator,create_date,updater,update_date)
SELECT 911171500000000001,t.id,'Waveshare ESP32-P4 4.3 寸音箱','waveshare-esp32-p4-wifi6-touch-lcd-4.3',
 '第一批音箱；16MB partitions/v2/16m.csv；与其他屏幕型号分别发布',0,1,NOW(),1,NOW()
FROM sys_dict_type t WHERE t.dict_type='FIRMWARE_TYPE'
 AND NOT EXISTS(SELECT 1 FROM sys_dict_data d WHERE d.dict_type_id=t.id AND d.dict_value='waveshare-esp32-p4-wifi6-touch-lcd-4.3');
