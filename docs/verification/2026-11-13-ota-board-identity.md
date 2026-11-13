# OTA 精确板型识别修复

2026-11-13，Ubuntu `jt@192.168.168.114`。实现提交 `4602a66e02a3d87ce18c85d920ddba9ea5401181`；[PR #109](https://github.com/yanghan24/xiaozhi/pull/109) 已审核并 squash 合并为 `7a1a097e8a23e63776fc155a6d1929bb134d1f97`，受测提交与合并提交的 `main/manager-api` 内容一致。

## 问题与修复

B 的当前认证会话上报 `esp32-p4-wifi6-touch-lcd`、ESP32-P4/model 18、revision 302、16MiB Flash，分区满足 `partitions/v2/16m.csv`。所选已校验固件的精确板型为 `waveshare-esp32-p4-wifi6-touch-lcd-4.3`；原字符串相等比较因此返回 `incompatible_board`。

固件 `12f2ff1` 的多个屏幕变体共用该系列名称，不能无条件建立别名映射。A、B 的现有 `dc_delivery_operation` 均记录精确 4.3 寸型号，且为 `delivered` 并有设备验证及完成时间。交付工具的 `scripts/delivery_hardware.py` 在登记前调用 `scripts/verify_product_build.py`，要求 4.3 寸专用编译配置。这是管理员授权的交付登记证据，不是硬件远程证明。

修复在预检和实际下发前分别读取当前设备的交付记录：只有上述精确登记可以补足系列上报的型号信息。未知名称、其他屏幕、缺失及未完成交付继续拒绝；直接上报精确型号也不能覆盖矛盾登记。芯片、revision、Flash、分区与 ELF 检查保留。原固件清单、二进制、SHA 去重及设备身份数据均无需修改。

## Ubuntu 验证

- 独立 Compose 项目 `ota-board-identity`，专用 MySQL/Redis，与生产业务库分离。
- 首轮空库启动揭示原 master changelog 的 OTA `ALTER TABLE` 排在 `CREATE TABLE` 前。修复仅将五项迁移按依赖顺序移到尾部，134 个 changeSet 的内容、ID、author、SQL 路径保持相同。
- 最终全量 Java 回归：401 项，失败 0、错误 0、跳过 0；`scripts/check-integration-tests.sh` 通过，JAR 构建成功。
- 新 HTTP 回归覆盖精确 manifest 上传、系列/未知 manifest 拒绝、系列上报结合已完成登记的预检及确认、其他屏幕/未知登记拒绝，以及确认前登记被更改/撤回/删除时 `not_sent` 且未发送命令。
- Standards 与 Spec 两轴复审通过；早期无条件别名方案已撤回，不属于最终实现。

候选 API 镜像：`sha256:cb7c7d1774c3d8dae13402e0c9f1656903c5eb1077f7b87c8fff14f655e72c7b`。镜像内 JAR 与受测 JAR 的 SHA-256 同为 `a006c015471b99cfb330cebdaf7319a591e9230c2c1acbae9ee501108326d9b6`。生产准备备份位于 `/home/jt/ota-board-identity-20261113/rollout/backup`，包含必要身份、固件及操作记录，排除对话和音频。

回退仅需恢复此前兼容 API 镜像；无新增数据库迁移或数据转换。服务回退不会撤销设备已经执行的 OTA。本次修复验证不发送真实设备升级命令。

## 上线与在线预检

17:33:57（Asia/Hong_Kong）已部署上述 API 镜像，受测 JAR、镜像内 JAR 和现网镜像 ID 已核对。初次部署脚本误用 `/pub/config` 作为就绪地址，触发了恢复原 API；改为实际 `/user/pub-config` 后，先确认原镜像、配置及依赖完整，再重新部署成功。该过程没有设备 OTA 下发。

仅重建产品 API，并平滑重载两个 Nginx 以刷新上游地址。语音和管理页面、数据库、Redis、TLS 入口容器 ID 保持；语音在本次准备时已由其他工作更新为 `sha256:578524ae8454c0d69334844a563af35df2c6c7bd73ed550865155fd09176b686`，本次沿用该现场版本。原私有配置、设备身份/许可/归属、交付映射、固件目录及操作记录的前后摘要一致。正常 TLS OTA 发现返回 200，公网管理/内部入口和无签名固件下载均返回 404。

17:34:12 的 B 预检为 `offline`，因此在线实机结果仍待唤醒后复查，不宣称已通过。原固件 `447d6bea932a4803866ac18e78701ba4` 保留精确 4.3 寸 manifest；重复上传及修改旧记录均不需要。真实 OTA 操作数前后均为 0。

脱敏结果见 [deployment.json](evidence/2026-11-13-ota-board-identity.json)。测试和部署私有日志保存在 `/home/jt/ota-board-identity-20261113/`，备份不公开。
