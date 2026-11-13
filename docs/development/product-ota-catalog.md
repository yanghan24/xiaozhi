# 产品设备固件目录

入口：管理员菜单「产品设备 OTA」，路由 `/#/product-ota`。选择本项目明确的 Release 和 `.bin` 资产，点击「校验并导入」。预发布始终标识；没有 latest 跟随、本地上传或任意 URL 输入。

导入并不发布 OTA。已校验列表按固定 `firmware_id` 选择，显示实际应用版本、ELF、SHA-256、P4 revision 与分区容量。同名应用版本的不同构建是不同目录项。GitHub 暂时不可用时，已导入目录仍可读取。

管理 API 使用现有管理员 Bearer 身份和数据库中的当前管理员/启用状态，支持当前 SSH 隧道内的私有管理 HTTP。公网反向代理不得开放 `/admin/product-ota/**`；设备交付 API 的 HTTPS 要求不变。客户、设备与烧录凭据不能管理目录。

| 接口（管理 API context 下） | 行为 |
| --- | --- |
| `GET /admin/product-ota/github-releases?page=1` | 每页 20 个项目 Release，排除草稿，白名单返回 tag、预发布及 bin 资产摘要 |
| `POST /admin/product-ota/firmware` | 仅接受整数 `release_id`、`asset_id`，拒绝其他字段、重复字段和超限请求 |
| `GET /admin/product-ota/firmware` | 读取已固定目录；不返回镜像内容、manifest 原文或配置 |

来源固定为 `yanghan24/xiaozhi` GitHub API；下载不携带管理员、设备或 GitHub 秘密。只跟随 HTTPS GitHub API/资产域名，拒绝私网/本机解析及其他目标。元数据上限 1 MiB、manifest 上限 64 KiB、镜像上限 8 MiB，单次导入并发为 1。超时和远端错误不会写入部分目录；发生超时后刷新目录确认结果，不自动重试导入。

Release 必须带 `manifest.json` 和 GitHub SHA-256 digest。manifest 需声明 `schema_version=1`、匹配的 `release_tag`、40位 `source_commit`、板型、应用版本和 application 的 file/bytes/sha256/elf_sha256，以及 partition_layout、partition_table_sha256、app_partition_bytes。首版接受 `waveshare-esp32-p4-wifi6-touch-lcd-4.3`、`partitions/v2/16m.csv`、4,128,768 字节应用槽。布局 SHA 属于发布方元数据，实际设备是否使用该布局仍须发布前读取核对，不能仅凭该目录推断设备兼容。

镜像校验包含 P4 chip ID、应用描述符、分段边界、ESP 校验和/可选附加 hash、实际版本/ELF、全文件 SHA、16 MiB flash 和镜像 revision 范围；拒绝 bootloader、合并镜像、错误或截断数据。下载后再次核对 Release/资产固定字段，变动即拒绝。相同资产重复导入返回 409。

`dc_ota_firmware` 保存不可变应用字节及元数据，未来发布模块按 `firmware_id` 读取，不能重新下载 mutable Release 替换固定字节。迁移仅新增表，可停用新入口并保留该表；删除目录会影响后续发布引用，不提供删除接口。备份未来使用目录和发布状态时应一并保留该表，镜像可能包含构建配置，禁止加入匿名下载或普通备份日志。

自动 HTTP 测试在 Ubuntu 独立 MySQL/Redis 运行，外部 GitHub 边界使用受控夹具。真实网络检查需显式执行 `mvn test -DskipTests=false -Dtest=ProductOtaCatalogLiveProbe`，该类不属于默认离线回归命名。它核对明确的 12f2ff1 候选 SHA/ELF并清理自己的管理员和目录夹具，不向设备发送命令。
