<template>
  <SpeakerPage title="用户管理">
    <template slot="tools"><el-input v-model="keyword" placeholder="搜索集市姓名、用户名或用户标识" clearable @keyup.enter.native="search" />
      <CustomButton type="confirm" icon="el-icon-search" @click="search">查询</CustomButton></template>
    <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
    <el-alert title="客户通过音箱扫码登录，身份与组织资料来自 AI 集市。" type="info" :closable="false" />
    <CustomTable :data="items" :columns="columns" :loading="loading" :show-operations="true" :total="total" :current-page="page" :page-size="limit"
      @page-change="changePage" @size-change="changeSize">
      <template slot="display_name" slot-scope="scope">{{ scope.row.display_name || scope.row.username || '未提供姓名' }}</template>
      <template slot="device_count" slot-scope="scope"><el-tag :type="Number(scope.row.device_count) ? 'success' : 'info'">{{ scope.row.device_count }} 台</el-tag></template>
      <template slot="online_count" slot-scope="scope">{{ scope.row.online_count }} 台在线</template>
      <template slot="operations" slot-scope="scope"><el-button type="text" @click="open(scope.row.id)">用户详情</el-button></template>
    </CustomTable>
    <template slot="dialogs">
      <el-dialog title="用户详情" :visible.sync="visible" width="900px" @close="closeDetail">
        <div v-loading="detailLoading">
          <template v-if="detail">
            <el-descriptions :column="2" border>
              <el-descriptions-item label="姓名">{{ detail.display_name || '未提供' }}</el-descriptions-item>
              <el-descriptions-item label="集市用户名">{{ detail.username || '未提供' }}</el-descriptions-item>
              <el-descriptions-item label="账号类型">{{ detail.account_type === 'group' ? '集团账号' : detail.account_type === 'ordinary' ? '普通账号' : '未提供' }}</el-descriptions-item>
              <el-descriptions-item label="积分余额">{{ balanceText(detail) }}</el-descriptions-item>
              <el-descriptions-item label="组织关系" :span="2">{{ organizationText(detail.organization) }}</el-descriptions-item>
              <el-descriptions-item label="集市用户标识" :span="2">{{ detail.subject }}</el-descriptions-item>
              <el-descriptions-item label="身份来源" :span="2">{{ detail.issuer }}</el-descriptions-item>
              <el-descriptions-item label="资料更新时间">{{ displayTime(detail.profile_updated_at_ms) }}</el-descriptions-item>
              <el-descriptions-item label="余额查询时间">{{ displayTime(detail.balance_checked_at_ms) }}</el-descriptions-item>
              <el-descriptions-item label="应用授权">{{ authorizationLabel(detail.authorization) }}</el-descriptions-item>
              <el-descriptions-item label="关联应用">{{ detail.authorization ? detail.authorization.client_id : '未关联' }}</el-descriptions-item>
            </el-descriptions>
            <div class="section-header"><h3>绑定音箱 · {{ detail.devices.length }} 台</h3><CustomButton size="small" :loading="refreshing" @click="refresh">刷新集市资料与积分</CustomButton></div>
            <el-table :data="detail.devices" max-height="300" border>
              <el-table-column prop="name" label="音箱名称" />
              <el-table-column prop="device_id" label="设备标识" />
              <el-table-column label="在线情况"><template slot-scope="scope"><el-tag :type="scope.row.online ? 'success' : 'info'">{{ scope.row.online ? '在线' : '离线' }}</el-tag></template></el-table-column>
              <el-table-column label="最近心跳"><template slot-scope="scope">{{ displayTime(scope.row.last_seen_at_ms) }}</template></el-table-column>
              <el-table-column label="操作"><template slot-scope="scope"><el-button type="text" @click="unbind(scope.row)">解绑</el-button></template></el-table-column>
            </el-table>
          </template>
        </div>
      </el-dialog>
    </template>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
import CustomTable from '@/components/CustomTable.vue';
import CustomButton from '@/components/CustomButton.vue';
import { adminToken, speakerRequest, displayTime, balanceText, organizationText, requestKey } from '@/apis/speaker';
export default {
  components: { SpeakerPage, CustomTable, CustomButton },
  data() { return { keyword: '', items: [], total: 0, page: 1, limit: 10, loading: false, error: '', visible: false, detail: null, detailLoading: false, refreshing: false, detailRequest: 0,
    columns: [{ prop: 'display_name', label: '用户姓名' }, { prop: 'username', label: '集市用户名' }, { prop: 'device_count', label: '绑定音箱' }, { prop: 'online_count', label: '在线音箱' }] }; },
  mounted() { this.load(); if (this.$route.query.id) this.open(this.$route.query.id); },
  methods: {
    displayTime, balanceText, organizationText,
    api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    authorizationLabel(value) { if (!value) return '未关联'; return value.token_status === 'active' ? '已授权' : value.credential_status === 'active' ? '调用凭据可用，资料授权待确认' : '需要重新授权'; },
    async load() { this.loading = true; this.error = ''; try { const data = await this.api(`customers?keyword=${encodeURIComponent(this.keyword)}&page=${this.page}&limit=${this.limit}`); this.items = data.items; this.total = data.total; } catch (e) { this.error = e.message; } finally { this.loading = false; } },
    search() { this.page = 1; this.load(); }, changePage(value) { this.page = value; this.load(); }, changeSize(value) { this.limit = value; this.search(); },
    closeDetail() { ++this.detailRequest; this.detail = null; this.detailLoading = false; this.refreshing = false; },
    async open(id) { const request = ++this.detailRequest; this.visible = true; this.detail = null; this.detailLoading = true; this.refreshing = false;
      try { const detail = await this.api(`customers/${encodeURIComponent(id)}`); if (request === this.detailRequest && this.visible) this.detail = detail; } catch (e) { if (request === this.detailRequest) this.$message.error(e.message); } finally { if (request === this.detailRequest) this.detailLoading = false; } },
    async refresh() { const id = this.detail.id, request = this.detailRequest; this.refreshing = true;
      try { const detail = await this.api(`customers/${id}/refresh`, {}); if (request === this.detailRequest && this.visible) this.detail = detail; } catch (e) { if (request === this.detailRequest) this.$message.error(e.message); } finally { if (request === this.detailRequest) this.refreshing = false; } },
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
<style scoped>.section-header { display: flex; align-items: center; justify-content: space-between; margin-top: 16px; }</style>
