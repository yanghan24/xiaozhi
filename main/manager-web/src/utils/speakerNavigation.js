/** Product entry points; old self-service pages do not become customer administration. */
export function speakerRedirect(path, productMode) {
  if (!productMode) return null;
  const settingsTabs = {
    '/admin-accounts': 'accounts',
    '/params-management': 'parameters',
    '/dict-management': 'dictionaries',
    '/provider-management': 'providers',
    '/server-side-management': 'services',
  };
  if (settingsTabs[path]) return { path: '/system-settings', query: { tab: settingsTabs[path] }, replace: true };
  if (path === '/ota-management') return { path: '/speaker-devices', query: { tab: 'firmware' }, replace: true };
  if (path === '/agent-template-management') return { path: '/speaker-agents', replace: true };
  if (path === '/home') return { path: '/speaker-agents', replace: true };
  if (path === '/user-management') return { path: '/speaker-users', replace: true };
  if (path === '/device-management') return { path: '/speaker-devices', replace: true };
  if (path === '/product-ota') return { path: '/speaker-devices', query: { tab: 'firmware' }, replace: true };
  if (['/register', '/retrieve-password'].includes(path)) return { path: '/login', replace: true };
  return null;
}
