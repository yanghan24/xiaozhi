# PR #63 conflict integration — 2026-11-11

- Tested source: `b7126967df68fbd9a0234fc7723bbaa90ae446ef`; base: `cd79270fd19f97edaee032131084f4de2394dfc3`.
- Forward merge preserves both the mainline `device-control/v1/unbind` route and credential recovery routes. API, manager web and device-entry trees match previously deployed combination `1b50709fd1cadfad966b12d4afedf78b1f279aa6` byte for byte.
- Fresh Ubuntu regression: 35 suites, 362 tests, 0 failures, 0 errors, 0 skipped; Maven and integration-test guard exit 0. Isolated Compose project `xiaozhi-pr63-integration-b712696`; reports retained in `/home/jt/pr63-integration-b712696/.runtime/pr52/`.
- Ubuntu isolated `nginx -t`: passed with offline image `xiaozhi-offline-nginx:1.27.4-20261106` (`1ff4bb4faebc`), temporary test certificate, no published ports. Registry image fetch timed out; the existing offline image was used explicitly.
- Standards review: 0 findings. Spec review: 0 findings, assessed as the backend slice of #42.
- No production deployment, service restart or physical-device operation was performed. Current deployment remains `1b50709`.
- #42 remains open: factory reset, real OTA, abnormal identity-loss recovery, historical ownership/permission comparison, recovery-time restart and applicable interrupted retries require their own acceptance evidence. Healthy credential rotation is not abnormal-loss acceptance.

Historical implementation evidence: [backend report](2026-11-11-issue42-backend.md). This report records a new integration run and does not replace its historical counts.
