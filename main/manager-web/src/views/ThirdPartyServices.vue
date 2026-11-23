<template>
  <section class="third-party-services">
    <el-alert v-if="error" :title="error" type="error" :closable="false" />
    <el-button v-if="error" type="text" @click="load">重试</el-button>
    <CustomTable :data="entries" :columns="columns" :loading="loading" :show-operations="true" :show-pagination="false">
      <template slot="endpoints" slot-scope="{ row }">{{ row.endpoints.map(endpoint => endpoint.purpose).join('、') || '待配置' }}</template>
      <template slot="url" slot-scope="{ row }">{{ row.url || '待配置' }}</template>
      <template slot="enabled" slot-scope="{ row }"><el-tag :type="row.enabled ? 'success' : 'info'">{{ row.enabled ? '启用' : '停用' }}</el-tag></template>
      <template slot="authMode" slot-scope="{ row }">
        {{ authModes[row.authMode] }}<template v-if="row.authMode !== 'none'"> · {{ row.hasCredential ? '已配置' : '未配置' }}</template>
      </template>
      <template slot="operations" slot-scope="{ row }"><el-button type="text" @click="edit(row)">编辑</el-button></template>
    </CustomTable>

    <CustomDialog title="编辑第三方服务" :visible.sync="visible" width="720px" class="management-dialog"
      confirm-text="保存" :confirm-loading="saving" @confirm="save" @close="credential = ''">
      <el-form v-if="form" :model="form" label-width="105px" class="management-form" @submit.native.prevent="save">
        <section class="dialog-section">
          <h3 class="section-heading">基本信息</h3>
          <el-form-item label="服务名称" required><el-input v-model.trim="form.name" maxlength="128" /></el-form-item>
          <el-form-item label="服务地址"><el-input v-model.trim="form.url" maxlength="1024" placeholder="https://api.example.com" /></el-form-item>
          <el-form-item label="启用服务" class="last-form-item"><el-switch v-model="form.enabled" /></el-form-item>
        </section>
        <section class="dialog-section">
          <h3 class="section-heading">接口</h3>
          <div v-for="(endpoint, index) in form.endpoints" :key="index" class="endpoint-row">
            <el-input v-model.trim="endpoint.purpose" maxlength="128" placeholder="用途" aria-label="接口用途" />
            <el-input v-model.trim="endpoint.path" maxlength="512" placeholder="/path" aria-label="接口路径" />
            <el-button type="text" @click="form.endpoints.splice(index, 1)">移除</el-button>
          </div>
          <el-button type="text" icon="el-icon-plus" :disabled="form.endpoints.length >= 16"
            @click="form.endpoints.push({ purpose: '', path: '' })">添加接口</el-button>
        </section>
        <section class="dialog-section">
          <h3 class="section-heading">认证配置</h3>
          <el-form-item label="认证方式">
            <el-select v-model="form.authMode">
              <el-option v-for="(label, mode) in form.id === 'qweather' ? { qweather_jwt: 'JWT（Ed25519）' } : basicAuthModes"
                :key="mode" :value="mode" :label="label" />
            </el-select>
          </el-form-item>
          <template v-if="form.authMode !== 'none'">
            <template v-if="form.authMode === 'qweather_jwt'">
              <el-form-item label="开发者 ID" required><el-input v-model.trim="form.jwtIssuer" maxlength="128" /></el-form-item>
              <el-form-item label="项目 ID" required><el-input v-model.trim="form.jwtProject" maxlength="128" /></el-form-item>
              <el-form-item label="凭据 ID" required><el-input v-model.trim="form.jwtKeyId" maxlength="128" /></el-form-item>
              <el-form-item label="默认城市"><el-input v-model.trim="form.defaultCity" maxlength="64" placeholder="IP 无法定位时使用" /></el-form-item>
            </template>
            <el-form-item v-else-if="form.authMode !== 'bearer'" :label="form.authMode === 'query' ? '参数名' : '请求头名称'" required>
              <el-input v-model.trim="form.authName" maxlength="64" />
            </el-form-item>
            <el-form-item :label="form.authMode === 'qweather_jwt' ? 'Ed25519 私钥' : '凭据'" class="last-form-item">
              <el-input v-if="form.authMode === 'qweather_jwt'" v-model="credential" type="textarea" :rows="4"
                autocomplete="off" maxlength="4096" :disabled="form.clearCredential"
                :placeholder="form.hasCredential ? '已配置，留空保留' : '粘贴 PKCS#8 PEM 私钥'" />
              <el-input v-else v-model="credential" type="password" autocomplete="new-password" maxlength="4096"
                :disabled="form.clearCredential" :placeholder="form.hasCredential ? '已配置' : '输入供应商凭据'" />
              <p class="field-hint">认证方式和名称相同时，留空保留原凭据。</p>
              <el-checkbox v-if="form.hasCredential" v-model="form.clearCredential" @change="credential = ''">清除已保存的凭据</el-checkbox>
            </el-form-item>
          </template>
        </section>
      </el-form>
    </CustomDialog>
  </section>
