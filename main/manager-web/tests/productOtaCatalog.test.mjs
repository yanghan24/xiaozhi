import Vue from 'vue';
import { mount } from '@vue/test-utils';
import { parseComponent, compileToFunctions } from 'vue-template-compiler';
import { beforeEach, afterEach, it, expect, vi } from 'vitest';
import source from '../src/components/ProductOtaCatalog.vue?raw';

const parsed=parseComponent(source);
const component=new Function('getServiceUrl',parsed.script.content.replace(/^import .*;$/gm,'').replace('export default','return'))(()=>'/xiaozhi');
Object.assign(component,compileToFunctions(parsed.template.content));
const response=data=>({ok:true,json:async()=>({code:0,data})});
const settle=async()=>{for(let i=0;i<8;i++) await Vue.nextTick();};
let wrapper;
beforeEach(()=>vi.stubGlobal('fetch',vi.fn()));
afterEach(()=>{wrapper?.destroy();vi.unstubAllGlobals();});
it('shows prerelease, imports explicit asset then reads and selects the fixed build',async()=>{
  fetch.mockResolvedValueOnce(response([{id:'9',tag:'candidate',prerelease:true,assets:[{id:'90',name:'app.bin',size:4003072}]}]))
    .mockResolvedValueOnce(response([]));
  wrapper=mount(component,{mocks:{$store:{getters:{getToken:JSON.stringify({token:'fixture'})}}}});
  await settle();
  expect(wrapper.text()).toContain('预发布');
  await wrapper.find('[data-test=release]').setValue('9');
  await wrapper.find('[data-test=asset]').setValue('90');
  const artifact={firmware_id:'fixed1',app_version:'2.2.6',elf_sha256:'1111',sha256:'2222',board_type:'P4',verification_status:'verified',prerelease:true};
  fetch.mockResolvedValueOnce(response(artifact)).mockResolvedValueOnce(response([artifact]));
  await wrapper.find('[data-test=import]').trigger('click'); await settle();
  expect(JSON.parse(fetch.mock.calls[2][1].body)).toEqual({release_id:9,asset_id:90});
  expect(wrapper.text()).toContain('1111'); expect(wrapper.text()).toContain('校验通过');
  await wrapper.find('input[type=radio]').setChecked();
  expect(wrapper.emitted('selected').at(-1)[0].firmware_id).toBe('fixed1');
});
it('keeps fixed firmware available when GitHub cannot be reached',async()=>{
  fetch.mockResolvedValueOnce({ok:false,status:502,json:async()=>({code:'GITHUB_UNAVAILABLE'})})
    .mockResolvedValueOnce(response([{firmware_id:'saved',app_version:'2.2.6',elf_sha256:'saved-elf',verification_status:'verified'}]));
  wrapper=mount(component,{mocks:{$store:{getters:{getToken:JSON.stringify({token:'fixture'})}}}});
  await settle();
  expect(wrapper.text()).toContain('saved-elf');
  expect(wrapper.find('[role=alert]').exists()).toBe(true);
});
