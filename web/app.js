import { EXAMPLES } from './examples.js';

const $ = (id) => document.getElementById(id);
const code = $('code'), gutter = $('gutter'), stdin = $('stdin'), view = $('view'), messages = $('messages');
const stdinForm = $('stdin-form'), interactiveValue = $('interactive-value');
const PHASES = ['Lexical analysis', 'Syntax analysis (CLR(1))', 'Semantic analysis', 'Intermediate code', 'Optimization', 'Execution'];
const DEFAULT_TIMEOUT_MS = 15000;

let worker = null, workerReady = false, runId = 0, pending = null, timer = null;
let current = { sections: {}, tab: 'output', stderr: '', failed: false };

// ---------------------------------------------------------------- storage helpers
function store(key, value) { try { localStorage.setItem(key, value); } catch (e) { /* private mode */ } }
function load(key) { try { return localStorage.getItem(key); } catch (e) { return null; } }

function b64encode(s) { return btoa(String.fromCharCode(...new TextEncoder().encode(s))).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, ''); }
function b64decode(s) {
  const bin = atob(s.replace(/-/g, '+').replace(/_/g, '/'));
  return new TextDecoder().decode(Uint8Array.from(bin, (c) => c.charCodeAt(0)));
}

// ---------------------------------------------------------------- editor
let errorLines = new Set();
function updateGutter() {
  const n = code.value.split('\n').length;
  let html = '';
  for (let i = 1; i <= n; i++) html += errorLines.has(i) ? `<span class="err">${i}</span>\n` : `${i}\n`;
  gutter.innerHTML = html.replace(/\n$/, '');
  code.style.height = Math.max(n * 20 + 20, 300) + 'px';
}
code.addEventListener('input', () => { errorLines = new Set(); updateGutter(); store('nova.code', code.value); });
stdin.addEventListener('input', () => store('nova.stdin', stdin.value));
code.addEventListener('keydown', (e) => {
  if (e.key === 'Tab' && !e.ctrlKey && !e.metaKey && !e.altKey) {
    e.preventDefault();
    const s = code.selectionStart, t = code.selectionEnd;
    code.setRangeText('    ', s, t, 'end');
    code.dispatchEvent(new Event('input'));
  }
});
document.addEventListener('keydown', (e) => {
  if ((e.ctrlKey || e.metaKey) && e.key === 'Enter') { e.preventDefault(); runProgram(); }
});

function jumpTo(line, col) {
  const lines = code.value.split('\n');
  let pos = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1;
  pos += Math.max(0, Math.min((col || 1) - 1, (lines[line - 1] || '').length));
  code.focus();
  code.setSelectionRange(pos, pos);
  const box = code.parentElement;
  box.scrollTop = Math.max(0, (line - 4) * 20);
}

// ---------------------------------------------------------------- examples and sharing
const sel = $('example');
sel.innerHTML = '<option value="">Choose an example...</option>' + EXAMPLES.map((e, i) => `<option value="${i}">${e.title}</option>`).join('');
sel.addEventListener('change', () => {
  if (sel.value === '') return;
  const ex = EXAMPLES[+sel.value];
  code.value = ex.source; stdin.value = ex.stdin || '';
  errorLines = new Set(); updateGutter();
  store('nova.code', code.value); store('nova.stdin', stdin.value);
  resetResults();
});

$('share').addEventListener('click', async () => {
  const url = location.href.split('#')[0] + '#code=' + b64encode(code.value) + (stdin.value ? '&in=' + b64encode(stdin.value) : '');
  history.replaceState(null, '', url);
  try { await navigator.clipboard.writeText(url); $('status').textContent = 'Link copied to the clipboard.'; }
  catch (e) { $('status').textContent = 'Link placed in the address bar.'; }
});

function download(name, text, type) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type }));
  a.download = name; a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
$('download-leo').addEventListener('click', () => download('program.leo', code.value, 'text/plain'));
$('download-c').addEventListener('click', () => download('program.c', current.sections.c || '', 'text/x-csrc'));

// ---------------------------------------------------------------- worker handling
function startWorker() {
  if (worker) worker.terminate();
  workerReady = false;
  worker = new Worker('worker.js?v=leo1', { type: 'module' });
  worker.onmessage = onWorkerMessage;
  worker.onerror = (e) => finishWithError('The compiler could not be started (' + (e.message || 'worker error') + '). Serve this folder over http, for example: python3 -m http.server');
}

