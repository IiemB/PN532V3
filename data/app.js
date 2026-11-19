const state = {
  status: {},
  tag: null,
  tagPresent: false,
  tagReceivedAt: 0,
  history: [],
  lastAction: null,
};

const $ = (selector) => document.querySelector(selector);

function fmtDuration(ms) {
  if (!ms || ms < 0) return '0s';
  const s = Math.floor(ms / 1000);
  if (s < 60) return `${s}s`;
  const m = Math.floor(s / 60);
  if (m < 60) return `${m}m ${s % 60}s`;
  const h = Math.floor(m / 60);
  return `${h}h ${m % 60}m`;
}

function fmtBytes(n) {
  return `${n} B`;
}

async function api(path, body) {
  const options = { headers: { 'Content-Type': 'application/json' } };
  if (body !== undefined) {
    options.method = 'POST';
    options.body = JSON.stringify(body);
  }
  const response = await fetch(path, options);
  let data = {};
  try {
    data = await response.json();
  } catch (error) {
    data = { ok: false, code: 'ERR_HTTP', message: `HTTP ${response.status}` };
  }
  if (!response.ok && data.ok === undefined) {
    data.ok = false;
  }
  return data;
}

function toast(message, kind = '') {
  const node = $('#toast');
  node.textContent = message;
  node.className = `toast ${kind}`;
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => node.classList.add('hidden'), 4200);
}

function setPill(selector, text, kind) {
  const node = $(selector);
  node.textContent = text;
  node.className = `pill ${kind || ''}`;
}

function appendLog(line) {
  const log = $('#log');
  const stamp = new Date().toLocaleTimeString();
  log.textContent += `[${stamp}] ${line}\n`;
  if (log.textContent.length > 20000) {
    log.textContent = log.textContent.slice(-15000);
  }
  log.scrollTop = log.scrollHeight;
}

function renderStatus() {
  const status = state.status || {};
  setPill('#pill-reader', status.readerReady ? 'reader ready' : 'reader offline',
    status.readerReady ? 'ok' : 'error');
  setPill('#pill-net', `${status.mode || '—'} · ${status.ip || '—'}`,
    status.mode === 'STA' ? 'ok' : 'warn');
  setPill('#pill-uptime', `up ${fmtDuration(status.uptimeMs || 0)}`);
  $('#version').textContent =
    `v${status.version || '?'} · PN532 fw ${status.firmware || '?'} · ${status.scans || 0} scans`;

  const sourceUid = status.sourceUid || '';
  if (sourceUid) {
    const compact = sourceUid.replace(/\s+/g, '');
    const entry = state.history.find((item) => item.uidCompact === compact);
    $('#source-uid').textContent = sourceUid;
    $('#source-uid').classList.remove('muted');
    $('#source-meta').textContent = entry
      ? `${entry.model || entry.kind} · ${entry.records} record(s) · ${fmtDuration(entry.ageMs)} ago`
      : 'selected';
  } else {
    $('#source-uid').textContent = 'not selected';
    $('#source-uid').classList.add('muted');
    $('#source-meta').textContent = 'Scan a tag, then use “Set as clone source”.';
  }

  updateTarget();
}

function updateTarget() {
  const tag = state.tag;
  const hasTag = !!(tag && tag.uid);
  if (hasTag && state.tagPresent) {
    $('#target-uid').textContent = tag.uid;
    $('#target-uid').classList.remove('muted');
    $('#target-meta').textContent = `${tag.model || tag.kind} · ${fmtBytes(tag.totalBytes)}`;
  } else {
    $('#target-uid').textContent = hasTag ? `${tag.uid} (removed)` : 'waiting for tag';
    $('#target-uid').classList.add('muted');
    $('#target-meta').textContent = hasTag ? 'place the target tag on the reader' : '—';
  }
}

function renderTag() {
  const tag = state.tag;
  const hasTag = !!(tag && tag.uid);

  $('#tag-empty').classList.toggle('hidden', hasTag);
  $('#tag-view').classList.toggle('hidden', !hasTag);
  if (!hasTag) {
    updateTarget();
    return;
  }

  $('#tag-uid').textContent = tag.uid;
  $('#tag-model').textContent = tag.model || tag.kind || 'tag';
  $('#tag-kind').textContent = tag.kind || '—';
  renderTagProperties(tag);
  $('#tag-capacity').textContent = `${tag.capacity} / ${tag.totalBytes} B`;
  $('#tag-state').textContent = state.tagPresent
    ? (tag.complete ? 'complete dump' : `incomplete: ${tag.error || 'read error'}`)
    : 'removed (snapshot kept)';

  renderAge();
  renderRecords(tag);
  renderMemory(tag);
  updateTarget();
}

