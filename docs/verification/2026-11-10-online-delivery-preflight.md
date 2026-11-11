# 在线交付：其余公网检查与固件产物核对

2026-11-10 20:48（UTC+08:00），入口 `https://bridge.hncig.cn:18443`，正常验证 TLS 证书。
用户报告部署版本为 PR #52 `d06f459`；本机没有读取服务器镜像元数据，检查结论仅覆盖实际公网响应。
本轮未配置管理员、使用真实凭据、登记设备或打开串口。

## 公网结果

```bash
python3 scripts/check-public-device-preflight.py \
  --output /tmp/xiaozhi-public-preflight.json
```

24 项全部通过，结果见[脱敏 JSON](evidence/2026-11-10-online-delivery/public-preflight.json)。

| 检查 | 数量 | 观察 |
| --- | ---: | --- |
| OTA、语音 HTTP 存活 | 2 | HTTP 200；仅证明入口可达 |
| OTA 公开发现及旧数字激活 | 2 | 发现响应只有正式控制接口地址；旧激活返回 403 |
| 设备状态、请求和 ACK 鉴权 | 4 | 匿名或未签发凭据返回 v1 401、no-store |
| 工具查询、授权及设备验证鉴权 | 4 | 匿名或未签发凭据返回相应 v1 401、no-store |
| 语音 ready 与未签发音频 URL | 2 | 分别为 401、404 |
| 管理、内部接口及非正式交付路径 | 5 | HTTP 404 |
| 方法与请求大小限制 | 3 | 错误方法 403，超过 4KB 返回 413 |
| 匿名及错误凭据 WSS | 2 | TLS/升级成功后，首帧即以 1008 关闭，无业务数据 |

OTA POST 的 200 是公开发现协议，不能把它解释为设备已鉴权或语音已放行。
这 24 项没有覆盖已知的错误 Content-Type；其专用 20 项检查仍须在修复部署后执行。

## 固件产物

现场固件分支 `feat/41-online-device-delivery` 的主机工具提交为 `aa5004e`。
镜像来自 `924fe72`，后续提交只修改工具和文档。运行产品构建检查的 9 项均通过；
`build_artifacts` 重新解析两应用、核对 ELF 和分区，结果与既存 manifest 完全一致。

| 产物 | 字节 | SHA-256 |
| --- | ---: | --- |
| 产品镜像 | 3,979,392 | `ba041a38884a823b02c7b619b47c1dccb9adf84b9358e464dfcc4654f1916f48` |
| UART 维护镜像 | 250,512 | `c8dd65eb71b1afa703eb3d256a4154a615cd76585a9dacc62e26a6c4b2be4a17` |

产品镜像适用 P4 revision 3.1–3.99，保留原 16 MB 分区、首页和 Logo，两个应用槽均剩余 149,376 字节。
此前 26 项 Python 工具测试结果继续有效，本轮没有重复计数或将其视为真实硬件验收。

## 剩余验证

Content-Type 修复 `873fa63` 及 12 个 delivery HTTP 测试方法仍待 Ubuntu 独立数据库执行全套回归，
通过后更新产品 API，再运行 `scripts/check-delivery-rejections.py`。
管理员与工具授权验收交由服务器执行者处理；真实签发、USB 写入/读回、设备公网验证、第二台实物隔离、
手机绑定、音箱收放音、停播 ACK 和断线重连均保持待验收。PR #26/#52 尚未合并。
