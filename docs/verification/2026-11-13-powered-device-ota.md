# 开机联网在线与待机 OTA

2026-11-13 19:12（Asia/Hong_Kong），Ubuntu `jt@192.168.168.114` 已部署 [PR #114](https://github.com/yanghan24/xiaozhi/pull/114)，合并提交 `a285acc3cd904aa567b01814b39b5da4f888dde8`。设备在线来自近期认证管理联系，语音连接单独显示；配套固件通过管理通道在空闲时领取 OTA。旧固件已能通过原状态轮询提供在线证据，首次安装配套固件仍需既有语音 OTA。

## 代码与验证

服务端审核范围 `b4707e3...62f68b9`，合并后的完整树与受审树一致。API tree 为 `171e10adec260ed06c0daeb7ab2cd54c8ed45f86`，Web tree 为 `d28de6d589085f1241bad250fc0d6504f1d567ca`。

- Ubuntu 独立项目 `powered-ota-backend-release`：完整 Java 回归 **432/432**，失败、错误、跳过均为 0；集成测试守卫及打包通过。596 个 API 源文件与受测提交逐一校验一致。
- 使用最终固件 `b471c10` 的生产 C++ 请求生成和响应解析，与真实 Java HTTP 完成领取、解析、接收确认及持久 ACK，**1/1 通过**。使用隔离测试身份和数据库，不代表物理设备安装。
- Web：94 项单元测试、10 项契约测试通过；最终文案补充后 9 项 OTA 组件测试再次通过。生产构建 hash `14bdc793d198633c`，镜像中 444 个文件逐一与构建目录一致。浏览器自动授权拒绝本地页面地址，视觉检查未完成；没有改用其他地址绕过。
- Standards 与 Spec 两轴最终均无剩余阻断项。修复了未暴露预留的异常/重启回收、失败回执跨启动、管理设备真实语音状态及硬件快照一致性，并补充故障注入和真实服务重启回归。

证据：[Java 回归](evidence/2026-11-13-powered-device-ota/backend-tests.json)、[跨端验收](evidence/2026-11-13-powered-device-ota/interop-results.json)、[源码核验](evidence/2026-11-13-powered-device-ota/source-verification.json)、[产物核验](evidence/2026-11-13-powered-device-ota/artifact-verification.json)。

## 配套固件

[PR #113](https://github.com/yanghan24/xiaozhi/pull/113) 已 squash 合入 `firmware/p4` 为 `f6cbee7ad0e5b99d1e37cc63125ca5d8e79a1dd9`，完整树与受测 `b471c1019566d204839e80c3ac2026caf1b16739` 一致。以 A/B 已安装的 `12f2ff1` 为功能基线，保留其自动停止监听与静默打断行为。

最终固件 40/40 主机测试、ESP-IDF 5.5.5 P4 完整编译及 10 项产品校验通过；1,660 个源文件与提交归档一致。原子音频门禁覆盖录音/音乐启动与恢复、排队和在途音频，冻结新活动后才判断空闲。维护账本 `ota_maint` 与身份 `device_ctrl` 均保留于本机设置恢复。

| 项目 | 值 |
| --- | --- |
| 后台候选名称 | `p4-powered-ota-2.2.6-b471c10-rc1` |
| 固件目录 ID | `1761d154409f4214b66bd605d70428b7` |
| 来源 / 状态 | `backend_upload` / `verified`，候选标记开启 |
| 硬件 | 精确 4.3 寸、P4 revision 301–399、16MiB、C6 SDIO；原分区与构建配置保持 |
| BIN 大小 | 4,022,608 字节，应用槽剩余 106,160 字节 |
| BIN SHA256 | `f343d890a30ede6ad045ddd4eaf629e7c455e2b24af367c121cb53e35041103d` |
| ELF SHA256 | `de97cbaa11ef327a9b986516f779eb4243924012d58e3dc335583d597cc902d0` |

已通过小智管理后台真实上传并核对完整 manifest、源提交、SHA、ELF 和候选标记；没有发布公开固件下载。私有包位于 `/home/jt/powered-ota-firmware/artifacts/b471c10/`。见[固件构建证据](evidence/2026-11-13-powered-device-ota/firmware-build.json)与[后台目录结果](evidence/2026-11-13-powered-device-ota/firmware-catalog.json)。

## 部署与恢复

API 镜像 `sha256:0af7ce75dc8af5b1f5d21e74913dc90cb032867b473190fc421e2a0c79f90eb5`，Web 镜像 `sha256:f801eab396d85cc78b5dc0e442aaaeaf19816e1c145e133215d3e0d2f53d9db2`。受测 JAR 与镜像内 JAR 的 SHA256 同为 `513064a0351ebb535108eac8a7047988ab875a56706ecce238b318c0707b46ab`；Web 归档 SHA256 为 `2be7380ae2c08e4ac7fb42c13b1040327201a791d21300a1e2fdf49f4ce1b728`。

只重建 API/Web，两个 Nginx 平滑重载；现场语音 `sha256:578524ae8454c0d69334844a563af35df2c6c7bd73ed550865155fd09176b686`、数据库、Redis、TLS 容器保持。原设备身份/归属/许可、交付映射、固件目录、OTA 操作及私有配置摘要前后相同。新增迁移只增加在线联系表、启动记录和操作字段，原列逐项对比，不把正常新增字段误报为业务数据变化。

管理 API 就绪检查通过。Ubuntu 本机网卡 443 的正常 TLS 检查：发现 200；维护入口匿名 POST 401、错误方法 403、超限请求 413；公网代理中的管理/内部入口和无签名固件下载均 404。这是本机 TLS 入口验证，不代表本轮已从外网重新验证 18443 可达性或浏览器页面。

私有范围备份位于 `/home/jt/powered-device-ota-20261113/rollout/backup`，包含所需身份、固件与操作记录，排除对话和录音。回退只替换兼容 API/Web 与代理配置；停止新发布并核对无活动维护任务后执行，保留新增数据库结构、操作账本及原密钥，不能覆盖数据库以制造重试。服务回退不会降级设备已安装固件。见[部署证据](evidence/2026-11-13-powered-device-ota/deployment.json)。

## 实机范围

19:14:26 与 19:15:06 通过真实管理员设备列表读取：B 均为 `online=true`、`voice_status=offline`、`status=online_requires_wake`；最近认证联系从 `1794568464500` 推进到 `1794568494736`。这证明旧固件 B 无需语音唤醒即可显示在线，升级条件仍如实标为首次升级需唤醒。A 没有近期联系，保持离线，不宣称已验证 A 的开机联网行为。见[实际联系观察](evidence/2026-11-13-powered-device-ota/presence.json)。读取期间没有使用设备凭据模拟心跳，也没有提交预览/升级；持久 OTA 操作数前后均为 0。

本轮没有刷写、复位或下发 A/B。配套固件首次安装、实际下载/写入/重启、目标 ELF/VALID、身份保持及后续无唤醒 OTA 仍需独立实机窗口，#99 与 #112 保持待验。离线排队、定时和暂停等 #100 扩展仍在 backlog。
