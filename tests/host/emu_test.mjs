import fs from "fs";
const wasm = fs.readFileSync(process.argv[2]);
let mem;
const wasi = new Proxy({ clock_time_get: (id, p, out) => { new DataView(mem.buffer).setBigUint64(out, 0n, true); return 0; } }, { get: (t, k) => (k in t ? t[k] : () => 0) });
const { instance } = await WebAssembly.instantiate(wasm, { wasi_snapshot_preview1: wasi, env: new Proxy({}, { get: () => () => 0 }) });
const ex = instance.exports; mem = ex.memory;
ex._initialize(); ex.web_master(2800); ex.web_boot();
const render = (n) => { let pk = 0; for (let i = 0; i < n; i++) { ex.web_render(128); const l = new Float32Array(mem.buffer, ex.web_out_l(), 128); for (const v of l) pk = Math.max(pk, Math.abs(v)); } return pk; };
const quiet = render(100);
ex.web_keys(1);                 // key 0: the first white key, the I chord
const loud = render(200);
ex.web_keys(0);
render(600);
const after = render(50);
console.log({ quiet, loud: loud.toFixed(3), after: after.toFixed(4), blits: ex.web_blits() });
if (!(quiet === 0 && loud > 0.05 && after < 0.05)) { console.log("FAIL"); process.exit(1); }
console.log("emu ok");
