# P4 身份保留与授权补发补丁

关联 [#42](https://github.com/yanghan24/xiaozhi/issues/42)。本目录交付独立固件工程的增量源码；主项目仍为服务端仓库，不包含完整 ESP32 固件基线或私有构建配置。

| 项目 | 固定值 |
| --- | --- |
| 固件基点 | `00896abbb266bf5d2dda90a49b71ea7bb95c1a6f` |
| 实现提交 | `6983bb589ed173c537a2fdd865d7953a771fe18f` |
| 修正及最终受测提交 | `f03d98d67a0262b64ca72db1f97010675310a45c` |
| 增量文件 | [42-device-identity-recovery.patch](42-device-identity-recovery.patch) |
| 增量 SHA-256 | `e69c70ab8d937c8a7c42834cfd43b58b25933be7615a9b225fe8159f5ac62536` |

现场完整基线在 `/home/bimcenter/Projects/xiaozhi/.worktrees/41-firmware-online-delivery`，受测实现位于其同级的 `42-firmware-identity-recovery`，分支为 `feat/42-identity-recovery`。旧 `/home/bimcenter/Projects/ESP32-XiaoZhi-Custom` 有既有未提交内容，不作为本补丁基点。现场固件的 origin 是本地镜像；这些源码提交没有推送到第三方固件上游。

在拥有上述固定基点的干净、独立固件 checkout 中应用；没有基点时须先取得经核对的基线，不能把本补丁直接应用到服务端仓库或任意上游版本：

```bash
git rev-parse HEAD
git status --short
git apply --check /absolute/path/xiaozhi/integrations/firmware/42-device-identity-recovery.patch
git apply /absolute/path/xiaozhi/integrations/firmware/42-device-identity-recovery.patch
```

应用后的操作说明位于固件 `docs/device-identity-recovery.md`，主项目对应入口为[身份恢复说明](../../docs/development/device-identity-recovery.md)。私有 sdkconfig、依赖缓存、Flash/NVS 备份、交付 journal、设备凭据和固件镜像均不包含在补丁中。两份受测构建的配置与现场基线逐项相同，产物标识及本机测试结果见[机器摘要](../../docs/verification/evidence/2026-11-11-device-identity-retention/build-and-tests.json)。

本次 `git apply --check` 已在固定基点通过。构建需要 ESP-IDF v5.5.5 及原工程依赖；构建和主机测试命令见[验证记录](../../docs/verification/2026-11-11-device-identity-retention.md)。真实 B 只更新当前应用槽；不得使用全量 flash_args 覆盖 NVS、启动材料或资源区。
