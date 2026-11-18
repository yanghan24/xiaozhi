# 固件源码同步与 GitHub 发布

2026-11-12 用户要求：固件源码修改后及时更新 GitHub，对应固件也上传。每次固件任务在结束前完成源码推送及已有构建的上传；通过 GitHub 远端提交和下载后的校验值确认完成。

## 源码与下载入口

服务端继续使用 `yanghan24/xiaozhi` 的 `main`；完整 ESP32 固件位于同仓库独立历史的 [`firmware/p4`](https://github.com/yanghan24/xiaozhi/tree/firmware/p4) 分支。固件分支不能作为服务端变更合入 `main`。该分支包含原固件基线及项目增量，保留上游 MIT 许可；P4 集成基点为 `f639dd332dd603e3b9ec7c00bd888a97b9770baa`，来源为 `RinBoy/ESP32-XiaoZhi-Custom`。

新电脑可用 `git clone --branch firmware/p4 https://github.com/yanghan24/xiaozhi.git xiaozhi-firmware` 取得完整固件工程；复现指定版本时切换到表中的固定标签。

| 版本 | 固定源码与发布 | 使用状态 |
| --- | --- | --- |
| 当前现场烧录版 `860e045` | [源码标签](https://github.com/yanghan24/xiaozhi/tree/firmware/p4-2.2.6-860e045) · [下载](https://github.com/yanghan24/xiaozhi/releases/tag/firmware/p4-2.2.6-860e045) | B 已安装；本机解绑、换账号、完整回答等已有现场证据 |
| 恢复出厂候选 `a30f6a8` | [源码标签](https://github.com/yanghan24/xiaozhi/tree/firmware/p4-2.2.6-a30f6a8-rc1) · [下载](https://github.com/yanghan24/xiaozhi/releases/tag/firmware/p4-2.2.6-a30f6a8-rc1) | 未刷入 B；仅完成构建、主机测试和产品检查 |

两版应用版本均为 `2.2.6`，应以源码提交、应用 SHA-256 和 ELF 摘要识别。板型均为 Waveshare ESP32-P4 WiFi6 Touch LCD 4.3，使用 ESP-IDF v5.5.5，保留 `partitions/v2/16m.csv` 布局；现场物理 Flash 32 MiB 不代表采用 32 MiB 分区。

| 应用 | 字节数 | SHA-256 |
| --- | --- | --- |
| `xiaozhi-p4-2.2.6-860e045.bin` | 3,998,640 | `0947b2a879a5ed3e5390796f55997706b189707a8593383049957fbc3514db4f` |
| `xiaozhi-p4-2.2.6-a30f6a8.bin` | 4,003,072 | `b34b7ccf64ca50d2ae6052e8bdd24a5abfc6bacb50d7fca03b1c8495a36a84b7` |

两次上传均原样使用归档产物；真实 OTA 与恢复出厂尚未完成真机验收，因此均标记预发布，未设为 Latest。本次上传不修改设备镜像、后台部署或 OTA 发布安排。

## 每次固件修改的完成条件

1. 在独立固件任务分支开发。提交前核对工作区、完整差异和适用预检；推送当前任务的源码，不能仅上传服务端说明或局部补丁。现场工程的 `origin` 可能指向本地路径，应显式核对 GitHub 目标为 `yanghan24/xiaozhi`。
2. 记录干净源码提交、板型、应用版本、ESP-IDF/编译器、分区布局及实际构建输入。已有构建在同一任务内发布；构建受阻时先推送源码，明确说明未产出固件及阻塞原因。
3. 先检查源码与附件。设备独立身份、管理员登录、烧录工具授权、交付 journal 及 Flash/NVS 备份留在私有存储。服务凭据按该次用户明确授权的范围处理；不要从本次两项凭据的授权推导其他秘密也可公开。
4. 给固定源码创建新的带注释标签并推送，附件准备完成后用 `gh release create --verify-tag --notes-file ...` 上传。未完整验收使用 `--prerelease --latest=false`，并写明已经验证、尚未验证和受阻内容。后续改动使用新标签，不覆盖共享标签和既有附件。
5. 每次发布至少附应用 `.bin`、`manifest.json`、实际构建配置、`dependencies.lock`、`README.md` 与 `SHA256SUMS`；GitHub 标签的 Source code 下载提供完整受版本控制源码。配置及依赖锁通过 Release 附件管理，构建缓存与依赖目录不进入源码提交。清单将源码、应用及配置的校验值关联起来。
6. 上传后重新查询远端分支、标签及 Release，下载全部附件并执行 `sha256sum -c SHA256SUMS`，核对清单中的源码提交、应用大小及 SHA-256。回复用户源码和固件的实际链接。

`firmware/p4` 当前指向最新候选 `a30f6a8`。后续从该分支建立固件任务分支并及时推送；候选完成相应审查和验证后，再向前推进固件集成分支。服务端 `main` 的管理员 PR 合并要求继续按 `CONTRIBUTING.md` 执行。

## 当前附件及验证范围

每份 Release 的应用文件仅包含产品应用，不包含整机首次交付所需的启动、分区和设备独立身份。只对已有正确布局及身份的设备执行受控应用升级；USB 维护先核对活动槽并备份，保留 NVS、启动材料与 assets，使用完整 Flash 擦除不能作为该升级包的安装方式。

`sdkconfig.build` 与 `dependencies.lock` 是这两份归档产物的实际输入。重构建时使用 ESP-IDF v5.5.5，将 `sdkconfig.build` 放回固件根目录作为 `sdkconfig`，保留 `ESP_IDF_VERSION=5.5` 并使用依赖锁；组件下载仍需可用服务。跨机器重构建可能产生不同 ELF 和应用摘要。

现有镜像编译了高德 Key 和遥测 HTTP Token。用户在知晓 GitHub 公开上传的情况下明确允许保留这两项，因此本次原始应用及构建配置随 Release 公开，文档和清单不重复列出凭据值。设备独立身份未加入附件。

打包时两份构建均通过 10/10 产品检查，应用与现场归档摘要一致。此前 28/28 CTest、43/43 Python 主机测试保留原验证范围；本次没有改动运行代码、重构建、连接串口或重跑后端测试。现有现场证据见[当前烧录版记录](../verification/2026-11-11-device-local-unbind-firmware.md)；候选证据见[固定提交下的本机准备报告](https://github.com/yanghan24/xiaozhi/blob/b693d642abbc9a51e61826776d3a6f11cc91043f/docs/verification/2026-11-12-p4-maintenance-preparation.md)。

上传后已重新查询 GitHub 分支、带注释标签和两份公开预发布，并分别下载全部 6 个附件。下载文件与本机打包文件逐字节一致，两份 `sha256sum -c SHA256SUMS` 均通过，清单中的源码、应用大小与摘要匹配。脱敏[发布检查结果](../verification/evidence/2026-11-12-firmware-github-publishing.json)仅记录版本、附件标识与检查结果。
