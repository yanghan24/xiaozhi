<template>
  <el-card class="product-asr" shadow="never" v-loading="loading">
    <h3>小智设备语音识别</h3>
    <p>所有已绑定设备共用此设置，保存后从下一次语音转写生效。</p>
    <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
    <el-form label-width="120px" @submit.native.prevent="save">
      <el-form-item label="服务来源">
        <el-radio-group v-model="provider" :disabled="loading || revision === null">
          <el-radio label="market">AI 集市 · rtasr-llm</el-radio>
          <el-radio label="xfyun">讯飞直连 · 实时语音转写大模型</el-radio>
        </el-radio-group>
      </el-form-item>
      <p v-if="provider === 'market'">使用设备归属账号的集市授权与额度。</p>
      <template v-else>
        <p>使用独立的讯飞额度。密钥{{ configured ? '已配置，留空保留原值；更换时请填写全部三项。' : '尚未配置，请填写全部三项。' }}</p>
        <el-form-item label="APPID"><el-input v-model="appId" autocomplete="off" maxlength="64" /></el-form-item>
        <el-form-item label="APIKey"><el-input v-model="apiKey" type="password" autocomplete="new-password" maxlength="256" /></el-form-item>
        <el-form-item label="APISecret"><el-input v-model="apiSecret" type="password" autocomplete="new-password" maxlength="256" /></el-form-item>
      </template>
      <el-button type="primary" :disabled="loading || revision === null" @click="save">保存语音识别设置</el-button>
      <el-button :disabled="loading" @click="load">重新读取</el-button>
    </el-form>
  </el-card>
</template>

<script>
import { getServiceUrl } from '@/apis/api';

export default {
  data() {
    return { provider: 'market', revision: null, configured: false, appId: '', apiKey: '', apiSecret: '', loading: false, error: '' };
  },
  mounted() { this.load(); },
  methods: {
    async request(method, body) {
      const token = JSON.parse(this.$store.getters.getToken || '{}').token;
      const response = await fetch(`${getServiceUrl()}/admin/product-asr`, {
        method, cache: 'no-store', headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token || ''}` },
        body: body ? JSON.stringify(body) : undefined
      });
      if (!response.ok) {
        if (response.status === 409) throw new Error('设置已被其他管理员修改，请重新读取后再保存。');
        if ([401, 403].includes(response.status)) throw new Error('需要管理员登录权限，请重新登录。');
        if (response.status === 400) throw new Error('请检查服务来源，并完整填写讯飞 APPID、APIKey 和 APISecret。');
        throw new Error('无法访问语音识别设置，请稍后重试。');
      }
      const result = await response.json();
      if (result.code !== 0 || !result.data) throw new Error('读取或保存失败，请检查管理员权限。');
      return result.data;
    },
    apply(data) {
      this.provider = data.provider; this.revision = data.revision; this.configured = data.xfyunConfigured;
      this.appId = ''; this.apiKey = ''; this.apiSecret = '';
    },
    async load() {
      this.loading = true; this.error = '';
      try { this.apply(await this.request('GET')); }
      catch (error) { this.error = error.message; this.revision = null; }
      finally { this.loading = false; }
    },
    async save() {
      if (this.loading || this.revision === null) return;
      this.loading = true; this.error = '';
      try {
        const body = { provider: this.provider, revision: this.revision };
        if (this.provider === 'xfyun') Object.assign(body, { appId: this.appId, apiKey: this.apiKey, apiSecret: this.apiSecret });
        this.apply(await this.request('PUT', body));
        this.$message.success('已保存，下次语音转写生效');
      } catch (error) { this.error = error.message; }
      finally { this.loading = false; }
    }
  }
};
</script>

<style scoped>
.product-asr { margin-bottom: 20px; }
.product-asr h3 { margin-top: 0; }
.product-asr p { color: #606266; line-height: 1.6; }
.product-asr .el-form { margin-top: 18px; max-width: 760px; }
</style>
