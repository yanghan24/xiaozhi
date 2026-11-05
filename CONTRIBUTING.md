# 团队协作与安全回退

## 分支与提交

`main` 保存通过审阅和验证的集成结果。每个任务从最新的 `origin/main` 创建独立分支，例如 `feat/123-oidc-login`、`fix/124-device-binding` 或 `docs/125-ota-plan`。一个 PR 交付一项可验收的能力，以 squash 方式合并，使一次功能变更对应一个容易回退的主线提交。

多人或多个代理并行开发时，各自使用独立 clone 或 worktree，在自己的任务分支提交。共享分支上的历史通过新提交向前修正，恢复错误改动使用 `git revert`；避免改写协作者已经基于其开发的提交。

开始任务前检查工作区。已有未提交改动先保存到所属任务分支，明确属于当前任务的文件再暂存；不要用清理命令丢弃不明来源文件。

```bash
git status --short --branch
git fetch origin
git switch --no-track -c feat/123-oidc-login origin/main
```

需要独立工作目录时，在本仓库的 `.worktrees/` 下建立 worktree。该目录已被 Git 忽略；同一分支一次只在一个 worktree 中工作。

```bash
git worktree add --no-track -b feat/123-oidc-login .worktrees/123-oidc-login origin/main
```

提交应完整表达一项变更并通过对应检查。准备合并前，检查完整 diff、运行适合改动的验证，并在 PR 中写清行为、验证证据及回退影响。配置中的真实凭据、客户录音和运行数据留在版本库之外；固件和构建产物通过有校验值的发布存储管理。

```bash
git push -u origin HEAD
```

每个 PR 由仓库管理员审核，解决讨论后由管理员执行 squash 合并，合并操作即记录管理员的同意。管理员自己发起的 PR 也可以检查后自行合并，不额外要求另一位开发者批准。其他开发者可以提交分支、参与审阅，但不能更新 `main`。

代理可以提交、推送任务分支和准备 PR；合并到 `main` 前须获得管理员针对该 PR 的明确同意，即使代理使用的 GitHub 凭据属于管理员也一样。

当前仓库尚未导入应用代码，后续将真实运行的构建、测试任务加入必需检查；不要将不存在的检查名称设为合并门槛。

## 每个 clone 的本地设置

以下设置仅影响执行命令的 clone，新协作者需要各自设置。它们让普通 pull 在分叉时停止，并让普通 push 使用名称匹配的当前分支；真正的主线写入限制由 GitHub 分支保护执行。

```bash
git config --local pull.ff only
git config --local push.default simple
git config --local core.logAllRefUpdates true
```

## 撤销已经合并的功能

先确认工作区没有未保存的改动，定位需要撤销的 PR 在 `main` 上的 squash 提交。下面的 `COMMIT_SHA` 需替换为实际提交；先查看内容，再创建恢复分支。

```bash
git status --short --branch
git fetch origin
git log --oneline origin/main
git show COMMIT_SHA
git switch --no-track -c revert/123-oidc-login origin/main
git revert --no-edit COMMIT_SHA
```

`revert` 创建反向提交，保留后续协作者的历史。若发生冲突，先核对后来改动依赖了哪些行为，再解决并继续；需要退出本次操作时，使用 `git revert --abort`。恢复分支同样运行测试、推送并通过 PR 合并。

历史上若存在 merge commit，需要先确认其主线父提交，不能直接套用上述 squash 提交示例。一次回退多个功能时，先理清依赖并逐项验证，不直接覆盖整个工作区为旧快照。

## 查看旧版本与找回提交

用独立 worktree 检查旧标签，保留当前工作区。将 `RELEASE_TAG` 换成需要检查的标签，例如 `checkpoint/requirements-2026-11-05`。

```bash
git worktree add --detach .worktrees/inspect-checkpoint RELEASE_TAG
```

误操作后，可以从本地 reflog 找到旧提交，并先创建救援分支保存引用，再决定如何合回主线。

```bash
git reflog --date=iso
git show COMMIT_SHA
git branch rescue/recovered-work COMMIT_SHA
```

