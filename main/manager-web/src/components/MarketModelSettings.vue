<template>
  <section class="market-panel" v-loading="loading">
    <header class="market-panel-header">
      <div class="market-panel-title">
        <span class="market-brand-icon"><i class="el-icon-connection" /></span>
        <div>
          <h3>集市模型配置</h3>
          <p>统一配置模型，供智能体选择使用</p>
        </div>
      </div>
      <el-button size="small" plain icon="el-icon-refresh" :disabled="loading || saving" @click="reload">刷新列表</el-button>
    </header>

    <el-alert v-if="error" :title="error" type="warning" :closable="false" show-icon class="market-alert" />

    <div v-if="!supported" class="market-empty">
      <span class="empty-icon"><i class="el-icon-monitor" /></span>
      <h4>当前类型使用小智系统配置</h4>
      <p>保留已有提供者和参数，暂不接入集市模型。</p>
      <el-button size="small" @click="$emit('use-system')">查看小智系统配置</el-button>
    </div>

    <template v-else>
      <div class="market-config-box">
        <div class="model-field">
          <div class="field-heading">
            <label for="market-model-choice">使用模型</label>
            <span v-if="!loading && !error" class="available-count">{{ options.length }} 个兼容模型</span>
          </div>
          <p class="field-description">{{ help }}</p>
          <el-select id="market-model-choice" v-model="selection" filterable
            placeholder="请选择兼容模型" class="market-select" :disabled="loading || saving">
            <el-option v-if="selection && !selectedAvailable" :key="'saved-' + selection"
              :label="selection + '（当前不可选）'" :value="selection" disabled />
            <el-option v-for="model in options" :key="model.publicId" :label="model.name || model.publicId" :value="model.publicId">
              <span>{{ model.name || model.publicId }}</span>
            </el-option>
          </el-select>
          <div v-if="capabilityLabels.length" class="capability-tags">
            <span v-for="label in capabilityLabels" :key="label"><i class="el-icon-check" />{{ label }}</span>
          </div>
          <p v-if="!loading && !error && options.length === 0" class="field-description">暂无符合当前用途的可用模型。</p>
        </div>
        <div class="enable-row">
          <div class="enable-copy">
            <span class="enable-icon"><i class="el-icon-switch-button" /></span>
            <div><h4>启用集市配置</h4><p>启用后，可在智能体中选择对应的 AI 集市配置</p></div>
          </div>
          <div class="enable-control">
            <span :class="{ enabled }">{{ enabled ? '已启用' : '未启用' }}</span>
            <el-switch v-model="enabled" active-color="#6b7bff" :disabled="loading || saving" />
          </div>
        </div>
      </div>
      <div class="market-note">
        <i class="el-icon-info" />
        <span>实际调用使用音箱归属用户的集市凭据，无需在这里填写 Key。</span>
      </div>
      <footer class="market-panel-footer">
        <span>已使用此配置的智能体，将在下一轮对话采用新设置。</span>
        <CustomButton type="confirm" :disabled="!canSave" :loading="saving" @click="save">保存配置</CustomButton>
      </footer>
    </template>
  </section>
</template>

<script>
import CustomButton from "@/components/CustomButton.vue";
import { adminToken } from "@/apis/speaker";
import { marketModelRequest } from "@/apis/marketModels";

export default {
  components: { CustomButton },
  props: { modelType: { type: String, required: true } },
  data() {
    return { options: [], selection: "", enabled: false, savedSelection: "", error: "",
      loading: false, saving: false, bindingLoaded: false, requestVersion: 0, controller: null };
  },
  computed: {
    capabilityLabels() {
      const model = this.options.find(item => item.publicId === this.selection);
      if (!model) return [];
      if (this.modelType === "asr") return ["流式听写", "单轮不超过 60 秒"];
      if (this.modelType === "tts") return ["双向流式合成"];
      const names = { streaming: "流式输出", tools: "工具调用", vision: "图片理解", "structured-output": "结构化输出" };
      return Object.keys(names).filter(key => (model.capabilities || []).includes(key) && (model.verifiedCapabilities || []).includes(key)).map(key => names[key]);
    },
    supported() { return ["llm", "vllm", "asr", "tts", "intent", "memory"].includes(this.modelType); },
    selectedAvailable() { return this.options.some(model => model.publicId === this.selection); },
    canSave() {
      return this.bindingLoaded && !this.loading && !this.saving && Boolean(this.selection)
        && (this.selectedAvailable || !this.enabled && this.selection === this.savedSelection);
    },
    help() {
      if (this.modelType === "asr") return "使用语音听写 V2.0，单次音频不超过 60 秒。";
      if (this.modelType === "tts") return "使用讯飞超拟人双向流式合成，播报音色在角色配置中选择。";
      if (this.modelType === "intent") return "此处配置独立意图模型。工具调用模式可继续在小智系统中选择，并复用主大语言模型。";
      if (this.modelType === "memory") return "使用集市模型生成摘要记忆，记忆的保存和使用仍由小智负责。";
      if (this.modelType === "vllm") return "仅展示支持当前图片输入方式的视觉模型。";
      return "仅展示已验证支持流式对话和工具调用的兼容模型。";
    },
  },
  watch: { modelType: { immediate: true, handler() { this.reload(); } } },
  beforeDestroy() { this.requestVersion++; if (this.controller) this.controller.abort(); },
  methods: {
    async reload() {
      const version = ++this.requestVersion;
      if (this.controller) this.controller.abort();
      this.controller = new AbortController();
      this.options = []; this.selection = ""; this.savedSelection = ""; this.enabled = false;
      this.error = ""; this.bindingLoaded = false; this.saving = false; this.loading = this.supported;
      if (!this.supported) return;
      const token = adminToken(this.$store), type = this.modelType, signal = this.controller.signal;
      const [binding, directory] = await Promise.allSettled([
        marketModelRequest(token, type, "", undefined, signal),
        marketModelRequest(token, type, "options", undefined, signal),
      ]);
      if (version !== this.requestVersion) return;
      if (binding.status === "fulfilled") {
        this.selection = binding.value.publicId; this.savedSelection = binding.value.publicId;
        this.enabled = binding.value.enabled; this.bindingLoaded = true;
      } else if (binding.reason.name !== "AbortError") this.error = binding.reason.message;
      if (directory.status === "fulfilled") this.options = directory.value.items;
      else if (directory.reason.name !== "AbortError") this.error = directory.reason.message;
      this.loading = false;
    },
    async save() {
      if (!this.canSave) return;
      this.saving = true;
      const version = this.requestVersion;
      try {
        await marketModelRequest(adminToken(this.$store), this.modelType, "", {
          publicId: this.selection, enabled: this.enabled,
        }, this.controller.signal);
        if (version !== this.requestVersion) return;
        this.savedSelection = this.selection;
        this.$message.success("集市模型配置已保存");
        this.$emit("saved");
      } catch (error) {
        if (version === this.requestVersion && error.name !== "AbortError") {
          this.error = error.message; this.$message.error(error.message);
        }
      } finally { if (version === this.requestVersion) this.saving = false; }
    },
  },
};
</script>

