# #11 公网 TLS 入口核验

日期：2026-11-09，Asia/Hong_Kong。公网入口已启动并验证；设备地址发布和 #11 整体验收尚未完成。

## 拓扑与修改

用户确认运维执行纯 TCP 转发：`bridge.hncig.cn:18443` → `192.168.168.114:443`。本轮 DNS 返回 `222.244.138.216`。TLS 在 Ubuntu 终止。

新增 `compose.public-entry.yaml` 和 `deploy/device-entry/nginx-tls.conf`。使用独立项目 `xiaozhi-ubuntu-test-public-entry`，接入已有 `xiaozhi-ubuntu-test_default` 网络；仅绑定指定 LAN IP 的 443。原有五个容器的 ID、启动时间和挂载在启动前后相同，数据库、账号及数据卷复用。另一个正在运行的测试项目未操作，JetLinks 保持停止。

入口只开放 OTA 检查/激活、语音 WebSocket 和 ready 路径。`/`、管理公开配置、管理参数接口返回 404。绑定页、OIDC 回调、注销通知和固件下载尚未在此入口提供；不能将本轮结果当作这些功能验收。保留外部 Host 的 `:18443`，支持 Upgrade 和 300 秒读写超时，关闭访问日志。

Ubuntu 配置位于 `/home/jt/xiaozhi-ubuntu-test/.runtime/public-entry/`；独立环境文件为 `entry.env`，未修改应用 `.env`。使用已缓存镜像 `xiaozhi-offline-nginx:1.27.4-20261106`，镜像 ID 为 `sha256:1ff4bb4faebcfb1f7e01144fa9904a570ab9bab88694457855feb6c6bba3fa07`。未拉取镜像或重建应用。

| 部署文件 | SHA-256 |
| --- | --- |
| nginx-tls.conf | fbe101157dc9f1c94c98816ee7fe2ac310b40f517235a556fc8c6d3a596a88eb |
| compose.public-entry.yaml | a22186b0e1529562a729ebcd901467484645749fe1c5fd318d50ca4c24618006 |

## 证书

用户提供的 RAR 包包含 PFX 和其密码文件。已在私有目录转换为 Nginx PEM，验证域名、服务端用途、证书链及公私钥匹配。正式使用文件位于 `/home/jt/.local/share/xiaozhi-public-entry/tls/`，目录权限 700、文件权限 600，只读挂载到入口容器。证书及私钥不提交。

- SAN：`*.hncig.cn`、`hncig.cn`，匹配 `bridge.hncig.cn`。
- 颁发者：DNSPod TLS RSA CA 2025；包含一个中间证书。
- 有效期：2026-09-30 00:00:00 UTC 至 2027-04-16 23:59:59 UTC（北京时间 2027-04-17 07:59:59 到期）。
- SHA-256 指纹：`FB:03:C7:6B:13:0D:B3:70:D4:D6:60:2E:0F:71:92:2D:23:1F:A4:DC:A6:D5:77:03:CF:2F:E9:20:5E:C5:67:2D`。

## 验证结果与边界

1. Compose 配置检查及容器内 `nginx -t` 通过，实际发布 `192.168.168.114:443`。
2. Windows 直接访问公网地址，保持系统证书校验，语音健康路径 HTTP 200；WebSocket 握手 HTTP 101。curl 握手探测最终因未实现 WebSocket 关闭交互而超时，因此完整协议另用下列客户端验证。
3. Ubuntu 协议客户端经内网 443 访问 TLS 入口，22 项检查通过。
4. Ubuntu 协议客户端经临时 SSH 反向 TCP 转发，从 Windows 网络访问公网 `bridge.hncig.cn:18443`，同样 22 项通过。TLS 主机名和信任链仍由 Ubuntu 客户端验证，未使用不安全证书选项。这证明该来源网络的公网入口，不证明 Ubuntu 直接公网回连或手机/音箱网络。

22 项覆盖管理路径隔离、HTTP 健康、服务配置认证、临时设备 OTA/激活/绑定、错误 WSS 凭据拒绝、正确凭据 hello、同会话 ready、错误 token 和其他客户端 ready 拒绝、三次 Ping/Pong、关闭后 ready 移除及临时设备/智能体清理。OTA 响应额外断言不含 `mqtt` 键。未测试真实音频推理或音箱播放。

客户端复用既有 `scripts/check-voice.py`，将 OTA 请求指向入口，并显式覆盖 WSS 测试目标；**OTA 实际下发地址仍为内部地址，不能把目标覆盖当作地址发布成功**。测试宿主机为 Python 3.8，临时使用 websockets 13.1 纯 Python wheel，并适配其 `close_code` 和缺失的 `asyncio.to_thread`；应用容器依赖未变。wheel SHA-256：`a9a396a6ad26130cdae92ae10c36af09d9bfe6cafe69670fd3b6da9b07b4044f`。

服务端私有报告：`.runtime/public-entry/lan-tls-protocol.json`、`public-tls-protocol.json`。检查脚本保存在 `/home/jt/.cache/xiaozhi-11/check-lan-tls.py` 和 `check-public-tls.py`；后者依赖临时反向转发，测试后关闭。前两次内网检查因宿主机客户端版本兼容问题未完成，临时对象均清理；适配后内网和公网路径各完整通过一次。

## 未完成：Ubuntu 公网回连与地址发布

已有管理员 token 过期；通过 SSH 隧道和真实验证码登录复用了原有测试账号，没有注册账号或重置密码。

通过真实管理 API 更新 `server.websocket` 为 `wss://bridge.hncig.cn:18443/xiaozhi/v1/`，服务端连通性校验返回 `10101`，因此没有保存。`server.ota` 尚未更新。原值已备份到私有 `urls-before.json`，当前仍为：

```text
server.websocket=ws://voice:8000/xiaozhi/v1/
server.ota=http://api:8002/xiaozhi/ota/
server.auth.enabled=true
```

Ubuntu 最初一次公网 TLS 1.3 握手成功，但后续多次直连域名和同一公网 IP 的 18443 均超时；同期内网 443 和 Windows 公网访问正常。证据优先指向公网回流 NAT/访问策略或其稳定性，尚未获得网关侧证据，也未直接检查 API 容器的连接日志，不能确定唯一根因。

运维需核对 `192.168.168.114` 及其 Docker 出站连接访问 `222.244.138.216:18443` 的回流路径。修复后重新从 Ubuntu 和 API 容器检查，使用管理 API 发布上述 WSS 和 `https://bridge.hncig.cn:18443/xiaozhi/ota/`，再用实际 OTA 下发地址完成协议验证。不能单纯把域名解析成 LAN IP：外部端口为 18443，而当前内网监听为 443，内部方案还需解决端口差异。

#11 保持未完成；手机/设备网络、OIDC 登记及真实授权/刷新/撤销仍待各自验收。集市出站 443/4090 在本轮早期探测超时，不能宣称已放行。

## 运维与回退

当前未配置自动续期或额外 restart policy，未执行主机重启验收。续期后替换完整证书链及匹配私钥，先执行 `nginx -t` 再 reload，重新核验证书与 WSS。

仅停止此次入口的命令：

```bash
sudo docker compose \
  --env-file /home/jt/xiaozhi-ubuntu-test/.runtime/public-entry/entry.env \
  -f /home/jt/xiaozhi-ubuntu-test/.runtime/public-entry/compose.public-entry.yaml \
  stop public-entry
```

入口停止不会影响原有五个容器或删除数据。若后续发布了设备地址，回退还需依据私有备份恢复对应参数；本轮地址未发生变化。
