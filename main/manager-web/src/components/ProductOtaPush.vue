<template>
  <section class="ota-push" aria-label="在线设备 OTA 推送">
    <h2>在线设备 · 单台推送</h2>
    <p>在线状态以当前有效产品会话为准。选择设备后检查升级工具与硬件兼容性；离线设备不会等待上线补发。</p>
    <p v-if="error" role="alert" class="error">{{ error }}</p>
    <button :disabled="busy" @click="load">刷新设备与结果</button>
    <p v-if="!loading && !devices.length">当前页没有产品设备。</p>
    <div class="table-scroll" v-if="devices.length">
      <table>
        <thead><tr><th>选择</th><th>设备</th><th>当前可用状态</th><th>检查时间</th></tr></thead>
        <tbody><tr v-for="device in devices" :key="device.device_id">
          <td><input type="radio" name="ota-device" :data-test="'device-' + device.device_id" :aria-label="'选择设备 ' + device.device_id" :value="device.device_id" v-model="deviceId" :disabled="busy || !selectable(device)" @change="clearPreview" /></td>
          <td>{{ device.display_name || device.device_id }}<br><code>{{ device.device_id }}</code></td>
          <td>{{ label(device.status) }}<br>{{ device.reason ? label(device.reason) : '' }}</td>
          <td>{{ time(device.checked_at_ms) }}</td>
        </tr></tbody>
      </table>
    </div>
    <div class="pagination">
      <button data-test="previous-page" :disabled="busy || page <= 1" @click="changePage(-1)">上一页设备</button>
      <span>第 {{ page }} 页 · 共 {{ total }} 台</span>
      <button data-test="next-page" :disabled="busy || page * limit >= total" @click="changePage(1)">下一页设备</button>
    </div>
    <p>目标固件：<strong>{{ firmware ? firmware.app_version : '请先在上方目录选择已校验固件' }}</strong><span v-if="firmware"> · ELF <code>{{ firmware.elf_sha256 }}</code></span></p>
    <button data-test="preview" :disabled="busy || !deviceId || !firmware || blocked(deviceId)" @click="previewDevice">检查单台兼容性</button>
    <p v-if="deviceId && blocked(deviceId)">本台已有待核对的操作。请查看结果，不要重复推送。</p>
    <div v-if="preview" class="preview" role="status">
      <h3>推送预览 · {{ preview.device_id }}</h3>
      <p>当前：{{ preview.current_version || '未知' }} · ELF <code>{{ preview.current_elf_sha256 || '未知' }}</code></p>
      <p>目标：{{ preview.target_version || firmware.app_version }} · ELF <code>{{ preview.target_elf_sha256 || firmware.elf_sha256 }}</code></p>
      <p v-if="!preview.eligible" class="error">不可推送：{{ label(preview.reason) }}</p>
      <template v-else>
        <p class="warning">本次会立即升级并重启，可能中断正在进行的语音或播放，不会自动等待空闲。设备接受命令不等于安装成功。</p>
        <p>预览有效至 {{ time(preview.expires_at_ms) }}，发送前服务器会再次检查当前会话与许可。</p>
        <label><input data-test="acknowledge" type="checkbox" v-model="acknowledged" :disabled="busy" /> 我确认只向上述设备推送所选固件，并接受立即中断与重启。</label>
        <button data-test="confirm" :disabled="busy || !acknowledged" @click="confirmPush">确认立即推送</button>
      </template>
    </div>
    <h3>逐台结果</h3>
    <p>命令传输结果与实际运行固件证据分别记录。结果未知时请核对设备，不会自动重发。</p>
    <p v-if="uncertain" role="alert" class="warning">{{ uncertain.device_id }}：请求响应丢失，结果未知。请刷新结果核对，切勿重新推送。请求号：<code>{{ uncertain.request_key }}</code></p>
    <p v-if="!operations.length">暂无推送记录。</p>
    <div class="table-scroll" v-else>
      <table aria-label="OTA 操作结果">
        <thead><tr><th>设备 / 操作号</th><th>固定固件</th><th>传输结果</th><th>运行产物观察</th><th>更新时间</th><th>操作</th></tr></thead>
        <tbody><tr v-for="operation in operations" :key="operation.operation_id">
          <td>{{ operation.device_id }}<br><code>{{ operation.operation_id }}</code></td>
          <td><code>{{ operation.firmware_id }}</code></td>
          <td>{{ label(operation.status) }}<br>{{ operation.reason ? label(operation.reason) : '' }}</td>
          <td>{{ observation(operation.observation_status) }}</td>
          <td>{{ time(operation.updated_at_ms || operation.created_at_ms) }}</td>
          <td><button :disabled="busy" @click="refreshOperation(operation.operation_id)">刷新结果</button></td>
        </tr></tbody>
      </table>
    </div>
  </section>
</template>

