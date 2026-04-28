'use strict';

/* ---- Auth ---- */
async function checkAuth(requireAdmin = false) {
    try {
        const r = await fetch('/api/me');
        if (!r.ok) { window.location = '/login.html'; return null; }
        const user = await r.json();
        if (requireAdmin && user.role !== 'admin') {
            window.location = '/dashboard.html'; return null;
        }
        return user;
    } catch {
        window.location = '/login.html'; return null;
    }
}

async function logout() {
    await fetch('/api/logout', { method: 'POST' });
    window.location = '/login.html';
}

/* ---- Alert ---- */
function showAlert(msg, type = 'info', container = '#alerts') {
    const el = document.querySelector(container);
    if (!el) return;
    const div = document.createElement('div');
    div.className = `alert alert-${type}`;
    div.textContent = msg;
    el.prepend(div);
    setTimeout(() => div.remove(), 5000);
}

/* ---- Sidebar ---- */
function initSidebar(user, activePage) {
    const s = document.getElementById('sidebar');
    if (!s) return;
    const adminLink = user.role === 'admin'
        ? `<hr style="border-color:#30363d;margin:8px 0">
           <a href="/users.html" class="nav-link ${activePage==='users'?'active':''}">
             <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16">
               <path d="M15 14s1 0 1-1-1-4-5-4-5 3-5 4 1 1 1 1zm-7.978-1A.261.261 0 0 1 7 12.996c.001-.264.167-1.03.76-1.72C8.312 10.629 9.282 10 11 10c1.717 0 2.687.63 3.24 1.276.593.69.758 1.457.76 1.72l-.008.002-.014.002zM11 7a2 2 0 1 0 0-4 2 2 0 0 0 0 4m3-2a3 3 0 1 1-6 0 3 3 0 0 1 6 0M6.936 9.28a6 6 0 0 0-1.23-.247A7 7 0 0 0 5 9c-4 0-5 3-5 4q0 1 1 1h4.216A2.24 2.24 0 0 1 5 13c0-1.01.377-2.042 1.09-2.904.243-.294.526-.569.846-.816M4.92 10A5.5 5.5 0 0 0 4 13H1c0-.26.164-1.03.76-1.724.545-.636 1.492-1.256 3.16-1.275ZM1.5 5.5a3 3 0 1 1 6 0 3 3 0 0 1-6 0m3-2a2 2 0 1 0 0 4 2 2 0 0 0 0-4"/>
             </svg>
             Users
           </a>` : '';
    s.innerHTML = `
      <div class="brand"><span class="re">Re</span><span class="boot">-BOOT</span><small>Web</small></div>
      <a href="/dashboard.html" class="nav-link ${activePage==='dashboard'?'active':''}">
        <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16">
          <path d="M1 2.5A1.5 1.5 0 0 1 2.5 1h3A1.5 1.5 0 0 1 7 2.5v3A1.5 1.5 0 0 1 5.5 7h-3A1.5 1.5 0 0 1 1 5.5zm8 0A1.5 1.5 0 0 1 10.5 1h3A1.5 1.5 0 0 1 15 2.5v3A1.5 1.5 0 0 1 13.5 7h-3A1.5 1.5 0 0 1 9 5.5zm-8 8A1.5 1.5 0 0 1 2.5 9h3A1.5 1.5 0 0 1 7 10.5v3A1.5 1.5 0 0 1 5.5 15h-3A1.5 1.5 0 0 1 1 13.5zm8 0A1.5 1.5 0 0 1 10.5 9h3a1.5 1.5 0 0 1 1.5 1.5v3a1.5 1.5 0 0 1-1.5 1.5h-3A1.5 1.5 0 0 1 9 13.5z"/>
        </svg>
        Dashboard
      </a>
      <a href="/new_job.html" class="nav-link ${activePage==='new_job'?'active':''}">
        <svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" fill="currentColor" viewBox="0 0 16 16">
          <path d="M8 15A7 7 0 1 1 8 1a7 7 0 0 1 0 14m0 1A8 8 0 1 0 8 0a8 8 0 0 0 0 16"/>
          <path d="M8 4a.5.5 0 0 1 .5.5v3h3a.5.5 0 0 1 0 1h-3v3a.5.5 0 0 1-1 0v-3h-3a.5.5 0 0 1 0-1h3v-3A.5.5 0 0 1 8 4"/>
        </svg>
        New Job
      </a>
      ${adminLink}
      <div style="flex:1"></div>
      <div style="padding:8px 12px;font-size:12px;color:var(--muted);border-top:1px solid var(--border);margin-top:8px;padding-top:12px;">
        <div>${user.username}</div>
        <a href="#" onclick="logout()" style="font-size:11px;color:var(--muted);">Sign out</a>
      </div>`;
}

/* ---- Badge helpers ---- */
const STATUS_CLASS = {
    pending: 'badge-pending', running: 'badge-running',
    success: 'badge-success', failed: 'badge-failed',
};
function statusBadge(s) {
    const cls = STATUS_CLASS[s] || 'badge-pending';
    const dot = s === 'running'
        ? '<span class="live-dot" style="width:6px;height:6px;margin-right:3px;"></span>' : '';
    return `<span class="badge ${cls}">${dot}${s}</span>`;
}
function ifaceBadge(iface) {
    return `<span class="badge badge-${iface}">${iface}</span>`;
}

window.logout = logout;
window.checkAuth = checkAuth;
window.showAlert = showAlert;
window.initSidebar = initSidebar;
window.statusBadge = statusBadge;
window.ifaceBadge = ifaceBadge;
