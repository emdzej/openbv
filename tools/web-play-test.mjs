#!/usr/bin/env node
// Headless Chrome test of the browser player (docs/public/play/), through the page's own code.
//
// Hash runs (?hashframes=N&input=...): the page must print the hash line gasm-run --headless N prints for
// the same module, data and input. gasm-run calls the guest's gasm_exit before printing; the page doesn't,
// so the test also runs gasm's GasmHost in Node (the same package as the page) and checks that its line
// is the same before and after gasm_exit, and equal to the page's and to gasm-run's.
// A live run (?autostart): the page starts the game on WebGL 2 with the content from the site's cache and
// keeps running; a screenshot of it goes to the out dir.
//
//   docs/scripts/copy-wasm.sh && docs/scripts/copy-content.sh && (cd docs && scripts/vendor-web.sh && pnpm build)
//   node tools/web-play-test.mjs [out dir]
// Needs Chrome (CHROME=<binary>) and gasm-run (GASM_RUN=<binary>; default ../gasm's build, else
// .deps/gasm-runner-macos-universal/gasm-run). Serves docs/.vitepress/dist on 127.0.0.1:8794. Never opens a window.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, readFileSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repo = fileURLToPath(new URL('..', import.meta.url));
const OUT = resolve(process.argv[2] ?? '/tmp/openbv-play');
const DIST = join(repo, 'docs/.vitepress/dist');
const WASM = join(DIST, 'play/build/openbv.wasm');   // the module the page runs: gasm-run gets the same one
const DATA = join(DIST, 'play/data');                 // and the same data
const PORT = 8794, DEBUG = 9337;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const RUN = process.env.GASM_RUN ?? [join(repo, '../gasm/runners/native/target/release/gasm-run'),
  join(repo, '.deps/gasm-runner-macos-universal/gasm-run')].find(existsSync);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
mkdirSync(OUT, { recursive: true });
if (!existsSync(WASM) || !existsSync(join(DATA, 'files.json'))) {
  console.error('build the site first (docs/scripts/copy-wasm.sh, copy-content.sh, scripts/vendor-web.sh, pnpm build)');
  process.exit(1);
}
if (!existsSync(CHROME)) { console.error(`no Chrome at ${CHROME} (CHROME=<binary>)`); process.exit(1); }

// The main menu, and a hosted game played to the first shots (AGENTS.md's script).
const PLAY = '610:PTR(494,44),620:PTR(494,44,L),625:PTR(494,44),700:PTR(116,95),720:PTR(116,95,L),740:PTR(116,95),' +
  '1000:PTR(579,174),1010:PTR(579,174,L),1020:PTR(579,174),1060:PTR(309,174),1070:PTR(309,174,L),1080:PTR(309,174),' +
  '1300:PTR(800,300),1310:PTR(800,300,L),1320:PTR(800,300),1450-1520:KEY(KeyW),1600-1640:PTR(900,250,L),1641:PTR(900,250)';
const CASES = [
  { name: 'menu', frames: 600, input: '', expect: 'video_fnv32=1be66dd1 audio_fnv32=b56bc7ab' },
  { name: 'host and play', frames: 1650, input: PLAY },
];

const hashPart = (line) => line?.match(/video_fnv32=\w+ audio_fnv32=\w+ audio_frames=\d+/)?.[0] ?? line;

function native(c) {
  if (!RUN) return null;
  const args = [WASM, '--asset-dir', DATA, '--headless', String(c.frames)];
  if (c.input) args.push('--input', c.input);
  return execFileSync(RUN, args, { stdio: ['ignore', 'pipe', 'ignore'], maxBuffer: 1 << 26 }).toString()
    .split('\n').find((l) => l.includes('video_fnv32'))?.trim() ?? null;
}

// gasm's GasmHost in Node on the same data (the page's loader, without the browser), the hash line before
// and after gasm_exit.
const gasm = await import(pathToFileURL(join(repo, 'docs/node_modules/@emdzej/gasm-host/gasm-host.js')).href);
const { InputScript } = await import(pathToFileURL(join(repo, 'docs/node_modules/@emdzej/gasm-host/input-script.mjs')).href);
async function nodeRef(c) {
  const { files } = JSON.parse(readFileSync(join(DATA, 'files.json'), 'utf8'));
  const table = new gasm.AssetTable(() => {});
  for (const [name] of files) table.add(name, gasm.bytesSource(new Uint8Array(readFileSync(join(DATA, name)))), { fromDir: true });
  table.finish();
  const script = new InputScript(c.input);
  const host = new gasm.GasmHost({ assets: table, storage: new gasm.MemoryStorage(), virtualTime: true, onLog: () => {} });
  host.text = '';
  host.showFrame = false;
  await host.load(new Uint8Array(readFileSync(WASM)));
  const st = {};
  for (let f = 0; f < c.frames; f++) {
    host.input = script.raw(f, st, [1280, 720], host.inputMode);
    host.text = script.textAt(f);
    host.getPad = (p) => (p === 0 ? script.pad(f) : 0);
    await host.frameAsync();
  }
  const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
  const line = () => `video_fnv32=${hex(host.videoHash)} audio_fnv32=${hex(host.audioHash)} audio_frames=${host.audioFrames}`;
  const before = line();
  try { await host.shutdown(); } catch {}
  return { before, after: line() };
}

