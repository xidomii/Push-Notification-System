const API = "http://localhost:5000/api";

let devices = [];

/* ── Bootstrap ─────────────────────────────────────── */
fetchDevices();

/* ── Data ──────────────────────────────────────────── */
async function fetchDevices() {
  try {
    const res = await fetch(`${API}/devices`);
    if (!res.ok) throw new Error("Fetch failed");
    devices = await res.json();
    devices = devices.map(d => ({
      ...d,
      mac:       d.mac       || "–",
      status:    d.status    || "offline",
      last_seen: d.last_seen || null,
    }));
  } catch (e) {
    showToast("Geräte konnten nicht geladen werden", "error");
  }
  render();
}

/* ── Helpers ───────────────────────────────────────── */
function timeAgo(iso) {
  if (!iso) return "–";
  const s = Math.floor((Date.now() - new Date(iso + "Z")) / 1000);
  if (s < 60)   return `vor ${s} Sek.`;
  if (s < 3600) return `vor ${Math.floor(s / 60)} Min.`;
  return `vor ${Math.floor(s / 3600)} Std.`;
}

function escHtml(str) {
  return String(str)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
}

/* ── Render ────────────────────────────────────────── */
function render() {
  const q  = document.getElementById("search").value.toLowerCase();
  const sf = document.getElementById("filter").value;

  const list = devices.filter(d =>
    (sf === "all" || d.status === sf) &&
    (d.name.toLowerCase().includes(q) || d.mac.toLowerCase().includes(q))
  );

  document.getElementById("device-count").textContent =
    `${list.length} Gerät${list.length !== 1 ? "e" : ""}`;

  document.getElementById("tbody").innerHTML = list.length
    ? list.map(d => `
        <tr>
          <td>${escHtml(d.name)}</td>
          <td><code>${escHtml(d.mac)}</code></td>
          <td>
            <span class="status-badge ${d.status}">
              ${d.status === "online" ? "Online" : "Offline"}
            </span>
          </td>
          <td class="time-col">${timeAgo(d.last_seen)}</td>
          <td>
            <button class="btn-row-delete" onclick="del(${d.id})">Löschen</button>
          </td>
        </tr>`).join("")
    : `<tr class="empty-row"><td colspan="5">Keine Geräte gefunden</td></tr>`;
}

/* ── Delete ────────────────────────────────────────── */
async function del(id) {
  if (!confirm("Gerät wirklich löschen?")) return;
  try {
    const res = await fetch(`${API}/devices/${id}`, { method: "DELETE" });
    if (!res.ok) throw new Error();
    devices = devices.filter(d => d.id !== id);
    render();
    showToast("Gerät gelöscht", "success");
  } catch {
    showToast("Löschen fehlgeschlagen", "error");
  }
}

/* ── Add Form ──────────────────────────────────────── */
function toggleForm() {
  const f = document.getElementById("add-form");
  const open = f.style.display === "block";
  f.style.display = open ? "none" : "block";
  if (!open) document.getElementById("f-name").focus();
}

function fmtMac(input) {
  let v = input.value.replace(/[^0-9A-Fa-f]/g, "").toUpperCase();
  v = v.match(/.{1,2}/g)?.join(":") || v;
  input.value = v.slice(0, 17);
}

async function addDevice() {
  const name = document.getElementById("f-name").value.trim();
  const mac  = document.getElementById("f-mac").value.trim();

  if (!name)           { showToast("Name erforderlich", "error"); return; }
  if (mac.length !== 17) { showToast("Gültige MAC-Adresse erforderlich", "error"); return; }

  try {
    const res = await fetch(`${API}/devices`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ name, mac }),
    });
    if (res.status === 409) { showToast("MAC bereits registriert", "error"); return; }
    if (!res.ok) throw new Error();

    const device = await res.json();
    devices.push({
      ...device,
      mac:       device.mac       || mac,
      status:    device.status    || "offline",
      last_seen: device.last_seen || null,
    });
    document.getElementById("f-name").value = "";
    document.getElementById("f-mac").value  = "";
    toggleForm();
    render();
    showToast(`„${name}" hinzugefügt`, "success");
  } catch {
    showToast("Fehler beim Hinzufügen", "error");
  }
}

/* ── Toast ─────────────────────────────────────────── */
let toastTimer;
function showToast(msg, type = "") {
  clearTimeout(toastTimer);
  const el = document.getElementById("toast");
  el.textContent = msg;
  el.className = "toast" + (type ? " " + type : "") + " show";
  toastTimer = setTimeout(() => el.classList.remove("show"), 3000);
}
