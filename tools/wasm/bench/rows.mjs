import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [wasm_file, mode, arm] = process.argv.slice(2);
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; };

const adjectives = ["pretty", "large", "big", "small", "tall", "short", "long", "handsome", "plain", "quaint", "clean", "elegant", "easy", "angry", "crazy", "helpful", "mushy", "odd", "unsightly", "adorable", "important", "inexpensive", "cheap", "expensive", "fancy"];
const colours = ["red", "yellow", "blue", "green", "pink", "brown", "purple", "brown", "white", "black", "orange"];
const nouns = ["table", "chair", "house", "bbq", "desk", "car", "pony", "cookie", "sandwich", "burger", "pizza", "mouse", "keyboard"];

function rows_of(n) {
  let seed = 1;
  const random = (m) => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed % m; };
  const rows = new Array(n);
  for (let i = 0; i < n; ++i) rows[i] = { id: i + 1, label: `${adjectives[random(adjectives.length)]} ${colours[random(colours.length)]} ${nouns[random(nouns.length)]}` };
  return rows;
}

function cbor_of(rows) {
  const enc = new TextEncoder(), out = [];
  const head = (major, v) => {
    if (v < 24) out.push((major << 5) | v);
    else if (v < 256) out.push((major << 5) | 24, v);
    else if (v < 65536) out.push((major << 5) | 25, v >> 8, v & 255);
    else out.push((major << 5) | 26, (v >>> 24) & 255, (v >>> 16) & 255, (v >>> 8) & 255, v & 255);
  };
  const text = (s) => { const b = enc.encode(s); head(3, b.length); for (const c of b) out.push(c); };
  head(4, rows.length);
  for (const r of rows) { head(5, 2); text("id"); head(0, r.id); text("label"); text(r.label); }
  return new Uint8Array(out);
}

async function wasm_of(bytes) {
  const memory = new WebAssembly.Memory({ initial: 2, maximum: 16384 });
  const module = await WebAssembly.compile(readFileSync(wasm_file));
  const x = (await WebAssembly.instantiate(module, { env: { memory }, wasi_snapshot_preview1: { fd_write: () => 0 } })).exports;
  const decoder = new TextDecoder();
  const text = (packed) => { const p = Number(packed >> 32n), n = Number(packed & 0xffffffffn); return decoder.decode(new Uint8Array(memory.buffer, p, n)); };
  const load = () => { const p = x.alloc(bytes.length); new Uint8Array(memory.buffer).set(bytes, p); const n = x.rows_load(p, bytes.length); x.release(p); return n; };
  return { x, memory, decoder, text, load };
}

const window_start = 5000 % 1000 === 0 ? 0 : 0;
const read_all = (rows) => { let s = 0; for (const r of rows) s += r.id + r.label.length; return s; };

async function arm_of(name) {
  const [scenario, n_text, kind] = name.replace(/_aa$/, "").split(":");
  const n = Number(n_text);
  const rows = rows_of(n);
  const json = JSON.stringify(rows);
  const bytes = cbor_of(rows);
  const start = Math.floor(n / 2), visible = 30, target = Math.floor(n / 2) + 1;
  if (kind === "json") {
    if (scenario === "full") return () => read_all(JSON.parse(json));
    if (scenario === "window") return () => { const r = JSON.parse(json); let s = 0; for (let i = start; i < start + visible; ++i) s += r[i].id + r[i].label.length; return s; };
    if (scenario === "find") return () => JSON.parse(json).findIndex((r) => r.id === target);
  }
  const w = await wasm_of(bytes);
  if (kind === "proxy") {
    const handler = { get: (t, k) => (k === "id" ? w.x.row_id(t.i) : k === "label" ? w.text(w.x.row_label(t.i)) : undefined) };
    if (scenario === "full") return () => { const count = w.load(); const r = new Array(count); for (let i = 0; i < count; ++i) r[i] = new Proxy({ i }, handler); return read_all(r); };
    if (scenario === "window") return () => { w.load(); let s = 0; for (let i = start; i < start + visible; ++i) { const p = new Proxy({ i }, handler); s += p.id + p.label.length; } return s; };
    if (scenario === "find") return () => { w.load(); return w.x.row_find(target); };
  }
  if (kind === "columns") {
    return () => {
      const count = w.load();
      const ids_p = w.x.alloc(count * 4), labels_p = w.x.alloc(count * 8);
      w.x.rows_columns(ids_p, labels_p);
      const ids = new Int32Array(w.memory.buffer, ids_p, count), labels = new Uint32Array(w.memory.buffer, labels_p, count * 2);
      const r = new Array(count);
      for (let i = 0; i < count; ++i) r[i] = { id: ids[i], label: w.decoder.decode(new Uint8Array(w.memory.buffer, labels[2 * i], labels[2 * i + 1])) };
      w.x.release(ids_p); w.x.release(labels_p);
      return read_all(r);
    };
  }
}

if (mode === "arm") {
  const run = await arm_of(arm);
  const times = [];
  let result;
  for (let k = 0; k < 7; ++k) { const t0 = performance.now(); result = run(); times.push(performance.now() - t0); }
  console.log(JSON.stringify({ arm, result, first_ms: times[0], ms: median(times.slice(2)) }));
} else if (mode === "run") {
  const reps = Number(process.env.REPS ?? 10);
  const names = [];
  for (const n of [1000, 10000]) {
    names.push(`full:${n}:json`, `full:${n}:proxy`, `full:${n}:columns`, `window:${n}:json`, `window:${n}:proxy`, `find:${n}:json`, `find:${n}:proxy`);
  }
  names.push("full:10000:json_aa", "full:10000:columns_aa");
  const results = Object.fromEntries(names.map((nm) => [nm, []]));
  for (let r = 0; r < reps; ++r)
    for (const nm of r % 2 ? [...names].reverse() : names)
      results[nm].push(JSON.parse(execFileSync(process.execPath, [process.argv[1], wasm_file, "arm", nm], { encoding: "utf8" })));
  const summary = Object.fromEntries(names.map((nm) => {
    const ms = results[nm].map((o) => o.ms);
    return [nm, { result: results[nm][0].result, first_median_ms: +median(results[nm].map((o) => o.first_ms)).toFixed(3), median_ms: +median(ms).toFixed(3), spread: +(Math.max(...ms) / Math.min(...ms) - 1).toFixed(3) }];
  }));
  const context = {
    time: new Date().toISOString(), node: process.version, v8: process.versions.v8, kernel: os.release(), cpus: os.cpus().length, cpu: os.cpus()[0].model, reps,
    iterations_per_process: "7, first and median of the last 5; each iteration includes JSON.parse or the copy into Wasm memory, the load and the row index",
    data: "rows {id, label} as in js-framework-benchmark (krausest), labels from its word lists with a fixed LCG; CBOR encoded by the bench (definite lengths, shortest heads)",
    scenarios: "full: read id and label of every row; window: read 30 rows from the middle (virtual list); find: index of the row with id n/2+1",
    arms: "json: JSON.parse + plain objects; proxy: a Proxy per row whose get trap calls Wasm (Vue, Solid, Svelte 5 style); columns: one Wasm call fills ids and label views, JS builds plain objects",
    commit: execFileSync("git", ["-C", new URL("../../..", import.meta.url).pathname, "describe", "--always", "--dirty"], { encoding: "utf8" }).trim(),
  };
  console.log(JSON.stringify({ context, summary, results }, null, 1));
}
