<template><div><HeaderBar /><main class="ota-page"><ProductOtaCatalog @selected="firmware = $event" /><ProductOtaPush :firmware="firmware" /></main><VersionFooter /></div></template>
<script>
import HeaderBar from '@/components/HeaderBar.vue';
import VersionFooter from '@/components/VersionFooter.vue';
import ProductOtaCatalog from '@/components/ProductOtaCatalog.vue';
import ProductOtaPush from '@/components/ProductOtaPush.vue';
export default { components: { HeaderBar, VersionFooter, ProductOtaCatalog, ProductOtaPush }, data() { return { firmware: null }; } };
</script>
<style scoped>.ota-page { max-width: 1400px; margin: 24px auto; padding: 0 24px; }</style>
