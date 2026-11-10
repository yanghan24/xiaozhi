# LLM 流式成功统计修复

关联 PR #38，审核基点 `00d0b4f`。审核人员已在 Ubuntu 隔离容器复现：上游 HTTP 200 返回 SSE error，旧实现仍记录 `llm_succeeded=1`、`failures=0`、`error_type=null`。原有 6 项 HTTP 检查通过，未覆盖这个错误流场景。

## 行为修复

旧实现以 HTTP 响应流结束作为成功条件，未检查流内事件。现在逐个解析 SSE 事件并保留正常事件原字节，只有收到完整的 `data: [DONE]` 事件且下发完成后才增加 `llm_succeeded`、记录 `llm_complete`。

- `event: error`、JSON `error` 或错误码事件记录为 `ModelStreamError`；缺少完成标记、空流或残缺事件结束记录为 `IncompleteModelStream`。失败计数增加，成功计数不增加。
- 请求日志保留 `llm_stream` 阶段、上游 HTTP 状态、安全错误码及异常类型。流式响应头已经发送时，下游 HTTP 状态仍可能为 200，应同时检查错误字段和完成标记。
- 支持跨网络分块的事件、UTF-8 字节、LF/CRLF/CR 换行、多行 `data` 和注释心跳；保留原有 1 MiB 响应限制及授权租约检查。
- ASR、LLM 非 200 响应共用至多读取 8192 字节的错误解析函数，只提取限长且符合白名单的 code；不记录错误正文、消息、账号凭据或录音。

## 回归范围与执行状态

`scripts/check-market-test-bridge.py` 新增 4 个 HTTP 回归方法，整套共 10 个测试方法：

| 场景 | 必须观察到的结果 |
| --- | --- |
| HTTP 200 内的 JSON error、SSE error、正常片段后的 error、BOM 前缀错误事件及无效 JSON | `llm_succeeded=0`，失败计数增加，日志含错误类型及可用的安全错误码 |
| 有内容但无 `[DONE]`、仅 finish_reason、截断 JSON/完成标记、仅心跳或空流 | 记录 `IncompleteModelStream`，不记完成或成功 |
| 逐字节传输、多行 data、中文 UTF-8、三种换行及完整完成标记 | 响应字节完整保留，成功计数增加，错误字段为空 |
| LLM HTTP 400 的普通及超长错误正文 | 返回 502，读取有界，日志不含错误消息或测试凭据 |

本次已通过两份 Python 文件的 AST 语法检查及 `git diff --check`。Ubuntu SSH 直连超时后，用户明确要求暂不进行需要直连的审核，因此**本次新增回归尚未执行，也未在修复版本上重跑原有 6 项测试**。没有发起付费请求、重新测试音箱、部署修复或修改运行服务。

可连接后，在 Ubuntu 的该任务分支目录使用既有镜像执行隔离检查：

```bash
sudo docker run --rm --network none --read-only \
  --tmpfs /tmp:rw,nosuid,nodev,noexec,size=32m \
  --mount type=bind,source="$PWD/scripts",target=/review,readonly \
  --workdir /review --entrypoint python \
  xiaozhi-voice:local-20261106-netmirror \
  -B check-market-test-bridge.py -v
```

这个命令使用容器内的回环 HTTP 测试服务和假凭据，不连接集市或既有运行容器；预期执行 10 项，实际结果以届时输出为准。

## 历史证据

修复不重新解释历史成功或失败计数。先前的 `llm_succeeded` 和 `llm_complete` 使用旧口径，单独不能证明 SSE 正常完成；真机实际回答、音频事件及现场播放反馈作为独立证据保留。原部署 SHA-256 及服务器归档继续对应当时运行的旧脚本，本修复尚未部署。
