// openbv's browser player: openbv.wasm (gasm:gl) on @emdzej/gasm-host, on the main thread (gasm:gl
// games run there), with the game's content served by this site (data/, from the GPL source release;
// copied in by docs/scripts/copy-content.sh). Loosely after gasm's own player (runners/web/app.js).
//
// Query parameters:
//   master=HOST:PORT  the master server (the online game list); none by default
//   hashframes=N      run N frames on virtual time with the null GL and in-memory storage, then print the
//                     hash line gasm-run --headless N prints (globalThis.__openbvResult)
//   input=SCRIPT      with hashframes: scripted input, gasm-run's --input syntax
//   autostart         start without the click (audio then starts at the first key or click)
import {
  GasmHost, IdbStorage, MemoryStorage, ProcExit, Resampler, AssetTable, bytesSource, BrowserInput, INPUT_KEYS_RAW,
} from './gasm-host.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const HASH_FRAMES = Number(query.get('hashframes') || 0);
// headless runs' drawable (gasm-run --headless): the same size, so scripted pointer positions match
const HEADLESS_SIZE = [1280, 720];
// hosts the game may reach without asking: a server on this machine (none in the browser yet)
const ALLOW_NET = ['127.0.0.1', 'localhost'];

const canvas = $('screen');
let host = null, running = false, rafId = 0, assets = null, wasm = null, gl = null;

function status(text, error = false) { $('status').textContent = text; $('status').classList.toggle('error', error); }
function progress(text, error = false) { $('progress').textContent = text; $('progress').classList.toggle('error', error); }
const log = (m) => console.log(`[openbv] ${m}`);

// ---- the game's data: data/files.json lists it; whole files, kept in the Cache API under the content's
// version, so later visits read them from disk ---------------------------------------------------------------
async function loadData() {
  const r = await fetch('data/files.json');
  if (!r.ok) throw new Error(`data/files.json: HTTP ${r.status}`);
  const { version, files } = await r.json();
  const total = files.reduce((s, f) => s + f[1], 0);
  const cacheName = `openbv-data-${version.slice(0, 16)}`;
  let cache = null;
  try {
    cache = await caches.open(cacheName);
    for (const k of await caches.keys()) if (k.startsWith('openbv-data-') && k !== cacheName) caches.delete(k);
  } catch { cache = null; }   // no Cache API (file:, some private modes): plain fetches
  const table = new AssetTable(log);
  let done = 0, bytes = 0, next = 0;
  const one = async ([name, size]) => {
    const url = new URL(`data/${name.split('/').map(encodeURIComponent).join('/')}`, location.href).href;
    let res = cache && await cache.match(url);
    if (!res) {
      res = await fetch(url);
      if (!res.ok) throw new Error(`${name}: HTTP ${res.status}`);
      if (cache) await cache.put(url, res.clone()).catch(() => {});
    }
    const b = new Uint8Array(await res.arrayBuffer());
    if (b.length !== size) throw new Error(`${name}: ${b.length} bytes, expected ${size}`);
    table.add(name, bytesSource(b), { fromDir: true });   // folder rules: case-insensitive, like --asset-dir
    done++; bytes += size;
    progress(`game data: ${done} / ${files.length} files, ${(bytes / 1048576).toFixed(1)} of ${(total / 1048576).toFixed(1)} MB`);
  };
  const worker = async () => { while (next < files.length) await one(files[next++]); };
  await Promise.all(Array.from({ length: 8 }, worker));
  return table.finish();
}

// ---- audio: an AudioWorklet with a small queue, as gasm's player ------------------------------------------
const WORKLET = `
class OpenbvOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('openbv-out', OpenbvOut);`;
let audioCtx = null, audioNode = null, resampler = null;
for (const type of ['pointerdown', 'keydown']) addEventListener(type, () => { if (audioCtx?.state === 'suspended') audioCtx.resume(); });
async function initAudio() {
  if (audioCtx) return;
  try {
    audioCtx = new AudioContext({ latencyHint: 'interactive' });
    await audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' })));
    audioNode = new AudioWorkletNode(audioCtx, 'openbv-out', { outputChannelCount: [2] });
    audioNode.connect(audioCtx.destination);
    resampler = new Resampler(audioCtx.sampleRate);
  } catch (e) {
    log(`audio disabled: ${e.message}`);
    audioCtx = null;
  }
}
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out, [out.buffer]);
}

