<template>
  <SpeakerPage title="用户管理">
    <template slot="tools"><el-input v-model="keyword" placeholder="搜索姓名、集市账号 ID 或用户名" clearable @keyup.enter.native="search" />
      <CustomButton type="confirm" icon="el-icon-search" @click="search">查询</CustomButton></template>
    <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
    <el-alert title="客户通过音箱扫码登录，身份与组织资料来自 AI 集市。" type="info" :closable="false" />
    <CustomTable :data="items" :columns="columns" :loading="loading" :show-operations="true" :total="total" :current-page="page" :page-size="limit"
      @page-change="changePage" @size-change="changeSize">
      <template slot="display_name" slot-scope="scope">{{ scope.row.display_name || '未提供姓名' }}</template>
      <template slot="device_count" slot-scope="scope"><el-tag :type="Number(scope.row.device_count) ? 'success' : 'info'">{{ scope.row.device_count }} 台</el-tag></template>
      <template slot="online_count" slot-scope="scope">{{ scope.row.online_count }} 台在线</template>
      <template slot="operations" slot-scope="scope"><el-button type="text" @click="open(scope.row.id)">用户详情</el-button></template>
    </CustomTable>
    <template slot="dialogs">
      <CustomDialog title="用户详情" :visible.sync="visible" width="960px" class="management-dialog" :footer="false" @close="closeDetail">
        <div v-loading="detailLoading" class="detail-content">
          <template v-if="detail">
            <section class="dialog-section">
              <h3 class="section-heading">基本资料</h3>
              <el-descriptions :column="2" border class="detail-descriptions">
                <el-descriptions-item label="姓名">{{ detail.display_name || '未提供' }}</el-descriptions-item>
                <el-descriptions-item label="集市用户名">{{ detail.username || '未提供' }}</el-descriptions-item>
                <el-descriptions-item label="账号类型">{{ detail.account_type === 'group' ? '集团账号' : detail.account_type === 'ordinary' ? '普通账号' : '未提供' }}</el-descriptions-item>
                <el-descriptions-item label="积分余额">{{ balanceText(detail) }}</el-descriptions-item>
                <el-descriptions-item label="组织关系" :span="2">{{ organizationText(detail.organization) }}</el-descriptions-item>
              </el-descriptions>
            </section>
            <section class="dialog-section">
              <h3 class="section-heading">集市关联</h3>
              <el-descriptions :column="2" border class="detail-descriptions">
                <el-descriptions-item label="集市账号 ID" :span="2">{{ detail.subject }}</el-descriptions-item>
                <el-descriptions-item label="身份来源" :span="2">{{ detail.issuer }}</el-descriptions-item>
                <el-descriptions-item label="应用授权" :span="2">{{ authorizationLabel(detail.authorization) }}</el-descriptions-item>
                <el-descriptions-item label="关联应用" :span="2">{{ detail.authorization ? detail.authorization.client_id : '未关联' }}</el-descriptions-item>
              </el-descriptions>
            </section>
            <section class="dialog-section">
              <h3 class="section-heading">同步信息</h3>
              <el-descriptions :column="2" border class="detail-descriptions">
                <el-descriptions-item label="资料更新时间">{{ displayTime(detail.profile_updated_at_ms) }}</el-descriptions-item>
                <el-descriptions-item label="余额查询时间">{{ displayTime(detail.balance_checked_at_ms) }}</el-descriptions-item>
              </el-descriptions>
            </section>
            <section class="dialog-section">
              <div class="section-toolbar"><h3 class="section-heading">绑定音箱 · {{ detail.devices.length }} 台</h3><CustomButton size="small" :loading="refreshing" @click="refresh">刷新集市资料与积分</CustomButton></div>
              <el-table :data="detail.devices" max-height="300" border>
                <el-table-column prop="name" label="音箱名称" min-width="140" />
                <el-table-column prop="device_id" label="设备标识" min-width="180" />
                <el-table-column label="在线情况" width="100"><template slot-scope="scope"><el-tag size="small" :type="scope.row.online ? 'success' : 'info'">{{ scope.row.online ? '在线' : '离线' }}</el-tag></template></el-table-column>
                <el-table-column label="最近心跳" min-width="180"><template slot-scope="scope">{{ displayTime(scope.row.last_seen_at_ms) }}</template></el-table-column>
                <el-table-column label="操作" :width="detail.authorization && detail.authorization.token_status === 'uncertain' ? 155 : 80">
                  <template slot-scope="scope">
                    <el-button v-if="detail.authorization && detail.authorization.token_status === 'uncertain' && scope.row.admin_enabled && scope.row.use_enabled"
                      type="text" :loading="reauthorizing === scope.row.device_id" @click="reauthorize(scope.row)">重新授权</el-button>
                    <el-button type="text" @click="unbind(scope.row)">解绑</el-button>
                  </template>
                </el-table-column>
              </el-table>
            </section>
          </template>
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
import { adminToken, speakerRequest, displayTime, balanceText, organizationText, requestKey } from '@/apis/speaker';
export default {
  components: { SpeakerPage, CustomTable, CustomButton, CustomDialog },
  data() { return { keyword: '', items: [], total: 0, page: 1, limit: 10, loading: false, error: '', visible: false, detail: null, detailLoading: false, refreshing: false, reauthorizing: '', detailRequest: 0,
    columns: [{ prop: 'display_name', label: '用户姓名' }, { prop: 'subject', label: '集市账号 ID', minWidth: 300 }, { prop: 'device_count', label: '绑定音箱' }, { prop: 'online_count', label: '在线音箱' }] }; },
  mounted() { this.load(); if (this.$route.query.id) this.open(this.$route.query.id); },
  methods: {
    displayTime, balanceText, organizationText,
    api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    authorizationLabel(value) {
      if (!value) return '未关联';
      if (value.token_status === 'active' && Number(value.token_retry_at_ms) > 0) return '续期暂时失败，后台会重试';
      if (value.token_status === 'config_error') return value.token_error === 'MARKET_CLIENT_CONFIGURATION'
        ? '续期失败，请管理员检查集市客户端配置' : '续期失败，请管理员检查集市授权参数';
      return { active: '已授权', refreshing: '资料授权刷新中', uncertain: '续期结果待确认，可以从绑定音箱发起重新授权', reauth_required: '需要重新授权' }[value.token_status] || '需要重新授权';
    },
    async load() { this.loading = true; this.error = ''; try { const data = await this.api(`customers?keyword=${encodeURIComponent(this.keyword)}&page=${this.page}&limit=${this.limit}`); this.items = data.items; this.total = data.total; } catch (e) { this.error = e.message; } finally { this.loading = false; } },
    search() { this.page = 1; this.load(); }, changePage(value) { this.page = value; this.load(); }, changeSize(value) { this.limit = value; this.search(); },
    closeDetail() { ++this.detailRequest; this.detail = null; this.detailLoading = false; this.refreshing = false; },
    async open(id) { const request = ++this.detailRequest; this.visible = true; this.detail = null; this.detailLoading = true; this.refreshing = false;
      try { const detail = await this.api(`customers/${encodeURIComponent(id)}`); if (request === this.detailRequest && this.visible) this.detail = detail; } catch (e) { if (request === this.detailRequest) this.$message.error(e.message); } finally { if (request === this.detailRequest) this.detailLoading = false; } },
    async refresh() { const id = this.detail.id, request = this.detailRequest; this.refreshing = true;
      try { const detail = await this.api(`customers/${id}/refresh`, {}); if (request === this.detailRequest && this.visible) this.detail = detail; } catch (e) { if (request === this.detailRequest) this.$message.error(e.message); } finally { if (request === this.detailRequest) this.refreshing = false; } },
    async reauthorize(device) {
      const customer = this.detail.id;
      try { await this.$confirm(`请用原集市账号扫描 ${device.name} 生成的二维码。设备绑定和本地提醒会保留。`, '重新授权', { type: 'info' }); }
      catch (_) { return; }
      this.reauthorizing = device.device_id;
      try {
        await this.api(`customers/${encodeURIComponent(customer)}/devices/${encodeURIComponent(device.device_id)}/reauthorization`,
          { expected_permission_version: device.permission_version });
        this.$message.success('请求已发送，请在音箱上扫码授权');
      } catch (e) { this.$message.error(e.message); }
      finally { this.reauthorizing = ''; }
    },
    async unbind(device) {
      const customer = this.detail.id;
      try { await this.$confirm(`解除 ${device.name} 与当前用户的绑定，并停止该用户在本台的使用？其他音箱不受影响。`, '解绑音箱', { type: 'warning' }); }
      catch (_) { return; }
      try { await this.api(`devices/${encodeURIComponent(device.device_id)}/unbind`, { expected_permission_version: device.permission_version, request_key: requestKey() }); if (this.visible && this.detail?.id === customer) await this.open(customer); await this.load(); }
      catch (e) { this.$message.error(e.message); }
    },
  },
};
</script>
<style lang="scss" scoped>
@import "@/styles/managementDialog.scss";
</style>
