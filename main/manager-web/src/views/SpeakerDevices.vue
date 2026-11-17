<template>
  <SpeakerPage title="设备管理" :show-title="false">
    <el-tabs v-model="tab" class="device-tabs" @tab-click="changeTab">
      <el-tab-pane label="设备列表" name="devices">
        <div class="device-toolbar">
          <el-input v-model="keyword" placeholder="搜索音箱名称或设备标识" clearable @keyup.enter.native="search" />
          <CustomButton type="confirm" icon="el-icon-search" @click="search">查询</CustomButton>
          <CustomButton type="add" icon="el-icon-plus" @click="registerVisible = true">登记设备</CustomButton>
        </div>
        <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
        <CustomTable :data="items" :columns="columns" :loading="loading" :show-operations="true" :total="total" :current-page="page" :page-size="limit"
          @page-change="changePage" @size-change="changeSize">
          <template slot="online" slot-scope="scope"><el-tag :type="scope.row.online ? 'success' : 'info'">{{ scope.row.online ? '在线' : '离线' }}</el-tag></template>
          <template slot="firmware_version" slot-scope="scope">{{ scope.row.firmware_version || '未上报' }}</template>
          <template slot="last_seen_at_ms" slot-scope="scope">{{ displayTime(scope.row.last_seen_at_ms) }}</template>
          <template slot="operations" slot-scope="scope"><el-button type="text" @click="open(scope.row.device_id)">设备详情</el-button></template>
        </CustomTable>
      </el-tab-pane>
      <el-tab-pane label="固件管理" name="firmware" lazy><OtaManagement embedded /></el-tab-pane>
    </el-tabs>
    <template slot="dialogs">
      <CustomDialog title="设备详情" :visible.sync="visible" width="900px" class="management-dialog" :footer="false" @close="closeDetail">
        <div v-loading="detailLoading" class="detail-content">
          <template v-if="detail">
            <section class="dialog-section">
              <h3 class="section-heading">设备信息</h3>
              <el-descriptions :column="2" border class="detail-descriptions">
                <el-descriptions-item label="设备名称">{{ detail.name }}</el-descriptions-item>
                <el-descriptions-item label="设备标识">{{ detail.device_id }}</el-descriptions-item>
                <el-descriptions-item label="硬件型号">{{ detail.board_type || '未上报' }}</el-descriptions-item>
                <el-descriptions-item label="固件版本">{{ detail.firmware_version || '未上报' }}</el-descriptions-item>
              </el-descriptions>
            </section>
            <section class="dialog-section">
              <h3 class="section-heading">运行与归属</h3>
              <el-descriptions :column="2" border class="detail-descriptions">
                <el-descriptions-item label="在线状态"><el-tag size="small" :type="detail.online ? 'success' : 'info'">{{ detail.online ? '在线' : '离线' }}</el-tag></el-descriptions-item>
                <el-descriptions-item label="管理状态"><el-tag size="small" :type="detail.admin_enabled ? 'success' : 'info'">{{ detail.admin_enabled ? '已启用' : '已停用' }}</el-tag></el-descriptions-item>
                <el-descriptions-item label="最近心跳" :span="2">{{ displayTime(detail.last_seen_at_ms) }}</el-descriptions-item>
                <el-descriptions-item label="归属用户" :span="2"><el-button v-if="detail.customer_id" type="text" @click="goCustomer">{{ detail.customer_name || '查看用户' }}</el-button><span v-else>未绑定</span></el-descriptions-item>
              </el-descriptions>
              <div class="detail-actions"><CustomButton size="small" @click="rename">修改名称</CustomButton><CustomButton size="small" :type="detail.admin_enabled ? 'delete' : 'add'" @click="toggle">{{ detail.admin_enabled ? '停用音箱' : '启用音箱' }}</CustomButton></div>
            </section>
            <section class="dialog-section">
              <h3 class="section-heading">维护记录</h3>
              <el-table :data="detail.events" max-height="250" border>
                <el-table-column label="时间" min-width="180"><template slot-scope="scope">{{ displayTime(scope.row.created_at_ms) }}</template></el-table-column>
                <el-table-column label="操作"><template slot-scope="scope">{{ eventLabel(scope.row.action) }}</template></el-table-column>
                <el-table-column label="来源"><template slot-scope="scope">{{ scope.row.actor.startsWith('admin:') ? '管理员' : scope.row.actor === 'device' ? '音箱本机' : '用户扫码' }}</template></el-table-column>
              </el-table>
            </section>
          </template>
        </div>
      </CustomDialog>
      <CustomDialog
        title="登记音箱" :visible.sync="registerVisible" width="600px" class="management-dialog"
        :footer="!credential" confirm-text="登记" :confirm-loading="registering"
        @confirm="register" @close="clearRegistration"
      >
        <el-form v-if="!credential" :model="registration" label-width="100px" class="management-form" @submit.native.prevent="register">
          <section class="dialog-section">
            <h3 class="section-heading">设备信息</h3>
            <el-form-item label="设备标识" required>
              <el-input v-model="registration.device_id" maxlength="128" placeholder="独立且稳定的设备标识" />
            </el-form-item>
            <el-form-item label="设备名称" required><el-input v-model="registration.name" maxlength="128" placeholder="请输入设备名称" /></el-form-item>
            <el-form-item label="硬件型号" class="last-form-item"><el-input v-model="registration.board_type" maxlength="128" placeholder="请输入硬件型号（选填）" /></el-form-item>
          </section>
          <el-alert title="登记后还需完成设备凭据交付，音箱通过认证心跳后才显示在线。" type="info" :closable="false" show-icon />
        </el-form>
        <div v-else class="credential-result">
          <el-alert title="设备已登记" description="请妥善保存本次设备凭据，关闭后不再显示。" type="success" :closable="false" show-icon />
          <div class="credential-result-field">
            <label for="registered-device-credential">本次设备凭据</label>
            <el-input id="registered-device-credential" :value="credential" type="password" show-password readonly aria-label="本次设备凭据" />
          </div>
        </div>
      </CustomDialog>
    </template>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