// ---- input: the raw keyboard, the pointer and gamepads (BrowserInput), typed text, Escape ---------------
const rawInput = new BrowserInput(canvas).attach();
let typed = '';
let escDown = 0;   // a tap of Escape goes to the game (its menu); holding it stops the game
addEventListener('keydown', (e) => {
  if (!running) return;
  if (e.code === 'Escape' && !e.repeat) escDown = performance.now();
  // the game reads every key itself: Space doesn't scroll, Tab doesn't move the focus, F5 doesn't reload
  if (host && host.inputMode & INPUT_KEYS_RAW && e.code !== 'Escape' && !e.metaKey) e.preventDefault();
  if (e.key === 'Enter') typed += '\n';
  else if (e.key === 'Backspace') typed += '\b';
  else if (e.key.length === 1 && !e.ctrlKey && !e.metaKey) typed += e.key;
});
addEventListener('keyup', (e) => { if (e.code === 'Escape') escDown = 0; });
canvas.addEventListener('contextmenu', (e) => e.preventDefault());   // the right button throws grenades
const takeTyped = () => { const t = typed; typed = ''; return t; };
const batch = (n) => Array.from({ length: n }, (_, k) => ({ pads: [0, 0, 0, 0], text: k === 0 ? takeTyped() : '', input: rawInput.frame(k === 0) }));

// gasm:gl's default framebuffer is the canvas at its display size (the game letterboxes its 4:3 in it)
function fitCanvas() {
  const w = Math.round(canvas.clientWidth * (devicePixelRatio || 1)) || 800;
  const h = Math.round(canvas.clientHeight * (devicePixelRatio || 1)) || 600;
  if (canvas.width !== w || canvas.height !== h) { canvas.width = w; canvas.height = h; }
}

// ---- running ------------------------------------------------------------------------------------------
function params() {
  const p = {};
  const master = query.get('master');
  if (master) p.master = master;
  return p;
}

async function start() {
  $('start').disabled = true;
  await initAudio();
  $('cover').hidden = true;
  canvas.focus();
  fitCanvas();
  gl = canvas.getContext('webgl2', { alpha: false, antialias: false, depth: true, stencil: true });
  if (!gl) { status('This game needs WebGL 2, which this browser doesn\'t offer.', true); return; }
  const storage = await IdbStorage.open('openbv').catch((e) => {
    log(`storage unavailable (${e.message}); settings won't persist`);
    return new MemoryStorage();
  });
  host = new GasmHost({
    assets, params: params(), gl, storage, allowNet: ALLOW_NET, ask: () => false,
    onAudio, onLog: log, onTitle: (t) => { document.title = t ? `${t} | openbv` : 'Play | openbv'; },
  });
  host.text = '';
  try {
    await host.load(wasm);
  } catch (e) {
    status(e instanceof ProcExit ? `the game exited while starting (code ${e.code})` : `could not start: ${e.message}`, true);
    console.error(e);
    return;
  }
  $('fullscreen').disabled = $('stop').disabled = false;
  running = true;
  last = performance.now(); acc = 0;
  rafId = requestAnimationFrame(tick);
}

let acc = 0, last = 0, fpsN = 0, fpsT = 0;
function tick(now) {
  if (!running) return;
  rafId = requestAnimationFrame(tick);
  if (escDown && now - escDown >= 1000) { escDown = 0; stop('stopped (Escape held)'); return; }
  const period = 1000 / host.frameRate;
  acc += Math.min(now - last, 100);   // clamped after tab switches
  last = now;
  // fixed timestep; when catching up (at most 4), only the last frame is drawn
  const due = Math.min(4, Math.floor(acc / period));
  if (due > 0) {
    fitCanvas();
    try { host.runFrames(batch(due), true); } catch (e) { stopped(e); return; }
    acc -= due * period; fpsN += due;
    rawInput.setMode(host.inputMode);   // the game draws its own cursor: hide the system one
  }
  if (acc > period * 4) acc = 0;
  if (now - fpsT >= 1000) { status(`${fpsN} frames/s`); fpsN = 0; fpsT = now; }
}

