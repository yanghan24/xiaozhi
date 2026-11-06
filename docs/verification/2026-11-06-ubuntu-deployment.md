# Ubuntu 部署与服务端验收

日期：2026-11-06，Asia/Hong_Kong。用户通过 `implement` 授权执行“部署服务”和“Ubuntu 复验”：启动管理端、语音及独立数据库，检查登录、配置保存、设备绑定和语音协议，并验证同一设备的绑定与配置跨服务重启保留。本轮上述范围通过；设备网络接入及真机对话属于后续步骤。

## 部署结果

服务器为 `jt@192.168.168.114:22`，Ubuntu 20.04.3、Docker 26.1.3、发行版提供的 Compose 2.27.1。工作目录 `/home/jt/xiaozhi-ubuntu-test`，项目名 `xiaozhi-ubuntu-test`。新建该项目专属 MySQL、Redis、上传数据卷及私有 `.env`，初始化密码仅生成一次。最终保留 mysql、redis、api、web、voice 五个容器运行，详见 [最终状态](evidence/2026-11-06-ubuntu/final-summary.json)。

服务源码固定为 `6e354edf292c3df14fd783c5dc7314a4402e3b74`。部署开始时重新取得的 `origin/main` 仍为 `46e7f33c9d0d94603c2a58c8e4dc197c1d9e0464`，尚不包含语音基线；本轮使用已验证资料分支产物进行独立测试。没有合并 main 或发布正式版本。

运行包已通过 SFTP 传到 `/home/jt/xiaozhi-package-20261106`，Linux `sha256sum -c` 校验八个产物通过，六个镜像导入后逐项核对镜像身份、平台、文件系统层和 revision。归档来源及镜像标识见[既有镜像清单](evidence/2026-11-06-windows/image-identities.json)。运行镜像归档 SHA-256 为 `ab707ee4183f5c001d84c89e9ecaf1bd734aaf090c5df76f462fa0433aa5ba5f`，源码归档为 `4aa75cd3376c606bfeb3dcaeed418448e3f3da539e95b853013c017bcdc96855`。

这些镜像由先前 Windows Docker 环境构建，本轮在 Ubuntu 加载和执行。语音镜像标签 `local-20261106-netmirror`、revision `working-tree`；仅 Debian 软件源改为阿里云 HTTPS，配方与依赖已随包保留。没有将这次镜像导入记为 Ubuntu 源码构建。

管理页面监听 `127.0.0.1:18002`，语音监听 `127.0.0.1:18000/18003`。MySQL、Redis、API 未发布宿主机端口；设备入口 18080 尚未启动。当前 OTA/WS 配置为内部 `http://api:8002/xiaozhi/ota/` 和 `ws://voice:8000/xiaozhi/v1/`，供服务间协议检查使用，不能直接填入物理设备。JetLinks 六个容器保持停止，其数据保留。

## 实际验证

| 检查 | 本轮结果与证据 |
| --- | --- |
| 部署预检 | 目标目录、项目和数据卷不存在；计划端口空闲，JetLinks 停机；Docker、配置及资源预检通过 |
| 管理 HTTP | 页面包含 Vue 应用，公开配置已初始化，匿名管理请求被拒绝；重启后再次通过 |
| 管理页面 | 本机 Edge 经 SSH 隧道操作 Ubuntu：首个管理员注册、有效验证码下错误密码拒绝、正确登录、参数保存、整页刷新回读共 5 项通过；[浏览器结果](evidence/2026-11-06-ubuntu/manager-browser.json) |
| 设备协议第 1 轮 | 真实 API/MySQL/Redis/voice 的 19 项检查通过，独立设备身份及清理通过；[结果](evidence/2026-11-06-ubuntu/protocol-round1.json) |
| 设备协议第 2 轮 | 另一独立身份的 19 项检查通过；[结果](evidence/2026-11-06-ubuntu/protocol-round2.json) |
| 提供者错误配置与恢复 | 错误提供者在 hello 后被同会话 ready 判为失败，恢复配置后完整协议通过；[结果](evidence/2026-11-06-ubuntu/provider-result.json) |
| 启动错误配置与恢复 | 缺配置、缺密钥、错误密钥按预期失败，正确配置恢复真实 HTTP 响应，4 项通过；[结果](evidence/2026-11-06-ubuntu/startup-result.json) |
| Java 回归 | 206 tests，0 failures/errors/skips；设备 2 项、模型持久化 3 项、登录 3 项强制集成覆盖检查均通过 |
| Python 回归 | 129 passed / 5 skipped，134.33 秒，命令退出 0；两套回归的[汇总](evidence/2026-11-06-ubuntu/regression-summary.json)和[测试镜像身份](evidence/2026-11-06-ubuntu/regression-images.json) |
| 同设备跨服务重启 | prepare 12 项、verify 11 项通过，绑定和模型配置快照完全一致；见下节 |
| 原管理参数跨重启 | `baseline.acceptance.1793956177540` 仍读回 `manager-write-read-verified`，原管理员会话有效；[结果](evidence/2026-11-06-ubuntu/manager-after-restart.json) |