function onWorkerMessage(e) {
  const m = e.data;
  if ('ready' in m) {
    workerReady = m.ready;
    if (!m.ready) finishWithError('Could not load the compiler: ' + m.error);
    else if (!pending) $('status').textContent = 'Compiler loaded.';
    return;
  }
  if (!pending || m.id !== pending.id) return;
  clearTimeout(timer);
  const p = pending; pending = null;
  setBusy(false);
  if (!m.ok) { finishWithError(m.error); return; }
  showResult(m, p);
}

function setBusy(busy) {
  $('run').disabled = busy;
  $('stop').hidden = !busy;
  if (busy) $('status').textContent = 'Running...';
}

function finishWithError(text) {
  if (timer) clearTimeout(timer);
  pending = null;
  setBusy(false);
  setView(text, false);
  $('status').textContent = '';
}

$('stop').addEventListener('click', () => stopRun('Stopped by you. The program was terminated.'));
function stopRun(text) {
  if (!pending) return;
  clearTimeout(timer);
  pending = null;
  startWorker();
  setBusy(false);
  setView(text, false);
  $('status').textContent = '';
}

function runProgram() {
  if (pending) return;
  if (!worker) startWorker();
  resetResults();
  const id = ++runId;
  pending = { id };
  setBusy(true);
  worker.postMessage({ id, source: code.value, stdin: stdin.value, optimize: $('optimize').checked });
  timer = setTimeout(() => stopRun(`The program ran for more than ${DEFAULT_TIMEOUT_MS / 1000} seconds and was stopped (an endless loop?).`), DEFAULT_TIMEOUT_MS);
}
$('run').addEventListener('click', runProgram);
stdinForm.addEventListener('submit', (e) => {
  e.preventDefault();
  const value = interactiveValue.value.trim();
  if (!value) { interactiveValue.focus(); return; }
  stdin.value = stdin.value.trim() ? `${stdin.value.trim()}\n${value}` : value;
  store('nova.stdin', stdin.value);
  stdinForm.hidden = true;
  runProgram();
});

// ---------------------------------------------------------------- results
function resetResults() {
  current = { sections: {}, tab: current.tab || 'output', stderr: '', failed: false };
  $('phases').innerHTML = '';
  messages.hidden = true; messages.innerHTML = '';
  $('download-c').hidden = true;
  stdinForm.hidden = true;
  errorLines = new Set(); updateGutter();
}

function setView(text, placeholder) {
  view.textContent = text;
  view.classList.toggle('placeholder', !!placeholder);
}

function parseSections(stdout) {
  const sections = {};
  const parts = stdout.split(/\n?@@LEO-SECTION ([a-z-]+)@@\n/);
  // parts: [before, name1, body1, name2, body2, ...]
  for (let i = 1; i < parts.length; i += 2) sections[parts[i]] = parts[i + 1];
  return sections;
}

function parsePhases(stderr) {
  const states = {};
  for (const line of stderr.split('\n')) {
    const m = line.match(/^\[(\d)\/6\] (.+?)\s+(OK|FAILED|SKIPPED|WAITING)\s*(.*)$/);
    if (m) states[+m[1]] = { state: m[3], detail: m[4] };
  }
  return states;
}

function parseDiagnostics(stderr) {
  const out = [];
  let cur = null;
  for (const line of stderr.split('\n')) {
    let m = line.match(/^\/prog\.leo:(\d+):(\d+): (.*)$/);
    if (m) {
      const text = m[3];
      const kind = /warning/.test(text.split(':')[0]) ? 'warning' : 'error';
      cur = { line: +m[1], col: +m[2], kind, text, extra: [] };
      out.push(cur);
      continue;
    }
    m = line.match(/^runtime error \(line (\d+)\): (.*)$/);
    if (m) { cur = { line: +m[1], col: 1, kind: 'runtime', text: 'runtime error: ' + m[2], extra: [] }; out.push(cur); continue; }
    if (cur && /^\s*\d*\s*\|/.test(line)) { cur.extra.push(line); continue; }
    if (line.startsWith('leoc:') && !line.startsWith('leoc: compiling')) {
      cur = { line: 0, col: 0, kind: 'error', text: line, extra: [] };
      out.push(cur);
    }
  }
  return out;
}