未提交、未被 Git 跟踪的文件不受这些恢复方法保护。reflog 属于本地且有保留期限；重要工作应及时形成提交并推送到任务分支。

## 检查点与发布

需求文档的初始检查点为 `checkpoint/requirements-2026-11-05`，指向 `792fb85`。这是文档恢复点，不是可部署的软件版本。

该初始快照包含后续已纠正的设备管理职责描述；当前职责以 [ADR-0005](docs/adr/0005-backend-owns-device-management.md) 为准。恢复检查点用于查看历史，不能将其中的旧需求直接当作当前实现依据。

发布前给通过验收的提交创建带注释的版本标签，例如 `v0.1.0`，再推送该标签。已经共享的发布标签保持指向原提交，修复版本使用新标签。每次发布记录源代码提交、上游基线、构建产物标识，以及数据库兼容条件；固件另外记录板型、版本和产物校验值。

| 恢复对象 | 恢复方式 |
| --- | --- |
| 已合并代码 | 恢复分支上 `git revert`，经测试和 PR 合并 |
| 已部署的服务 | 部署已验证版本对应的构建产物，先核对配置与数据库兼容性 |
| 数据库或外部业务状态 | 使用经过验证的数据恢复、兼容迁移或业务接口处理；Git 不撤销已执行的写入 |
| 已发布 OTA 固件 | 先暂停继续向设备提供问题版本；已升级设备按照经真机验证的固件恢复方案处理 |

数据库变更优先采用旧、新服务兼容的分阶段迁移。涉及不可逆数据变化的发布，须明确恢复数据所需条件再上线。设备自动回滚和降级仍需核实实际固件；撤销服务器代码不会让已刷入固件自动恢复。

## 上游代码集成

本项目的 `origin` 为 `yanghan24/xiaozhi`，上游来源为 `xinnan-tech/xiaozhi-esp32-server`。导入上游代码使用独立分支和 PR，记录固定的 tag 或 commit，保留许可、已有需求文档及项目配置。此后更新上游也通过专门分支验证后合并。

## GitHub 保护配置

主线使用两个 GitHub ruleset，均只作用于 `refs/heads/main`：

| 配置 | 作用 |
| --- | --- |
| [main-safety.json](.github/rulesets/main-safety.json) | 所有人通过 PR、解决讨论并 squash 合并；禁止强推和删除，要求线性历史；无绕过名单 |
| [main-admin-merge.json](.github/rulesets/main-admin-merge.json) | 限制更新，仅仓库管理员可通过 PR 合并；管理员也不能直接推送主线 |

管理员权限使用 GitHub 内置的仓库 Admin 角色（`RepositoryRole`，ID `5`），授权模式为 `pull_request`。独立的安全规则没有绕过名单，因此管理员合并时仍须遵守这些限制。GitHub 不允许批准自己发起的 PR；这里将必需的 Approve 数量设为 `0`，由管理员审核后的合并动作确认同意，不再叠加第二人的审阅门槛。仓库管理员指 GitHub 权限，与产品中的运营管理员无关。

JSON 保存配置目标，是否生效以 GitHub 设置为准。维护者应用前先读取现有 ruleset 和经典分支保护，保留已有额外限制。同名 ruleset 已存在时使用其 ID 更新；仅在不存在时创建，避免重复规则。不要为了降低审阅人数而关闭所有管理员保护。

```bash
gh api repos/yanghan24/xiaozhi/rulesets
gh api repos/yanghan24/xiaozhi/rules/branches/main
gh api repos/yanghan24/xiaozhi/branches/main/protection
```

创建或更新规则使用 [GitHub ruleset API](https://docs.github.com/en/rest/repos/rules) 的 POST 或 PUT，并通过 `--input` 传入对应 JSON。管理员授权的行为见 [仅通过 PR 绕过更新限制](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/managing-rulesets/creating-rulesets-for-a-repository#granting-bypass-permissions-for-your-branch-or-tag-ruleset)。
