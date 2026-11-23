import { expect, it } from 'vitest';
import { modelOptionGroups } from '../src/utils/modelOptionGroups';

it('keeps market first, stable options and hidden intent choices excluded', () => {
  const system = { value: 'Intent_nointent', label: '不识别' };
  const market = { value: 'MARKET_Intent', label: '集市意图' };
  const original = [system, market, { value: 'Intent_function_call', isHidden: true }];
  const groups = modelOptionGroups(original);
  expect(groups.map(group => group.label)).toEqual(['AI 集市', '小智系统']);
  expect(groups[0].options).toEqual([market]);
  expect(groups[1].options).toEqual([system]);
  expect(original).toHaveLength(3);
});
it('always renders both groups and disables the empty placeholder', () => {
  for (const group of modelOptionGroups()) expect(group.options).toEqual([expect.objectContaining({ label: '无', disabled: true })]);
});
it('retains the selected unavailable id without turning it into a usable option', () => {
  const groups = modelOptionGroups([], 'MARKET_LLM');
  expect(groups[0].options[0]).toMatchObject({ value: 'MARKET_LLM', disabled: true });
  expect(groups[0].options[1]).toMatchObject({ label: '无', disabled: true });
});
