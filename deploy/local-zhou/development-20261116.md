# 独立开发环境交付记录

日期：2026-11-16。源码基础 `e708ba21f6d36e6b3dde5879572d57b6dd466eef`，配置变更位于任务分支 `dev/local-zhou`，尚未发布或合并主线。

## 当前运行

| 内容 | 位置 |
| --- | --- |
| 本机开发容器 | `xiaozhi-dev`，Compose 项目 `xiaozhi-development` |
| 本人开发配置 | `deploy/local-zhou/`；同事的 `deploy/local/` 未覆盖 |
| 管理页面 | `http://172.16.111.196:18001/#/login`，或本机 `127.0.0.1:18001` |
| Java API | 容器 8002，本机回环 18002 |
| Python WebSocket / HTTP | 容器 8000 / 8003，本机回环 18000 / 18003 |
| MySQL | 104 的 `xiaozhi-dev-mysql`，内网 13306，数据库/账号 `xiaozhi_dev` |
| Redis | 104 的 `xiaozhi-dev-redis`，内网 16380，独立认证、DB 0 |
| 104 运行配置、数据、备份 | `/home/snow/app/xiaozhi-dev/` |
| 104 部署和备份脚本 | `/home/snow/packages/xiaozhi-dev/` |
| 本机私有配置、登录资料 | `/home/timyai/dev/runtime/xiaozhi/` |

104 的目录按该机 `/home/snow/README.md` 执行：compose、配置、data/mysql、data/redis、backups 在 app，脚本在 packages；没有把运行数据放 packages。MySQL/Redis 固定为项目已有 8.0.41/8.0.0 镜像及摘要，实际在 CentOS 7 / 3.10 内核下启动、健康检查及连接通过；没有修改宿主机内核或安全策略。

本机开发镜像固定 Node 20.19.0、Maven 3.9.9、Python 3.10、Temurin Java 21 的来源摘要；实际工具版本为 Node 20.19.0、Java 21.0.7、Python 3.10.22、Git 2.39.5。镜像含 OpenSSH、FFmpeg、Opus、Supervisor 和访问辅助工具。Python venv、依赖缓存、node_modules、Java target、Web dist、语音 data/tmp、上传和 runtime 均已核对为独立命名卷。宿主机没有安装开发工具或项目依赖。

## 已验证

- 前端锁文件安装成功，Java 打包成功，Python 依赖检查无冲突。旧 pip 解析失败通过仅在容器虚拟环境固定 pip 25.3 解决；setup 按依赖输入摘要复用卷，不在每次打开时重新安装。
- Java 使用 104 新开发库完成 **136** 项 Liquibase 迁移，三个服务由容器入口的 Supervisor 自动启动，服务 stop/start 正常。
- 网页、Java 公开配置、Vue 开发代理及语音 HTTP 均 200；匿名管理访问被拒，无密码 Redis 访问被拒，无效语音令牌被拒。
- 仅重启小智的 MySQL/Redis，再重建小智开发容器：数据库迁移数、管理服务密钥摘要、Redis 临时验证值与语音配置摘要均保持。临时验证键已删除。
- 104 原有容器的名称/ID/镜像前后相同，没有对 AI 集市服务执行重启或修改。没有将 114 数据或配置接入开发服务。
- 备份脚本实际生成 MySQL 结构/排除对话表数据的业务备份、开发 Redis RDB 和私有配置，最新目录 `backups/20261116-150041/`；检查 SHA256 清单。未执行恢复演练或安排定时备份。
- 访问认证材料保存在私有位置；生产访问说明已从仓库文档中移除。

脱敏检查结果保存在容器 `/opt/xiaozhi-dev/verification-final.json`、`verification-persistence.json`；完整依赖版本清单为 `python-requirements-installed.txt`。宿主 runtime 的 logs 目录保留镜像构建日志及检查摘要；原始应用日志只在小智 runtime 卷，私有凭据不入 Git。

## 功能边界

本轮交付开发基础设施及服务启动，不等于业务、真机或容量验收。开发库尚未创建管理员，首次管理员需通过开发页面正常注册和验证码登录。远程 ASR/模型/MCP/OIDC 的正式凭据未配置；本轮未调用模型、知识库或付费服务，也未接入音箱。114 保持另一套独立部署环境。