import CustomTable from '@/components/CustomTable.vue';
import CustomButton from '@/components/CustomButton.vue';
import CustomDialog from '@/components/CustomDialog.vue';
import OtaManagement from './OtaManagement.vue';
import { adminToken, speakerRequest, displayTime, requestKey } from '@/apis/speaker';
export default {
  components: { SpeakerPage, CustomTable, CustomButton, CustomDialog, OtaManagement },
  data() { return { tab: this.$route.query.tab === 'firmware' ? 'firmware' : 'devices', keyword: '', items: [], total: 0, page: 1, limit: 10, loading: false, error: '', visible: false, detail: null, detailLoading: false, detailRequest: 0, registerVisible: false, registering: false, credential: '', registration: { device_id: '', name: '', board_type: '' },
    columns: [{ prop: 'name', label: '音箱名称' }, { prop: 'device_id', label: '设备标识' }, { prop: 'board_type', label: '硬件型号' }, { prop: 'firmware_version', label: '固件版本' }, { prop: 'online', label: '在线状态' }, { prop: 'last_seen_at_ms', label: '最近心跳' }] }; },
  mounted() { this.load(); if (this.$route.query.id) this.open(this.$route.query.id); },
  watch: { '$route.query.tab'(value) { this.tab = value === 'firmware' ? 'firmware' : 'devices'; } },
  methods: {
    changeTab() { if (this.$route.query.tab !== this.tab) this.$router.push({ query: { ...this.$route.query, tab: this.tab } }); },
    displayTime, api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    async load() { this.loading = true; this.error = ''; try { const data = await this.api(`devices?keyword=${encodeURIComponent(this.keyword)}&page=${this.page}&limit=${this.limit}`); this.items = data.items; this.total = data.total; } catch (e) { this.error = e.message; } finally { this.loading = false; } },
    search() { this.page = 1; this.load(); }, changePage(value) { this.page = value; this.load(); }, changeSize(value) { this.limit = value; this.search(); },
    closeDetail() { ++this.detailRequest; this.detail = null; this.detailLoading = false; },
    async open(id) { const request = ++this.detailRequest; this.visible = true; this.detail = null; this.detailLoading = true;
      try { const detail = await this.api(`devices/${encodeURIComponent(id)}`); if (request === this.detailRequest && this.visible) this.detail = detail; } catch (e) { if (request === this.detailRequest) this.$message.error(e.message); } finally { if (request === this.detailRequest) this.detailLoading = false; } },
    goCustomer() { this.$router.push({ path: '/speaker-users', query: { id: this.detail.customer_id } }); },
    eventLabel(value) { return { registered: '设备登记', bound: '用户绑定', resumed: '恢复使用', 'sign-out': '退出本台', unbind: '解除绑定', enable: '管理员启用', disable: '管理员停用', rename: '修改名称' }[value] || '设备状态更新'; },
    clearRegistration() { this.credential = ''; this.registration = { device_id: '', name: '', board_type: '' }; },
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
.device-toolbar { display: flex; justify-content: flex-end; align-items: center; gap: 10px; padding-bottom: 16px;
  .el-input { width: 240px; } }
@import "@/styles/managementDialog.scss";
</style>
