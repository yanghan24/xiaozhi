# 阶段四清理边界复现（保留失败）

日期2026-11-12。本机CPython3.12.10，SDK stub；不修改产品和原75条脚本，不连接真实服务。结果：**77条，75通过、2失败、0错误/跳过，退出码1**。原75条通过，新增2条实际client清理要求失败，不把本轮写成全部通过。

| 文件 | 类型 |
| --- | --- |
| `result.json` | 实际本机诊断报告，14受测文件hash与既有源码一致 |
| `tests.txt` | 实际日志和两个AssertionError，不是人为构造的失败文本 |
| `review-scope.json` | wrapper/原脚本hash、注入用例和未改产品/未做Ubuntu复验的范围 |
| `reproduce-cleanup-review.snapshot.py` | 运行wrapper原字节快照；复用原Harness及网络保护，仅注入2条新要求 |
| `archive-sha256.json` | 上述4附件hash |

## 刺激与可证结论

使用真实ServerMCPManager/ServerMCPClient，替换SSE传输的关闭阶段，令其暂缓退出并观察到CancelledError；将15秒/2秒等待临时缩短为0.02/0.02，不改变产品源码。分别覆盖清理截止后返回和调用方取消两种路径。断言前已确认：原worker仍未done，且它确实收到取消，避免用未触发刺激伪造失败。

两条均在`client._worker_task is worker`断言失败：实际为None，原worker仍处于cancelling/pending。说明清理失去回收引用。manager对正常返回的client.cleanup会按成功移除client，这是对应静态代码路径；本诊断不声称发生了真实网络会话泄漏事故。

finally会释放合成关闭事件、等待该worker退出并清理manager，未故意遗留测试任务。Python网络/子进程保护由原main启用；不是OS沙箱，也不是Ubuntu/安装SDK复验。

## 复现位置与快照限制

原执行wrapper：仓库外`issue-12/reproduce-cleanup-review.py`，它按相邻`xiaozhi/.worktrees/issue-12-mcp-contract`定位受测脚本：

```text
python -I -B <仓库父目录>/issue-12/reproduce-cleanup-review.py --sdk stub --group all --output <全新目录>
```

归档snapshot是证据副本，不能在此目录直接运行来定位工作区。恢复复现时把相同字节放回上述仓库外相对布局，核对原脚本与14文件hash后再运行。不要在已下载的不受信任材料目录启动解释器；用-I，并将脚本和数据分开。

该结果使D-05重新待修复、R-02不能完成；不覆盖阶段二历史75/75或真实协议记录。后续修复必须新增记录并补受影响Ubuntu回归，不能继续沿用旧源码hash声称已验证修复。

完整结论见[阶段四审查](../../../2026-11-12-mcp-delivery-review.md)。
