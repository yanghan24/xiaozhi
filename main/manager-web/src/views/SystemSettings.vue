<template>
  <SpeakerPage title="系统设置" :show-title="false">
    <el-tabs v-model="activeTab" class="settings-tabs">
      <el-tab-pane label="管理员账号" name="accounts" lazy><SystemAccounts embedded /></el-tab-pane>
      <el-tab-pane label="系统参数" name="parameters" lazy><SystemParameters embedded /></el-tab-pane>
      <el-tab-pane label="字典管理" name="dictionaries" lazy><SystemDictionaries embedded /></el-tab-pane>
      <el-tab-pane label="模型供应商" name="providers" lazy><SystemProviders embedded /></el-tab-pane>
      <el-tab-pane label="服务管理" name="services" lazy><SystemServices embedded /></el-tab-pane>
      <el-tab-pane label="功能插件管理" name="function-plugins" lazy><FunctionPluginSettings v-if="activeTab === 'function-plugins'" /></el-tab-pane>
      <el-tab-pane label="第三方服务" name="third-party-services" lazy><ThirdPartyServices /></el-tab-pane>
    </el-tabs>
  </SpeakerPage>
</template>
<script>
import SpeakerPage from '@/components/SpeakerPage.vue';
const tabs = ['accounts', 'parameters', 'dictionaries', 'providers', 'services', 'function-plugins', 'third-party-services'];
export default {
  components: {
    SpeakerPage,
    SystemAccounts: () => import('./UserManagement.vue'),
    SystemParameters: () => import('./ParamsManagement.vue'),
    SystemDictionaries: () => import('./DictManagement.vue'),
    SystemProviders: () => import('./ProviderManagement.vue'),
    SystemServices: () => import('./ServerSideManager.vue'),
    FunctionPluginSettings: () => import('./FunctionPluginSettings.vue'),
    ThirdPartyServices: () => import('./ThirdPartyServices.vue'),
  },
  computed: {
    activeTab: {
      get() { return tabs.includes(this.$route.query.tab) ? this.$route.query.tab : 'accounts'; },
      set(tab) { if (tab !== this.activeTab) this.$router.push({ query: { ...this.$route.query, tab } }); },
    },
  },
};
</script>
<style lang="scss" scoped>
.settings-tabs {
  flex: 1;
  min-height: 0;
  display: flex;
  flex-direction: column;
  ::v-deep .el-tabs__content { flex: 1; min-height: 0; }
  ::v-deep .el-tab-pane { height: 100%; }
}
</style>
