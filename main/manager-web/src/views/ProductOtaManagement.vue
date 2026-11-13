<template><div><HeaderBar /><main class="ota-page"><ProductOtaCatalog /></main><VersionFooter /></div></template>
<script>
import HeaderBar from '@/components/HeaderBar.vue';
import VersionFooter from '@/components/VersionFooter.vue';
import ProductOtaCatalog from '@/components/ProductOtaCatalog.vue';
export default { components: { HeaderBar, VersionFooter, ProductOtaCatalog } };
</script>
<style scoped>.ota-page { max-width: 1400px; margin: 24px auto; padding: 0 24px; }</style>
