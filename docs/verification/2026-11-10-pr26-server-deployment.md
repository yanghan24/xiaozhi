# PR #26 Ubuntu 服务端部署记录

日期：2026-11-10（Asia/Hong_Kong）。服务器：jt@192.168.168.114。

已部署 PR #26 固定提交 `885918d24d69f0b2ed87114f1937292727111d4e`，未合并 PR。Java 与 Python 从该提交源码构建，复用已安装的基础镜像和依赖。部署目录：`/home/jt/pr26-deployment-885918d`，Compose：该目录下的 `private/compose.json`。

## 公网入口

| 用途 | 地址 |
| --- | --- |
| OTA 产品发现 | https://bridge.hncig.cn:18443/xiaozhi/ota/ |
| 设备状态 | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/state |
| 发起绑定/恢复请求 | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/requests |
| 停播确认 | https://bridge.hncig.cn:18443/xiaozhi/device-control/v1/state-acks |
| 语音连接 | wss://bridge.hncig.cn:18443/xiaozhi/v1/ |
| 手机绑定页 | 设备正式发码返回的 https://bridge.hncig.cn:18443/xiaozhi/bind/{handle} |

绑定页必须使用设备生成的有效二维码；根路径返回 404 是预期行为。控制接口需要每台设备的独立接入凭据，语音接口需要当前许可签发的产品令牌。公网没有开放管理页面、数据库、Redis 或内部 model-access/key-rejected 接口。

## 实际部署

- 产品 API：`xiaozhi-pr26-api`，镜像 `xiaozhi-product-api:885918d`。
- 产品语音：`xiaozhi-pr26-voice`，镜像 `xiaozhi-product-voice:885918d`；单实例。
- 身份数据继续使用现有 `issue29-db` / `issue29-redis`，保留 OIDC 配置、账号授权和原加密密钥；新增十项 Liquibase 迁移正常执行，未清空校验和。
- 旧 `issue11-identity` 和旧语音容器已停止，作为保留的历史部署。旧管理 API / MySQL / Redis 继续供现有管理服务和语音启动配置使用。
- 公网 Nginx 继续使用原有效证书和 18443→443 映射，精确路由指向新版产品服务。代理可信地址限制为当前入口在身份网络上的 `172.26.0.5`；重建入口或更换网络后须重新核对该地址与 DNS 解析并重载 Nginx。
- 产品服务及所需数据库、Redis、配置 API、入口设置 `unless-stopped`。验证的是容器服务重启，未重启 Ubuntu 主机。
- 语音失效通知 SQLite 位于 `private/voice-data/product-voice`，是持久私有目录。不能删除它或故障标记来解除禁用。
- JetLinks 所有容器仍停止，未修改其数据。

## 本次验证

| 验证 | 结果 |
| --- | --- |
| Ubuntu Java 完整回归、实际 MySQL/Redis/HTTP 与集成测试守卫 | 321 tests，0 failures/errors/skipped |
| Python 正式 WebSocket/ConnectionHandler、ASR、模型/ASR Key 失效回归 | 通过；外部模型及 Edge 传输在这些回归中受控替代 |
| 真实公网 HTTPS、证书校验、无凭据控制请求 | 正常；控制请求 401 |
| 独立临时设备：状态、正式发码、当前停播 ACK | 通过；未绑定设备保持 blocked |
| 手机页 UTF-8、CSRF、安全 Cookie、真实 OIDC 授权跳转与 PKCE | 通过 |
| OTA 产品发现 | 返回 device_control，不返回旧 websocket 令牌或数字激活 |
| 公网 WSS 无效令牌 | 1008 拒绝 |
| 真实 Edge TTS 出站合成 | 成功，9648 字节音频；没有写入录音文件 |
| 新 API/voice 服务重启、同一临时设备请求及许可版本保留 | 通过 |
| 临时设备与对应流程清理 | 通过；未修改真实设备或真实授权 |
| 管理与内部接口公网隔离 | 404 |

Python 各套件需使用新建夹具：首次串行共用夹具时，上一套件的退出行为使下一套件被正确拒绝；分别重建夹具后 ASR 和 Key 失效套件通过。没有修改 PR 源码来绕过检查。

公网检查从 Ubuntu 使用域名和公开端口完成，不等于手机蜂窝网络或真实音箱验收。真实账号的最终手机确认、产品设备完整归属映射、真实麦克风→ASR→模型→扬声器、物理停播 ACK 尚未执行。先前模拟设备的真实模型链路证据不能替代这些项目。本次没有烧录设备、预登记真实设备或发布固件下载。

## 备份与运维

升级前的必要身份数据、表结构、配置及原密钥保存在部署目录的 `private/`。升级后备份保存在 `private/backups/`，包含必要 customer/dc/sys/模型配置数据与产品失效通知 SQLite；明确排除对话正文、音频和录音，SQLite 副本完整性检查通过。未执行数据库恢复演练；当前备份在同一服务器，不是异地灾备。

以授权 sudo 在部署目录运行 `python3 backup.py` 可再次生成范围相同的新备份；未新增定时备份任务。证据位于 `evidence/`，私有运行日志和凭据位于 `private/`，不得上传 Git。

本部署用于单实例、少量设备联调，未作并发容量验收。设备归属、操作历史和 ACK 不自动清理。过期语音令牌只有在无会话引用时才能删除；令牌过期不代表已建立语音会话失效，不能仅按令牌期限删除活动会话。维护清理遗留会话时先停止产品语音，确认无活动连接，再清理会话及未被引用的过期令牌，重启后设备凭当前许可重新连接；保留归属与许可状态。扩大试用范围前应落实定期清理和容量验证。

## 暂停与恢复原则

正常暂停：在部署目录执行 `docker compose -f private/compose.json stop product-voice`；保持产品 API 闸门和数据库，不退回旧语音鉴权。

恢复当前版本：确认原私有配置、密钥、数据库和持久失效通知目录完整，再执行 `docker compose -f private/compose.json up -d`，验证服务后重载公网 Nginx。已退出设备仍需原账号明确恢复；重启不得批量恢复许可。

回退到旧版本前必须先撤下公网语音/ASR路由。保留所有新增表、归属、密钥及失效通知；旧 Nginx 配置和旧容器只作为恢复参考，不能直接恢复旧公网语音路由。应用回退不撤销已经发生的绑定或退出，也不自动执行数据库回滚。