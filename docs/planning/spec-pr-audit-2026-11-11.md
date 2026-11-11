# 规格与 PR 审计（2026-11-11）

## 范围与依据

按用户授权核对全部 30 个 GitHub Issue（含父规格与实施任务）及 44 个既有 PR（含已合并和关闭）。主线基点 `63c41c9b3e296b1deda53e2de343a359f183c054`。使用 ask-matt 选择规范路径，参考 to-spec 的规格结构、pr 的三段模板，并按 code-review 将 Standards 与 Spec 分开复核；仓库导航按 writing-for-agents 的单一事实来源与历史边界原则整理。

25 个 Issue 正文得到修正或换行整理，44 个既有 PR 描述统一结构或补充归档边界。未更改 Issue 的开闭状态、标签或已有验收完成勾选，也未合并既有 PR、运行服务、重启或刷机。本次新增的审计文档 PR 不计入上述 44 个。

历史 PR 检查范围为描述、已有证据、链接及当前/历史状态；没有对全部历史源码重新逐行审查或重新执行历史测试。4 个未合并 PR 则固定 base/head 做了双轴 diff 审查。

## 已修正的问题

| 优先级 | 问题 | 修正与依据 |
| --- | --- | --- |
| P1 | #10/#13/#15 仍一律要求原账号授权转交，与已合并 ADR-0008 冲突 | 区分手机页面/旧接口停用与本机明确确认解绑，保留原账号手机授权路径；同步日程旧归属隔离及最终验收 |
| P2 | #10/#40/#44/#19 环境容量仍写 350 必过、1,000 探索 | 按 #43 / PR #53 后续确认统一为 50 路环境请求、冷热各至少 30 分钟；350 台装机规划和历史负载仍保留，完整语音单独验收 |
| P2 | #6/#11/#41 已关闭，但规格和导航仍称未完成，二维码/凭据交付仍称待开发 | 更新为已有切片与证据、剩余集成关卡；已关闭前置保留依赖边但不再视为阻塞 |
| P2 | #70 的配网规格未进入总规格与最终验收 | 将 #70 关联为 #10 原生子规格，#19 增加 #73 原生前置；#71→#72→#73 原有边保留，#73 含整体操作复核 |
| P2 | 已合并 PR #38/#67 仍自称草稿/尚未合并；部分 PR 无规定章节或链接不可靠 | 统一 Summary/Evidence/Merge Danger，明确历史验证边界；相对文档链接及可变分支证据改为可解析的固定提交链接 |
| P2 | PR #66 将已执行凭据轮换标为 two-way | 明确业务状态为 one-way，仓库补丁仍可回退；禁止以旧 NVS 撤销凭据代际 |
| P2 | #63/#66/#68 描述未引用后续写回/核验，且旧部署文档与新主线重叠 | 关联 #66/#74 补充证据，说明 #68 历史报告已由 #69 归档；整合保留 main 当前 `1b50709`，避免旧 `7738647` 覆盖入口 |

## 未合并 PR 的双轴审查

| PR | 固定 base | 固定 head |
| --- | --- | --- |
| #63 | `cdd24a5595d7c04ded3ce8b8547a27b59b846cce` | `7738647759689d8a345a0f563b103467ae0b3c68` |
| #66 | `a1577d7476b00af8a7940ad8d61cd148a3883bbe` | `1d029f524377de926d363fb8400f1399cf11a9e2` |
| #68 | `a1577d7476b00af8a7940ad8d61cd148a3883bbe` | `c81b1b6a514c333fa6f7b0a490702437d923972d` |
| #74 | `63c41c9b3e296b1deda53e2de343a359f183c054` | `c17722dcd261e9732150a18f7a74fc4741414957` |

### Standards

4 个固定 diff 未发现可确定的规范违规或值得报告的代码异味。按 CONTRIBUTING.md、领域文档及 ADR 核对凭据、部署证据和恢复边界。整合注意：#66 与 #68 包含相同历史部署提交 `c81b1b6`；合并冲突不能整文件采用旧分支部署入口。#74 保留较新入口并追加核验结果。

### Spec