// SAK -> type per NXP AN10833 (MIFARE type identification procedure).
const SAK_TYPES = {
  0x00: 'MIFARE Ultralight / NTAG',
  0x01: 'MIFARE Classic 1K (TNP3xxx)',
  0x08: 'MIFARE Classic 1K',
  0x09: 'MIFARE Mini',
  0x10: 'MIFARE Plus 2K (SL2)',
  0x11: 'MIFARE Plus 4K (SL2)',
  0x18: 'MIFARE Classic 4K',
  0x20: 'ISO 14443-4 (DESFire / Plus SL3 / smart card)',
  0x28: 'MIFARE Classic 1K emulation (SmartMX)',
  0x38: 'MIFARE Classic 4K emulation (SmartMX)',
  0x88: 'MIFARE Classic 1K (Infineon)',
  0x98: 'MIFARE Pro',
};

// ISO/IEC 7816-6 manufacturer codes seen on ISO 14443A tags (UID byte 0).
const MANUFACTURERS = {
  0x02: 'STMicroelectronics',
  0x04: 'NXP Semiconductors',
  0x05: 'Infineon',
  0x07: 'Texas Instruments',
  0x16: 'EM Microelectronic',
  0x28: 'Fudan Microelectronics',
};

function tagTypeName(atqa, sak) {
  if (sak === 0x20 && atqa === 0x0344) return 'MIFARE DESFire';
  if (sak === 0x20 && (atqa === 0x0002 || atqa === 0x0004)) return 'MIFARE Plus (SL3)';
  if (sak in SAK_TYPES) return SAK_TYPES[sak];
  return sak & 0x20 ? 'ISO 14443-4 compliant' : 'unknown ISO 14443A';
}

function renderTagProperties(tag) {
  const hex = (value) => (value ? `0x${value}` : '—');
  const atqa = parseInt(tag.atqa, 16);
  const sak = parseInt(tag.sak, 16);
  const known = typeof tag.atqa === 'string';
  const firstByte = parseInt((tag.uidCompact || '').slice(0, 2), 16);
  const uidSizes = { 4: 'single (4 B)', 7: 'double (7 B)', 10: 'triple (10 B)' };

  let maker = '—';
  if (tag.uidLength === 4) {
    maker = firstByte === 0x08 ? 'n/a (random UID)' : 'n/a (4-byte NUID)';
  } else if (tag.uidLength > 4) {
    maker = MANUFACTURERS[firstByte] || `unknown (0x${tag.uidCompact.slice(0, 2)})`;
  }

  $('#tag-type').textContent = known ? tagTypeName(atqa, sak) : '—';
  $('#tag-sak').textContent = hex(tag.sak);
  $('#tag-atqa').textContent = tag.atqa ? `0x${tag.atqa.slice(0, 2)} 0x${tag.atqa.slice(2)}` : '—';
  $('#tag-uid-size').textContent = uidSizes[tag.uidLength] || (tag.uidLength ? `${tag.uidLength} B` : '—');
  $('#tag-protocol').textContent = known
    ? (sak & 0x20 ? 'ISO 14443-4 (T=CL)' : 'ISO 14443-3A')
    : '—';
  $('#tag-maker').textContent = maker;
}

function renderAge() {
  if (!state.tagReceivedAt) return;
  const age = Date.now() - state.tagReceivedAt;
  $('#tag-age').textContent = age < 1500 ? 'just now' : `${fmtDuration(age)} ago`;
}

