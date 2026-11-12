# #42：设备方接续身份异常恢复

交接时点：2026-11-12 17:43（Asia/Hong_Kong）。用户要求先完成独立工作并提交，后续由设备方完成实机测试后提交报告。已通过内容和证据范围见[单键身份丢失记录](../verification/2026-11-12-controlled-identity-loss.md)，专用测试工具见[草稿PR #85](https://github.com/yanghan24/xiaozhi/pull/85)。

## 接手状态

- B：Waveshare ESP32-P4 WiFi6 Touch LCD4.3，P4 rev3.2、物理32MB Flash、原16MB分区布局、ota_1／VALID。
- 原产品已还原并全量读回一致：bin SHA256 `b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7`，ELF `364a1d6f2d33a9fa0d1312956aa22daae0076c758a8d5ff4e433b0e7d52022a3`。
- `device_ctrl/identity_v1` 已受控删除；原 `device_id` 锚点及其余9条存活NVS记录保持。B尚不能正常产品对话，不要再次删除身份或恢复出厂来“准备”本轮。
- 服务器仍保存原档案、当前旧凭据代际及原归属，B为ready／许可21／归属7；A、交付及补发记录保持。尚无本轮新补发、新凭据或恢复journal。
- Windows串口转发和Ubuntu反向SSH隧道均已关闭；台架阶段标记为硬件不可用。所有新操作先核实实际接线和设备身份。

## 已准备材料

| 材料 | 私有位置与用途 |
| --- | --- |
| 原32MB完整备份及应用／NVS／原身份／硬件报告 | Windows `D:/AI/xiaozhi/.runtime/issue42-device/private/segmented-backup-20261112-164333` |
| 第二份32MB完整备份 | Ubuntu `/home/jt/issue42-device-private/b-flash-before-identity-loss-20261112.bin`，摘要与设备摘要一致 |
| 固定产品与普通维护镜像、只供核对的分区表及manifest | Windows `D:/AI/xiaozhi/.runtime/issue42-device/private/hardware-handoff-20261112.tar.gz`；Ubuntu `/home/jt/issue42-idf-inputs/private-b-20261112/hardware-handoff-20261112.tar.gz` |
| 当前私有状态、原身份、明确标记来源的前驱快照及服务器基线 | Ubuntu `/home/jt/issue42-idf-inputs/private-b-20261112`；本轮状态为 `bench-state.json` 的 `product_identity_missing_verified` |
| 正常运营管理员登录 | Ubuntu `/home/jt/issue42-device-private/operator-login.json`；接续前重新验证，到期后通过正常入口登录 |
| 官方SDK与交叉编译环境 | Ubuntu `/home/jt/issue42-idf-inputs`，ESP-IDF5.5.5／GCC14.2；新维护构建已完成 |
| 已准备但尚未执行新补发的台架适配器 | Ubuntu `/home/jt/issue42-idf-inputs/bench-device.py` 和 `/home/jt/issue42-run-bench.sh`，调用固定恢复引擎；恢复阶段仍需实机核验 |

传输包只含应用、核对材料和公开交接说明，不含设备身份、NVS、Flash备份或管理员登录。它保留原产品已有服务配置，应经私有文件传输交付；分区表仅供比较，不能据此重写设备布局。普通维护bin SHA256为 `18dbe4c1df33e03f613cbf7e815e97439e7300d62781e8a8dfb1163c76c37b07`。

传输包共2,408,439字节，SHA256为 `3623123c44e83085c9324b345be1b4e7731dfb434f4ba58fd8027341d52cb6c2`。Ubuntu另有可私有传输的导出副本 `/home/jt/issue42-device-hardware-handoff-20261112.tar.gz`；包内 `handoff-manifest.json` 和 `SHA256SUMS` 可逐文件核对。

原硬件电脑journal和原产品ELF未找回。本次只读管理员回执与实际身份／硬件核对生成了带来源说明的新前驱接续快照，使用精确原发布bin准备台架接续。没有改造标准工具以自动支持缺journal／缺ELF，也尚未以该适配器完成新恢复。执行前需检查这份快照仍对应服务器当前代际；不得伪造历史checkpoint或用首次交付号替代当前已完成恢复。

## 设备方与服务器方执行顺序

1. 设备方接回B，核对板型、P4/C6、原分区、ota_1、原目标产品和当前身份缺失；保留当前状态的私有快照。若换了电脑或串口，记录新传输方式并重新验证只读连接，不直接套用原COM3端点。
2. 先取得缺身份产品的完整启动／UART记录，实际唤醒并尝试一次语音，确认无法进入正常产品对话。普通Wi-Fi联网可以存在。回传脱敏结论后再进入补发；目前只有 `device_id=null` 启动报告，没有这项现场语音结果。
3. 服务器方检查[当前部署](../deployment/current.md)、正常管理员登录、B即时档案／凭据hash／归属／许可、A及当前已完成recovery回执。固定本轮产品和普通维护构建，先持久保存唯一的新补发号、写入号及恢复journal，再由正常管理员接口创建本轮恢复。设备方不使用普通烧录凭据发起补发。
4. 通过已核对的串口只临时写当前应用槽的普通维护镜像，用现有 `recover` 协议写入后台新身份并核对提交回执；随后还原精确原产品应用、核对读回及P4/C6／设备ID／ELF。由产品设备自行执行新凭据device-check，服务器再提交完成回执。台架适配器复用 `scripts/device_recovery.py` 的恢复状态机。
5. 本轮适用中断检查使用同一持久journal：例如新凭据已保存但未写入、提交回执已保存但未还原产品两个检查点退出后续作。未知USB／HTTP结果先查记录，不另建操作、不自动重复补发；不把这些进程检查声称为真实断电试验。
6. 服务器核对旧凭据401、新凭据200、原档案／P4/C6／客户归属和归属版本保持，准确记录许可代际的正常增加；检查旧恢复及原交付迟到complete不能恢复旧凭据，同操作重放返回相同新身份，A保持。不得以测试端主动调用device-check替代产品自行验证。
7. 设备方执行一次独立重启，核对身份和原目标产品，扫码确认仍为原账号，完成一次完整语音回答；服务器核对前后快照及恢复delivered状态。报告和证据随后以文档PR提交，再按#42全部标准判断能否关闭。

一旦新凭据轮换，旧健康NVS／旧journal仅保留审计，不能再写回设备来回退代际。管理员device_disabled维护路径及最初补发瞬间的历史归属／许可缺口仍单列；已通过的正常OTA、恢复出厂和历史API重启无需重做。原11:20未知OTA操作也不得重发。

## 设备方报告格式

每项填写通过／失败／未执行及时间，引用脱敏证据；真实身份、凭据、扫码handle、Wi-Fi和原始Flash／NVS／UART材料经私有通道保存。

| 检查 | 必须记录 |
| --- | --- |
| 接手实物及版本 | 时间／时区、板型与修订、分区摘要、活动槽及状态、应用bin／ELF摘要、私有备份摘要 |
| 缺身份拒绝 | 身份缺失、anchor／其他NVS保持、产品启动、实际语音尝试及失败表现 |
| 补发前后 | 服务器版本、操作／写入号私有关联、原档案／硬件／账号保持、许可和归属版本前后值 |
| 写回与续作 | 原生提交回执、独立NVS／应用读回、适用中断位置和同journal续作结果 |
| 新旧凭据及迟到请求 | HTTP／协议码、请求前后状态、A隔离、设备自行验证和delivered完成 |
| 最终使用 | 独立重启、原账号扫码及完整语音结果；时间、UART／音频证据来源及缺口 |

未执行项目保留未执行，不能用模拟、历史其他轮次或服务器ready替代本轮现场结果。