未发现可确认的新增实现错误或范围越界。#63 服务端轮换、#66 固件补发及 #68/#74 的证据切片均未将局部结果冒充完整 #42 验收。明确保留 1 类未完成交付边界：异常身份丢失后完整恢复仍依赖前驱 journal、既有布局、有效 otadata 和可初始化 NVS；裸片、整分区损坏及不可读 anchor 的完整恢复工序尚未交付。

Standards：0 项确定违规；Spec：0 项新增实现错误，1 类已披露的部分交付边界。此结论不代替运行验证或管理员合并决定。

## 保留的验收缺口

- #42：正常恢复出厂、真实 OTA、异常身份丢失后的完整补发、补发前后历史归属/许可比对、补发后服务重启及适用中断续作仍需证据；健康身份轮换成功不能替代异常丢失。
- #12/#14：MCP 凭据与账号授权/计费/取消契约及真实设备知识库闭环仍未全部验收。
- #19：50 路完整语音、环境认证接口与真机、日程及产品重启/恢复仍须适用版本证据；原始失败和未执行项继续保留。
- #70–#73：规格已拆分，配网实现及真实 P4/Ubuntu 全流程验收仍待交付。
- 审计时 #63/#66/#68 与 main 存在合并冲突；本次只规范描述与文档，未合并这些分支。整合须保留本机解绑、凭据补发两侧能力及较新的当前部署记录；#68 的历史报告已在 #69 中归档。
- 历史 PR #38 披露的修复后 Ubuntu 回归缺口不因合并而成为通过；历史单次 ASR/设备重启异常不因本次文档整理被视为解决。

## 覆盖清单

### Issues