function renderRecords(tag) {
  const host = $('#view-records');
  host.innerHTML = '';
  const records = tag.records || [];
  if (!records.length) {
    const note = document.createElement('p');
    note.className = 'empty';
    note.textContent = tag.ndef
      ? 'NDEF container found, but no supported records.'
      : 'No NDEF records on this tag.';
    host.appendChild(note);
    return;
  }

  for (const record of records) {
    const card = document.createElement('div');
    card.className = 'record';

    const head = document.createElement('div');
    head.className = 'record-head';
    const chip = document.createElement('span');
    chip.className = 'chip';
    chip.textContent = record.type;
    head.appendChild(chip);
    if (record.language) {
      const lang = document.createElement('span');
      lang.className = 'muted';
      lang.style.fontSize = '11px';
      lang.textContent = `lang ${record.language}${record.utf16 ? ' · UTF-16' : ''}`;
      head.appendChild(lang);
    }
    if (record.tnf) {
      const tnf = document.createElement('span');
      tnf.className = 'muted';
      tnf.style.fontSize = '11px';
      tnf.textContent = record.tnf;
      head.appendChild(tnf);
    }
    card.appendChild(head);

    const value = document.createElement('div');
    value.className = 'record-value';
    if (record.type === 'uri' && /^(https?|tel|mailto):/i.test(record.value)) {
      const link = document.createElement('a');
      link.href = record.value;
      link.target = '_blank';
      link.rel = 'noopener noreferrer';
      link.textContent = record.value;
      value.appendChild(link);
    } else {
      value.textContent = record.value;
    }
    card.appendChild(value);
    host.appendChild(card);
  }
}

function renderMemory(tag) {
  const host = $('#view-memory');
  const bytes = (tag.memory || '').match(/.{2}/g) || [];
  const chunk = tag.chunkSize || 4;
  const isType2 = tag.kind === 'Type 2';

  const cells = [];
  for (let i = 0; i < bytes.length; i += chunk) {
    const index = i / chunk;
    const hex = bytes.slice(i, i + chunk).join(' ');
    const classes = ['mem-cell'];
    if (isType2 && index < 4) classes.push('locked');
    if (isType2 && index === 4) classes.push('ndef');
    if (!isType2 && index % 4 === 3) classes.push('locked');
    const label = isType2
      ? index.toString(16).toUpperCase().padStart(2, '0')
      : index;
    cells.push(
      `<div class="${classes.join(' ')}"><span class="mem-index">${label}</span><span class="mem-hex">${hex}</span></div>`
    );
  }

  host.innerHTML = `<div class="mem-grid">${cells.join('')}</div>`;
}

function renderHistory() {
  const host = $('#history');
  host.innerHTML = '';
  if (!state.history.length) {
    const item = document.createElement('li');
    item.className = 'muted';
    item.textContent = 'No tags seen yet.';
    host.appendChild(item);
    return;
  }

  for (const entry of state.history) {
    const item = document.createElement('li');
    item.title = 'Set as clone source';
    const left = document.createElement('div');
    const uid = document.createElement('div');
    uid.className = 'h-uid';
    uid.textContent = entry.uid;
    const meta = document.createElement('div');
    meta.className = 'h-meta';
    meta.textContent = `${entry.model || entry.kind} · ${entry.records} record(s) · ${fmtDuration(entry.ageMs)} ago`;
    left.appendChild(uid);
    left.appendChild(meta);
    item.appendChild(left);
    item.addEventListener('click', () => setSource(entry.uidCompact));
    host.appendChild(item);
  }
}

function renderResult(hostSelector, result) {
  const host = $(hostSelector);
  host.classList.remove('hidden');
  host.classList.toggle('fail', !result.ok);
  host.innerHTML = '';

  const title = document.createElement('h3');
  if (result.armed) {
    title.textContent = `Waiting — ${result.message || 'place the target tag'}`;
    host.appendChild(title);
    return;
  }
  title.textContent = `${result.ok ? 'Verified' : result.code || 'Failed'} — ${result.message || ''}`;
  host.appendChild(title);

  const metric = document.createElement('p');
  metric.className = 'metric';
  const uidText = result.uidAttempted
    ? ` · UID ${result.uidMatch ? 'matched' : 'not changed'}`
    : '';
  metric.textContent =
    `${result.fieldsMatched}/${result.fieldsTotal} fields matched · ` +
    `${result.bytesWritten} bytes written${uidText}`;
  host.appendChild(metric);

  if (result.mismatches && result.mismatches.length) {
    const details = document.createElement('details');
    const summary = document.createElement('summary');
    summary.textContent = `${result.mismatches.length} mismatch(es)`;
    details.appendChild(summary);
    const list = document.createElement('ul');
    for (const line of result.mismatches) {
      const item = document.createElement('li');
      item.textContent = line;
      list.appendChild(item);
    }
    details.appendChild(list);
    host.appendChild(details);
  }

  if (result.notes && result.notes.length) {
    const details = document.createElement('details');
    const summary = document.createElement('summary');
    summary.textContent = `${result.notes.length} note(s)`;
    details.appendChild(summary);
    const list = document.createElement('ul');
    for (const line of result.notes) {
      const item = document.createElement('li');
      item.textContent = line;
      list.appendChild(item);
    }
    details.appendChild(list);
    host.appendChild(details);
  }
}

