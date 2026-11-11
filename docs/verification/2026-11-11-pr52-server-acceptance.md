# PR #52 合并前服务器核验

2026-11-11，Ubuntu jt@192.168.168.114。对应[双机交付、中断恢复与隔离报告](2026-11-11-two-device-delivery.md)。

## 固定版本与回归

PR服务端代码为87839aa2da1abd076702d6c191182f30402fbe73；58ec29e仅增加真机报告。
13:33重新核对API为1468eeb0ec0962e6a2862274f79471d3f7699e00，镜像xiaozhi-product-api:1468eeb。
它在PR #52基础上增加长期工具凭据、设备账号及退出、页面样式和昵称修正；不宣称实物运行在原始87839aa。

12:39:52完成独立Ubuntu/MySQL/Redis全套Java回归：349 tests，0 failures/errors/skipped，集成守卫通过。
包括16项DeviceDeliveryHttpTest、18项CustomerVoicePermissionHttpTest、6项CustomerProfileDisplayTest。
测试镜像与运行API JAR SHA-256一致：
fbb10d3d145beea3bbde9f70c3204213d52e96bdef9f1dca55098cc36bee240d。
后续版本、镜像、启动时间未变化；文档提交未改变受测源码。独立测试容器已停止，生产服务未变更。
本轮未重跑Python语音回归；原始87839aa的独立333项结果不与本次349项混称。

13:33后在Ubuntu再次通过公网HTTPS运行check-delivery-rejections.py，20/20通过，正常校验证书。
正常JSON返回鉴权拒绝；缺失/错误媒体类型返回协议400，确认873fa63修复已部署。
语音实际镜像为xiaozhi-product-voice:prompt-7f10cbd；旧文档中885918d不表示当前语音版本。

## B停播ACK

用户提供B后台标识后，于13:35只读查询dc_state_ack_history。
精确限定B、permission_version=4、audio_stopped=TRUE，不以恢复二维码后的revision=6过滤。
结果为1条，revision=5，received_at_ms=1794374372519，与设备端报告顺序一致。
ACK证明服务器收到停止声明；物理停播与另一台继续回答由设备端观察补充。
此前许可版本6的ACK不作为本次B事件证据。真实标识和SQL未提交Git。

## 结论与边界

完整交付、三处真实I/O后进程中断续作、双机凭据隔离及本台退出停播证据已补齐。
未测试Flash/NVS写入瞬间断电；A此前单次重启根因仍未定位，不宣称已修复。
不扩大为容量、所有语音场景或长期稳定性验收。

脱敏证据：
- [运行版本](evidence/2026-11-11-pr52-server-acceptance/runtime.json)
- [完整Java回归](evidence/2026-11-11-pr52-server-acceptance/regression.json)
- [B停播ACK](evidence/2026-11-11-pr52-server-acceptance/b-ack.json)

PR #52按现有目标合入codex/13-device-control-v1，不代表PR #26或main已合并，不改变线上部署。