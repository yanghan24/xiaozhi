# 在线烧录配套服务交接

关联 #41；叠加 PR #26 `885918d`，不要求提前合并 PR #26。
现有部署由 PR #48 记录，公网 OTA / WSS / device-control 可用。
用户已报告 delivery API `d06f459`、路由和迁移部署完成，13 项公网检查通过，备份保留。
新增 Java 套件的完整执行结果尚未传入；不能直接把 PR #26 的 321 项结果归入新版本。
当前产品库管理员配置由服务器执行者负责，尚未完成工具授权验收。
本 PR 后续补充缺少/错误 Content-Type 的 v1 400 响应修复和第 12 个集成测试，验证后更新该 API。

服务器执行者先核对本 PR 的固定 head SHA 并记录，全部构建、回归和部署使用同一 SHA。
连接使用现场指定的公网部署通道；`bridge.hncig.cn:18443` 是产品 HTTPS 入口，不是 SSH 地址。
无须修改现有主线、注册新客户或重新初始化数据库/管理员密码。

## 部署前验证

1. 读取现有部署 `private/compose.json`，核对 `xiaozhi-pr26-api`、voice、identity DB、Redis、入口、镜像及私有目录；
   复用现有账号、OIDC 密钥和数据。JetLinks 保持原停止状态。
2. 在独立源码 checkout 固定本次 SHA，按照 [接口与部署说明](../development/device-delivery-v1.md)运行 Ubuntu 独立 Java 全量测试及集成守卫。
   新增 `DeviceDeliveryHttpTest` 必须实际执行。保存测试数量、失败/跳过数及兼容性报文；测试不得连接现场客户库。
3. 通过后构建固定 SHA 的产品 API。新增的两个表可加到原库，不删除或重建原表，不清空 Liquibase 校验和。
4. 使用现有 `backup.py` 的必要数据范围备份，并纳入新增交付表；排除对话正文和音频。单独保留交付专用 AES 密钥。

## 密钥与版本更新

在现有部署私有目录下创建专用 key 文件，已存在时停止生成并复用；不覆盖旧 key。
以下仅在确认尚无此 key 的 Ubuntu 部署私有目录执行，输出不含秘密：

```bash
python3 - <<'PY'
import base64, os, secrets
from pathlib import Path
directory = Path('private/device-delivery')
directory.mkdir(mode=0o700, parents=True, exist_ok=True)
path = directory / 'encryption-key'
fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, 'wb') as stream:
    stream.write(base64.b64encode(secrets.token_bytes(32)) + b'\n')
    stream.flush()
    os.fsync(stream.fileno())
print('Created private delivery encryption key; retain it for restart and recovery.')
PY
```

将此文件只读挂载到新产品 API，并在该服务设置：

```yaml
environment:
  DEVICE_CONTROL_ENABLED: "true"
  DEVICE_CONTROL_PUBLIC_ORIGIN: https://bridge.hncig.cn:18443
  DEVICE_DELIVERY_ENABLED: "true"
  DEVICE_DELIVERY_ENCRYPTION_KEY_FILE: /run/secrets/device-delivery-key
volumes:
  - ./device-delivery/encryption-key:/run/secrets/device-delivery-key:ro
```

这是合入现有私有 Compose 的片段，不是完整替代文件。volume 的相对路径以**实际 Compose 文件所在目录**核对；
现有文件是 `private/compose.json` 时，上例 `./device-delivery` 对应部署的 `private/device-delivery`。
保持原 datasource、OIDC、proxy 信任和其他挂载。只重建产品 API，voice 继续使用已部署的产品闸门版本。

在现有公网入口合入模板新增的五类精确 delivery 路由，指向新产品 API，保留实际 upstream 和全部既有路径。
先 `nginx -t`，API 启动且迁移完成后再重载。不要整体覆盖现场私有入口配置或发布完整管理 API。

## 部署后交付

- 通过公网正常验证证书；无凭据 `device-control/v1/state` 应为 401，新 `device-delivery/v1/authorize` 应为 401，管理/内部路径仍为 404。
- 执行 `python3 scripts/check-delivery-rejections.py` 的 20 项无凭据检查；媒体类型缺失/错误须返回 v1 400 和 no-store，不能返回 HTTP 200 / code 500。
- 确认本产品 API 账号库中已有启用的超级管理员，并通过既有受保护管理入口完成正常登录。
  工具只读取含 `token` 的私有 0600 JSON，交换一小时交付授权；不改数据库来伪造登录或绕过验证码。
- 使用独立测试设备完成两设备签发与隔离、错误凭据拒绝、完成前禁止准入、重复操作/服务重启和撤权检查。
  保存脱敏证据，清理仅限本轮专用测试身份，不能触及真实样机或原账号。
- 通过后由本机工具执行真实 P4：备份、应用槽写入、两硬件身份核对、独立凭据写入/读回、恢复产品镜像、设备公网验证及最终 delivered。
  设备二维码由固件从正式控制接口取得，不手工拼接手机链接。

现场固件和工具已准备在独立任务分支；产品/维护构建、原 UI/分区检查和 5 个 C++ 测试程序已通过。
工具授权预检补充后 26 个 Python 测试通过；工具在打开设备、写 Flash 前先验证有效后台授权，失效/撤权/接口不可用时直接停止。
这些结果不替代 Ubuntu 新后端回归或真实 USB、手机、音箱、停播 ACK、重连和第二台实物隔离。
全部实机验收通过并获管理员针对 PR 的明确同意后，才安排合并。

回退先停止新交付，保留新增表和 key。已经交付的在线固件每次启动仍会使用 device-check，撤回该接口会使其保持关闭。
恢复相同数据、key 和兼容 API 后再继续；不能用旧匿名入口或批量启用来处理故障。
