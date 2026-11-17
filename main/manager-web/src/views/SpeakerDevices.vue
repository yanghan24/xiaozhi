<template>
  <SpeakerPage title="设备管理">
    <template slot="tools"><el-input v-if="tab === 'devices'" v-model="keyword" placeholder="搜索音箱名称或设备标识" clearable @keyup.enter.native="search" />
      <CustomButton v-if="tab === 'devices'" type="confirm" icon="el-icon-search" @click="search">查询</CustomButton>
      <CustomButton v-if="tab === 'devices'" type="add" icon="el-icon-plus" @click="registerVisible = true">登记设备</CustomButton></template>
    <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
    <el-tabs v-model="tab" class="device-tabs">
      <el-tab-pane label="音箱设备" name="devices">
        <CustomTable :data="items" :columns="columns" :loading="loading" :show-operations="true" :total="total" :current-page="page" :page-size="limit"
          @page-change="changePage" @size-change="changeSize">
          <template slot="online" slot-scope="scope"><el-tag :type="scope.row.online ? 'success' : 'info'">{{ scope.row.online ? '在线' : '离线' }}</el-tag></template>
          <template slot="firmware_version" slot-scope="scope">{{ scope.row.firmware_version || '未上报' }}</template>
          <template slot="last_seen_at_ms" slot-scope="scope">{{ displayTime(scope.row.last_seen_at_ms) }}</template>
          <template slot="operations" slot-scope="scope"><el-button type="text" @click="open(scope.row.device_id)">设备详情</el-button></template>
        </CustomTable>
      </el-tab-pane>
      <el-tab-pane label="固件管理" name="firmware" lazy><div class="firmware-panel"><ProductOtaCatalog @selected="selectFirmware" /></div></el-tab-pane>
      <el-tab-pane label="OTA 升级" name="ota" lazy><div class="firmware-panel"><el-alert v-if="!firmware" title="请先在固件管理中选择经过校验的目标固件。" :closable="false" type="info" /><ProductOtaPush :firmware="firmware" /></div></el-tab-pane>
    </el-tabs>
    <template slot="dialogs">
      <el-dialog title="设备详情" :visible.sync="visible" width="850px" @close="closeDetail">
        <div v-loading="detailLoading" v-if="detail">
          <el-descriptions :column="2" border>
            <el-descriptions-item label="设备名称">{{ detail.name }}</el-descriptions-item><el-descriptions-item label="设备标识">{{ detail.device_id }}</el-descriptions-item>
            <el-descriptions-item label="硬件型号">{{ detail.board_type || '未上报' }}</el-descriptions-item><el-descriptions-item label="固件版本">{{ detail.firmware_version || '未上报' }}</el-descriptions-item>
            <el-descriptions-item label="在线状态">{{ detail.online ? '在线' : '离线' }}</el-descriptions-item><el-descriptions-item label="最近心跳">{{ displayTime(detail.last_seen_at_ms) }}</el-descriptions-item>
            <el-descriptions-item label="归属用户"><el-button v-if="detail.customer_id" type="text" @click="goCustomer">{{ detail.customer_name || '查看用户' }}</el-button><span v-else>未绑定</span></el-descriptions-item>
            <el-descriptions-item label="管理状态">{{ detail.admin_enabled ? '已启用' : '已停用' }}</el-descriptions-item>
          </el-descriptions>
          <div class="device-actions"><CustomButton size="small" @click="rename">修改名称</CustomButton><CustomButton size="small" :type="detail.admin_enabled ? 'delete' : 'add'" @click="toggle">{{ detail.admin_enabled ? '停用音箱' : '启用音箱' }}</CustomButton></div>
          <h3>维护记录</h3><el-table :data="detail.events" max-height="250"><el-table-column label="时间"><template slot-scope="scope">{{ displayTime(scope.row.created_at_ms) }}</template></el-table-column><el-table-column label="操作"><template slot-scope="scope">{{ eventLabel(scope.row.action) }}</template></el-table-column><el-table-column label="来源"><template slot-scope="scope">{{ scope.row.actor.startsWith('admin:') ? '管理员' : scope.row.actor === 'device' ? '音箱本机' : '用户扫码' }}</template></el-table-column></el-table>
        </div>
      </el-dialog>
      <el-dialog title="登记音箱" :visible.sync="registerVisible" width="520px" @closed="credential = ''; registration = { device_id: '', name: '', board_type: '' }">
        <el-form v-if="!credential" :model="registration" label-width="100px" @submit.native.prevent="register">
          <el-form-item label="设备标识" required><el-input v-model="registration.device_id" maxlength="128" placeholder="独立且稳定的设备标识" /></el-form-item>
          <el-form-item label="设备名称" required><el-input v-model="registration.name" maxlength="128" /></el-form-item>
          <el-form-item label="硬件型号"><el-input v-model="registration.board_type" maxlength="128" /></el-form-item>
          <el-alert title="登记后还需完成设备凭据交付，音箱通过认证心跳后才显示在线。" type="info" :closable="false" />
          <CustomButton type="confirm" :loading="registering" @click="register">登记</CustomButton>
        </el-form>
        <template v-else><el-alert title="设备已登记。请妥善保存本次设备凭据，关闭后不再显示。" type="success" :closable="false" /><el-input :value="credential" type="password" show-password readonly aria-label="本次设备凭据" /></template>
      </el-dialog>
    </template>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