<style scoped>
.market-panel { box-sizing: border-box; width: 100%; max-width: 1080px; text-align: left; color: #303b59; padding: 4px 0 28px; overflow: auto; }
.market-panel-header { display: flex; align-items: center; justify-content: space-between; gap: 20px; padding: 0 0 24px; }
.market-panel-title { display: flex; align-items: center; gap: 14px; min-width: 0; }
.market-brand-icon { display: flex; align-items: center; justify-content: center; width: 44px; height: 44px; flex-shrink: 0; border-radius: 13px; background: linear-gradient(135deg, #eaf0ff, #f1eaff); color: #7280e9; font-size: 23px; }
.market-panel-title h3 { font-size: 17px; line-height: 1.5; font-weight: 600; margin: 0; }
.market-panel-title p { margin: 4px 0 0; font-size: 12px; color: #9098ac; line-height: 1.6; }
.market-panel-header ::v-deep .el-button { border-color: #dfe5f2; color: #68738f; border-radius: 7px; background: #fff; }
.market-alert { margin-bottom: 18px; }
.market-config-box { border: 1px solid #e6ebf5; border-radius: 12px; overflow: hidden; }
.model-field { padding: 24px; background: linear-gradient(120deg, #f8faff, #fcfcff); }
.field-heading { display: flex; align-items: center; justify-content: space-between; gap: 16px; }
.field-heading label { color: #3d4763; font-size: 14px; font-weight: 600; }
.available-count { color: #929bb0; font-size: 12px; }
.field-description { margin: 9px 0 17px; color: #8992a8; font-size: 12px; line-height: 1.8; }
.market-select { width: 100%; }
.market-select ::v-deep .el-input__inner { height: 44px; line-height: 44px; border-color: #dfe5f2; border-radius: 8px; color: #3d4763; background: #fff; text-align: left; }
.market-select ::v-deep .el-input__inner:focus { border-color: #7c8aff; }
.capability-tags { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 13px; }
.capability-tags span { display: inline-flex; gap: 4px; align-items: center; padding: 4px 8px; background: #edf1ff; color: #7584bd; border-radius: 5px; font-size: 11px; }
.model-hint { margin-left: 12px; }
.enable-row { display: flex; align-items: center; justify-content: space-between; gap: 20px; padding: 21px 24px; background: #fff; border-top: 1px solid #e9edf6; }
.enable-copy { display: flex; align-items: center; gap: 12px; }
.enable-icon { width: 32px; height: 32px; display: flex; align-items: center; justify-content: center; color: #8794b8; background: #f2f5fb; border-radius: 9px; flex-shrink: 0; font-size: 16px; }
.enable-copy h4 { margin: 0 0 5px; font-size: 13px; font-weight: 500; color: #48536d; }
.enable-copy p { margin: 0; font-size: 12px; color: #9aa2b4; line-height: 1.6; }
.enable-control { display: flex; align-items: center; flex-shrink: 0; gap: 12px; }
.enable-control > span { font-size: 12px; color: #a1a8b8; }
.enable-control > span.enabled { color: #7180da; }
.market-note { display: flex; align-items: flex-start; gap: 7px; font-size: 12px; color: #9aa3b7; margin: 17px 0 25px; line-height: 1.8; }
.market-note i { margin-top: 4px; color: #a8b4d7; }
.market-panel-footer { display: flex; align-items: center; justify-content: space-between; gap: 20px; padding-top: 21px; border-top: 1px solid #eef1f7; }
.market-panel-footer > span { font-size: 12px; color: #a0a8ba; line-height: 1.8; }
.market-panel-footer ::v-deep .el-button { min-width: 112px; border-radius: 7px; }
.market-empty { padding: 50px 24px; text-align: center; border: 1px dashed #e1e7f2; border-radius: 12px; background: #fafbff; }
.empty-icon { display: inline-flex; align-items: center; justify-content: center; width: 54px; height: 54px; border-radius: 16px; background: #edf1fc; color: #9eacd1; font-size: 26px; }
.market-empty h4 { margin: 18px 0 8px; font-size: 14px; font-weight: 500; color: #66728d; }
.market-empty p { color: #a0a8b8; font-size: 12px; margin: 0 0 23px; }
@media (max-width: 1100px) { .model-field { padding: 20px; } .enable-row { padding: 20px; } .enable-copy p { max-width: 260px; } .market-panel-footer > span { max-width: 280px; } }
</style>
