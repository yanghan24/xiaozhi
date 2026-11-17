# Issue #12 离线证据归档（2026-11-12）

验证说明：[阶段二修复与离线验证记录](../../../2026-11-12-mcp-offline-development.md)。

本目录为合成测试报告的原字节副本，尚未提交。没有真实服务凭据、客户问题、录音或业务数据；修复前失败日志中的 secret/token 等均为脚本主动注入的合成标记，用于检验日志与公开错误是否泄露输入，不能理解为真实秘密。

| 路径 | 用途 |
| --- | --- |
| `before-run07/result.json`、`tests.txt` | 修复前60条：43通过/17失败，历史红灯不能当当前状态 |
| `local-fixed15/result.json`、`tests.txt` | 本机最终首轮75/75 |
| `local-fixed16/result.json`、`tests.txt` | 本机最终重复轮75/75，case/source与15相同 |
| `source-manifest.json` | 最终包14源码文件逐文件SHA-256，固定基线与未提交改动 |
| `archive-sha256.json` | 上述复制附件的原字节SHA-256；不含本README |

原字节可能使用Windows换行。复制后哈希可核对；Git后续检出时若换行自动转换，须区分存储/检出字节，不把转换后哈希套用为本次运行字节。源码包留在仓库外，未将二进制包纳入本目录。

Ubuntu原始报告没有复制进此目录。服务器 `/home/jt/issue12-offline-stage2-20261112/results/final-installed15/` 与 `final-installed16/` 各保留 `result.json/tests.txt`。两轮JSON现场核对SHA-256相同：`9f3e319ededf20b169e1c3f5bbd4f23cb18bf46e0d521b4bb13652a8dd5c553a`；结果各75/75，0失败/错误/跳过。该值是观察记录，不是一份本地持有的服务器报告。

## 固定版本复查

1. 以源码基线 `b448aa080ba057f8e48383adf6501daa4df32011` 和 source-manifest 中14文件确定受测范围；Git提交本身不包含本次未提交修复。
2. 最终脚本SHA-256 `a9e0fb86db34777493678edf50940f528bb0a12de5fff7480581410bf70cdac1`；最终包SHA-256 `e406aa57282b717a241c4656d7341ad607b62a79e4a6b614b7b745d74b816db9`。
3. 对附件执行SHA-256与archive-sha256对照，对受测源码与source-manifest对照。旧before-run07具有自己的source_sha256，不能用最终清单反推它的代码。
4. 使用验证文档中的stub/installed命令和全新输出目录。installed仍是合成传输，只验证安装的SDK模型兼容和本地代码；不能产生真实业务验收结论。

本次不把重复轮次加总成不同用例，不把75个方法当成75项Issue验收。真实Schema、服务语义、账号权限、远端取消及最终模型回答仍缺证据。