</template>

<script>
import CustomDialog from '@/components/CustomDialog.vue';
import CustomTable from '@/components/CustomTable.vue';
import { adminToken, speakerRequest } from '@/apis/speaker';

export default {
  components: { CustomDialog, CustomTable },
  data() {
    return {
      entries: [], error: '', loading: false, saving: false, visible: false, form: null, credential: '',
      basicAuthModes: { none: '无需认证', query: '查询参数', header: '请求头', bearer: 'Bearer Token' },
      columns: [
        { prop: 'name', label: '服务名称' }, { prop: 'endpoints', label: '用途', minWidth: 190 },
        { prop: 'url', label: '服务地址', minWidth: 190 }, { prop: 'authMode', label: '认证配置', minWidth: 120 },
        { prop: 'enabled', label: '状态' },
      ],
    };
  },
  computed: {
    authModes() { return { ...this.basicAuthModes, qweather_jwt: 'JWT（Ed25519）' }; },
  },
  watch: {
    '$route.query.service'() { this.openRequestedService(); },
  },
  mounted() { this.load(); },
  methods: {
    request(path, body) { return speakerRequest(adminToken(this.$store), 'third-party-services' + path, body); },
    async load() {
      this.loading = true;
      this.error = '';
      try {
        this.entries = await this.request('');
        this.openRequestedService();
      }
      catch (failure) { this.error = failure.message; }
      finally { this.loading = false; }
    },
    edit(entry) {
      this.form = { ...entry, endpoints: entry.endpoints.map(endpoint => ({ ...endpoint })), clearCredential: false };
      if (entry.id === 'qweather' && entry.authMode !== 'qweather_jwt') {
        Object.assign(this.form, { authMode: 'qweather_jwt', authName: 'Authorization', hasCredential: false });
      }
      this.credential = '';
      this.visible = true;
    },
    openRequestedService() {
      if (!this.$route.query.service || this.$route.query.tab !== 'third-party-services') return;
      const entry = this.entries.find(item => item.id === this.$route.query.service);
      if (entry) {
        const query = { ...this.$route.query };
        delete query.service;
        this.$router.replace({ query });
        this.edit(entry);
      }
    },
    async save() {
      if (this.saving || !this.form) return;
      const { id, name, url, endpoints, authMode, authName, clearCredential, enabled, version,
        jwtIssuer, jwtProject, jwtKeyId, defaultCity } = this.form;
      this.saving = true;
      try {
        await this.request('/' + encodeURIComponent(id), {
          name, url, endpoints, authMode, authName, enabled, version, jwtIssuer, jwtProject, jwtKeyId, defaultCity,
          credential: authMode !== 'none' && !clearCredential ? this.credential : null,
          clearCredential,
        });
        this.visible = false;
        this.credential = '';
        this.$message.success('服务配置已保存');
        await this.load();
      } catch (failure) {
        this.$message.error(failure.message);
      } finally {
        this.saving = false;
      }
    },
  },
};
</script>

<style lang="scss" scoped>
@import "@/styles/managementDialog.scss";

.third-party-services {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
}

.endpoint-row {
  display: grid;
  grid-template-columns: minmax(120px, 1fr) minmax(180px, 2fr) auto;
  gap: 12px;
  margin-bottom: 8px;
}
</style>
