/**
 * NOTIFY ADMIN — script.js
 * Gruppen-Verwaltung mit SQLite-Persistenz über REST-API
 *
 * Erwartete Backend-Endpunkte:
 *   GET    /api/groups                    → [{id, name, device_ids:[]}]
 *   POST   /api/groups          {name}    → {id, name, device_ids:[]}
 *   PUT    /api/groups/:id      {name}    → {id, name}
 *   DELETE /api/groups/:id               → {ok:true}
 *   GET    /api/devices                  → [{id, name, type}]
 *   PUT    /api/groups/:id/devices {device_ids:[]} → {ok:true}
 *
 * Für lokale Entwicklung ohne Backend wird ein In-Memory-Mock
 * aktiviert, wenn window.USE_MOCK_API === true (oder kein Backend
 * erreichbar). Setze in der HTML oder per Konsole:
 *   window.USE_MOCK_API = true;
 */

'use strict';

/* ══════════════════════════════════════════════════════
   MOCK API (Fallback für Entwicklung ohne Backend)
   ══════════════════════════════════════════════════════ */
const MOCK_STORE = {
  groups: [
    { id: 1, name: 'Küche',   device_ids: [1, 3] },
    { id: 2, name: 'Service', device_ids: [2]    },
  ],
  devices: [
    { id: 1, name: 'Terminal Küche',    type: 'Display'  },
    { id: 2, name: 'Tablet Theke',      type: 'Tablet'   },
    { id: 3, name: 'Drucker Station 1', type: 'Drucker'  },
    { id: 4, name: 'Monitor Eingang',   type: 'Display'  },
    { id: 5, name: 'Tablet Außenbereich', type: 'Tablet' },
  ],
  nextId: 3,
};

const MockAPI = {
  async getGroups()  { return structuredClone(MOCK_STORE.groups); },
  async getDevices() { return structuredClone(MOCK_STORE.devices); },
  async createGroup(name) {
    const g = { id: MOCK_STORE.nextId++, name, device_ids: [] };
    MOCK_STORE.groups.push(g);
    return structuredClone(g);
  },
  async renameGroup(id, name) {
    const g = MOCK_STORE.groups.find(x => x.id === id);
    if (!g) throw new Error('Gruppe nicht gefunden');
    g.name = name;
    return structuredClone(g);
  },
  async deleteGroup(id) {
    const idx = MOCK_STORE.groups.findIndex(x => x.id === id);
    if (idx === -1) throw new Error('Gruppe nicht gefunden');
    MOCK_STORE.groups.splice(idx, 1);
    return { ok: true };
  },
  async setDevices(groupId, deviceIds) {
    const g = MOCK_STORE.groups.find(x => x.id === groupId);
    if (!g) throw new Error('Gruppe nicht gefunden');
    g.device_ids = [...deviceIds];
    return { ok: true };
  },
};

/* ══════════════════════════════════════════════════════
   REAL API (spricht gegen euer Backend)
   ══════════════════════════════════════════════════════ */
const BASE = '/api';

async function apiFetch(path, options = {}) {
  const res = await fetch(BASE + path, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({}));
    throw new Error(err.message || `HTTP ${res.status}`);
  }
  return res.json();
}

const RealAPI = {
  getGroups:              ()           => apiFetch('/groups'),
  getDevices:             ()           => apiFetch('/devices'),
  createGroup:            (name)       => apiFetch('/groups', { method: 'POST', body: JSON.stringify({ name }) }),
  renameGroup:            (id, name)   => apiFetch(`/groups/${id}`, { method: 'PUT',  body: JSON.stringify({ name }) }),
  deleteGroup:            (id)         => apiFetch(`/groups/${id}`, { method: 'DELETE' }),
  setDevices:             (id, ids)    => apiFetch(`/groups/${id}/devices`, { method: 'PUT', body: JSON.stringify({ device_ids: ids }) }),
};

/* Wähle API automatisch */
let API;
(async () => {
  if (window.USE_MOCK_API) {
    API = MockAPI;
    init();
    return;
  }
  try {
    await fetch(BASE + '/groups', { method: 'HEAD' });
    API = RealAPI;
  } catch {
    console.warn('[NotifyAdmin] Backend nicht erreichbar – Mock-API aktiviert');
    API = MockAPI;
  }
  init();
})();

/* ══════════════════════════════════════════════════════
   STATE
   ══════════════════════════════════════════════════════ */
let groups  = [];   // [{id, name, device_ids}]
let devices = [];   // [{id, name, type}]
let activeGroupId = null;

/* ══════════════════════════════════════════════════════
   DOM REFS
   ══════════════════════════════════════════════════════ */
const $ = id => document.getElementById(id);

