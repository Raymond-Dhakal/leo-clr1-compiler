// Runs the WebAssembly build of leoc (the real compiler: lexer, CLR(1) parser built from
// grammar.txt, semantic analysis, IR, optimizer, and the IR interpreter as execution engine).
import { WASI, Fd, File, OpenFile, PreopenDirectory, wasi as defs } from './vendor/browser_wasi_shim/index.js';

class Capture extends Fd {
  constructor() { super(); this.chunks = []; }
  fd_write(data) { this.chunks.push(data.slice()); return { ret: 0, nwritten: data.byteLength }; }
  fd_fdstat_get() {
    const f = new defs.Fdstat(defs.FILETYPE_CHARACTER_DEVICE, 0);
    f.fs_rights_base = BigInt(defs.RIGHTS_FD_WRITE);
    return { ret: 0, fdstat: f };
  }
  fd_filestat_get() { return { ret: 0, filestat: new defs.Filestat(defs.FILETYPE_CHARACTER_DEVICE, 0n) }; }
  text() {
    const dec = new TextDecoder('utf-8');
    let s = '';
    for (const c of this.chunks) s += dec.decode(c, { stream: true });
    return s + dec.decode();
  }
}

const enc = new TextEncoder();
let ready = null;

function load() {
  if (!ready) {
    ready = Promise.all([
      WebAssembly.compileStreaming(fetch('leo.wasm')),
      fetch('grammar.txt').then(r => { if (!r.ok) throw new Error('grammar.txt not found'); return r.arrayBuffer(); }),
    ]).then(([module, grammar]) => ({ module, grammar: new Uint8Array(grammar) }));
  }
  return ready;
}

async function run({ source, stdin, optimize }) {
  const { module, grammar } = await load();
  const out = new Capture(), err = new Capture();
  const args = ['leoc', '/prog.leo', '--tokens', '--ast', '--ir', '--opt-ir', '--emit-c', '--interp', '--interactive-input', '--markers'];
  if (!optimize) args.push('--no-opt');
  const fds = [
    new OpenFile(new File(enc.encode(stdin))),
    out, err,
    new PreopenDirectory('/', new Map([
      ['grammar.txt', new File(grammar)],
      ['prog.leo', new File(enc.encode(source))],
    ])),
  ];
  const wasi = new WASI(args, [], fds);
  const instance = await WebAssembly.instantiate(module, { wasi_snapshot_preview1: wasi.wasiImport });
  const t0 = performance.now();
  const code = wasi.start(instance);
  const rawStderr = err.text();
  // Accept protocol markers from older committed WebAssembly builds while
  // presenting the current Leo names to the page.
  const needInput = rawStderr.includes('@@LEO-NEED-INPUT@@') || rawStderr.includes('@@NOVA-NEED-INPUT@@');
  const stdout = out.text().replaceAll('@@NOVA-SECTION ', '@@LEO-SECTION ');
  const stderr = rawStderr
    .replace('@@NOVA-NEED-INPUT@@\n', '')
    .replace('@@LEO-NEED-INPUT@@\n', '')
    .replaceAll('@@NOVA-SECTION ', '@@LEO-SECTION ')
    .replaceAll('novac:', 'leoc:');
  return { code, stdout, stderr, needInput, ms: Math.round(performance.now() - t0) };
}

self.onmessage = async (e) => {
  const { id } = e.data;
  try {
    const r = await run(e.data);
    self.postMessage({ id, ok: true, ...r });
  } catch (ex) {
    self.postMessage({ id, ok: false, error: String(ex && ex.message || ex) });
  }
};
// Tell the page that the module can be fetched ahead of the first run.
load().then(() => self.postMessage({ ready: true }), (ex) => self.postMessage({ ready: false, error: String(ex) }));
