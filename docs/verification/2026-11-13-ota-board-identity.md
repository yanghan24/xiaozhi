# OTA 精确板型识别修复

2026-11-13，Ubuntu `jt@192.168.168.114`。实现提交 `4602a66e02a3d87ce18c85d920ddba9ea5401181`；本记录先保存回归与候选产物，部署结果另行补充。

## 问题与修复

B 的当前认证会话上报 `esp32-p4-wifi6-touch-lcd`、ESP32-P4/model 18、revision 302、16MiB Flash，分区满足 `partitions/v2/16m.csv`。所选已校验固件的精确板型为 `waveshare-esp32-p4-wifi6-touch-lcd-4.3`；原字符串相等比较因此返回 `incompatible_board`。

固件 `12f2ff1` 的多个屏幕变体共用该系列名称，不能无条件建立别名映射。A、B 的现有 `dc_delivery_operation` 均记录精确 4.3 寸型号，且为 `delivered` 并有设备验证及完成时间。交付工具的 `scripts/delivery_hardware.py` 在登记前调用 `scripts/verify_product_build.py`，要求 4.3 寸专用编译配置。这是管理员授权的交付登记证据，不是硬件远程证明。

修复在预检和实际下发前分别读取当前设备的交付记录：只有上述精确登记可以补足系列上报的型号信息。未知名称、其他屏幕、缺失及未完成交付继续拒绝；直接上报精确型号也不能覆盖矛盾登记。芯片、revision、Flash、分区与 ELF 检查保留。原固件清单、二进制、SHA 去重及设备身份数据均无需修改。

## Ubuntu 验证

- 独立 Compose 项目 `ota-board-identity`，专用 MySQL/Redis，与生产业务库分离。
- 首轮空库启动揭示原 master changelog 的 OTA `ALTER TABLE` 排在 `CREATE TABLE` 前。修复仅将五项迁移按依赖顺序移到尾部，133 个 changeSet 的内容、ID、author、SQL 路径保持相同。
- 最终全量 Java 回归：401 项，失败 0、错误 0、跳过 0；`scripts/check-integration-tests.sh` 通过，JAR 构建成功。
- 新 HTTP 回归覆盖精确 manifest 上传、系列/未知 manifest 拒绝、系列上报结合已完成登记的预检及确认、其他屏幕/未知登记拒绝，以及确认前登记被更改/撤回/删除时 `not_sent` 且未发送命令。
- Standards 与 Spec 两轴复审通过；早期无条件别名方案已撤回，不属于最终实现。

候选 API 镜像：`sha256:cb7c7d1774c3d8dae13402e0c9f1656903c5eb1077f7b87c8fff14f655e72c7b`。镜像内 JAR 与受测 JAR 的 SHA-256 同为 `a006c015471b99cfb330cebdaf7319a591e9230c2c1acbae9ee501108326d9b6`。生产准备备份位于 `/home/jt/ota-board-identity-20261113/rollout/backup`，包含必要身份、固件及操作记录，排除对话和音频。

回退仅需恢复此前兼容 API 镜像；无新增数据库迁移或数据转换。服务回退不会撤销设备已经执行的 OTA。本次修复验证不发送真实设备升级命令。
