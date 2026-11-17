<template>
  <SpeakerPage title="智能体管理">
    <template slot="tools"><CustomButton type="add" icon="el-icon-plus" @click="edit()">新增智能体</CustomButton></template>
    <el-alert v-if="error" :title="error" type="error" :closable="false" show-icon />
    <el-alert v-if="!defaultId" title="请先创建并设置一个默认智能体，音箱才能开始对话。" type="warning" :closable="false" show-icon />
    <el-alert v-else title="所有音箱使用当前默认智能体。切换后，当前回答先完成，下一轮使用新角色并清空旧会话上下文。" type="info" :closable="false" />
    <CustomTable :data="items" :columns="columns" :loading="loading" :show-pagination="false" :show-operations="true" :operations-width="240">
      <template slot="is_default" slot-scope="scope"><el-tag :type="scope.row.id === defaultId ? 'success' : 'info'">{{ scope.row.id === defaultId ? '当前默认' : '备用角色' }}</el-tag></template>
      <template slot="operations" slot-scope="scope"><el-button type="text" @click="edit(scope.row)">编辑</el-button><el-button type="text" :disabled="scope.row.id === defaultId || switching" @click="select(scope.row)">设为默认</el-button></template>
    </CustomTable>
    <template slot="dialogs"><el-dialog :title="form.id ? '编辑智能体' : '新增智能体'" :visible.sync="visible" width="720px">
      <el-form label-width="120px" :model="form" @submit.native.prevent="save">
        <el-form-item label="角色名称" required><el-input v-model="form.name" maxlength="128" /></el-form-item>
        <el-form-item label="角色提示词" required><el-input v-model="form.prompt" type="textarea" :rows="7" maxlength="16000" show-word-limit placeholder="描述角色身份、回答方式及需要遵守的规则" /></el-form-item>
        <el-form-item label="集市对话模型" required><el-input v-model="form.llm_model" maxlength="128" /></el-form-item>
        <el-form-item label="集市识别模型" required><el-input v-model="form.asr_model" maxlength="128" /></el-form-item>
        <el-form-item label="播报音色" required><el-input v-model="form.tts_voice" maxlength="128" /></el-form-item>
        <el-alert title="模型由后台配置，调用凭据使用音箱归属用户在集市授权取得的 Key，无需在角色中填写。" type="info" :closable="false" />
      </el-form>
      <template slot="footer"><el-button @click="visible = false">取消</el-button><CustomButton type="confirm" :loading="saving" @click="save">保存</CustomButton></template>
    </el-dialog></template>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
import CustomTable from '@/components/CustomTable.vue';
import CustomButton from '@/components/CustomButton.vue';
import { adminToken, speakerRequest } from '@/apis/speaker';
const blank = () => ({ name: '', prompt: '', llm_model: 'qwen3.8-flash', asr_model: 'rtasr-llm', tts_voice: 'zh-CN-XiaoxiaoNeural', version: 0 });
export default {
  components: { SpeakerPage, CustomTable, CustomButton },
  data() { return { items: [], defaultId: null, revision: 0, loading: false, saving: false, switching: false, error: '', visible: false, form: blank(),
    columns: [{ prop: 'name', label: '角色名称' }, { prop: 'llm_model', label: '对话模型' }, { prop: 'asr_model', label: '语音识别' }, { prop: 'tts_voice', label: '播报音色' }, { prop: 'is_default', label: '使用状态' }] }; },
  mounted() { this.load(); },
  methods: {
    api(path, body) { return speakerRequest(adminToken(this.$store), path, body); },
    async load() { this.loading = true; this.error = ''; try { const data = await this.api('agents'); this.items = data.items; this.defaultId = data.default_agent_id; this.revision = data.revision; } catch (e) { this.error = e.message; } finally { this.loading = false; } },
    edit(row) { this.form = row ? { ...row } : blank(); this.visible = true; },
    async save() { if (![this.form.name, this.form.prompt, this.form.llm_model, this.form.asr_model, this.form.tts_voice].every(value => value?.trim())) { this.$message.warning('请完整填写角色名称、提示词、模型和音色。'); return; }
      this.saving = true; try { const { name, prompt, llm_model, asr_model, tts_voice, version } = this.form;
        await this.api(this.form.id ? `agents/${this.form.id}` : 'agents', { name, prompt, llm_model, asr_model, tts_voice, version }); this.visible = false; await this.load(); } catch (e) { this.$message.error(e.message); } finally { this.saving = false; } },
    async select(row) { try { await this.$confirm(`将「${row.name}」设为所有音箱的默认智能体？正在进行的回答会先完成。`, '切换默认智能体', { type: 'warning' }); } catch (_) { return; }
      this.switching = true; try { await this.api(`agents/${row.id}/default`, { expected_revision: this.revision }); await this.load(); } catch (e) { this.$message.error(e.message); } finally { this.switching = false; } },
  },
};
</script>
