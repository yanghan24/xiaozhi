<template>
  <el-form-item label="MCP 服务">
    <div v-loading="loading">
      <el-select v-model="selected" multiple filterable placeholder="选择服务端 MCP 服务" style="width: 100%" :disabled="!!error || saving">
        <el-option v-for="service in services" :key="service.id" :value="service.id" :label="service.name + (service.enabled ? '' : '（已停用）')" />
      </el-select>
      <el-button type="text" :loading="saving" :disabled="!!error || loading" @click="save">保存 MCP 关联</el-button>
      <el-button type="text" @click="$router.push('/mcp-management')">管理服务目录</el-button>
      <div v-if="error" role="alert">{{ error }} <el-button type="text" @click="load">重试</el-button></div>
      <div class="mcp-hint">关联独立保存。请在功能配置中启用函数调用；默认智能体的变更从下一轮对话生效。</div>
    </div>
  </el-form-item>
</template>
<script>
import { adminToken, speakerRequest } from '@/apis/speaker';
export default {
  props: { agentId: { type: String, required: true } },
  data() { return { services: [], selected: [], loading: false, saving: false, error: '' }; },
  watch: { agentId: { immediate: true, handler() { this.load(); } } },
  methods: {
    api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    async load() { const id = this.agentId; if (!id) return; this.loading = true; this.error = ''; try { const [services, selected] = await Promise.all([this.api('mcp'), this.api(`agent-mcp/${id}`)]); if (id === this.agentId) { this.services = services; this.selected = selected; } } catch (e) { if (id === this.agentId) this.error = e.message; } finally { if (id === this.agentId) this.loading = false; } },
    async save() { this.saving = true; try { await this.api(`agent-mcp/${this.agentId}`, this.selected); this.$message.success('MCP 关联已保存，默认角色在下一轮生效。'); } catch (e) { this.$message.error(e.message); } finally { this.saving = false; } },
  },
};
</script>
<style scoped>.mcp-hint { color: #909399; font-size: 12px; line-height: 1.6; }</style>
