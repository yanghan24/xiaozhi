<template>
  <SpeakerPage title="MCP 知识库">
    <template slot="tools"><CustomButton type="add" icon="el-icon-plus" @click="edit()">新增服务</CustomButton></template>
    <el-alert title="维护服务端知识库等 MCP 服务，再到智能体配置中关联。音箱的音量、亮度等设备工具由固件提供。" type="info" :closable="false" />
    <el-alert v-if="error" :title="error" type="error" :closable="false" />
    <CustomTable :data="items" :columns="columns" :loading="loading" :show-operations="true" :show-pagination="false">
      <template slot="enabled" slot-scope="scope"><el-tag :type="scope.row.enabled ? 'success' : 'info'">{{ scope.row.enabled ? '启用' : '停用' }}</el-tag></template>
      <template slot="has_credentials" slot-scope="scope">{{ scope.row.has_credentials ? '已配置（不回显）' : '未配置' }}</template>
      <template slot="operations" slot-scope="scope"><el-button type="text" @click="edit(scope.row)">编辑</el-button></template>
    </CustomTable>
    <template slot="dialogs">
      <el-dialog :title="form.id ? '编辑 MCP 服务' : '新增 MCP 服务'" :visible.sync="visible" width="640px" @closed="headers = ''">
        <el-form :model="form" label-width="110px" @submit.native.prevent="save">
          <el-form-item label="服务名称" required><el-input v-model="form.name" maxlength="128" /></el-form-item>
          <el-form-item label="服务地址" required><el-input v-model="form.url" placeholder="https://example.com/mcp" maxlength="2048" /></el-form-item>
          <el-form-item label="传输方式"><el-select v-model="form.transport"><el-option label="Streamable HTTP" value="streamable-http" /><el-option label="SSE" value="sse" /></el-select></el-form-item>
          <el-form-item label="认证请求头"><el-input v-model="headers" type="textarea" :rows="3" placeholder='JSON，例如 {"Authorization":"Bearer …"}；留空保留原凭据' autocomplete="off" /><el-checkbox v-if="form.has_credentials" v-model="form.clear_credentials">清除已保存的认证凭据</el-checkbox></el-form-item>
          <el-form-item label="启用服务"><el-switch v-model="form.enabled" /></el-form-item>
        </el-form>
        <span slot="footer"><CustomButton @click="visible = false">取消</CustomButton><CustomButton type="confirm" :loading="saving" @click="save">保存</CustomButton></span>
      </el-dialog>
    </template>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
import CustomTable from '@/components/CustomTable.vue';
import CustomButton from '@/components/CustomButton.vue';
import { adminToken, speakerRequest } from '@/apis/speaker';
export default {
  components: { SpeakerPage, CustomTable, CustomButton },
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
