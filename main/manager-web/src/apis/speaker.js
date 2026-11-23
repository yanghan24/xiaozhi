import { getServiceUrl } from './api';

const errors = {
  SPEAKER_DISABLED: '设备与用户管理尚未启用，请联系管理员完成配置。',
  DEFAULT_AGENT_REQUIRED: '请先创建并设置默认智能体。',
  DEVICE_CHANGED: '设备状态已变化，请刷新后重试。',
  DEVICE_ALREADY_REGISTERED: '设备标识已经登记，请查看现有设备。',
  OWNER_REQUIRED: '请使用这台音箱的原归属账号重新扫码。',
  CUSTOMER_NOT_FOUND: '用户不存在。',
  DEVICE_NOT_FOUND: '设备不存在。',
  DEVICE_DIAGNOSTICS_UNSUPPORTED: '请先升级固件，并等待音箱上报诊断支持状态。',
  MARKET_UNAVAILABLE: 'AI 集市暂时不可用，请稍后重试。',
  MARKET_CLIENT_NOT_CONFIGURED: '尚未配置集市设备应用。',
  INVALID_AGENT: '请完整填写角色名称、提示词、模型和音色。',
  AGENT_CHANGED: '角色已被修改，请刷新后再保存。',
  AGENT_MODEL_REQUIRED: '请先在角色配置中选择已启用的模型。',
  AGENT_VOICE_REQUIRED: '请先在角色配置中选择有效音色。',
  INVALID_MCP: '请填写有效服务名称、传输方式和认证请求头；地址不包含查询参数。',
  MCP_CHANGED: '服务已被修改，请刷新后重试。',
  MCP_NOT_FOUND: 'MCP 服务不存在，请刷新目录。',
  INVALID_THIRD_PARTY_SERVICE: '请检查服务名称、地址、接口和认证配置。启用时需填完整。',
  THIRD_PARTY_SERVICE_CHANGED: '服务配置已变化，请刷新页面后重试。',
  INVALID_PLUGIN_CONFIGURATION: '请检查功能插件的参数和必填凭据。',
  PLUGIN_CONFIGURATION_REQUIRED: '请先完成插件配置，再启用。',
  PLUGIN_CONFIGURATION_CHANGED: '功能插件配置已变化，请刷新后重试。',
  PLUGIN_NOT_FOUND: '功能插件不存在，请刷新后重试。',
};

export function adminToken(store) {
  try { return JSON.parse(store.getters.getToken || '{}').token || ''; } catch (_) { return ''; }
}
export function functionPluginName(id, name, translate) {
  return id === 'SYSTEM_PLUGIN_WEATHER' ? translate('agentSnapshot.plugin.SYSTEM_PLUGIN_WEATHER') : name;
}
export async function speakerRequest(token, path, body, signal) {
  const response = await fetch(`${getServiceUrl()}/admin/speaker/${path}`, {
    method: body === undefined ? 'GET' : 'POST', cache: 'no-store', credentials: 'omit', signal,
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  let result;
  try { result = await response.json(); } catch (_) { throw new Error('服务暂时不可用，请稍后重试。'); }
  if (!response.ok || typeof result.code === 'number' && result.code !== 0) {
    if (response.status === 401 || response.status === 403) throw new Error('需要管理员登录或重新登录。');
    throw new Error(errors[result.code] || '操作未完成，请刷新后重试。');
  }
  return result;
}
export function displayTime(value) { return value ? new Date(Number(value)).toLocaleString('zh-CN', { hour12: false }) : '未上报'; }
export function requestKey() { return Array.from(crypto.getRandomValues(new Uint8Array(16)), x => x.toString(16).padStart(2, '0')).join(''); }
export function balanceText(customer) {
  if (customer.balance_status === 'available' && customer.balance !== null && customer.balance !== undefined) return `${customer.balance} 积分`;
  return customer.balance_status === 'not_requested' ? '尚未查询' : '暂时无法查询';
}
export function organizationText(organization) {
  if (!organization) return '集市未提供组织信息';
  if (Array.isArray(organization.path) && organization.path.length) return organization.path.map(item => item.name).filter(Boolean).join(' / ');
  return [organization.organization?.name, organization.department?.name].filter(Boolean).join(' / ') || '集市未提供组织信息';
}
