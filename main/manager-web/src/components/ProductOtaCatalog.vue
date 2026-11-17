<template>
  <section class="product-ota" aria-label="产品设备固件目录">
    <h2>产品设备 OTA · 固件目录</h2>
    <p>可从 GitHub Release 导入，或从小智后台上传固件。导入只做校验，不会向设备下发。</p>
    <p v-if="error" role="alert" class="error">{{ error }}</p>
    <fieldset :disabled="loading">
      <legend>导入 GitHub Release</legend>
      <label>Release
        <select data-test="release" v-model="releaseId" @change="assetId = ''">
          <option value="">请选择明确版本</option>
          <option v-for="release in releases" :key="release.id" :value="String(release.id)">{{ release.tag }}{{ release.prerelease ? ' 预发布' : '' }}</option>
        </select>
      </label>
      <label>应用镜像
        <select data-test="asset" v-model="assetId">
          <option value="">请选择 .bin 资产</option>
          <option v-for="asset in assets" :key="asset.id" :value="String(asset.id)">{{ asset.name }} · {{ asset.size }} 字节</option>
        </select>
      </label>
      <button data-test="import" :disabled="!releaseId || !assetId" @click="importFirmware">校验并导入</button>
      <button :disabled="page === 1" @click="changePage(-1)">上一页 Release</button>
      <span>第 {{ page }} 页</span>
      <button :disabled="releases.length < 20" @click="changePage(1)">下一页 Release</button>
    </fieldset>
    <fieldset :disabled="loading">
      <legend>从小智后台上传固件</legend>
      <label>应用镜像（.bin）
        <input data-test="upload-image" type="file" accept=".bin,application/octet-stream" @change="uploadImage = $event.target.files[0] || null" />
      </label>
      <label>校验清单（manifest.json）
        <input data-test="upload-manifest" type="file" accept=".json,application/json" @change="uploadManifest = $event.target.files[0] || null" />
      </label>
      <button data-test="upload" :disabled="!uploadImage || !uploadManifest" @click="uploadFirmware">校验并上传</button>
    </fieldset>
    <h3>已校验固件</h3>
    <button :disabled="loading" @click="load">刷新目录</button>
    <p v-if="!loading && !firmware.length">尚未上传固件。</p>
    <div class="table-scroll" v-if="firmware.length">
      <table>
        <thead><tr><th>选择</th><th>版本 / Release</th><th>板型与容量</th><th>构建与校验</th></tr></thead>
        <tbody><tr v-for="item in firmware" :key="item.firmware_id">
          <td><input type="radio" name="ota-firmware" :disabled="loading || item.verification_status !== 'verified'" :aria-label="'选择 ' + item.app_version + ' ' + item.elf_sha256" :value="item.firmware_id" v-model="selectedId" @change="$emit('selected', item)" /></td>
          <td>{{ item.app_version }}<br>{{ item.release_tag }}<br><strong v-if="item.prerelease">预发布</strong></td>
          <td>{{ item.board_type }}<br>P4 revision {{ item.min_revision }}–{{ item.max_revision }}<br>{{ item.image_size }} / {{ item.app_partition_bytes }} 字节<br>{{ item.partition_layout }}</td>
          <td><strong>{{ item.verification_status === 'verified' ? '校验通过' : '不可发布' }}</strong><br>ELF: <code>{{ item.elf_sha256 }}</code><br>SHA-256: <code>{{ item.sha256 }}</code><br>源码: <code>{{ item.source_commit }}</code></td>
        </tr></tbody>
      </table>
    </div>
    <p v-if="selectedId" role="status">已选择固件。选择本身不会升级设备。</p>
  </section>
</template>