function addRecordRow(type = 'text', value = '', language = 'en') {
  const host = $('#record-editor');
  const row = document.createElement('div');
  row.className = 'record-row';
  row.innerHTML = `
    <select class="rec-type">
      <option value="text">Text</option>
      <option value="uri">URI</option>
    </select>
    <input class="rec-value" placeholder="hello tag" />
    <input class="rec-lang" value="en" title="language code" />
    <button class="ghost rec-remove" type="button" title="Remove">×</button>`;
  row.querySelector('.rec-type').value = type;
  row.querySelector('.rec-value').value = value;
  row.querySelector('.rec-lang').value = language;
  const langInput = row.querySelector('.rec-lang');
  const typeSelect = row.querySelector('.rec-type');
  const syncLang = () => {
    langInput.style.visibility = typeSelect.value === 'text' ? 'visible' : 'hidden';
  };
  typeSelect.addEventListener('change', syncLang);
  syncLang();
  row.querySelector('.rec-remove').addEventListener('click', () => {
    if (host.children.length > 1) row.remove();
  });
  host.appendChild(row);
}

function collectRecords() {
  const records = [];
  for (const row of $('#record-editor').querySelectorAll('.record-row')) {
    const type = row.querySelector('.rec-type').value;
    const value = row.querySelector('.rec-value').value.trim();
    const language = row.querySelector('.rec-lang').value.trim() || 'en';
    if (!value) continue;
    records.push(type === 'text' ? { type, value, language } : { type, value });
  }
  return records;
}

async function withBusy(button, task) {
  button.disabled = true;
  try {
    return await task();
  } finally {
    button.disabled = false;
  }
}

async function refreshStatus() {
  state.status = await api('/api/status');
  renderStatus();
}

async function refreshTag() {
  const data = await api('/api/tag');
  if (data.tag) {
    applyTag(data.tag, data.present);
  }
}

async function refreshHistory() {
  const data = await api('/api/history');
  state.history = data.items || [];
  renderHistory();
  renderStatus();
}

function applyTag(tag, present) {
  state.tag = tag;
  state.tagPresent = present;
  state.tagReceivedAt = Date.now();
  renderTag();
}

async function setSource(uid) {
  const body = uid ? { uid } : {};
  const result = await api('/api/source', body);
  if (result.ok) {
    toast(`Clone source set: ${result.sourceUid}`, 'ok');
    await refreshStatus();
    await refreshHistory();
  } else {
    toast(result.message || 'Could not set source', 'error');
  }
}

async function reread() {
  const button = $('#btn-reread');
  await withBusy(button, async () => {
    const result = await api('/api/read', {});
    if (result.ok && result.tag) {
      applyTag(result.tag, true);
      toast('Tag re-read', 'ok');
    } else {
      toast(result.message || 'Read failed', 'error');
    }
  });
}

async function cloneTag() {
  const button = $('#btn-clone');
  state.lastAction = 'clone';
  await withBusy(button, async () => {
    const body = {
      writeUid: $('#opt-uid').checked,
      includeTrailers: $('#opt-trailers').checked,
    };
    const result = await api('/api/clone', body);
    renderResult('#result', result);
    if (result.ok) {
      toast('Clone verified', 'ok');
    } else {
      toast(result.message || 'Clone failed', 'error');
    }
    await refreshTag();
    await refreshHistory();
    await refreshStatus();
  });
}

async function writeRecords() {
  const button = $('#btn-write');
  state.lastAction = 'write';
  const records = collectRecords();
  if (!records.length) {
    toast('Add at least one record with a value', 'error');
    return;
  }

  await withBusy(button, async () => {
    const result = await api('/api/write', { records });
    renderResult('#write-result', result);
    if (result.ok) {
      toast('Records written and verified', 'ok');
    } else {
      toast(result.message || 'Write failed', 'error');
    }
    await refreshTag();
    await refreshHistory();
    await refreshStatus();
  });
}