const els = {
  groupList:        $('group-list'),
  groupCount:       $('group-count'),
  emptyHint:        $('empty-hint'),
  newGroupName:     $('new-group-name'),
  btnCreateGroup:   $('btn-create-group'),

  panelTitle:       $('panel-title'),
  panelActions:     $('panel-actions'),
  btnRenameGroup:   $('btn-rename-group'),
  btnDeleteGroup:   $('btn-delete-group'),
  noSelection:      $('no-selection'),
  deviceSection:    $('device-section'),

  deviceSearch:     $('device-search'),
  btnSelectAll:     $('btn-select-all'),
  btnDeselectAll:   $('btn-deselect-all'),
  deviceList:       $('device-list'),
  saveHint:         $('save-hint'),
  btnSaveAssignment:$('btn-save-assignment'),

  renameModal:      $('rename-modal'),
  renameInput:      $('rename-input'),
  btnRenameCancel:  $('btn-rename-cancel'),
  btnRenameConfirm: $('btn-rename-confirm'),

  toast:            $('toast'),
};

/* ══════════════════════════════════════════════════════
   INIT
   ══════════════════════════════════════════════════════ */
async function init() {
  try {
    [groups, devices] = await Promise.all([API.getGroups(), API.getDevices()]);
  } catch (e) {
    showToast('Ladefehler: ' + e.message, 'error');
    groups = []; devices = [];
  }
  renderGroupList();
  bindEvents();
}

/* ══════════════════════════════════════════════════════
   RENDER
   ══════════════════════════════════════════════════════ */
function renderGroupList() {
  els.groupList.innerHTML = '';
  els.groupCount.textContent = groups.length;
  els.emptyHint.style.display = groups.length === 0 ? 'block' : 'none';

  groups.forEach(g => {
    const li = document.createElement('li');
    li.className = 'group-item' + (g.id === activeGroupId ? ' active' : '');
    li.dataset.id = g.id;

    // icon: erste 2 Zeichen des Namens
    const abbr = g.name.replace(/\s+/g, '').slice(0, 2).toUpperCase();

    li.innerHTML = `
      <div class="group-icon">${abbr}</div>
      <span class="group-label">${escHtml(g.name)}</span>
      <span class="group-meta">${g.device_ids.length} Ger.</span>
    `;
    li.addEventListener('click', () => selectGroup(g.id));
    els.groupList.appendChild(li);
  });
}

function renderDeviceList() {
  const group = activeGroup();
  els.deviceList.innerHTML = '';

  devices.forEach(d => {
    const checked = group.device_ids.includes(d.id);
    const li = document.createElement('li');
    li.className = 'device-item';
    li.dataset.name = d.name.toLowerCase();

    li.innerHTML = `
      <input type="checkbox" id="dev-${d.id}" data-id="${d.id}" ${checked ? 'checked' : ''} />
      <div class="device-info">
        <div class="device-name">${escHtml(d.name)}</div>
        <div class="device-id">ID: ${d.id}</div>
      </div>
      <span class="device-type">${escHtml(d.type)}</span>
    `;
    els.deviceList.appendChild(li);
  });

  updateSaveHint();
  applyDeviceFilter();
}

function updateSaveHint() {
  const n = checkedDeviceIds().length;
  els.saveHint.textContent = `${n} Gerät${n !== 1 ? 'e' : ''} ausgewählt`;
}

function applyDeviceFilter() {
  const q = els.deviceSearch.value.toLowerCase().trim();
  els.deviceList.querySelectorAll('.device-item').forEach(li => {
    li.classList.toggle('hidden', q !== '' && !li.dataset.name.includes(q));
  });
}

/* ══════════════════════════════════════════════════════
   SELECTION
   ══════════════════════════════════════════════════════ */
function selectGroup(id) {
  activeGroupId = id;
  renderGroupList();

  const g = activeGroup();
  els.panelTitle.textContent = g.name;
  els.panelActions.style.display = 'flex';
  els.noSelection.style.display = 'none';
  els.deviceSection.style.display = 'flex';
  els.deviceSearch.value = '';
  renderDeviceList();
}

function activeGroup() {
  return groups.find(g => g.id === activeGroupId);
}

function checkedDeviceIds() {
  return [...els.deviceList.querySelectorAll('input[type="checkbox"]:checked')]
    .map(cb => parseInt(cb.dataset.id, 10));
}

/* ══════════════════════════════════════════════════════
   ACTIONS
   ══════════════════════════════════════════════════════ */
async function createGroup() {
  const name = els.newGroupName.value.trim();
  if (!name) { showToast('Bitte Gruppenname eingeben', 'error'); return; }
  if (groups.some(g => g.name.toLowerCase() === name.toLowerCase())) {
    showToast('Gruppe existiert bereits', 'error'); return;
  }
  try {
    const newGroup = await API.createGroup(name);
    groups.push(newGroup);
    els.newGroupName.value = '';
    renderGroupList();
    selectGroup(newGroup.id);
    showToast(`Gruppe „${name}" erstellt`, 'success');
  } catch (e) {
    showToast('Fehler: ' + e.message, 'error');
  }
}

