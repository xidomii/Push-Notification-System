'use strict';

const API = '/api';

(async () => {
  await loadGroups();
  await loadNotifications();
  bindEvents();
})();

async function loadGroups() {
  try {
    const res = await fetch(`${API}/groups`);
    if (!res.ok) throw new Error();
    const groups = await res.json();
    const sel = document.getElementById('sel-group');
    if (groups.length === 0) {
      sel.innerHTML = '<option value="">— Keine Gruppen vorhanden —</option>';
      return;
    }
    sel.innerHTML = '<option value="">— Gruppe wählen —</option>' +
      groups.map(g => `<option value="${g.id}">${escHtml(g.name)}</option>`).join('');
  } catch {
    showToast('Gruppen konnten nicht geladen werden', 'error');
  }
}

async function loadNotifications() {
  try {
    const res = await fetch(`${API}/notifications`);
    if (!res.ok) throw new Error();
    const notifications = await res.json();
    renderTable(notifications);
  } catch {
    showToast('Benachrichtigungen konnten nicht geladen werden', 'error');
  }
}

function renderTable(notifications) {
  document.getElementById('notif-count').textContent = notifications.length;
  const tbody = document.getElementById('notif-tbody');
  if (notifications.length === 0) {
    tbody.innerHTML = '<tr class="empty-row"><td colspan="4">Noch keine Benachrichtigungen gesendet.</td></tr>';
    return;
  }
  tbody.innerHTML = notifications.map(n => `
    <tr>
      <td class="col-id">${n.id}</td>
      <td><span class="group-tag">${escHtml(n.group_name)}</span></td>
      <td>${escHtml(n.message)}</td>
      <td class="col-time">${formatTs(n.timestamp)}</td>
    </tr>
  `).join('');
}

function bindEvents() {
  document.getElementById('btn-send').addEventListener('click', sendNotification);
  document.getElementById('inp-message').addEventListener('keydown', e => {
    if (e.key === 'Enter') sendNotification();
  });
}

async function sendNotification() {
  const groupId = parseInt(document.getElementById('sel-group').value);
  const message = document.getElementById('inp-message').value.trim();
  const errEl   = document.getElementById('form-error');
  errEl.style.display = 'none';

  if (!groupId) { showFormError('Bitte eine Gruppe auswählen.'); return; }
  if (!message) { showFormError('Bitte eine Nachricht eingeben.'); return; }

  const btn = document.getElementById('btn-send');
  btn.disabled = true;

  try {
    const res = await fetch(`${API}/notifications`, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ message, group_id: groupId }),
    });
    if (!res.ok) {
      const err = await res.json().catch(() => ({}));
      showFormError(err.error || `Fehler ${res.status}`);
      return;
    }
    document.getElementById('inp-message').value = '';
    await loadNotifications();
    showToast('Benachrichtigung gesendet', 'success');
  } catch {
    showFormError('Netzwerkfehler. Bitte erneut versuchen.');
  } finally {
    btn.disabled = false;
  }
}

function showFormError(msg) {
  const el = document.getElementById('form-error');
  el.textContent = msg;
  el.style.display = 'block';
}

function formatTs(iso) {
  if (!iso) return '–';
  const d = new Date(iso);
  return d.toLocaleDateString('de-AT') + ' ' +
    d.toLocaleTimeString('de-AT', { hour: '2-digit', minute: '2-digit' });
}

function escHtml(str) {
  return String(str ?? '')
    .replace(/&/g, '&amp;').replace(/</g, '&lt;')
    .replace(/>/g, '&gt;').replace(/"/g, '&quot;');
}

let toastTimer;
function showToast(msg, type = '') {
  clearTimeout(toastTimer);
  const el = document.getElementById('toast');
  el.textContent = msg;
  el.className = 'toast' + (type ? ' ' + type : '') + ' show';
  toastTimer = setTimeout(() => el.classList.remove('show'), 3000);
}
