# 部署与构建入口

| 路径 | 用途 |
| --- | --- |
| [production/](production/README.md) | 生产部署架构、目录、备份、服务发布、启停和固件 OTA。 |
| 各开发环境目录 | 各自工作区的容器、依赖、数据库和启动方式，按对应环境说明使用。 |
| [manager/](manager/) | 根目录 Compose 管理端构建与隔离测试需要的镜像定义。 |
| [voice/](voice/) | 开发及发布共享语音依赖清单，以及根目录语音 Compose 的测试镜像和 pytest 配置。 |

团队的开发检查和生产操作边界见[协作规则](../CONTRIBUTING.md)。旧环境的说明与实测结果见[历史归档](../docs/archive/README.md)。
