<template>
  <section class="plugin-settings">
    <el-alert v-if="error" :title="error" type="error" :closable="false" />
    <el-button v-if="error" type="text" @click="load">重试</el-button>
    <CustomTable :data="entries" :columns="columns" :loading="loading" :show-pagination="false" :show-operations="true">
      <template slot="status" slot-scope="{ row }">
        <el-tag :type="row.configured ? 'success' : 'info'">{{ status(row) }}</el-tag>
      </template>
      <template slot="enabled" slot-scope="{ row }">
        <el-switch :value="row.enabled" :disabled="!row.configured || loading || updating"
          :aria-label="'启用' + row.name" @change="changeEnabled(row, $event)" />
      </template>
      <template slot="details" slot-scope="{ row }">{{ details(row) }}</template>
      <template slot="operations" slot-scope="{ row }">
        <el-button type="text" @click="edit(row)">编辑</el-button>
        <el-button v-if="row.provider === 'get_weather'" type="text" @click="openWeather">和风天气配置</el-button>
      </template>
    </CustomTable>

    <CustomDialog title="编辑功能插件" :visible.sync="visible" width="640px" class="management-dialog"
      confirm-text="保存" :confirm-loading="saving" @confirm="save" @close="credential = ''">
      <el-form v-if="entry" label-width="128px" class="management-form" @submit.native.prevent="save">
        <section class="dialog-section">
          <h3 class="section-heading">{{ entry.name }}</h3>
          <el-alert v-if="entry.legacyConflict" title="现有智能体的参数有差异。保存后，使用该插件的智能体将统一使用这里的配置。"
            type="warning" :closable="false" />
          <el-form-item v-if="entry.provider === 'get_weather'" label="服务配置">
            <el-button type="text" @click="openWeather">和风天气</el-button>
          </el-form-item>
          <el-form-item v-if="entry.provider === 'hass_play_music'" label="服务配置">
            与 HomeAssistant设备控制共用地址、令牌和设备清单。
          </el-form-item>
          <el-form-item v-for="field in entry.fields" :key="field.key" :label="field.label">
            <template v-if="field.key === 'api_key'">
              <el-input v-model="credential" type="password" autocomplete="new-password"
                :disabled="clearCredential" :placeholder="entry.hasCredential ? '已配置' : '请输入凭据'" />
              <el-checkbox v-if="entry.hasCredential" v-model="clearCredential" @change="credential = ''">清除凭据</el-checkbox>
            </template>
            <el-input v-else-if="field.type === 'array'" v-model="params[field.key]" type="textarea" :rows="3"
              placeholder="每行一个设备" />
            <el-input v-else v-model.trim="params[field.key]"
              :placeholder="field.key === 'news_sources' ? '用英文分号 ; 分隔，第一项为默认来源' : ''" />
          </el-form-item>
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
      entries: [], loading: false, saving: false, updating: false, visible: false, error: '',
      entry: null, params: {}, credential: '', clearCredential: false,
      columns: [
        { prop: 'name', label: '功能插件', minWidth: 165 },
        { prop: 'status', label: '配置状态', minWidth: 110 },
        { prop: 'enabled', label: '启用', width: 85 },
        { prop: 'details', label: '配置说明', minWidth: 230, align: 'left' },
      ],
    };
  },
  mounted() { this.load(); },
  activated() { if (this.entries.length) this.load(); },
  methods: {
    request(suffix, body) { return speakerRequest(adminToken(this.$store), 'function-plugins' + suffix, body); },
    status(entry) {
      if (entry.legacyConflict) return '待统一配置';
      return entry.configured ? '已配置' : '待配置';
    },
    details(entry) {
      if (entry.legacyConflict) return '现有智能体参数不同，请保存一套统一配置';
      if (entry.configured) return '—';
      const requirements = {
        get_weather: '在第三方服务中配置并启用和风天气 JWT',
        hass_state: '需要 HomeAssistant 地址、访问令牌和设备清单',
        hass_play_music: '需配置 HomeAssistant 连接和播放器',
        web_search: '需要搜索服务商、有效 Key 和返回数量',
      };
      return requirements[entry.provider] || '请补全插件配置';
    },
    openWeather() {
      this.visible = false;
      this.$router.push({ query: { ...this.$route.query, tab: 'third-party-services', service: 'qweather' } });
    },
    async load() {
      this.loading = true;
      this.error = '';
      try { this.entries = await this.request(''); }
      catch (failure) { this.error = failure.message; }
      finally { this.loading = false; }
    },
    edit(entry) {
      this.entry = entry;
      this.credential = '';
      this.clearCredential = false;
      this.params = Object.fromEntries(entry.fields.filter(field => field.key !== 'api_key').map(field => {
        const value = entry.params[field.key] ?? '';
        return [field.key, Array.isArray(value) ? value.join('\n') : value];
      }));
      this.visible = true;
    },
    async changeEnabled(entry, enabled) {
      if (!entry.configured || this.updating || enabled === entry.enabled) return;
      this.updating = true;
      try {
        await this.request('/' + encodeURIComponent(entry.id) + '/enabled', { enabled, version: entry.version });
        await this.load();
      } catch (failure) {
        this.$message.error(failure.message);
        await this.load();
      } finally {
        this.updating = false;
      }
    },
    async save() {
      if (!this.entry || this.saving) return;
      this.saving = true;
      const params = { ...this.params };
      for (const field of this.entry.fields.filter(item => item.type === 'array')) {
        params[field.key] = params[field.key].split(/\r?\n/).map(line => line.trim()).filter(Boolean);
      }
      try {
        await this.request('/' + encodeURIComponent(this.entry.id), {
          version: this.entry.version, params,
          credential: this.credential || null, clearCredential: this.clearCredential,
        });
        this.visible = false;
        this.credential = '';
        this.$message.success('功能插件配置已保存');
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
.plugin-settings {
  display: flex;
  flex-direction: column;
  height: 100%;
  min-height: 0;
  min-width: 0;
}
</style>