<script>
import { requestProductOta, uploadProductOta, ProductOtaError } from '@/apis/productOta';
export default {
  data() { return { releases: [], firmware: [], releaseId: '', assetId: '', uploadImage: null, uploadManifest: null, selectedId: '', page: 1, loading: false, error: '' }; },
  computed: {
    assets() { const release = this.releases.find(item => String(item.id) === this.releaseId); return release ? release.assets : []; }
  },
  mounted() { this.load(); },
  methods: {
    async request(path, body) {
      const token = JSON.parse(this.$store.getters.getToken || '{}').token;
      try { return await requestProductOta(token, path, body, undefined); }
      catch (error) {
        if (!(error instanceof ProductOtaError)) throw error;
        if ([401, 403].includes(error.status)) throw new Error('闇€瑕佹湁鏁堢殑杩愯惀绠＄悊管理员权限。');
        if (error.status === 409) throw new Error('璇ヨ祫浜у凡缁忓鍏ワ紝请刷新目录。');
        if (error.code === 'GITHUB_RATE_LIMITED') throw new Error('GitHub 璇锋眰棰濆害鏆傛椂鐢ㄥ敖锛岃稍后再试。');
        if (error.status === 400) throw new Error('瀵煎叆琚嫆缁濓細璧勪骇鎴栧吋瀹瑰厓鏁版嵁涓嶅畬鏁淬€佸凡鍙樺寲鎴栨牎楠屽け璐ャ€傝鏍稿 Release。');
        throw new Error('鏆傛椂鏃犳硶璇诲彇鎴栧鍏ュ浐浠讹紝璇风◢后重试。');
      }
    },
    async uploadFirmware() {
      if (this.loading || !this.uploadImage || !this.uploadManifest) return;
      this.loading = true; this.error = '';
      try {
        const token = JSON.parse(this.$store.getters.getToken || '{}').token;
        await uploadProductOta(token, this.uploadImage, this.uploadManifest);
        this.uploadImage = null; this.uploadManifest = null;
        this.firmware = await this.request('firmware');
      } catch (error) {
        if (error instanceof ProductOtaError && error.status === 409) this.error = '该固件已上传，请刷新目录。';
        else if (error instanceof ProductOtaError && error.status === 400) this.error = '上传被拒绝：镜像或 manifest 校验失败。';
        else this.error = error.message || '暂时无法上传固件，请稍后重试。';
      } finally { this.loading = false; }
    },    async load() {
      if (this.loading) return;
      this.loading = true; this.error = '';
      try {
        const results = await Promise.allSettled([this.request(`github-releases?page=${this.page}`), this.request('firmware')]);
        if (results[0].status === 'fulfilled') this.releases = results[0].value;
        else { this.releases = []; this.releaseId = ''; this.assetId = ''; this.error = results[0].reason.message; }
        if (results[1].status === 'fulfilled') this.firmware = results[1].value;
        else { this.firmware = []; this.selectedId = ''; this.$emit('selected', null); this.error = results[1].reason.message; }
      } catch (error) { this.error = error.message; }
      finally { this.loading = false; }
    },
    async changePage(delta) { this.page += delta; this.releaseId = ''; this.assetId = ''; await this.load(); },
    async importFirmware() {
      if (this.loading || !this.releaseId || !this.assetId) return;
      this.loading = true; this.error = '';
      try {
        await this.request('firmware', { release_id: Number(this.releaseId), asset_id: Number(this.assetId) });
        this.firmware = await this.request('firmware');
      } catch (error) { this.error = error.message; }
      finally { this.loading = false; }
    }
  }
};
</script>

<style scoped>
.product-ota { background: white; padding: 24px; border-radius: 12px; color: #303133; }
.product-ota p { line-height: 1.6; }
fieldset { border: 1px solid #dcdfe6; padding: 16px; border-radius: 8px; }
label { display: block; margin: 12px 0; }
select { margin-left: 12px; max-width: 90%; padding: 8px; }
button { padding: 8px 14px; margin: 8px 8px 8px 0; cursor: pointer; }
button:disabled { cursor: default; }
.error { color: #b42318; }
.table-scroll { overflow-x: auto; }
table { border-collapse: collapse; width: 100%; margin-top: 16px; text-align: left; }
td, th { padding: 12px; border-bottom: 1px solid #ebeef5; vertical-align: top; line-height: 1.6; }
code { overflow-wrap: anywhere; font-size: 12px; }
</style>
