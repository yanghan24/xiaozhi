# 当前受测版本：清理引用与SDK秒数兼容修复

日期：2026-11-12。**本目录是当前工作区对应的最终回归证据**，不是旧`sdk81-20261112`或`cleanup-sdk-20261112`的替名。旧目录保留各自字节，不能混用哈希/容器轮次。

## 结果与文件

| 环境 | 原运行目录/容器 | 结果 |
| --- | --- | --- |
| Windows CPython3.12.10，SDK stub | 仓库外`evidence/sdk-verified-local05` | 81/81，0失败/错误/跳过 |
| 同上重复轮 | `evidence/sdk-verified-local06` | 81/81，case/source相同 |
| Ubuntu Python3.10.22，安装MCP2.3.0 | `issue12-sdk-verified81-01`，`results/installed01` | 81/81，退出0 |
| 同上重复轮 | `issue12-sdk-verified81-02`，`results/installed02` | 81/81，case/source相同，退出0 |

Ubuntu根目录`/home/jt/issue12-sdk-verified81-20261112/`，源码`src/`。固定已有镜像`sha256:a0ce61af271cd24539236f0e069c062902bd153fb0d78cd237b28348a31e92c5`；network none、根文件系统只读、cap-drop ALL、no-new-privileges、user1000:1000，仅挂合成源码与独立结果，无真实凭据/业务数据库。两个容器均已退出，最后核对14文件0不一致。sudo缓存已清除。

安装模式新增一条实际SDK＋anyio内存流测试，证明数值秒数能得到明确SDK超时，不能写成所有会话均为替身；其余网络传输/大部分会话仍合成，该测试不连接外部。

目录文件：`local05/`与`local06/`含本机原报告/日志；`source-manifest.json`含14文件SHA-256；`archive-sha256.json`核对本地5附件。`server-observations.json`是现场统计/哈希人工摘要，另有本地哈希记录，明确不是服务器原JSON。Ubuntu原JSON/日志仍在服务器，本地未下载原件。

## 固定哈希

| 对象 | SHA-256 |
| --- | --- |
| 当前14文件包`issue12-sdk-verified81.tar.gz` | `e5b36b27d8ddb17f1ad038abf4921b6497f62eddab9f98554a01cbe418ab9916` |
| source-manifest.json | `74034812c32566f09eda845bebd9c4a9fa04eaf083037c0a678d318b63d6c8c8` |
| 当前诊断脚本 | `8faad45fdbc5b5139aa8e4d1cdb9a05bbb802d3c2da1ff6539a0421290db7612` |
| 本机两轮JSON（相同） | `92edc5ae3d8940e83e99ea61b0c592ddbf661ccba7eabaf2e101e8030cbc8d00` |
| Ubuntu两轮原JSON现场值（相同，原件未下载） | `1bbdf293608726c71967c1ad434fc34b4147e971221379d3b3747697fca2340f` |

SDK超时断言核对嵌套或顶层`code=-32001`以及准确初始化超时消息，不把任意MCPError视为通过。原75个方法全部保留；原审查wrapper的2个失败要求纳入正式测试并强化回收后重试，另补pending集合及SDK秒数/内存流检查，总数81，重复轮次不累加为不同用例。

## 历史和当前边界

- 77条75通过/2失败是修复前实际红灯，保留不覆盖。
- 中间79条本机/Ubuntu各两轮通过；增加SDK测试后第一次Ubuntu81条有1个断言失败：SDK用MCPError(-32001)包装超时，而非内建TimeoutError。依据无凭据内存流观察修正严格断言后复验。
- 本轮真实SDK客户端的bridge连接3次超时，host网络下发现成功、另一次直接工具成功。它们不是本目录81条禁网测试的一部分，见[本轮缺口修复记录](../../../2026-11-12-mcp-gap-fixes.md)。
- 清理/超时类型已修复不等于模型完整链、账号矩阵、计费/限流/远端取消验收完成；没有部署或发布。
