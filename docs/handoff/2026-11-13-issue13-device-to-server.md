# #13：B 设备侧验收与服务器接续

2026-11-13。用户要求设备侧完成所需验证，剩余提交服务器侧验证。本轮范围为 [#13](https://github.com/yanghan24/xiaozhi/issues/13) 的单台扫码登录和使用许可；设备侧以 B 交付。设备操作证据已核对，关闭手机页面后的缺项已由用户现场确认补齐。服务器接续已完成，#13 已按五项限定范围关闭。见[服务器签收及版本交付](../verification/2026-11-13-issue13-server-closeout.md)；下方接续步骤保留为原交接记录。

## 完成状态

服务器实查 B 为 ready／许可24／归属7，当前凭据200、前驱旧凭据401；当前 API 运行产物与既有362项回归一致。用户另确认 B 已刷入固件12f2ff1；相对a30f6a8的生产改动限于聆听模式及唤醒打断，身份、扫码、许可和退出实现未变。固件信息来源为用户确认，未新增 Flash 读回。#14/#18 使用交付版本及许可时须重读实时状态。

## 本次现场确认

用户先确认“现在b设备可用”，随后针对“关闭手机上的登录/绑定页面后，B 仍能完成一次普通语音问答并完整播报”明确回答：“B可用确认过，关闭手机上的登录也可以”。原话与确认范围保存在[现场确认记录](../verification/evidence/2026-11-13-issue13-device-confirmation.json)。

这两项是本次新增的现场结果。其他设备步骤复用下表明确的历史实机记录；本轮没有重新刷写、恢复出厂、补发凭据或重跑整套手机流程。当前执行环境未发现直连设备串口，因此本次现场结果来源为用户操作确认，记录不冒充代理串口采集或自动化输出。

## 设备侧交付结果

| 验证项 | 结果与来源 | 版本及证据边界 |
| --- | --- | --- |
| 正式扫码绑定或原账号恢复 | 通过，复用 [B 双机交付记录][D1]、[OTA 后账号及重新绑定记录][D2]和[原账号恢复现场结果][D4] | 2026-11-11 B 为 00896ab；2026-11-12 对应 a30f6a8。不是今天重新执行全部登录流程 |
| 普通语音完整播放 | 通过，复用 [D2]、[D4]；本次用户补充 B 当前可用 | 证明普通回答和设备播放；自动 MCP 知识库回答仍由 #14 验收 |
| 关闭手机登录/绑定页面后继续问答 | 通过，2026-11-13 用户针对该步骤明确确认 | 这是本次补齐的现场缺项；没有新增当前固件或服务器版本测量 |
| 单台停用后实际停播、原账号明确恢复 | 通过，复用 [B 停播及另一台独立使用][D1]、[对应 ACK][D6]与 [B 原账号恢复][D4] | 历史物理操作、服务器快照及 ACK 分别引用；今天未重新执行退出或解绑 |
| 停用状态跨重启保持 | 复用历史证据：[普通重启记录][D5]及 [B 维护重启记录][D3] | D5 是当时已绑定音箱的普通重启；D3 是 B 停用后维护/重启/重新配网。两种场景分别保留，不改写为今天 B 普通重启实测，也不新增恢复出厂实验 |

设备侧所需结果现已具备：已有步骤有实物证据可复用，当前可用性及关闭手机页面后的使用已获现场确认。若服务器核对发现相关固件/服务代码变更，只补受影响的行为；保持本轮单台范围。

## 原服务器接续清单（已完成）

服务器侧执行入口为 Ubuntu `jt@192.168.168.114`。先读[当前部署入口](../deployment/current.md)及下列固定版本证据；沿用当前私有配置和恢复记录。

1. **核对当前版本与 B 的实际许可。** 记录 API/语音镜像及源码标识、B 的固件标识、脱敏账号关联、准入、归属与许可版本。以 [PR #87 的恢复接续记录][D8]中当前 delivered 身份作为后续起点，核对其后的实际状态。将本次用户确认对应到当前 B；正常检查无需重新补发或还原旧凭据。
2. **对应已有的凭据拒绝、权限及页面证据。** [API 1b50709 的 362 项回归及 69 项公网检查][D7]包含凭据拒绝、管理/内部路由隔离和客户越权拒绝。核对当前相关代码与受测版本后引用；只为实际变更或证据缺口补验。手机页面敏感信息检查按 #11/PR #76 的已归档证据引用，普通应用日志专项按下一项接续。
3. **收回 PR #76 已启动的专项测试结果。** 先只读核对任务状态和既有结果；若结果缺失或未执行完成，再在隔离测试环境补目标用例。任务位置及结果判定见下一节。
4. **按本轮五项验收签收 #13。** 将身份/归属、扫码与关闭页面、本地许可的停用/恢复、必要拒绝与秘密保护、版本交付逐项对应到证据，分别标注历史复用、本次服务器验证及用户现场确认。条件齐备后在 #13 发布收尾记录并关闭；缺项只记录具体失败或缺口。

### 已启动测试的位置与判定

来源：[PR #76 固定版本验收说明][D9]。

| 项目 | 位置或名称 |
| --- | --- |
| 任务目录 | `/home/jt/issue13-acceptance-b448aa0` |
| 启动脚本 | `/home/jt/issue13-acceptance-run.sh` |
| Compose 项目 | `xiaozhi-issue13-acceptance-b448aa0` |
| 退出码 | 任务目录下 `.runtime/pr52/exit-code.txt` |
| JUnit 报告 | 任务目录内查找 `TEST-*CustomerSecretExposureHttpTest*.xml`；原交接没有写定其精确子目录 |
| 目标用例 | `CustomerSecretExposureHttpTest.bindingPagesDeviceResponsesAndLogsDoNotExposeSecrets` |

结果摘要应包含受测源码/构建标识、进程退出码、JUnit 路径及校验值、tests/failures/errors/skipped、目标用例是否实际执行及其结果。目标用例必须存在且未跳过，相关结果无 failure/error；退出码为 0 不能单独证明这个用例执行过。若不同记录矛盾，先核对是否来自同一轮任务。

先核对旧任务状态，避免重复启动仍运行的测试。只共享结果摘要，原始应用日志、system-out/system-err 和真实凭据留在受控环境。旧目录 `/home/jt/pr67-regression-1b50709/.runtime/pr52/surefire-reports` 是已有 362 项回归来源，不能冒充这个新增专项的结果。

原设备侧会话曾因 SSH 超时未取得结果。2026-11-13 服务器接续已取回原任务：退出码0，目标用例实际执行，1项通过、0失败/错误/跳过；隔离项目无运行容器。精确路径、XML校验值和受测源码对应关系见服务器签收报告，未重复启动测试。

## 版本与关闭边界

- D3/D4 的停用/恢复记录使用 API `1b50709`、B 固件 `a30f6a8`，语音记录为 `pr81-75290c4-pr79-f830a8c`；D8 后来记载的语音为 `pr79-f830a8c`。这些证据对应不同语音镜像，本次已核对当前实际版本为 D8 的语音镜像；历史证据仍按各自版本引用，不合写成同一版本全部重测。
- D8 记录 B 后续身份已恢复，但当时持续连接/问答失败。2026-11-13 用户的新现场确认说明 B 目前可用；它不改变 D8 当时的失败结论，也不证明已经找到并修复根因。
- #4/#11 已关闭，#42 已移出 #13 前置。自动 MCP 问答属于 #14，单台 OTA 及升级后整链复测属于 #18/#19；本交接不将其标成已验收，也不让它们反过来阻塞 #13 登录切片收尾。
- 旧 A01–A24 全矩阵、多账号全量生命周期、批量 OTA 和容量测试按 #88 backlog 排期。此处只交付当前单台范围；服务器签收后 #13 已关闭，其余事项保持各自范围。

[D1]: https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-11-two-device-delivery.md
[D2]: https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-12-p4-ota-server.md
[D3]: https://github.com/yanghan24/xiaozhi/blob/89d9f1623618985ccc4a72d2d92346553f37eaec/docs/verification/2026-11-12-issue42-acceptance.md
[D4]: https://github.com/yanghan24/xiaozhi/blob/89d9f1623618985ccc4a72d2d92346553f37eaec/docs/verification/evidence/2026-11-12-issue42-acceptance/hardware-original-account-resume.json
[D5]: https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-11-pr26-main-sync.md
[D6]: https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-11-pr52-server-acceptance.md
[D7]: https://github.com/yanghan24/xiaozhi/blob/6bf7620ea5ac6fc9dd7b852cb9cd26387bc96f80/docs/verification/2026-11-11-pr67-deployment.md
[D8]: https://github.com/yanghan24/xiaozhi/blob/6a20c8c8de190ac9b8ce7eda212ed277fdf2245a/docs/verification/2026-11-12-pr84-pr85-continuation.md
[D9]: https://github.com/yanghan24/xiaozhi/blob/f61d4234c5042a2e6dd1ab5ea0af8206c1b8c67c/docs/verification/2026-11-11-issue13-server-acceptance.md