function stopped(e) {
  running = false;
  cancelAnimationFrame(rafId);
  if (e instanceof ProcExit) status(`the game exited (code ${e.code})`);
  else { status(`the game stopped: ${e.message}`, true); console.error(e); }
  host?.shutdown();
  host = null;
  ended();
}

async function stop(why) {
  running = false;
  cancelAnimationFrame(rafId);
  const h = host;
  host = null;
  await h?.shutdown();   // gasm_exit: the game saves bv2.cfg
  status(why);
  ended();
}

function ended() {
  rawInput.setMode(0);
  $('fullscreen').disabled = $('stop').disabled = true;
  gl?.getExtension('WEBGL_lose_context')?.loseContext();
  gl = null;
  // the canvas' context is gone: playing again is a fresh page (the data comes from the cache)
  $('cover').hidden = false;
  $('start').textContent = 'Play again';
  $('start').disabled = false;
  $('start').onclick = () => location.reload();
}

$('stop').onclick = () => stop('stopped');
$('fullscreen').onclick = () => { $('stage').requestFullscreen?.(); canvas.focus(); };
addEventListener('pagehide', () => { host?.shutdown(); });

// ---- the hash run: gasm-run --headless N [--input SCRIPT] ----------------------------------------------
const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
async function hashRun(n) {
  const { InputScript } = await import('./input-script.mjs');
  let script;
  try { script = new InputScript(query.get('input') ?? ''); } catch (e) { status(`input: ${e.message}`, true); return; }
  // as the headless runners: virtual time, the null GL (gl: null), saves in memory, nothing shown
  canvas.width = HEADLESS_SIZE[0]; canvas.height = HEADLESS_SIZE[1];
  const h = new GasmHost({ assets, params: params(), storage: new MemoryStorage(), virtualTime: true, onLog: log });
  h.text = '';
  h.showFrame = false;
  const st = {};
  try {
    await h.load(wasm);
    for (let f = 0; f < n; f++) {
      h.input = script.raw(f, st, HEADLESS_SIZE, h.inputMode);
      h.text = script.textAt(f);
      h.getPad = (p) => (p === 0 ? script.pad(f) : 0);
      await h.frameAsync();
    }
  } catch (e) { if (!(e instanceof ProcExit)) { status(`hash run failed: ${e.message}`, true); throw e; } }
  const out = `frames=${h.frameIndex} presented=${h.framesPresented} size=${h.width}x${h.height} ` +
              `video_fnv32=${hex(h.videoHash)} audio_fnv32=${hex(h.audioHash)} audio_frames=${h.audioFrames}`;
  status(out);
  console.log(out);
  globalThis.__openbvResult = out;
}

// ---- boot ------------------------------------------------------------------------------------------------
(async () => {
  try {
    progress('loading the game...');
    const [w, a] = await Promise.all([
      fetch('build/openbv.wasm').then((r) => { if (!r.ok) throw new Error(`build/openbv.wasm: HTTP ${r.status}`); return r.arrayBuffer(); }),
      loadData(),
    ]);
    wasm = new Uint8Array(w);
    assets = a;
  } catch (e) {
    progress(`could not load the game: ${e.message}`, true);
    $('start').textContent = 'Unavailable';
    console.error(e);
    return;
  }
  if (HASH_FRAMES > 0) { progress('hash run...'); await hashRun(HASH_FRAMES); progress(globalThis.__openbvResult ?? ''); return; }
  progress('');
  $('start').textContent = 'Play';
  $('start').disabled = false;
  $('start').onclick = start;
  if (query.has('autostart')) start();
})();
