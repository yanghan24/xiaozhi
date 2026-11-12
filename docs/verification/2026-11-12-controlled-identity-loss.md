# #42：B 单键身份丢失与原产品启动

2026-11-12（Asia/Hong_Kong），本轮已完成真实 B 的受控身份键删除、独立 NVS 读回和原产品应用还原。17:40:54 启动报告确认原目标产品 ELF、原 P4/C6 和联网状态，`device_id=null`。用户随后要求拔走设备，现场语音拒绝、新补发和写回均未继续；[#42](https://github.com/yanghan24/xiaozhi/issues/42)／#13 保持打开。完整脱敏结果见[设备摘要](evidence/2026-11-12-controlled-identity-loss/device-summary.json)及[17:41服务器核对](evidence/2026-11-12-controlled-identity-loss/server-after-loss.json)。

## 环境与备份

操作由 Ubuntu `jt@192.168.168.114` 经 SSH RFC2217 连接 Windows COM3 上的 CH343 UART 执行。P4 rev3.2、Waveshare WiFi6 Touch LCD4.3、物理32MB Flash，继续使用原16MB分区布局和已确认 VALID 的 ota_1。Windows esptool5.6用于先前直接只读备份；本轮 Ubuntu 维护工具为 esptool4.13.0／460800。串口转发和反向SSH隧道在设备阶段完成后关闭。

16:43–16:58 已完成32MB Flash私有备份，32段分别与设备MD5比对；总SHA256为 `ed75c1af3b5c8286ddc8529518f0bd498697b372c324edbef2819cbfbfb524a5`。Ubuntu另存一份并核对摘要一致。应用、NVS、原身份和实际硬件报告均私有保存；这些文件及真实凭据没有进入仓库。

16:44、17:25及删除后的服务器观察均对应以下运行服务。本任务没有重建、重启或部署服务：

| 服务 | 镜像／SHA256 ID | 启动时间（香港） |
| --- | --- | --- |
| API | `xiaozhi-product-api:1b50709`／`429f8577b4559b27e7dacb558db0e4f53321faed2e2eb4f70798bef1f47ecb63` | 11:13:39 |
| 语音 | `xiaozhi-product-voice:pr79-f830a8c`／`19070d27ddd2332d43799c3caff669a6a131f180f98d500ef37824c3cc1bd7ca` | 14:50:32 |

该观察更新当前部署入口；它不替代此前PR81候选的13:38–14:05停用恢复出厂结果，也不把14:17时的镜像观察扩展为本轮功能验收。

## SDK、测试镜像与原产品

使用 Espressif 官方 ESP-IDF v5.5.5 源码（实际提交 `b774170ff46c393eeb5e495ea37936038d3f4f4f`）和 `esp-14.2.0_20260121`／GCC14.2.0，在 Ubuntu 隔离构建目录完成普通维护与专用测试镜像构建。没有安装或启动替代产品服务。

专用源码见[草稿 PR #85](https://github.com/yanghan24/xiaozhi/pull/85)，提交 `7450e9a`，基于固件 `a30f6a8`。受测 `identity_loss_main.cc` SHA256为 `2dcca81040561cfb65e804130bddc44ee01fb75dd91f8d2f5fc46a9d5cc3ea7b`，与该提交一致。17个主机防护场景通过（CTest为1个测试执行程序），覆盖输入、当前身份／锚点不匹配、各类NVS值保持及存储失败。

| 镜像 | bin SHA256 | ELF SHA256 |
| --- | --- | --- |
| 专用单键删除测试 | `a69a502ea24c0b57c354256398c3a3c32682ed37fa0bf01bd92aa6e28ca97c69` | `5eebc711e820427ce08cae6f4b68783c5881c1c9a228a05fc7185ca0a0997877` |
| 普通恢复维护 | `18dbe4c1df33e03f613cbf7e815e97439e7300d62781e8a8dfb1163c76c37b07` | `c274d6ace3758c2e7888ef87980416da7eb1b96f1f438745935f80ea75d1aa29` |
| 原已验收产品，4,003,072字节 | `b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7` | `364a1d6f2d33a9fa0d1312956aa22daae0076c758a8d5ff4e433b0e7d52022a3` |

原产品没有重新构建，也没有伪造缺失的原ELF文件：使用固定[发布候选](https://github.com/yanghan24/xiaozhi/releases/tag/firmware%2Fp4-2.2.6-a30f6a8-rc1)的manifest、真实sdkconfig／依赖锁摘要、已验收bin及其嵌入ELF摘要，与当前完整应用读回、实际分区表和启动报告交叉核对。本轮台架适配器据此固定产品产物；没有把这种输入路径声称为原 `build_artifacts` 的完整ELF构建检查。

## 已完成：单键删除与独立读回

专用镜像离线运行，没有普通产品UI、Wi-Fi、音频或原烧录入口；初始化NVS失败时停止，且保留整分区擦除保护。有效UART请求必须携带完整当前六字段身份、准确P4、完整device_id锚点、操作号和显式确认；namespace／key硬编码。

1. 仅把测试镜像写入当前ota_1并独立读回；分区表、bootloader、otadata及assets保持。在线产品之前刷新了`env/cache`，因此先在离线测试镜像中重新保存10条存活NVS记录作为精确删除基线。
2. 真实B拒绝错误确认、额外字段和错误凭据三个请求；随后独立NVS解析确认全部存活记录未变。
3. 持久保存删除意图后，只发送一次有效请求。程序通过原生 `nvs_erase_key("identity_v1")` 和commit执行删除，返回操作匹配的身份缺失、锚点及其余记录保持回执。
4. 独立Flash读回及官方NVS解析确认 `identity_v1` 消失，`device_id` 锚点和其余9条记录的类型／值逐键保持。SDK日志页布局不要求字节不变。
5. 仅写回原已验收产品应用；全量读回与原bin一致，ota_1／VALID、受保护启动区域和assets保持。17:40:54复位联网报告仍为原目标ELF、原P4/C6，且 `device_id=null`。

步骤5没有取得现场语音尝试或完整身份失败启动日志，因此只确认产品身份加载缺失；不能据此标记真实语音拒绝已验收。没有还原健康NVS、整分区或全片擦除、改写分区布局、再次OTA或发起新补发。

外层shell脚本在长设备步骤执行期间被更新，设备成功回执和状态落盘后，shell因残余尾命令报错并以1退出。本轮如实保留该包装层错误；设备状态文件及启动报告已经核对，没有重跑刷写。后续包装改为 `exec` 运行，并使用临时文件校验后替换，避免执行期间覆盖脚本。

## 受控接续材料与服务器状态

旧硬件电脑的原始已完成恢复journal和原产品ELF未找回。17:25在正常运营管理员登录仍有效时，以只读管理员recovery GET的当前delivered回执、实际NVS身份与后台当前hash、实际P4/C6报告及原映射保存新的前驱接续快照。`phase=delivered`来源是本次认证GET的当前status；没有补造历史本地检查点或声称找回原journal。

这份明确标注来源的私有快照和固定发布bin用于准备本轮台架接续；标准恢复引擎未改动，尚未用它执行新补发。本轮不交付通用的“journal缺失自动恢复”或任意损坏恢复能力。[原恢复工具限制](../development/device-identity-recovery.md)继续有效。

17:41的Ubuntu一致性快照和17:43收尾核对确认：B服务器仍为ready／admitted=1／local_enabled=1／许可21／归属7，原档案、凭据、原账号及P4/C6映射保持；A、全部交付／补发记录、所记录配置／密钥和原OTA预留不变。服务镜像与16:44基线的启动时间保持。服务器仍有有效凭据，不等于已删除该秘密的物理B能正常使用。

## 离场续作点

用户要求拔走设备后，停止在 `product_identity_missing_verified`，尚未创建本轮recovery目录、操作号或新凭据。原备份及本轮状态在Windows `.runtime/issue42-device` 和Ubuntu `/home/jt/issue42-idf-inputs/private-b-20261112` 私有保存。串口转发已关闭，台架调度阶段改为硬件不可用，防止意外执行。

下次B接回后，先核对实际硬件、原目标产品、ota_1和当前身份缺失状态，取得真实语音拒绝；再检查管理员正常登录、前驱回执和即时服务器基线，使用一个新持久journal完成管理员补发、原生维护写回、产品还原、设备自行device-check及完成回执。随后验证新旧凭据、适用中断／迟到请求、设备重启、扫码原账号和完整语音。当前凭据尚未轮换；一旦后续轮换，不能通过恢复旧NVS或旧journal回退后台代际。

本轮结果补齐“真实单键丢失／其余NVS保持／原产品身份加载缺失”的证据。真实异常恢复闭环、管理员device_disabled维护、适用新恢复边界和最初补发瞬间历史比对仍未齐；不得关闭#42或#13。已通过的正常OTA、恢复出厂和历史API重启继续复用，不为补日志重复执行。