// A static server for the site.
const { createServer } = await import('node:http');
const { readFile } = await import('node:fs/promises');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.webp': 'image/webp', '.woff2': 'font/woff2' };
const server = createServer(async (req, res) => {
  let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (path.endsWith('/')) path += 'index.html';
  const file = join(DIST, path);
  if (!file.startsWith(DIST)) { res.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[path.slice(path.lastIndexOf('.'))] ?? 'application/octet-stream' }).end(body);
  } catch { res.writeHead(404).end(); }
});
await new Promise((r) => server.listen(PORT, '127.0.0.1', r));
const PROFILE = mkdtempSync(join(tmpdir(), 'openbv-play-chrome-'));
const chromeArgs = ['--headless=new', `--remote-debugging-port=${DEBUG}`, `--user-data-dir=${PROFILE}`,
  '--autoplay-policy=no-user-gesture-required', '--enable-unsafe-swiftshader', '--use-angle=swiftshader', 'about:blank'];
if (process.env.CI) chromeArgs.unshift('--no-sandbox');
const chrome = spawn(CHROME, chromeArgs, { stdio: 'ignore' });

async function cdp() {
  let target;
  for (let i = 0; i < 50 && !target; i++) {
    await sleep(200);
    target = await fetch(`http://127.0.0.1:${DEBUG}/json`).then((r) => r.json()).then((t) => t.find((x) => x.type === 'page')).catch(() => null);
  }
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const logs = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    if (m.method === 'Runtime.exceptionThrown') logs.push(`EXCEPTION ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`);
  };
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Runtime.enable'); await send('Page.enable');
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result?.value;
  const until = async (expr, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) { const v = await evaluate(expr); if (v) return v; await sleep(250); }
    throw new Error(`timeout waiting for ${expr}\n  ${logs.slice(-8).join('\n  ')}`);
  };
  const open = (url) => send('Page.navigate', { url });
  return { send, evaluate, until, open, logs };
}

let failed = false;
const report = (ok, label, detail) => { failed ||= !ok; console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? `\n      ${detail}` : ''}`); };
const base = `http://127.0.0.1:${PORT}/play/`;
try {
  const page = await cdp();
  await page.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 1000, deviceScaleFactor: 1, mobile: false });

  for (const c of CASES) {
    const nat = native(c);
    const ref = await nodeRef(c);
    const t0 = Date.now();
    await page.open(`${base}?hashframes=${c.frames}&input=${encodeURIComponent(c.input)}`);
    const got = await page.until('globalThis.__openbvResult', 600000);
    const secs = ((Date.now() - t0) / 1000).toFixed(1);
    report(hashPart(got) === ref.before, `${c.name}: page = gasm-host in Node (${c.frames} frames, ${secs} s)`, `page ${hashPart(got)}\n      node ${ref.before}`);
    report(ref.before === ref.after, `${c.name}: gasm_exit changes no hash`, ref.before === ref.after ? '' : `after exit ${ref.after}`);
    if (nat) report(hashPart(nat) === ref.after, `${c.name}: = gasm-run`, `gasm-run ${hashPart(nat)}`);
    if (c.expect) report(hashPart(got).startsWith(c.expect), `${c.name}: = the reference in AGENTS.md`, c.expect);
  }

  // live: WebGL 2, the Cache API, real time
  await page.open(`${base}?autostart`);
  await page.until('document.getElementById("cover").hidden', 120000);
  await sleep(12000);
  const st = await page.evaluate('document.getElementById("status").textContent');
  const shot = await page.send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT, 'live.png'), Buffer.from(shot.result.data, 'base64'));
  const errors = page.logs.filter((l) => /EXCEPTION|trapped|could not/i.test(l));
  report(/frames\/s/.test(st) && !errors.length, 'live: running on WebGL 2', `status "${st}"${errors.length ? `\n      ${errors.join('\n      ')}` : ''}; ${join(OUT, 'live.png')}`);
  const cached = await page.evaluate('caches.keys().then((k) => k.join(","))');
  report(/^openbv-data-/.test(cached ?? ''), 'live: data in the Cache API', cached);
} catch (e) {
  report(false, 'test', e.stack ?? String(e));
} finally {
  chrome.kill();
  server.close();
}
process.exit(failed ? 1 : 0);