| Issue | 读取时状态 | 本次处理 |
| --- | --- | --- |
| [#2](https://github.com/yanghan24/xiaozhi/issues/2) | CLOSED | 正文/标题或格式修正 |
| [#3](https://github.com/yanghan24/xiaozhi/issues/3) | CLOSED | 正文/标题或格式修正 |
| [#4](https://github.com/yanghan24/xiaozhi/issues/4) | CLOSED | 正文/标题或格式修正 |
| [#5](https://github.com/yanghan24/xiaozhi/issues/5) | CLOSED | 正文/标题或格式修正 |
| [#6](https://github.com/yanghan24/xiaozhi/issues/6) | CLOSED | 正文/标题或格式修正 |
| [#10](https://github.com/yanghan24/xiaozhi/issues/10) | OPEN | 正文/标题或格式修正 |
| [#11](https://github.com/yanghan24/xiaozhi/issues/11) | CLOSED | 正文/标题或格式修正 |
| [#12](https://github.com/yanghan24/xiaozhi/issues/12) | OPEN | 正文/标题或格式修正 |
| [#13](https://github.com/yanghan24/xiaozhi/issues/13) | OPEN | 正文/标题或格式修正 |
| [#14](https://github.com/yanghan24/xiaozhi/issues/14) | OPEN | 正文/标题或格式修正 |
| [#15](https://github.com/yanghan24/xiaozhi/issues/15) | OPEN | 正文/标题或格式修正 |
| [#16](https://github.com/yanghan24/xiaozhi/issues/16) | OPEN | 正文/标题或格式修正 |
| [#17](https://github.com/yanghan24/xiaozhi/issues/17) | OPEN | 正文/标题或格式修正 |
| [#18](https://github.com/yanghan24/xiaozhi/issues/18) | OPEN | 正文/标题或格式修正 |
| [#19](https://github.com/yanghan24/xiaozhi/issues/19) | OPEN | 正文/标题或格式修正 |
| [#28](https://github.com/yanghan24/xiaozhi/issues/28) | CLOSED | 正文/标题或格式修正 |
| [#29](https://github.com/yanghan24/xiaozhi/issues/29) | CLOSED | 正文/标题或格式修正 |
| [#36](https://github.com/yanghan24/xiaozhi/issues/36) | CLOSED | 正文/标题或格式修正 |
| [#39](https://github.com/yanghan24/xiaozhi/issues/39) | OPEN | 正文/标题或格式修正 |
| [#40](https://github.com/yanghan24/xiaozhi/issues/40) | OPEN | 正文/标题或格式修正 |
| [#41](https://github.com/yanghan24/xiaozhi/issues/41) | CLOSED | 已检查，保留原正文 |
| [#42](https://github.com/yanghan24/xiaozhi/issues/42) | OPEN | 正文/标题或格式修正 |
| [#43](https://github.com/yanghan24/xiaozhi/issues/43) | CLOSED | 正文/标题或格式修正 |
| [#44](https://github.com/yanghan24/xiaozhi/issues/44) | OPEN | 正文/标题或格式修正 |
| [#45](https://github.com/yanghan24/xiaozhi/issues/45) | OPEN | 已检查，保留原正文 |
| [#46](https://github.com/yanghan24/xiaozhi/issues/46) | OPEN | 正文/标题或格式修正 |
| [#70](https://github.com/yanghan24/xiaozhi/issues/70) | OPEN | 正文/标题或格式修正 |
| [#71](https://github.com/yanghan24/xiaozhi/issues/71) | OPEN | 已检查，保留原正文 |
| [#72](https://github.com/yanghan24/xiaozhi/issues/72) | OPEN | 已检查，保留原正文 |
| [#73](https://github.com/yanghan24/xiaozhi/issues/73) | OPEN | 已检查，保留原正文 |

### 既有 PR

| PR | 读取时状态 | 本次处理 |
| --- | --- | --- |
| [#1](https://github.com/yanghan24/xiaozhi/pull/1) | MERGED | 历史描述规范化，保留原验证边界 |
| [#7](https://github.com/yanghan24/xiaozhi/pull/7) | MERGED | 历史描述规范化，保留原验证边界 |
| [#8](https://github.com/yanghan24/xiaozhi/pull/8) | CLOSED | 历史描述规范化，保留原验证边界 |
| [#9](https://github.com/yanghan24/xiaozhi/pull/9) | MERGED | 历史描述规范化，保留原验证边界 |
| [#20](https://github.com/yanghan24/xiaozhi/pull/20) | MERGED | 历史描述规范化，保留原验证边界 |
| [#21](https://github.com/yanghan24/xiaozhi/pull/21) | MERGED | 历史描述规范化，保留原验证边界 |
| [#22](https://github.com/yanghan24/xiaozhi/pull/22) | MERGED | 历史描述规范化，保留原验证边界 |
| [#23](https://github.com/yanghan24/xiaozhi/pull/23) | MERGED | 历史描述规范化，保留原验证边界 |
| [#24](https://github.com/yanghan24/xiaozhi/pull/24) | MERGED | 历史描述规范化，保留原验证边界 |
| [#25](https://github.com/yanghan24/xiaozhi/pull/25) | MERGED | 历史描述规范化，保留原验证边界 |
| [#26](https://github.com/yanghan24/xiaozhi/pull/26) | MERGED | 历史描述规范化，保留原验证边界 |
| [#27](https://github.com/yanghan24/xiaozhi/pull/27) | MERGED | 历史描述规范化，保留原验证边界 |
| [#30](https://github.com/yanghan24/xiaozhi/pull/30) | MERGED | 历史描述规范化，保留原验证边界 |
| [#31](https://github.com/yanghan24/xiaozhi/pull/31) | MERGED | 历史描述规范化，保留原验证边界 |
| [#32](https://github.com/yanghan24/xiaozhi/pull/32) | MERGED | 历史描述规范化，保留原验证边界 |
| [#33](https://github.com/yanghan24/xiaozhi/pull/33) | MERGED | 历史描述规范化，保留原验证边界 |
| [#34](https://github.com/yanghan24/xiaozhi/pull/34) | MERGED | 历史描述规范化，保留原验证边界 |
| [#35](https://github.com/yanghan24/xiaozhi/pull/35) | MERGED | 历史描述规范化，保留原验证边界 |
| [#37](https://github.com/yanghan24/xiaozhi/pull/37) | MERGED | 历史描述规范化，保留原验证边界 |
| [#38](https://github.com/yanghan24/xiaozhi/pull/38) | MERGED | 历史描述规范化，保留原验证边界 |
| [#47](https://github.com/yanghan24/xiaozhi/pull/47) | MERGED | 历史描述规范化，保留原验证边界 |
| [#48](https://github.com/yanghan24/xiaozhi/pull/48) | MERGED | 历史描述规范化，保留原验证边界 |
| [#49](https://github.com/yanghan24/xiaozhi/pull/49) | MERGED | 历史描述规范化，保留原验证边界 |
| [#50](https://github.com/yanghan24/xiaozhi/pull/50) | MERGED | 历史描述规范化，保留原验证边界 |
| [#51](https://github.com/yanghan24/xiaozhi/pull/51) | MERGED | 历史描述规范化，保留原验证边界 |
| [#52](https://github.com/yanghan24/xiaozhi/pull/52) | MERGED | 历史描述规范化，保留原验证边界 |
| [#53](https://github.com/yanghan24/xiaozhi/pull/53) | MERGED | 历史描述规范化，保留原验证边界 |
| [#54](https://github.com/yanghan24/xiaozhi/pull/54) | MERGED | 历史描述规范化，保留原验证边界 |
| [#55](https://github.com/yanghan24/xiaozhi/pull/55) | CLOSED | 历史描述规范化，保留原验证边界 |
| [#56](https://github.com/yanghan24/xiaozhi/pull/56) | MERGED | 历史描述规范化，保留原验证边界 |
| [#57](https://github.com/yanghan24/xiaozhi/pull/57) | MERGED | 历史描述规范化，保留原验证边界 |
| [#58](https://github.com/yanghan24/xiaozhi/pull/58) | MERGED | 历史描述规范化，保留原验证边界 |
| [#59](https://github.com/yanghan24/xiaozhi/pull/59) | MERGED | 历史描述规范化，保留原验证边界 |
| [#60](https://github.com/yanghan24/xiaozhi/pull/60) | MERGED | 历史描述规范化，保留原验证边界 |
| [#61](https://github.com/yanghan24/xiaozhi/pull/61) | MERGED | 历史描述规范化，保留原验证边界 |
| [#62](https://github.com/yanghan24/xiaozhi/pull/62) | MERGED | 历史描述规范化，保留原验证边界 |
| [#63](https://github.com/yanghan24/xiaozhi/pull/63) | OPEN | 固定 diff 双轴复核及描述修正 |
| [#64](https://github.com/yanghan24/xiaozhi/pull/64) | MERGED | 历史描述规范化，保留原验证边界 |
| [#65](https://github.com/yanghan24/xiaozhi/pull/65) | MERGED | 历史描述规范化，保留原验证边界 |
| [#66](https://github.com/yanghan24/xiaozhi/pull/66) | OPEN | 固定 diff 双轴复核及描述修正 |
| [#67](https://github.com/yanghan24/xiaozhi/pull/67) | MERGED | 历史描述规范化，保留原验证边界 |
| [#68](https://github.com/yanghan24/xiaozhi/pull/68) | OPEN | 固定 diff 双轴复核及描述修正 |
| [#69](https://github.com/yanghan24/xiaozhi/pull/69) | MERGED | 历史描述规范化，保留原验证边界 |
| [#74](https://github.com/yanghan24/xiaozhi/pull/74) | OPEN | 固定 diff 双轴复核及描述修正 |

## 验证与恢复

发布前核对目标仓库、逐项比较原正文防止覆盖并发编辑；发布后读回标题和正文精确比对，并核对开闭状态、PR head 与已完成勾选。依赖图检查无环，除 #10→#70 子规格及 #19 被 #73 阻塞外，原有关系保留。文档验证包括本地文件链接、锚点、固定提交证据目标及 diff 空白检查；本轮没有执行后端测试。

修改前后正文、逐项 diff、依赖快照及发布回执保存在本机本任务 worktree 的 `.runtime/spec-pr-audit-20261111/`。Git 文档可通过恢复 PR 撤销；GitHub 正文和新增两条关系需按快照分别恢复，回退此文档提交不会自动撤销 GitHub 编辑。恢复前再次核对是否存在后续编辑，不整批覆盖他人新内容。
