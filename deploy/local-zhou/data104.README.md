# 小智独立开发数据服务

运行目录 `/home/snow/app/xiaozhi-dev/`；部署和备份脚本 `/home/snow/packages/xiaozhi-dev/`。按本机 `/home/snow/README.md` 的目录规则部署，操作用户 snow。

## 范围与连接

仅支持本开发机的小智 Dev Container。114 是独立部署环境，本项目不导入其数据或配置。已盘点现有公共组件：小智原生需要 MySQL，现有 PostgreSQL 不作替代；Redis 按用户确认的独立开发方案新增，以保持与 AI 集市共享实例的运行、账号及数据完全隔离。没有升级或改动共享实例。

- Compose 项目 `xiaozhi-dev-data`，容器 `xiaozhi-dev-mysql`、`xiaozhi-dev-redis`，独立 Compose 网络。
- MySQL 固定 8.0.41 及摘要，内网 `172.16.18.104:13306`，数据库/账号 `xiaozhi_dev`。
- Redis 固定 8.0.0 Alpine 及摘要，内网 `172.16.18.104:16380`，独立密码、DB 0、AOF 持久化。
- 允许有内网路由的开发客户端访问；服务仍要求认证，不公开到公网。
- 凭据位置 `.env`（MySQL）和 `redis.conf`（Redis），权限 600；README 不记录密码。
- 数据目录 `data/mysql/`、`data/redis/`；备份目录 `backups/`。不在 packages 保存运行数据。

## 启停与检查

```sh
cd /home/snow/app/xiaozhi-dev
docker compose ps
docker compose stop
docker compose start
```

首次部署或需要重建本项目时：`bash /home/snow/packages/xiaozhi-dev/deploy-data104.sh`。
停止保留数据。不得重新生成已使用的密码，不运行 down --volumes 或删除 data。

## 备份

```sh
bash /home/snow/packages/xiaozhi-dev/backup-data104.sh
```

备份生成带时间戳的独立目录，含完整 MySQL 表结构、排除对话/录音/标题表数据的一致性逻辑备份、开发 Redis RDB、匹配配置及 SHA256 清单，目录私有。开发 Redis 仅供本项目配置缓存和会话，当前没有客户对话；本脚本不用于生产数据，不能据此绕过产品正文不备份的边界。备份仍在同一服务器，不代表异地灾备或恢复演练通过。未新增定时任务。

如需恢复，先停止本机小智应用，核对备份清单及匹配密码，在独立数据目录依次导入表结构和业务数据并验证；停止 Redis 后再恢复其匹配数据。不覆盖其他项目数据，不把单次备份成功记为恢复演练通过。
