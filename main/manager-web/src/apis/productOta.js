import { getServiceUrl } from '@/apis/api';

export class ProductOtaError extends Error {
  constructor(status, code) {
    super('OTA request failed');
    this.status = status;
    this.code = code;
  }
}

export async function requestProductOta(token, path, body, signal) {
  const response = await fetch(`${getServiceUrl()}/admin/product-ota/${path}`, {
    method: body ? 'POST' : 'GET', cache: 'no-store', signal,
    headers: { 'Content-Type': 'application/json', Authorization: `Bearer ${token || ''}` },
    body: body ? JSON.stringify(body) : undefined
  });
  const result = await response.json();
  if (!response.ok || result.code !== 0) throw new ProductOtaError(response.status, result.code);
  return result.data;
}
