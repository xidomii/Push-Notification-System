'use strict';

const API = '/api';

(async () => {
  const [devices, groups, notifications] = await Promise.all([
    apiFetch('/devices'),
    apiFetch('/groups'),
    apiFetch('/notifications'),
  ]);

  document.getElementById('stat-devices').textContent       = devices.length;
  document.getElementById('stat-groups').textContent        = groups.length;
  document.getElementById('stat-notifications').textContent = notifications.length;

  renderRecent(notifications.slice(0, 5));
})();

function renderRecent(notifications) {
  const tbody = document.getElementById('recent-tbody');
  if (notifications.length === 0) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="3">Noch keine Benachrichtigungen gesendet.</td></tr>';
    return;
  }
  tbody.innerHTML = notifications.map(n => `
    <tr>
      <td><span class="group-tag">${escHtml(n.group_name)}</span></td>
      <td>${escHtml(n.message)}</td>
      <td class="col-time">${formatTs(n.timestamp)}</td>
    </tr>
  `).join('');
}

async function apiFetch(path) {
  try {
    const res = await fetch(API + path);
    if (!res.ok) return [];
    return await res.json();
  } catch {
    return [];
  }
}

function formatTs(iso) {
  if (!iso) return '–';
  const d = new Date(iso);
  return d.toLocaleDateString('de-AT') + ' ' +
    d.toLocaleTimeString('de-AT', { hour: '2-digit', minute: '2-digit' });
}

function escHtml(str) {
  return String(str ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}