function showResult(m, p) {
  const sections = parseSections(m.stdout);
  const states = parsePhases(m.stderr);
  const diags = parseDiagnostics(m.stderr);
  current.sections = sections;
  current.stderr = m.stderr;

  // phase chips
  const ph = $('phases');
  ph.innerHTML = '';
  let failed = false;
  PHASES.forEach((name, i) => {
    const s = states[i + 1];
    const chip = document.createElement('span');
    let cls = 'skip', detail = failed ? 'not reached' : '';
    if (s) { cls = s.state === 'OK' ? 'ok' : s.state === 'FAILED' ? 'fail' : s.state === 'WAITING' ? 'waiting' : 'skip'; detail = s.detail; if (s.state === 'FAILED') failed = true; }
    chip.className = 'chip ' + cls;
    chip.textContent = detail ? `${name}: ${detail}` : name;
    ph.appendChild(chip);
  });
  current.failed = failed;

  // diagnostics
  messages.innerHTML = '';
  errorLines = new Set();
  for (const d of diags) {
    const b = document.createElement('button');
    b.className = 'msg ' + d.kind;
    const label = d.kind === 'runtime' ? '' : '';
    b.innerHTML = '';
    const strong = document.createElement('b');
    strong.textContent = d.line ? (d.kind === 'runtime' ? `line ${d.line}: ` : `${d.line}:${d.col} `) : '';
    b.append(strong, document.createTextNode(d.text.replace(/^[^:]*:\s*/, (x) => x) + (d.extra.length ? '\n' + d.extra.join('\n') : '') + label));
    if (d.line) { errorLines.add(d.line); b.addEventListener('click', () => jumpTo(d.line, d.col)); b.title = 'Go to line ' + d.line; }
    messages.appendChild(b);
  }
  messages.hidden = diags.length === 0;
  updateGutter();

  $('download-c').hidden = !sections.c;
  $('status').textContent = `${m.ms} ms in the compiler` + (m.code ? ` (exit status ${m.code})` : '');
  stdinForm.hidden = !m.needInput;
  if (m.needInput) {
    current.tab = 'output';
    $('status').textContent = 'Waiting for input';
    interactiveValue.value = '';
    requestAnimationFrame(() => interactiveValue.focus());
  }
  if (!failed && !/\S/.test(stdin.value) && /\bINPUT\b/.test(code.value) && m.code) {
    $('status').textContent += ', hint: fill in the Program input box before running';
  }
  selectTab(current.tab);
}

function tabText(tab) {
  const s = current.sections;
  if (tab === 'output') {
    if (current.failed) return 'The program was not run because compilation failed. See the messages above.';
    if (s.run === undefined) return '';
    return s.run === '' ? '(the program printed nothing)' : s.run;
  }
  const names = { tokens: 'tokens', ast: 'AST', ir: 'IR', 'opt-ir': 'optimized IR', c: 'C code' };
  if (s[tab] === undefined) return `(no ${names[tab]} was produced because an earlier phase failed)`;
  return s[tab];
}

function selectTab(tab) {
  current.tab = tab;
  document.querySelectorAll('#tabs button').forEach((b) => b.classList.toggle('active', b.dataset.tab === tab));
  if (!Object.keys(current.sections).length && !current.stderr) { setView('Press Run to compile and execute the program.', true); return; }
  setView(tabText(tab), false);
}
$('tabs').addEventListener('click', (e) => { const b = e.target.closest('button[data-tab]'); if (b) selectTab(b.dataset.tab); });

// ---------------------------------------------------------------- start
(function init() {
  let src = null, inp = null;
  const h = location.hash.slice(1);
  if (h) {
    try {
      const q = new URLSearchParams(h);
      if (q.get('code')) src = b64decode(q.get('code'));
      if (q.get('in')) inp = b64decode(q.get('in'));
    } catch (e) { /* ignore a damaged link */ }
  }
  if (src === null) { src = load('nova.code'); inp = load('nova.stdin'); }
  if (src === null || src === '') { const e = EXAMPLES.find((x) => x.id === '09_for_loops') || EXAMPLES[0]; src = e.source; inp = e.stdin; }
  code.value = src; stdin.value = inp || '';
  updateGutter();
  setView('Press Run to compile and execute the program.', true);
  startWorker();     // fetch and compile the WebAssembly module while the user reads the page
})();
