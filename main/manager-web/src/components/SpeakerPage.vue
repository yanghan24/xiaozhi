<template>
  <div class="welcome">
    <HeaderBar />
    <div class="main-wrapper"><div class="content-panel"><el-card class="management-card" shadow="never">
      <div v-if="showTitle || $slots.tools" class="operation-header"><h2 v-if="showTitle" class="page-title">{{ title }}</h2><div class="right-operations"><slot name="tools" /></div></div>
      <slot />
    </el-card></div></div>
    <el-footer height="35px"><VersionFooter /></el-footer>
    <slot name="dialogs" />
  </div>
</template>
<script>
import HeaderBar from './HeaderBar.vue';
import VersionFooter from './VersionFooter.vue';
export default { name: 'SpeakerPage', components: { HeaderBar, VersionFooter }, props: { title: { type: String, required: true }, showTitle: { type: Boolean, default: true } } };
</script>
<style lang="scss" scoped>
.welcome { min-width: 900px; height: 100vh; display: flex; flex-direction: column; background: #eff4ff; overflow: hidden; }
.main-wrapper { flex: 1; min-height: 0; padding: 20px 22px 0; box-sizing: border-box; }
.content-panel { display: flex; height: 100%; border-radius: 15px; overflow: hidden; border: 1px solid #fff; }
.management-card { flex: 1; border: none; display: flex; flex-direction: column; overflow: auto;
  ::v-deep .el-card__body { padding: 14px 20px; display: flex; flex-direction: column; flex: 1; min-height: 0; } }
.operation-header { display: flex; align-items: center; justify-content: space-between; padding-bottom: 16px; gap: 16px; }
.page-title { font-weight: 500; font-size: 24px; margin: 0; }
.right-operations { display: flex; gap: 10px; align-items: center; }
.right-operations ::v-deep .el-input { width: 240px; }
</style>
