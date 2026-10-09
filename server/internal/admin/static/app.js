// openbv's admin page: sign in (the admin token, or OIDC: authorization code + PKCE with a public
// client, as ../nowhereinparticular's support desk), then the sessions through /admin/api. Whatever
// players or logs say goes into the page as text, never as HTML.
'use strict';

const API = '/admin/api';
const REDIRECT = location.origin + '/admin/';
const $ = (id) => document.getElementById(id);
const types = ['Free for all', 'Team deathmatch', 'Capture the flag', 'Champion'];
const teams = { '-1': 'Spectator', '0': 'Blue', '1': 'Red', '2': 'Auto' };
const states = { '-1': 'Playing', '0': 'Blue won', '1': 'Red won', '2': 'Draw', '3': 'Round over', '4': 'Changing map' };
let cfg, oidc, auth = JSON.parse(sessionStorage.getItem('openbv-auth') || 'null');
let current = null, tab = 'overview', view = 'sessions', logSeq = 0, cvarsCache = [], allMaps = [];

// --- sign-in

const b64url = (bytes) => btoa(String.fromCharCode(...new Uint8Array(bytes))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
const random = (n) => b64url(crypto.getRandomValues(new Uint8Array(n)));

async function oidcSignIn() {
  const verifier = random(48), state = random(16);
  sessionStorage.setItem('openbv-pkce', JSON.stringify({ verifier, state }));
  const challenge = b64url(await crypto.subtle.digest('SHA-256', new TextEncoder().encode(verifier)));
  const u = new URL(oidc.authorization_endpoint);
  u.search = new URLSearchParams({ response_type: 'code', client_id: cfg.client_id, redirect_uri: REDIRECT,
    scope: 'openid profile', state, code_challenge: challenge, code_challenge_method: 'S256' });
  location.assign(u);
}

async function tokenRequest(params) {
  const r = await fetch(oidc.token_endpoint, { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: new URLSearchParams({ client_id: cfg.client_id, ...params }) });
  if (!r.ok) throw new Error('sign-in failed (' + r.status + ')');
  const t = await r.json();
  auth = { kind: 'oidc', access: t.access_token, refresh: t.refresh_token, id: t.id_token, until: Date.now() + (t.expires_in - 20) * 1000 };
  sessionStorage.setItem('openbv-auth', JSON.stringify(auth));
}

async function finishSignIn() {
  const q = new URLSearchParams(location.search);
  if (!q.has('code')) return;
  const saved = JSON.parse(sessionStorage.getItem('openbv-pkce') || 'null');
  sessionStorage.removeItem('openbv-pkce');
  history.replaceState(null, '', REDIRECT);
  if (!saved || saved.state !== q.get('state')) throw new Error('sign-in state mismatch: try again');
  await tokenRequest({ grant_type: 'authorization_code', code: q.get('code'), redirect_uri: REDIRECT, code_verifier: saved.verifier });
}

async function bearer() {
  if (!auth) return null;
  if (auth.kind === 'oidc' && Date.now() > auth.until) {
    try { await tokenRequest({ grant_type: 'refresh_token', refresh_token: auth.refresh }); } catch { signOut(false); return null; }
  }
  return auth.kind === 'oidc' ? auth.access : auth.token;
}

function signOut(atProvider = true) {
  const id = auth && auth.id, kind = auth && auth.kind;
  auth = null;
  sessionStorage.removeItem('openbv-auth');
  if (atProvider && kind === 'oidc' && oidc && oidc.end_session_endpoint) {
    const u = new URL(oidc.end_session_endpoint);
    u.search = new URLSearchParams({ client_id: cfg.client_id, post_logout_redirect_uri: REDIRECT, ...(id ? { id_token_hint: id } : {}) });
    location.assign(u);
    return;
  }
  show();
}

async function api(method, path, body) {
  const t = await bearer();
  const r = await fetch(API + path, { method, headers: { Authorization: 'Bearer ' + (t || ''), 'Content-Type': 'application/json' },
    body: body === undefined ? undefined : JSON.stringify(body) });
  if (r.status === 401) { signOut(false); throw new Error('please sign in again'); }
  if (!r.ok) { let e = r.statusText; try { e = (await r.json()).error || e; } catch {} throw new Error(e); }
  return r.status === 204 ? null : r.json().catch(() => null);
}

const showError = (e) => { $('error').textContent = e ? (e.message || String(e)) : ''; };
const guard = (f) => async (...a) => { try { showError(); await f(...a); } catch (e) { showError(e); } };

// --- small DOM helpers

function el(tag, props = {}, ...children) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(props)) {
    if (k === 'class') e.className = v;
    else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
    else if (k === 'text') e.textContent = v;
    else e.setAttribute(k, v);
  }
  for (const c of children) e.append(c instanceof Node ? c : document.createTextNode(String(c)));
  return e;
}
const btn = (text, onclick, cls = 'small') => el('button', { class: cls, type: 'button', onclick: guard(onclick) }, text);
const stat = (label, value) => el('div', { class: 'stat' }, el('b', { text: value }), el('span', { text: label }));
const fmtTime = (s) => { s = Math.max(0, Math.round(s)); return Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0'); };

// --- sessions

async function loadSessions() {
  const list = await api('GET', '/sessions');
  const ul = $('session-list');
  ul.replaceChildren(...list.map((s) => {
    const li = el('li', { class: s.id === current ? 'active' : '', onclick: () => { current = s.id; logSeq = 0; $('log').replaceChildren(); delete $('settings-form').dataset.for; refresh(); } },
      el('span', {}, s.status.name || s.settings.name, el('br'), el('small', { text: (types[s.status.gameType] || '') + ' · ' + s.status.map })),
      el('small', { text: s.status.players + '/' + s.status.maxPlayers }));
    return li;
  }));
  $('no-sessions').hidden = list.length > 0;
  if (current && !list.some((s) => s.id === current)) current = null;
  if (!current && list.length) current = list[0].id;
  const s = list.find((x) => x.id === current);
  $('detail').hidden = !s;
  if (s) renderSession(s);
}

function renderSession(s) {
  const st = s.status;
  $('d-name').textContent = st.name || s.settings.name;
  $('d-sub').textContent = `${types[st.gameType] || st.gameType} · ${st.map} · port ${st.port || '-'} · session ${s.id}` +
    (st.passworded ? ' · passworded' : '') + (st.public ? '' : ' · private');
  const stats = [stat('Players', st.players + ' / ' + st.maxPlayers), stat('State', states[st.roundState] ?? st.roundState),
    stat('Game time left', st.gameTimeLeft > 0 ? fmtTime(st.gameTimeLeft) : 'no limit'), stat('Next map', st.nextMap || '-')];
  if (st.gameType > 0) stats.push(stat('Blue / Red', `${st.blueScore} / ${st.redScore}  (${st.blueWin} / ${st.redWin} won)`));
  if (st.vote) stats.push(stat(`Vote by ${st.vote.from}: ${st.vote.what}`, `${st.vote.yes} yes, ${st.vote.no} no of ${st.vote.voters}, ${Math.ceil(st.vote.remaining)} s`));
  $('d-stats').replaceChildren(...stats);
  $('d-players').replaceChildren(...s.players.map((p) => el('tr', {},
    el('td', { text: p.id }),
    el('td', {}, p.name, p.admin ? el('span', { class: 'badge on', text: ' admin' }) : ''),
    el('td', { class: 'team-' + p.team, text: teams[p.team] || p.team }),
    el('td', { text: p.score }), el('td', { text: p.kills + ' / ' + p.deaths }),
    el('td', { text: p.ping < 0 ? '-' : p.ping + ' ms' }),
    el('td', { class: 'muted', text: p.remote + (p.mac ? '  ' + p.mac : '') }),
    el('td', { class: 'actions' },
      btn('Blue', () => playerAction(p, 'move', { team: 0 })), ' ',
      btn('Red', () => playerAction(p, 'move', { team: 1 })), ' ',
      btn('Spectate', () => playerAction(p, 'move', { team: -1 })), ' ',
      btn('Kick', () => confirm(`Kick ${p.name}?`) && playerAction(p, 'kick')), ' ',
      btn('Ban', () => confirm(`Ban ${p.name} (${p.remote})? They are disconnected and can't come back.`) && playerAction(p, 'ban'), 'small danger')))));
  $('d-noplayers').hidden = s.players.length > 0;
  renderRotation(st);
  if (tab === 'settings' && !$('settings-form').dataset.for) renderSettings(s);
}

async function playerAction(p, action, body = {}) {
  await api('POST', `/sessions/${current}/players/${p.id}/${action}`, body);
  await loadSessions();
}

function renderRotation(st) {
  $('rotation').replaceChildren(...st.rotation.map((m) => el('li', { class: m.toLowerCase() === st.map.toLowerCase() ? 'active' : '' },
    el('span', { text: m + (m.toLowerCase() === st.map.toLowerCase() ? '  (playing)' : '') }),
    m.toLowerCase() === st.map.toLowerCase() ? '' : btn('Remove', () => mapAction('remove', m)))));
}

async function mapAction(action, map) {
  const r = await api('POST', `/sessions/${current}/maps`, { action, map });
  if (r && r.output && r.output.length) showError(new Error(r.output.join(' · ')));
  await loadSessions();
}

// settings form (edit and create share it)
function settingsFields(form, s, creating) {
  const st = s ? s.settings : { name: 'Babo Violent 2 - Server', gameType: 0, maxPlayers: 16, port: 3333, maps: allMaps.slice(0, 1) };
  const field = (label, input, wide) => el('label', { class: wide ? 'wide' : '' }, label, input);
  const typeSel = el('select', { name: 'gameType' }, ...types.map((t, i) => { const o = el('option', { value: i, text: t }); if (i === st.gameType) o.selected = true; return o; }));
  const cvarsText = Object.entries(st.cvars || {}).map(([k, v]) => `${k} ${v}`).join('\n');
  form.replaceChildren(
    field('Name', el('input', { name: 'name', value: st.name || '', required: '', maxlength: 63 })),
    field('Game type', typeSel),
    field('Max players', el('input', { name: 'maxPlayers', type: 'number', min: 1, max: 32, value: st.maxPlayers || 16 })),
    field('Port (players connect to it)', el('input', { name: 'port', type: 'number', min: 1024, max: 65535, value: st.port || '' })),
    field('Maps, in order (comma-separated)', el('input', { name: 'maps', value: (st.maps || []).join(', '), list: 'all-maps' }), true),
    field(creating ? 'Password (empty: none)' : 'Password (empty: unchanged)', el('input', { name: 'password', type: 'password', maxlength: 15, autocomplete: 'new-password' })),
    field('In-game admin user', el('input', { name: 'adminUser', value: st.adminUser || '' })),
    field(creating ? 'In-game admin password' : 'In-game admin password (empty: unchanged)', el('input', { name: 'adminPassword', type: 'password', autocomplete: 'new-password' })),
    field('Votable commands (voteon, comma-separated: kick, changemap, sv_gameType)', el('input', { name: 'voteOn', value: (st.voteOn || []).join(', ') }), true),
    field('Other variables, one per line: sv_scoreLimit 30', el('textarea', { name: 'cvars', rows: 4, text: cvarsText }), true),
    el('label', { class: 'check' }, (() => { const c = el('input', { name: 'public', type: 'checkbox' }); c.checked = !st.private; return c; })(), el('span', { text: 'Listed in the Game Browser' })),
    el('div', { class: 'actions' },
      creating ? el('button', { type: 'button', onclick: () => $('create-dialog').close() }, 'Cancel') : '',
      el('button', { class: 'primary' }, creating ? 'Create' : 'Save and restart')));
}

function readSettings(form) {
  const f = new FormData(form);
  const list = (v) => String(v || '').split(',').map((x) => x.trim()).filter(Boolean);
  const cvars = {};
  for (const line of String(f.get('cvars') || '').split('\n')) {
    const m = line.trim().match(/^(\S+)\s+(.*)$/);
    if (m) cvars[m[1]] = m[2];
  }
  return { name: f.get('name'), gameType: +f.get('gameType'), maxPlayers: +f.get('maxPlayers'), port: +f.get('port') || 0,
    maps: list(f.get('maps')), password: f.get('password') || '', adminUser: f.get('adminUser') || '',
    adminPassword: f.get('adminPassword') || '', voteOn: list(f.get('voteOn')), cvars, private: !f.get('public') };
}

function renderSettings(s) {
  const form = $('settings-form');
  form.dataset.for = s.id;
  settingsFields(form, s, false);
}

$('settings-form').onsubmit = guard(async (ev) => {
  ev.preventDefault();
  if (!confirm('Save and restart the session? The players are disconnected.')) return;
  await api('PUT', '/sessions/' + current, readSettings(ev.target));
  delete $('settings-form').dataset.for;
  await loadSessions();
});

$('new-session').onclick = () => { settingsFields($('create-form'), null, true); $('create-dialog').showModal(); };
$('create-form').onsubmit = guard(async (ev) => {
  ev.preventDefault();
  const s = await api('POST', '/sessions', readSettings(ev.target));
  $('create-dialog').close();
  current = s.id;
  await loadSessions();
});

$('d-restart').onclick = guard(async () => { if (confirm('Restart the session? The players are disconnected.')) { await api('POST', `/sessions/${current}/restart`); await loadSessions(); } });
$('d-stop').onclick = guard(async () => { if (confirm('Stop the session and forget it?')) { await api('DELETE', '/sessions/' + current); current = null; await loadSessions(); } });

$('say').onsubmit = guard(async (ev) => {
  ev.preventDefault();
  const t = $('say-text').value.trim();
  if (!t) return;
  await api('POST', `/sessions/${current}/say`, { text: t });
  $('say-text').value = '';
});

// console: the log tail and commands
async function loadLog() {
  const lines = await api('GET', `/sessions/${current}/log?since=${logSeq}`);
  if (!lines.length) return;
  const pre = $('log'), atEnd = pre.scrollTop + pre.clientHeight >= pre.scrollHeight - 20;
  for (const l of lines) {
    logSeq = l.seq;
    pre.append(el('div', { class: l.level, text: new Date(l.at).toLocaleTimeString() + '  ' + l.text }));
  }
  while (pre.childElementCount > 1500) pre.firstChild.remove();
  if (atEnd) pre.scrollTop = pre.scrollHeight;
}

$('cmd').onsubmit = guard(async (ev) => {
  ev.preventDefault();
  const line = $('cmd-line').value.trim();
  if (!line) return;
  const pre = $('log');
  pre.append(el('div', { class: 'cmd', text: '> ' + line }));
  const r = await api('POST', `/sessions/${current}/command`, { line });
  for (const o of (r && r.output) || []) pre.append(el('div', { text: o }));
  pre.scrollTop = pre.scrollHeight;
  $('cmd-line').value = '';
});

// variables
async function loadCvars() {
  cvarsCache = await api('GET', `/sessions/${current}/cvars`);
  renderCvars();
}
function renderCvars() {
  const q = $('cvar-filter').value.toLowerCase();
  $('cvars').replaceChildren(...cvarsCache.filter((c) => !q || c.name.toLowerCase().includes(q) || c.help.toLowerCase().includes(q)).map((c) => {
    const input = c.kind === 'bool' ? el('select', {}, el('option', { text: 'true' }), el('option', { text: 'false' })) : el('input', { value: c.value });
    if (c.kind === 'bool') input.value = c.value;
    return el('tr', {}, el('td', {}, c.name, el('br'), el('small', { class: 'muted', text: c.help })), el('td', {}, input),
      el('td', { class: 'actions' }, btn('Set', async () => {
        const r = await api('POST', `/sessions/${current}/cvars`, { name: c.name, value: input.value });
        showError(r.output.some((o) => /Invalid|Unknown/.test(o)) ? new Error(r.output.join(' · ')) : null);
        await loadCvars();
      })));
  }));
}
$('cvar-filter').oninput = renderCvars;

$('addmap').onsubmit = guard(async (ev) => { ev.preventDefault(); await mapAction('add', $('addmap-name').value); });
$('changemap').onclick = guard(async () => { if (confirm('Change to ' + $('addmap-name').value + ' now?')) await mapAction('change', $('addmap-name').value); });
$('nextmap').onclick = guard(async () => { if (confirm('Go to the next map now?')) await mapAction('next', ''); });

// bans and audit
async function loadBans() {
  const list = await api('GET', '/bans');
  $('bans').replaceChildren(...list.map((b) => el('tr', {}, el('td', { text: b.index }), el('td', { text: b.name }), el('td', { text: b.ip || '' }),
    el('td', { text: b.mac || '' }), el('td', { text: new Date(b.at).toLocaleString() }), el('td', { text: b.by || '' }),
    el('td', { class: 'actions' }, btn('Unban', async () => { await api('DELETE', '/bans/' + b.index); await loadBans(); })))));
}
$('ban-form').onsubmit = guard(async (ev) => {
  ev.preventDefault();
  await api('POST', '/bans', { name: $('ban-name').value, ip: $('ban-ip').value, mac: $('ban-mac').value });
  for (const id of ['ban-name', 'ban-ip', 'ban-mac']) $(id).value = '';
  await loadBans();
});
async function loadAudit() {
  const list = (await api('GET', '/audit')).reverse();
  $('audit').replaceChildren(...list.map((e) => el('tr', {}, el('td', { text: new Date(e.at).toLocaleString() }), el('td', { text: e.who }),
    el('td', { text: e.action }), el('td', { text: e.session || '' }), el('td', { class: 'muted', text: e.detail || '' }))));
}

// --- tabs and refresh

for (const b of $('topnav').children) b.onclick = () => {
  view = b.dataset.view;
  for (const x of $('topnav').children) x.classList.toggle('active', x === b);
  for (const v of ['sessions', 'bans', 'audit']) $('view-' + v).hidden = v !== view;
  refresh();
};
for (const b of $('subtabs').children) b.onclick = () => {
  tab = b.dataset.tab;
  for (const x of $('subtabs').children) x.classList.toggle('active', x === b);
  for (const p of document.querySelectorAll('[data-pane]')) p.hidden = p.dataset.pane !== tab;
  delete $('settings-form').dataset.for;
  if (tab === 'cvars') guard(loadCvars)();
  refresh();
};

const refresh = guard(async () => {
  if (!auth) return;
  if (view === 'bans') return loadBans();
  if (view === 'audit') return loadAudit();
  await loadSessions();
  if (current && tab === 'console') await loadLog();
});

async function show() {
  const on = !!auth;
  $('signin').hidden = on;
  $('app').hidden = !on;
  $('signout').hidden = !on;
  $('token-form').hidden = on || cfg.auth !== 'token';
  $('oidc-signin').hidden = on || cfg.auth !== 'oidc';
  if (!on) return;
  try { $('who').textContent = (await api('GET', '/me')).who; } catch (e) { $('signin-error').textContent = e.message; return; }
  allMaps = await api('GET', '/maps');
  const dl = el('datalist', { id: 'all-maps' }, ...allMaps.map((m) => el('option', { value: m })));
  document.getElementById('all-maps')?.remove();
  document.body.append(dl);
  $('addmap-name').replaceChildren(...allMaps.map((m) => el('option', { value: m, text: m })));
  refresh();
}

$('token-form').onsubmit = (ev) => {
  ev.preventDefault();
  auth = { kind: 'token', token: $('token').value };
  sessionStorage.setItem('openbv-auth', JSON.stringify(auth));
  $('token').value = '';
  show();
};
$('oidc-signin').onclick = () => oidcSignIn();
$('signout').onclick = () => signOut();

async function main() {
  cfg = await (await fetch(API + '/config')).json();
  if (cfg.auth === 'oidc') {
    oidc = await (await fetch(cfg.issuer.replace(/\/$/, '') + '/.well-known/openid-configuration')).json();
    try { await finishSignIn(); } catch (e) { $('signin-error').textContent = e.message; }
  }
  await show();
  setInterval(() => { if (!document.hidden) refresh(); }, 2000);
}

main().catch((e) => { $('signin').hidden = false; $('signin-error').textContent = 'Can\'t start: ' + e.message; });
