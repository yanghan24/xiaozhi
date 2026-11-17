<template>
  <SpeakerPage title="MCP 管理">
    <template slot="tools"><CustomButton type="add" icon="el-icon-plus" @click="edit()">新增服务</CustomButton></template>
    <el-alert title="维护服务端知识库等 MCP 服务，再到智能体配置中关联。音箱的音量、亮度等设备工具由固件提供。" type="info" :closable="false" />
    <el-alert v-if="error" :title="error" type="error" :closable="false" />
    <CustomTable :data="items" :columns="columns" :loading="loading" :show-operations="true" :show-pagination="false">
      <template slot="enabled" slot-scope="scope"><el-tag :type="scope.row.enabled ? 'success' : 'info'">{{ scope.row.enabled ? '启用' : '停用' }}</el-tag></template>
      <template slot="has_credentials" slot-scope="scope">{{ scope.row.has_credentials ? '已配置（不回显）' : '未配置' }}</template>
      <template slot="operations" slot-scope="scope"><el-button type="text" @click="edit(scope.row)">编辑</el-button></template>
    </CustomTable>
    <template slot="dialogs">
      <CustomDialog
        :title="form.id ? '编辑 MCP 服务' : '新增 MCP 服务'"
        :visible.sync="visible" width="720px" class="management-dialog"
        confirm-text="保存" :confirm-loading="saving" @confirm="save" @close="headers = ''"
      >
        <el-form :model="form" label-width="100px" class="management-form" @submit.native.prevent="save">
          <section class="dialog-section">
            <h3 class="section-heading">基本信息</h3>
            <el-form-item label="服务名称" required>
              <el-input v-model="form.name" maxlength="128" placeholder="请输入服务名称" />
            </el-form-item>
            <el-form-item label="启用服务" class="last-form-item">
              <div class="switch-field"><el-switch v-model="form.enabled" /><span>{{ form.enabled ? '已启用' : '已停用' }}</span></div>
            </el-form-item>
          </section>
          <section class="dialog-section">
            <h3 class="section-heading">连接配置</h3>
            <el-form-item label="服务地址" required>
              <el-input v-model="form.url" placeholder="https://example.com/mcp" maxlength="2048" />
            </el-form-item>
            <el-form-item label="传输方式" class="last-form-item">
              <el-select v-model="form.transport">
                <el-option label="Streamable HTTP" value="streamable-http" />
                <el-option label="SSE" value="sse" />
              </el-select>
            </el-form-item>
          </section>
          <section class="dialog-section">
            <div class="section-toolbar">
              <h3 class="section-heading">认证配置</h3>
              <el-tag size="small" :type="form.has_credentials ? 'success' : 'info'">{{ form.has_credentials ? '已配置凭据' : '未配置凭据' }}</el-tag>
            </div>
            <el-form-item label="认证请求头" class="last-form-item">
              <el-input v-model="headers" type="textarea" :rows="4" class="credential-input" placeholder='{"Authorization":"Bearer …"}' autocomplete="off" />
              <p class="field-hint">填写 JSON 对象；留空保留原凭据，填写后替换原凭据。已保存的凭据不回显。</p>
              <div v-if="form.has_credentials" class="credential-clear">
                <el-checkbox v-model="form.clear_credentials">清除已保存的认证凭据</el-checkbox>
              </div>
            </el-form-item>
          </section>
        </el-form>
      </CustomDialog>
    </template>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
import CustomTable from '@/components/CustomTable.vue';
import CustomButton from '@/components/CustomButton.vue';
import CustomDialog from '@/components/CustomDialog.vue';
import { adminToken, speakerRequest } from '@/apis/speaker';
export default {
  components: { SpeakerPage, CustomTable, CustomButton, CustomDialog },
  data() { return { items: [], loading: false, saving: false, visible: false, error: '', headers: '', form: {}, columns: [
    { prop: 'name', label: '服务名称' }, { prop: 'url', label: '服务地址', minWidth: 220 }, { prop: 'transport', label: '传输方式' },
    { prop: 'enabled', label: '状态' }, { prop: 'has_credentials', label: '认证配置' }] }; },
  mounted() { this.load(); },
  methods: {
    api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    async load() { this.loading = true; this.error = ''; try { this.items = await this.api('mcp'); } catch (e) { this.error = e.message; } finally { this.loading = false; } },
    edit(row) { this.form = row ? { ...row, enabled: !!row.enabled, clear_credentials: false } : { name: '', url: '', transport: 'streamable-http', enabled: false, version: 0, clear_credentials: false }; this.headers = ''; this.visible = true; },
    async save() {
      let headers;
      try { headers = this.headers.trim() ? JSON.parse(this.headers) : null; if (headers && (Array.isArray(headers) || typeof headers !== 'object')) throw new Error(); }
      catch (_) { this.$message.error('认证请求头需要填写 JSON 对象。'); return; }
      this.saving = true;
      try { await this.api(this.form.id ? `mcp/${this.form.id}` : 'mcp', { name: this.form.name, url: this.form.url, transport: this.form.transport, enabled: this.form.enabled, version: this.form.version, clear_credentials: this.form.clear_credentials, headers }); this.headers = ''; this.visible = false; await this.load(); }
      catch (e) { this.$message.error(e.message); } finally { this.saving = false; }
    },
  },
};
</script>

<style lang="scss" scoped>
@import "@/styles/managementDialog.scss";
</style>
