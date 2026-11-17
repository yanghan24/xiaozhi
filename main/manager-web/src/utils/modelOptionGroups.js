/** Group local adapter configurations without importing the remote catalog as rows. */
export function modelOptionGroups(options = [], selected = '') {
  const visible = options.filter(item => !item.isHidden);
  return [
    { label: 'AI 集市', market: true },
    { label: '小智系统', market: false },
  ].map(group => {
    const items = visible.filter(item => String(item.value).startsWith('MARKET_') === group.market);
    const children = [...items];
    if (selected && String(selected).startsWith('MARKET_') === group.market && !visible.some(item => item.value === selected)) {
      children.push({ value: selected, label: `已保存配置（当前不可用）：${selected}`, disabled: true });
    }
    if (!items.length) children.push({ value: `__empty_${group.market ? 'market' : 'system'}`, label: '无', disabled: true });
    return { label: group.label, options: children };
  });
}