async function scanNetworks() {
  const list = $('#wifi-list');
  list.classList.remove('hidden');
  list.innerHTML = '<li class="muted">Scanning…</li>';
  const result = await api('/api/wifi/scan');
  list.innerHTML = '';
  if (!result.ok || !result.networks) {
    list.innerHTML = '<li class="muted">Scan failed</li>';
    return;
  }
  for (const network of result.networks) {
    const item = document.createElement('li');
    const name = document.createElement('span');
    name.textContent = network.ssid || '(hidden)';
    const meta = document.createElement('span');
    meta.className = 'muted';
    meta.textContent = `${network.rssi} dBm${network.encryption ? ' · locked' : ''}`;
    item.appendChild(name);
    item.appendChild(meta);
    item.addEventListener('click', () => {
      $('#wifi-ssid').value = network.ssid;
      $('#wifi-pass').focus();
    });
    list.appendChild(item);
  }
}

function saveWifi(event) {
  event.preventDefault();
  const ssid = $('#wifi-ssid').value.trim();
  const password = $('#wifi-pass').value;
  if (!ssid) {
    toast('Enter an SSID', 'error');
    return;
  }
  api('/api/wifi', { ssid, password }).then((result) => {
    if (result.ok) {
      toast('Saved, board is rebooting — reconnect to your network', 'ok');
    } else {
      toast(result.message || 'Save failed', 'error');
    }
  });
}

function connectWebSocket() {
  const protocol = location.protocol === 'https:' ? 'wss' : 'ws';
  let socket;
  try {
    socket = new WebSocket(`${protocol}://${location.hostname}:81/`);
  } catch (error) {
    setTimeout(connectWebSocket, 3000);
    return;
  }

  socket.addEventListener('message', (event) => {
    let message;
    try {
      message = JSON.parse(event.data);
    } catch (error) {
      return;
    }
    if (message.event === 'status') {
      state.status = message;
      renderStatus();
    } else if (message.event === 'tag' && message.tag) {
      applyTag(message.tag, message.present !== false);
      refreshHistory();
    } else if (message.event === 'removed') {
      state.tagPresent = false;
      renderTag();
    } else if (message.event === 'log') {
      appendLog(message.message);
    } else if (message.event === 'result') {
      const host = state.lastAction === 'write' ? '#write-result' : '#result';
      renderResult(host, message);
    }
  });

  socket.addEventListener('close', () => setTimeout(connectWebSocket, 3000));
  socket.addEventListener('open', () => {
    clearInterval(connectWebSocket.ping);
    connectWebSocket.ping = setInterval(() => {
      if (socket.readyState === WebSocket.OPEN) socket.send('ping');
    }, 25000);
  });
}

async function boot() {
  addRecordRow();
  try {
    await Promise.all([refreshStatus(), refreshTag(), refreshHistory()]);
  } catch (error) {
    toast('Could not reach the board', 'error');
  }

  $('#btn-reread').addEventListener('click', reread);
  $('#btn-source').addEventListener('click', () => setSource(null));
  $('#btn-clone').addEventListener('click', cloneTag);
  $('#btn-write').addEventListener('click', writeRecords);
  $('#btn-add-record').addEventListener('click', () => addRecordRow());
  $('#btn-scan').addEventListener('click', scanNetworks);
  $('#wifi-form').addEventListener('submit', saveWifi);
  $('#btn-clear-log').addEventListener('click', () => {
    $('#log').textContent = '';
  });

  for (const tab of document.querySelectorAll('.tab')) {
    tab.addEventListener('click', () => {
      for (const other of document.querySelectorAll('.tab')) {
        other.classList.toggle('active', other === tab);
      }
      $('#view-records').classList.toggle('hidden', tab.dataset.view !== 'records');
      $('#view-memory').classList.toggle('hidden', tab.dataset.view !== 'memory');
    });
  }

  connectWebSocket();
  setInterval(refreshStatus, 2500);
  setInterval(refreshTag, 5000);
  setInterval(renderAge, 1000);
}

document.addEventListener('DOMContentLoaded', boot);