回归测试镜像另行上传到 `/home/jt/xiaozhi-regression-images-20261106`，校验后导入。Java 使用独立 `xiaozhi-baseline-tests` 项目及测试卷，成功后仅清理该项目；Python 使用一次性 `voice-tests` 容器。两套测试均在 Ubuntu 执行，未复用先前 Windows 的测试结果。前端源码未改动，本轮执行实际页面验收；前端单元/契约/快照结果仍属于先前本机记录。

## 同一设备的持久化证据

新增 [check-device-persistence.py](../../scripts/check-device-persistence.py) 通过管理 HTTP、OTA 和 WebSocket 接口检查，复用固定镜像内的协议函数。prepare 阶段保留唯一标记的智能体和设备；verify 阶段不创建、重新绑定或修改对象，只使用同一账号读回并连接。私有 state 保存于服务器挂载目录中，报告只保留配置摘要和脱敏证据。

本轮保留的测试 MAC 为 `02:8c:15:73:70:38`，运行标识 `8c15737038c14bf1b9f5f762d023252c`。[准备结果](evidence/2026-11-06-ubuntu/persistence-prepare.json)记录绑定与模型选择；随后停止五个服务，保存[停止状态](evidence/2026-11-06-ubuntu/restart-stopped.json)，按数据库/管理端先就绪、语音随后启动的顺序恢复。镜像、容器 ID 和挂载路径一致，五个容器的 StartedAt 均改变，见[外部重启证据](evidence/2026-11-06-ubuntu/restart-result.json)。

[验证结果](evidence/2026-11-06-ubuntu/persistence-verify.json)显示原账号、原设备记录、原智能体和模型配置的快照 SHA-256 前后均为 `4388d2ea030f6fb81be9717c2a7f140e47fd1c3dd34551b330c0a2ae07cb7112`，变化项为空。原设备仍处于已激活状态，OTA 下发相同内部 WS 地址；认证、hello、同会话 ready、错误身份拒绝及 Ping/Pong 再次通过。

脚本另有两项实际负向检查：无 prepared state 时 verify 返回 1，拒绝创建对象；已有 state 时再次 prepare 返回 1，state 文件哈希保持不变。对应 [缺少状态](evidence/2026-11-06-ubuntu/negative-missing-state.json)和[拒绝覆盖](evidence/2026-11-06-ubuntu/negative-existing-state.json)报告的 `failed` 是这两个负向用例的预期结果。最后 [cleanup 3 项通过](evidence/2026-11-06-ubuntu/persistence-cleanup.json)，只删除本轮唯一标记、归属匹配的智能体及设备，再验证设备已解除绑定。

验收脚本 SHA-256 为 `b2995c55f88d5c89616f9c658b0d6102b848fe266312814c562380f77291457e`，与服务器执行文件和本分支文件一致。服务器 Python 3.8 与 voice Python 3.10 的语法编译均通过；实际执行使用 voice 容器依赖。失败报告、state 和原始运行日志保留在私有 `.runtime`，不提交账号、token、服务密钥或数据库密码。证据导出前已对本轮实际凭据作字符串匹配检查。

## 部署时修正的操作兼容性

首次校验因 Windows CRLF 清单将回车视为文件名一部分而失败；在创建服务前将 `SHA256SUMS` 转为 LF，重新验证全部产物成功，产物内容和哈希未变。

服务器 Compose 的 `run --pull` 和 `start --wait` 不受支持。Python 测试在参数解析阶段退出，修正为已校验镜像的 `run --rm --no-deps -T` 后完整通过。服务已停止后，使用 `up --no-recreate --no-build --pull never --wait` 恢复，并通过容器 ID 比对证明没有重建。已将有效命令写入部署 runbook。

SSH 批处理改为 `bash -c` 执行脚本文本，使 Docker exec 不会从共享 stdin 吞掉后续命令；以各阶段实际 JSON 和退出码核实完成情况。

## 使用与验收边界

管理端通过 SSH 隧道访问：

```bash
ssh -N -L 127.0.0.1:28002:127.0.0.1:18002 jt@192.168.168.114
```

隧道运行时打开 `http://127.0.0.1:28002`。服务器专属管理员凭据保存在 `/home/jt/xiaozhi-ubuntu-test/.runtime/manager-check/credentials.json`；本机浏览器客户端副本在 `D:\AI\xiaozhi\.runtime\ubuntu-client\.runtime\manager-check\credentials.json`。复用这套账号和服务器 `.env`，后续操作见 [Ubuntu runbook](../deployment/ubuntu-isolated-test.md)。

本轮完成的是内部服务部署、模拟设备协议及服务 stop/up 持久化验收。未启用设备 LAN 入口，未测试真实 ASR/LLM 调用、麦克风采集、设备播放、固件升级或回滚。尚未进行主机重启测试，当前服务 restart policy 为 `no`，不能视作已配置开机自动恢复或生产可用性。固件测试版本与设备网络信息留到后续真机阶段记录；本轮不关闭这些后续任务。