import CustomTable from '@/components/CustomTable.vue';
import CustomButton from '@/components/CustomButton.vue';
import ProductOtaCatalog from '@/components/ProductOtaCatalog.vue';
import ProductOtaPush from '@/components/ProductOtaPush.vue';
import { adminToken, speakerRequest, displayTime, requestKey } from '@/apis/speaker';
export default {
  components: { SpeakerPage, CustomTable, CustomButton, ProductOtaCatalog, ProductOtaPush },
  data() { return { tab: this.$route.query.tab || 'devices', keyword: '', items: [], total: 0, page: 1, limit: 10, loading: false, error: '', visible: false, detail: null, detailLoading: false, detailRequest: 0, firmware: null, registerVisible: false, registering: false, credential: '', registration: { device_id: '', name: '', board_type: '' },
    columns: [{ prop: 'name', label: '音箱名称' }, { prop: 'device_id', label: '设备标识' }, { prop: 'board_type', label: '硬件型号' }, { prop: 'firmware_version', label: '固件版本' }, { prop: 'online', label: '在线状态' }, { prop: 'last_seen_at_ms', label: '最近心跳' }] }; },
  mounted() { this.load(); if (this.$route.query.id) this.open(this.$route.query.id); },
  methods: {
    displayTime, api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    async load() { this.loading = true; this.error = ''; try { const data = await this.api(`devices?keyword=${encodeURIComponent(this.keyword)}&page=${this.page}&limit=${this.limit}`); this.items = data.items; this.total = data.total; } catch (e) { this.error = e.message; } finally { this.loading = false; } },
    search() { this.page = 1; this.load(); }, changePage(value) { this.page = value; this.load(); }, changeSize(value) { this.limit = value; this.search(); },
    closeDetail() { ++this.detailRequest; this.detail = null; this.detailLoading = false; },
    async open(id) { const request = ++this.detailRequest; this.visible = true; this.detail = null; this.detailLoading = true;
      try { const detail = await this.api(`devices/${encodeURIComponent(id)}`); if (request === this.detailRequest && this.visible) this.detail = detail; } catch (e) { if (request === this.detailRequest) this.$message.error(e.message); } finally { if (request === this.detailRequest) this.detailLoading = false; } },
    goCustomer() { this.$router.push({ path: '/speaker-users', query: { id: this.detail.customer_id } }); },
    eventLabel(value) { return { registered: '设备登记', bound: '用户绑定', resumed: '恢复使用', 'sign-out': '退出本台', unbind: '解除绑定', enable: '管理员启用', disable: '管理员停用', rename: '修改名称' }[value] || '设备状态更新'; },
    selectFirmware(value) { this.firmware = value; this.tab = 'ota'; },
    async register() { this.registering = true; try { const result = await this.api('devices', this.registration); this.credential = result.credential; await this.load(); } catch (e) { this.$message.error(e.message); } finally { this.registering = false; } },
    async rename() { const device = this.detail; let result; try { result = await this.$prompt('请输入音箱名称', '修改设备名称', { inputValue: device.name, inputValidator: value => !!value?.trim() && value.length <= 128 }); } catch (_) { return; } await this.perform('rename', result.value, device); },
    async toggle() { const device = this.detail, action = device.admin_enabled ? 'disable' : 'enable'; try { await this.$confirm(action === 'disable' ? '停用后本台不能进行语音对话，设备归属和维护连接保留。' : '启用后仍需满足用户授权和默认角色条件；用户主动退出的音箱需要重新扫码恢复。', '确认管理操作', { type: 'warning' }); } catch (_) { return; } await this.perform(action, requestKey(), device); },
    async perform(action, key, device) { try { await this.api(`devices/${encodeURIComponent(device.device_id)}/${action}`, { expected_permission_version: device.permission_version, request_key: key }); if (this.visible && this.detail?.device_id === device.device_id) await this.open(device.device_id); await this.load(); } catch (e) { this.$message.error(e.message); } },
  },
};
</script>
<style lang="scss" scoped>
.device-tabs { flex: 1; min-height: 0; display: flex; flex-direction: column;
  ::v-deep .el-tabs__content { flex: 1; min-height: 0; }
  ::v-deep .el-tab-pane { height: 100%; display: flex; flex-direction: column; } }
.firmware-panel { overflow: auto; padding: 0 4px 20px; }
.device-actions { display: flex; gap: 10px; margin: 16px 0; }
</style>
