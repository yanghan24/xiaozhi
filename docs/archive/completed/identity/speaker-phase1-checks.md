# 第一阶段开发检查与待验收项

> **归档｜交付或验证记录**。正文对应当时的版本、范围和结果。当前入口见[项目文档](../../../README.md)。

实施方案见 [speaker-phase1-implementation.md](speaker-phase1-implementation.md)。本次仅修改独立开发环境，服务端分支 feat/speaker-phase1；固件分支 feat/speaker-phase1-firmware。所有提交均为本地提交，未推送、合并或部署 114。

## 当前开发入口

后台：http://172.16.111.196:18001/#/login（本机也可使用 127.0.0.1:18001）。管理员登录后可进入智能体管理、用户管理和设备管理；系统设置保留原参数/字典等组件。前端沿用原布局、HeaderBar、CustomTable、CustomButton、Element UI 和固件/OTA 组件，没有引入新框架或设计系统。

首次必须新增并明确选定默认角色。创建角色不会自动成为默认；客户无法进入后台创建角色。模型引用、提示词和音色属于角色，用户集市业务 Key 不进入浏览器或固件。第一阶段保持现有 RTASR 接入形式和 Edge 播报；模型目录/启用默认语义重构属于第二阶段。

`local-zhou-protocol` 是明确标记为“非实机”的开发协议设备，身份仅在容器 runtime 和宿主 runtime/xiaozhi/config/speaker 中保存。它可用于开发扫码，不代表已经完成固件交付或音箱在线验收。心跳由模拟器限时发送；停止后按超时显示离线，不自动解绑。

## 已执行的定向验证

| 范围 | 结果和限制 |
| --- | --- |
| 设备身份/心跳 | SpeakerDevicesTest 4/4；只更新认证设备，不允许心跳传入用户归属 |
| 集市协议/密文 | SpeakerMarketTest / SpeakerCryptoTest 9/9；本机 HTTP/RSA 夹具覆盖设备授权、错误间隔、ID Token 和主体一致性、续期以及密文完整性 |
| 绑定与客户关系 | SpeakerLoginDatabaseTest 8/8；独立 104 开发库唯一夹具，验证双机隔离、禁止跨账号覆盖、幂等解绑和 Key/续期分离 |
| 管理员边界 | 管理入口/注册检查 4/4；运行 HTTP 又验证匿名和普通账号不能进入新管理接口 |
| 默认角色与语音 | Java 4/4、Python 2/2；当前轮固定快照，下一轮清上下文，用户归属不变；不调用真实模型 |
| 管理页面 | speakerConsole.test.mjs 5/5；新页面构建通过。联调后补充详情异步响应隔离，避免旧详情覆盖新选择；最终构建 hash 49239f439c073fac。尚未做浏览器视觉验收 |
| 固件协议 | speaker_test、speaker_worker_test 编译运行通过；真实 worker 使用 RTOS/HTTP 夹具；新增中文字符未超出原绑定字体集合，无麦克风/扬声器或 LVGL 验收 |
| P4 目标构建 | ESP-IDF 5.5.5、P4 revision 3.1+、微雪 4.3 寸、speaker v2、原 16 MB 分区：编译、链接、镜像及分区容量检查通过。首次因旧 SD 模块所需 FatFS 配置未选中失败，仅补构建 profile 后重建；没有改 SD 模块或分区 |
| 开发 HTTP/语音链路 | check-speaker.py 通过：管理员门禁、旧数字激活关闭、独立心跳、缺省角色拒绝、设备票据→语音 WebSocket hello、角色版本切换、解绑立即关闭旧语音、API 重启后状态保持、解绑后仍有心跳。没有发送音频或外联模型；合成夹具已删除，初始默认角色恢复为空 |

首次 HTTP 联调发现旧全局 Long 字符串序列化和异常处理覆盖设备契约，已在 speaker 模块内通过单独响应/异常处理解决，未改变旧模块的 JSON 格式。固件启动登录/心跳与可选 OTA 检查分开；OTA 在新模式只返回协议发现和时间，不进入旧数字激活流程。原固件目录/OTA 推送本轮仅复用入口，未执行升级或新增 OTA 能力。

收尾复查补充了“保存令牌前”的归属和代际事务检查；资料请求期间发生解绑的结果不会保存用户授权，非归属账号不会领取 Key。仅重跑对应 8 个数据库用例，通过；开发 API 已加载最后的保护修正。

日志和编译产物均在 xiaozhi-dev 的 /opt/xiaozhi-dev 下；MySQL/Redis 仅使用 104 的小智独立实例。未运行全项目回归或其他产品测试。

## 真实授权与剩余条件

用户提供的开发专用 device Client ID 已配置；真实发现和设备授权请求成功，集市返回有效的手机授权地址和核对码。首次事务经过开发 API 重启仍维持轮询，最终因未收到手机确认自然过期（LOGIN_EXPIRED）；未自动创建新事务。尚未取得真实身份、组织、积分或业务 Key，需要在用户方便时重新发码并完成扫码。设备授权不需要网站回调或 Client Secret。

P4 构建已通过，固件分支提交 deb3915（基础协议 3264476，启动衔接 64fe15b）。SDK、P4 工具和独立 Python 环境位于容器 /opt/xiaozhi-dev/tools；[官方完整归档](https://github.com/espressif/esp-idf/releases/tag/v5.5.5)的 SHA-256 已校验，未在宿主安装开发依赖。构建入口 deploy/local-zhou/build-speaker-firmware.sh；产物 /opt/xiaozhi-dev/run/speaker-p4/xiaozhi.bin，3,925,760 字节，SHA-256 为 2e7110c2094fccbeef9efd04cfa1aecc95bf7d7a9d46d225160cd4a1d3b40126。应用分区余量约 5%，本轮未扩容。详细日志 speaker-s6-p4-build-final.log。

目标构建产物不等于真机验收；保留基础版本号 2.2.6，仅作为开发构建，不作为已发布版本。114 部署、设备凭据交付/刷写、真实麦克风和扬声器、真机断网重连及 OTA 验收另行安排。浏览器视觉检查、管理员明确设置初始默认角色和真实扫码仍待完成；当前客户/角色/授权表没有合成夹具，只有一台明确标记的开发协议设备。

GitHub 需求登记仍需恢复当前会话的 GitHub 认证，实施方案已在仓库记录；未声称已创建 Issue。未改动 AI 集市源码、配置、数据库或容器，真实设备授权只调用其公开应用接入协议。