async function saveAssignment() {
  if (!activeGroupId) return;
  const ids = checkedDeviceIds();
  if (ids.length === 0) {
    showToast('Mindestens ein Gerät muss ausgewählt sein', 'error');
    return;
  }
  try {
    await API.setDevices(activeGroupId, ids);
    const g = activeGroup();
    g.device_ids = ids;
    renderGroupList(); // aktualisiert Gerätezahl in der Liste
    showToast('Zuweisungen gespeichert', 'success');
  } catch (e) {
    showToast('Fehler: ' + e.message, 'error');
  }
}

async function deleteGroup() {
  if (!activeGroupId) return;
  const g = activeGroup();
  if (!confirm(`Gruppe „${g.name}" wirklich löschen?`)) return;
  try {
    await API.deleteGroup(activeGroupId);
    groups = groups.filter(x => x.id !== activeGroupId);
    activeGroupId = null;
    els.panelTitle.textContent = 'Gruppe auswählen';
    els.panelActions.style.display = 'none';
    els.noSelection.style.display = 'flex';
    els.deviceSection.style.display = 'none';
    renderGroupList();
    showToast(`Gruppe „${g.name}" gelöscht`, 'success');
  } catch (e) {
    showToast('Fehler: ' + e.message, 'error');
  }
}

function openRenameModal() {
  if (!activeGroupId) return;
  els.renameInput.value = activeGroup().name;
  els.renameModal.style.display = 'flex';
  setTimeout(() => {
    els.renameInput.focus();
    els.renameInput.select();
  }, 50);
}

function closeRenameModal() {
  els.renameModal.style.display = 'none';
}

async function confirmRename() {
  const name = els.renameInput.value.trim();
  if (!name) { showToast('Name darf nicht leer sein', 'error'); return; }
  if (groups.some(g => g.id !== activeGroupId && g.name.toLowerCase() === name.toLowerCase())) {
    showToast('Name bereits vergeben', 'error'); return;
  }
  try {
    await API.renameGroup(activeGroupId, name);
    activeGroup().name = name;
    els.panelTitle.textContent = name;
    renderGroupList();
    closeRenameModal();
    showToast(`Umbenannt in „${name}"`, 'success');
  } catch (e) {
    showToast('Fehler: ' + e.message, 'error');
  }
}

/* ══════════════════════════════════════════════════════
   EVENTS
   ══════════════════════════════════════════════════════ */
function bindEvents() {
  // Create group
  els.btnCreateGroup.addEventListener('click', createGroup);
  els.newGroupName.addEventListener('keydown', e => { if (e.key === 'Enter') createGroup(); });

  // Panel actions
  els.btnRenameGroup.addEventListener('click', openRenameModal);
  els.btnDeleteGroup.addEventListener('click', deleteGroup);

  // Device list
  els.deviceList.addEventListener('change', e => {
    if (e.target.matches('input[type="checkbox"]')) updateSaveHint();
  });
  els.btnSelectAll.addEventListener('click', () => {
    els.deviceList.querySelectorAll('input[type="checkbox"]:not(:disabled)').forEach(cb => {
      if (!cb.closest('.device-item').classList.contains('hidden')) cb.checked = true;
    });
    updateSaveHint();
  });
  els.btnDeselectAll.addEventListener('click', () => {
    els.deviceList.querySelectorAll('input[type="checkbox"]').forEach(cb => cb.checked = false);
    updateSaveHint();
  });
  els.deviceSearch.addEventListener('input', applyDeviceFilter);
  els.btnSaveAssignment.addEventListener('click', saveAssignment);

  // Rename modal
  els.btnRenameCancel.addEventListener('click', closeRenameModal);
  els.btnRenameConfirm.addEventListener('click', confirmRename);
  els.renameInput.addEventListener('keydown', e => {
    if (e.key === 'Enter')  confirmRename();
    if (e.key === 'Escape') closeRenameModal();
  });
  els.renameModal.addEventListener('click', e => {
    if (e.target === els.renameModal) closeRenameModal();
  });
}

/* ══════════════════════════════════════════════════════
   TOAST
   ══════════════════════════════════════════════════════ */
let toastTimer;
function showToast(msg, type = '') {
  clearTimeout(toastTimer);
  els.toast.textContent = msg;
  els.toast.className = 'toast' + (type ? ' ' + type : '') + ' show';
  toastTimer = setTimeout(() => els.toast.classList.remove('show'), 3000);
}

/* ══════════════════════════════════════════════════════
   UTILS
   ══════════════════════════════════════════════════════ */
function escHtml(str) {
  return str
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}