<script>
import { getServiceUrl } from '@/apis/api';
export default {
  props: { firmware: { type: Object, default: null } },
  data() { return { devices: [], operations: [], deviceId: '', page: 1, limit: 50, total: 0, loading: false, previewing: false, sending: false, error: '', preview: null, acknowledged: false, requestKey: '', uncertain: null }; },
  computed: { busy() { return this.loading || this.previewing || this.sending; } },
  watch: { firmware() { this.clearPreview(); } },
  mounted() { this.load(); },
  methods: {
    label(value) {
      return ({
        current: '在线 · 待检查工具与兼容性', ready: '可推送', eligible: '可推送',
        offline: '离线 · 当前无产品会话', no_unique_session: '存在多个会话，无法安全推送',
        permission_unavailable: '当前许可不可用', mcp_initializing: '升级工具初始化中',
        upgrade_tool_unavailable: '当前固件不支持升级工具', bridge_unavailable: '设备状态服务暂不可用',
        already_running: '设备已运行同一固件，无需重装', hardware_unknown: '缺少可信硬件信息',
        missing_system_info: '缺少可信硬件信息', incompatible_board: '设备板型不兼容',
        incompatible_revision: '芯片修订版本不兼容', incompatible_layout: '分区布局或容量不兼容',
        operation_pending: '本台已有待核对操作', session_changed: '设备会话已变化，请重新检查',
        validating: '检查中', dispatch_reserved: '下发已预留 · 待核对',
        accepted_unconfirmed: '已接受 · 安装待核验', result_unknown: '结果未知 · 禁止重发',
        not_sent: '未发送', failed: '失败', result_requires_verification: '结果待核对'
      })[value] || (value ? '状态待核对（' + value + '）' : '原因待核对');
    },
    observation(value) { return value === 'target_running' ? '已观察到目标固件运行（新会话证据，不改变传输记录）' : '尚无目标运行证据'; },
    blocked(id) { return (this.uncertain && this.uncertain.device_id === id) || this.operations.some(item => item.device_id === id && item.observation_status !== 'target_running' && !['not_sent', 'failed'].includes(item.status)); },
    time(value) { return value ? new Date(Number(value)).toLocaleString() : '尚未检查'; },
    selectable(device) { return ['current', 'ready', 'eligible'].includes(device.status); },
    clearPreview() { this.preview = null; this.acknowledged = false; this.requestKey = ''; },
    async request(path, body) {
      const token = JSON.parse(this.$store.getters.getToken || '{}').token;
      const response = await fetch(`${getServiceUrl()}/admin/product-ota/${path}`, {
        method: body ? 'POST' : 'GET', cache: 'no-store',
        headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token || ''}` },
        body: body ? JSON.stringify(body) : undefined
      });
      const result = await response.json();
      if (!response.ok || result.code !== 0) {
        if ([401, 403].includes(response.status)) throw new Error('需要有效的运营管理员登录权限。');
        throw new Error('暂时无法完成请求，请刷新状态后核对。');
      }
      return result.data;
    },
    async load() {
      if (this.busy) return;
      this.loading = true; this.error = ''; this.clearPreview(); this.deviceId = '';
      const results = await Promise.allSettled([this.request(`devices?page=${this.page}&limit=${this.limit}`), this.request('operations')]);
      if (results[0].status === 'fulfilled') { this.devices = results[0].value.items; this.total = Number(results[0].value.total); }
      else { this.devices = []; this.error = results[0].reason.message; }
      if (results[1].status === 'fulfilled') {
        this.operations = results[1].value;
        if (this.uncertain && this.operations.some(item => item.request_key === this.uncertain.request_key && item.device_id === this.uncertain.device_id)) this.uncertain = null;
      }
      else this.error = results[1].reason.message;
      this.loading = false;
    },
    async changePage(delta) { this.page += delta; await this.load(); },
    async previewDevice() {
      if (this.busy || !this.deviceId || !this.firmware || this.blocked(this.deviceId)) return;
      this.previewing = true; this.error = ''; this.clearPreview();
      const deviceId = this.deviceId, firmwareId = this.firmware.firmware_id;
      try {
        const preview = await this.request('preview', { device_id: deviceId, firmware_id: firmwareId });
        if (this.deviceId === deviceId && this.firmware && this.firmware.firmware_id === firmwareId) this.preview = preview;
      }
      catch (error) { this.error = error.message; }
      finally { this.previewing = false; }
    },
    async confirmPush() {
      if (this.busy || !this.preview || !this.preview.eligible || !this.acknowledged || this.blocked(this.deviceId)) return;
      if (Number(this.preview.expires_at_ms) <= Date.now()) { this.clearPreview(); this.error = '预览已过期，请重新检查设备。'; return; }
      this.sending = true; this.error = '';
      const deviceId = this.deviceId;
      let requestKey = '';
      try {
        if (!this.requestKey) this.requestKey = Array.from(crypto.getRandomValues(new Uint8Array(16)), value => value.toString(16).padStart(2, '0')).join('');
        requestKey = this.requestKey;
        const operation = await this.request('operations', { preview_id: this.preview.preview_id, request_key: requestKey });
        this.operations = [operation, ...this.operations.filter(item => item.operation_id !== operation.operation_id)];
      } catch (error) {
        this.uncertain = { device_id: deviceId, request_key: requestKey };
        this.error = '未能确认推送结果。请刷新结果核对；不会自动重发。';
      } finally { this.clearPreview(); this.sending = false; }
    },
    async refreshOperation(id) {
      if (this.busy) return;
      this.loading = true; this.error = '';
      try { const operation = await this.request(`operations/${encodeURIComponent(id)}`); this.operations = this.operations.map(item => item.operation_id === id ? operation : item); }
      catch (error) { this.error = error.message; }
      finally { this.loading = false; }
    }
  }
};
</script>

<style scoped>
.ota-push { margin-top: 24px; padding: 24px; background: white; border-radius: 12px; color: #303133; }
p, label { line-height: 1.6; }
button { padding: 8px 14px; margin: 8px 8px 8px 0; cursor: pointer; }
button:disabled { cursor: default; }
.table-scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; text-align: left; }
td, th { padding: 12px; border-bottom: 1px solid #ebeef5; vertical-align: top; line-height: 1.6; }
code { overflow-wrap: anywhere; font-size: 12px; }
.preview { margin-top: 16px; border: 1px solid #dcdfe6; border-radius: 8px; padding: 16px; }
.preview label { display: block; }
.error { color: #b42318; }
.warning { background: #fff7e6; padding: 12px; border-radius: 6px; color: #8a5100; }
</style>
