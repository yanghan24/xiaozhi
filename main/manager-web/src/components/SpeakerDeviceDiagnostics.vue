<template>
  <section class="dialog-section" v-loading="busy">
    <div class="diagnostics-header">
      <h3 class="section-heading">设备诊断</h3>
      <CustomButton size="small" icon="el-icon-refresh" :disabled="busy" @click="load()">刷新日志</CustomButton>
    </div>
    <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
    <template v-if="diagnostics">
      <div class="diagnostics-header">
        <span id="speaker-diagnostic-switch">上传诊断日志</span>
        <el-switch
          :value="enabled" :disabled="busy || (!diagnostics.supported && !enabled)"
          aria-labelledby="speaker-diagnostic-switch" @change="load({ enabled: $event })"
        />
      </div>
      <p v-if="enabled" class="diagnostics-note">开启至 {{ displayTime(diagnostics.until_ms) }}。音箱将在下次心跳后开始上传诊断。</p>
      <p v-else-if="diagnostics.supported" class="diagnostics-note">开启后 30 分钟自动关闭。</p>
      <p v-else class="diagnostics-note">音箱尚未上报诊断支持，请升级固件并等待心跳。</p>
      <el-table :data="rows" border max-height="290" empty-text="暂无诊断日志">
        <el-table-column label="接收时间" min-width="165">
          <template slot-scope="{ row }">{{ displayTime(row.receivedAt) }}</template>
        </el-table-column>
        <el-table-column label="轮次耗时" min-width="140">
          <template slot-scope="{ row }">
            <el-tooltip v-if="row.trace_id" :content="row.trace_id" placement="top">
              <span>{{ row.elapsed_ms }} ms · {{ row.trace_id.slice(0, 8) }}</span>
            </el-tooltip>
          </template>
        </el-table-column>
        <el-table-column label="事件" min-width="200">
          <template slot-scope="{ row }">{{ describe(row) }}</template>
        </el-table-column>
        <el-table-column label="开机后秒数" width="108">
          <template slot-scope="{ row }">{{ (row.uptime_ms / 1000).toFixed(1) }}</template>
        </el-table-column>
        <el-table-column label="可用内存 / 最低值" min-width="132">
          <template slot-scope="{ row }">
            <span v-if="row.event !== 'display_underrun'">{{ kib(row.free_heap_bytes) }} / {{ kib(row.min_free_heap_bytes) }} KiB</span>
          </template>
        </el-table-column>
        <el-table-column label="固件版本" min-width="105">
          <template slot-scope="{ row }">
            <el-tooltip :content="row.appSha" placement="top"><span>{{ row.firmware }}</span></el-tooltip>
          </template>
        </el-table-column>
      </el-table>
    </template>
  </section>
</template>

<script>
import CustomButton from '@/components/CustomButton.vue';
import { adminToken, speakerRequest, displayTime } from '@/apis/speaker';

const states = ['未知', '启动', '配网', '待机', '连接中', '监听', '播报', '升级', '激活', '音频测试', '致命错误'];
const resetReasons = ['未知', '上电', '外部复位', '软件重启', '异常崩溃', '中断看门狗', '任务看门狗', '其他看门狗', '深睡唤醒', '欠压', 'SDIO', 'USB', 'JTAG', 'eFuse 错误', '电源毛刺', 'CPU 锁死'];
const labels = {
  voice_network_error: '语音连接异常',
  ota_check_failed: 'OTA 检查失败',
  ota_upgrade_failed: 'OTA 安装失败',
  heartbeat_failed: '心跳失败',
};
const timingPhases = {
  wake: '唤醒', channel_open: '语音连接就绪', listening: '开始收音', first_mic_frame: '首次上行音频',
  stt_received: '收到识别结果', first_audio_received: '收到首个回复音频包',
  first_output: '首次写入音频输出', playback_drained: '播报队列播完',
};
const displayModes = { idle: '非对话', connecting: '连接中', listening: '收音', waiting: '等待回复', speaking: '播报' };

export default {
  components: { CustomButton },
  props: { deviceId: { type: String, required: true } },
  data: () => ({ busy: false, error: '', diagnostics: null, enabled: false, timer: null, request: null }),
  computed: {
    rows() {
      return (this.diagnostics?.logs || []).flatMap(batch => batch.entries.slice().reverse().map(entry => ({
        ...entry, receivedAt: batch.received_at_ms, appSha: batch.app_sha256, firmware: batch.firmware_version,
      })));
    },
  },
  mounted() { this.load(); },
  beforeDestroy() {
    clearTimeout(this.timer);
    this.request?.abort();
  },
  methods: {
    displayTime,
    kib(bytes) { return Math.round(bytes / 1024); },
    describe(row) {
      if (row.event === 'voice_timing') return timingPhases[row.phase] || row.phase;
      if (row.event === 'display_underrun') {
        return `LCD 扫描缺帧 ${row.underrun_count} 次 · ${displayModes[row.display_mode]} · 波带${row.animation_active ? '播放中' : '静止'}`;
      }
      const state = states[row.state] || '未知';
      if (row.event === 'boot') {
        const reason = resetReasons[row.code] || `复位码 ${row.code}`;
        return row.state ? `启动 · ${reason}，重启前记录：${state}` : `启动 · ${reason}`;
      }
      if (row.event === 'state') return `状态切换 · ${state}`;
      return `${labels[row.event] || row.event} · ${state}${row.code ? `（${row.code}）` : ''}`;
    },
    async load(body) {
      if (this.busy) return;
      const request = new AbortController();
      this.request = request;
      this.busy = true;
      this.error = '';
      try {
        const result = await speakerRequest(adminToken(this.$store),
          `devices/${encodeURIComponent(this.deviceId)}/diagnostics`, body, request.signal);
        if (request.signal.aborted) return;
        this.diagnostics = result;
        this.enabled = result.enabled;
        clearTimeout(this.timer);
        if (result.enabled) {
          this.timer = setTimeout(() => { this.enabled = false; }, Math.max(0, result.until_ms - result.server_time_ms));
        }
      } catch (error) {
        if (!request.signal.aborted) this.error = error.message;
      } finally {
        if (!request.signal.aborted) this.busy = false;
      }
    },
  },
};
</script>

<style lang="scss" scoped>
@import "@/styles/managementDialog.scss";
.diagnostics-header { display: flex; align-items: center; justify-content: space-between; gap: 12px; margin-bottom: 10px; }
.diagnostics-header .section-heading { margin-bottom: 0; }
.diagnostics-note { color: #606266; margin: 0 0 12px; font-size: 13px; }
</style>
