# 模型兼容性、智能体分组与服务管理交付

2026-11-17，开发分支 `fix/model-compatibility-and-service-management`。依据[实施方案](model-and-service-consistency-plan-2026-11-17.md)。

## 完成范围

| 项目 | 结果 |
| --- | --- |
| 主大模型兼容性 | 统一要求 openai、chat、streaming、tools，声明与已验证能力同时满足；不再列出“仅普通聊天”候选，不使用固定模型名单 |
| 列表/保存/角色/运行时 | 共用兼容判断；旧不兼容适配从角色候选中排除，角色保存检查用途与字段类型；错误提供明确说明，原配置数据不自动删除或换模型 |
| 其他类型 | 视觉保留已验证图片输入方式、独立意图保留 JSON 调用限制、记忆保留摘要用途；SLM 沿用复用 LLM 配置；ASR 仍仅 spark_zh_iat_new / xfyun_asr_v2 |
| 能力展示 | 标签只展示声明与验证交集 |
| 智能体模型选择 | Element 原生 AI 集市/小智系统分组，集市优先；每个类型都保留两组，空组为不可选“无”；失效的当前值显示为不可选的已保存配置，不擅自更改 |
| 服务地址 | 设备状态下发与管理列表统一读 server.websocket；移除 speaker.voice-url 运行时来源；可使用私有显式运维地址映射，页面分别展示 |
| 服务管理页面 | 正确处理 null、空列表、读取失败和刷新；明确未检测运行状态，配置地址不冒充在线 |
| 管理操作 | 独立 /xiaozhi/management/ 路径，以既有服务间秘密认证；不伪造设备、不创建音频或用户会话；回执动作和成功状态必须匹配，失败不报成功，网络失败不自动重试 |
| 重启确认 | 页面只声明服务端接受重启请求，不冒充重启后健康检查；进程重载保留 Supervisor 所管理的 PID/描述符，测试通过替身执行 |

身份状态接口继续下发短期连接凭证及对应地址；原版语音处理、音频传输、模型调用、MCP执行、音色联动及 OTA 检查/安装流程保持。没有批量导入模型或增加自动同步/自动切换模型功能。

## 开发环境与迁移

- 已将开发库原有未配置的 server.websocket 迁为原私有 speaker.voice-url 中的本地模拟地址，移除旧属性并清理相关缓存。私有迁移前值保存在容器 `/opt/xiaozhi-dev/run/model-service-consistency/address-migration-private.json`，不入库。
- API 与 voice 仅在现有开发容器内加载新代码；服务管理真实接口只读返回统一地址，voice HTTP 为 200。没有通过管理接口向运行中的服务发送更新配置或重启指令。
- 默认角色、管理员已选模型、用户和设备绑定未修改。
- 使用说明、显式内外网映射及部署配套条件见[开发指南](../../deploy/local-zhou/devcontainer.md)。部署环境需要配套 Java/Python 版本与运维路径；本次没有部署到 114，也没有改 AI 集市或固件。

## 验证与证据

| 检查 | 结果 |
| --- | --- |
| Java 兼容/保存/隔离/原接口约束/地址解析/操作回执 | 18 项通过，Maven package 成功 |
| 前端能力标签与下拉分组 | 7 项通过，前端 build 成功 |
| Python 管理认证/动作/回执/原身份与角色边界 | 10 项通过，全部使用本地协议或调用替身 |
| 浏览器智能体模型分组 | 10 个选择框检查通过；无写入请求 |
| 浏览器服务管理 | 未配置、取消、失败回执、接受重启与网络失败不重试检查通过；3 次写入均被浏览器夹具拦截 |
| 真实开发服务列表 | 只读 API 及页面检查通过，1440/1024 宽度可显示统一配置；没有运维动作 |
| 真实开发管理鉴权边界 | 无效凭据被 1008 拒绝，未发送管理动作 |
| diff 空白检查 | 通过 |

Java 定向命令：`mvn -q -f main/manager-api/pom.xml -Dtest=VoiceServiceEndpointsTest,ServerSideManageControllerTest,MarketModelCompatibilityTest,MarketModelBindingsTest,MarketModelRuntimeTest,MarketLegacyEndpointTest package`。
前端命令：`npx vitest run tests/marketModels.test.mjs tests/modelOptionGroups.test.mjs`、`npm run build`。
Python 使用固定本地 YAML 配置预热配置缓存并在 pytest 启动事件循环之前导入原 ConnectionHandler，再执行 `--noconftest -q tests/test_management.py tests/test_device_session.py`，未请求真实模型。首次直接以 --noconftest 合跑旧身份测试遇到异步内首次导入的配置加载冲突，修正调用方式后 10 项通过，没有修改该旧测试。

截图与脱敏报告位于 `/home/timyai/dev/runtime/xiaozhi/reports/model-service-consistency-2026-11-17/`。日志位于容器 `/opt/xiaozhi-dev/logs/model-service-package.log`、`model-consistency-web.log`、`service-management-build.log`、`service-boundary-regression.log`。临时管理员会话文件已清理，不入库。

未验证真实模型准确率/用量、真实设备语音和升级、真实运维重启后的恢复；不将模拟回执当作这些验收结果。不连接 114、不修改 AI 集市、不安装依赖或重建容器。

## 本地提交

- `e4b8175`：实施方案。
- `db1aca1`：统一已验证模型兼容规则。
- `8ea2592`：智能体模型来源分组。
- `7634a79`：角色保存的兼容错误说明。
- `a46c241`：统一语音服务地址来源。
- `f925763`：独立管理通道与真实回执。
- `330d110`：角色模型字段用途校验。

未推送、未合并主线。回退服务管理需配套回退 Java/Python；旧代码若依赖 speaker.voice-url，按迁移前记录恢复该私有属性。Git 不会撤销已迁移的系统参数，不能通过回退代码恢复数据库配置。
