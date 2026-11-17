import { modelOptionGroups } from '@/utils/modelOptionGroups';

// Functional children keep Element's native option/group registration and filtering.
export default {
  name: 'GroupedModelOptions',
  functional: true,
  props: { options: { type: Array, default: () => [] }, selected: { type: String, default: '' } },
  render(h, { props }) {
    return modelOptionGroups(props.options, props.selected).map(group => h('el-option-group', {
      key: group.label, props: { label: group.label },
    }, group.options.map(item => h('el-option', { key: item.value, props: item }))));
  },
};
