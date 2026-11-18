# 小智生产部署

发布时先在开发容器生成 Web、API 和语音的封装输入，再用本机 Docker 导出镜像包。发布包包含镜像归档、部署模板、源码版本和 SHA-256 清单。部署机使用预先导入的镜像。

```sh
docker exec -u dev xiaozhi-dev bash /workspace/deploy/local-zhou/prepare-release.sh HEAD
bash deploy/local-zhou/pack-release.sh HEAD
```

## 服务器目录

| 位置 | 内容 |
| --- | --- |
| `/data/packages/xiaozhi/` | 上传包和校验清单 |
| `/data/apps/xiaozhi/` | 当前 Compose、私有配置、MySQL、Redis、上传文件和日志 |
| `/data/apps/nginx/` | 网关 Compose、路由和 TLS |
| `/data/backup/xiaozhi/` | 可用于恢复的备份 |
| `/data/bin/xiaozhi/`、`/data/bin/systemd/` | 启停脚本、初始化脚本和自启动单元 |
| `/data/` | 部署视图、运维说明和更新日志 |

## 首次初始化

1. 校验发布包后执行 `docker load`。对照镜像清单检查平台和文件系统层。配送模板解压到暂存目录，应用与网关分别安装到对应目录。
2. 为首次启动运行 `init-config.sh <发布标签>`。脚本准备新的 MySQL、Redis、后台管理员和应用加密配置。把生产专用的扫码客户端和模型目录客户端填入 `config/api/speaker.properties`，将目录客户端 Secret 写入权限为 600 的 `config/api/market-catalog.secret`。
3. 先启动 MySQL、Redis、API 和 Web。Java 会初始化数据库、生成服务密钥，再从私有启动文件创建首个管理员。管理员密码私下保存，初始化成功后移除启动文件。
4. 运行 `init-voice.sh` 生成 Python 的私有配置，再启动语音。使用后台配置默认智能体和模型。配置文件中的 Java 和 Python 内部地址使用容器服务名。
5. Nginx 的 `.env` 配置 `GATEWAY_BIND`，安装服务器证书链和私钥。从设备口验证 HTTPS、OTA 和 WSS，确认后台登录及公网对内部接口的隔离。使用相同域名、映射端口和 HTTPS 证书。

旧环境的容器、持久卷和原有目录适用原来的回退入口。新环境使用独立的新数据目录。生产客户端凭据、管理员密码和私钥由运维人员私下保存。首次部署后在服务器记录真实的上线时间、版本、启动方式和验证范围。
