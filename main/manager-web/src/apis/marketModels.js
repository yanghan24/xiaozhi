import { getServiceUrl } from './api';

const errors = {
  MARKET_CATALOG_CONFIG_REQUIRED: '尚未配置集市模型目录应用，请联系管理员。',
  MARKET_CATALOG_AUTH_REQUIRED: '集市目录应用凭证无效，请检查后台配置。',
  MARKET_CATALOG_FORBIDDEN: '集市应用未获准读取模型目录。',
  MARKET_CATALOG_UNAVAILABLE: '集市模型目录暂时不可用，请稍后刷新。',
  INVALID_MARKET_CATALOG: '集市目录响应不完整，请稍后刷新。',
  MARKET_MODEL_INCOMPATIBLE: '所选模型已不可用或不符合当前用途，请刷新后重新选择。',
  MARKET_MODEL_DISABLED: '请先启用该集市适配配置。',
  DEFAULT_MODEL_CANNOT_DISABLE: '默认模型不能直接停用，请先切换默认配置。',
  MODEL_REQUIRED: '请选择模型。',
};

export async function marketModelRequest(token, type, action = '', body, signal) {
  const response = await fetch(`${getServiceUrl()}/models/market/${encodeURIComponent(type)}${action ? '/' + action : ''}`, {
    method: body === undefined ? 'GET' : 'PUT',
    headers: { Authorization: `Bearer ${token}`, 'Content-Type': 'application/json' },
    credentials: 'omit', cache: 'no-store', signal,
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  let data;
  try { data = await response.json(); } catch (_) { throw new Error('服务响应异常，请稍后重试。'); }
  if (!response.ok || typeof data.code === 'string' || typeof data.code === 'number' && data.code !== 0) {
    throw new Error(errors[data.code] || ([401, 403].includes(response.status)
      ? '需要管理员登录或重新登录。' : '操作未完成，请刷新后重试。'));
  }
  return data;
}
