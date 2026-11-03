import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [wasm_file, mode, arm] = process.argv.slice(2);
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; };
const count = Number(process.env.POSTS ?? 30), per_page = 10, pages = Math.ceil(count / per_page);
const authors = ["Ada", "Grace", "Linus", "Barbara", "Ken", "Margaret", "Dennis"];

function posts_of(n) {
  const out = [];
  for (let i = 0; i < n; ++i) out.push({ id: i + 1, title: `Post number ${i + 1} <draft>`, author: authors[i % authors.length], summary: `Summary of post ${i + 1}: ` + "x".repeat(120), published: 1700000000 + i * 86400 });
  return out;
}

const escape = (s) => s.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const article = (p) => `<article data-id="${p.id}"><h2>${escape(p.title)}</h2><p class="by">${escape(p.author)} ${p.published}</p><p>${escape(p.summary)}</p></article>`;

async function instance() {
  const memory = new WebAssembly.Memory({ initial: 2, maximum: 16384 });
  const module = await WebAssembly.compile(readFileSync(wasm_file));
  const x = (await WebAssembly.instantiate(module, { env: { memory }, wasi_snapshot_preview1: { fd_write: () => 0 } })).exports;
  const decoder = new TextDecoder();
  const text = (packed) => { const p = Number(packed >> 32n), n = Number(packed & 0xffffffffn); return decoder.decode(new Uint8Array(memory.buffer, p, n)); };
  return { x, memory, text };
}

async function wire() {
  const s = await instance();
  const packed = s.x.server_encode(count);
  const p = Number(packed >> 32n), n = Number(packed & 0xffffffffn);
  return new Uint8Array(s.memory.buffer.slice(p, p + n));
}

async function app_of(kind) {
  if (kind === "json") {
    const text = JSON.stringify({ posts: posts_of(count) });
    let posts;
    return { load: () => { posts = JSON.parse(text).posts; }, page: (k) => posts.slice(k * per_page, (k + 1) * per_page).map(article).join(""), bytes: text.length };
  }
  const bytes = await wire();
  const w = await instance();
  const load = () => { const p = w.x.alloc(bytes.length); new Uint8Array(w.memory.buffer).set(bytes, p); w.x.feed_load(p, bytes.length); w.x.release(p); };
  if (kind === "wasm_html") return { load, page: (k) => w.text(w.x.page_html(k, per_page)), bytes: bytes.length };
  return {
    load, bytes: bytes.length,
    page: (k) => {
      const n = w.x.post_count();
      let out = "";
      for (let i = k * per_page; i < n && i < (k + 1) * per_page; ++i)
        out += article({ id: w.x.post_id(i), title: w.text(w.x.post_title(i)), author: w.text(w.x.post_author(i)), summary: w.text(w.x.post_summary(i)), published: w.x.post_published(i) });
      return out;
    },
  };
}

if (mode === "arm") {
  const app = await app_of(arm.replace(/_aa$/, ""));
  const initial = [], clicks = [];
  let html = "";
  for (let k = 0; k < 7; ++k) {
    let t0 = performance.now();
    app.load();
    html = app.page(0);
    initial.push(performance.now() - t0);
    const c = [];
    for (let p = 1; p < pages; ++p) { t0 = performance.now(); html += app.page(p); c.push(performance.now() - t0); }
    clicks.push(c);
  }
  console.log(JSON.stringify({ arm, bytes: app.bytes, html_length: html.length, html_hash: [...html].reduce((h, ch) => (h * 31 + ch.charCodeAt(0)) >>> 0, 7),
    initial_first_ms: initial[0], initial_ms: median(initial.slice(2)), click_first_ms: clicks[0][0], click_ms: median(clicks.slice(2).flat()) }));
} else if (mode === "run") {
  const reps = Number(process.env.REPS ?? 10);
  const names = ["json", "json_aa", "wasm_html", "wasm_html_aa", "wasm_fields"];
  const results = Object.fromEntries(names.map((n) => [n, []]));
  for (let r = 0; r < reps; ++r)
    for (const n of r % 2 ? [...names].reverse() : names)
      results[n].push(JSON.parse(execFileSync(process.execPath, [process.argv[1], wasm_file, "arm", n], { encoding: "utf8", env: { ...process.env } })));
  const summary = Object.fromEntries(names.map((n) => {
    const R = results[n], m = (f) => +median(R.map(f)).toFixed(4);
    return [n, { bytes: R[0].bytes, html_hash: R[0].html_hash, initial_first_ms: m((o) => o.initial_first_ms), initial_ms: m((o) => o.initial_ms), click_first_ms: m((o) => o.click_first_ms), click_ms: m((o) => o.click_ms) }];
  }));
  const context = {
    time: new Date().toISOString(), node: process.version, v8: process.versions.v8, kernel: os.release(), cpus: os.cpus().length, cpu: os.cpus()[0].model, reps,
    posts: count, per_page, iterations_per_process: "7; initial = load (JSON.parse, or copy + schema path) + render page 0; click = render one further page; first = iteration 1, warm = median of iterations 3-7",
    arms: "json: JSON.parse + template literal per post; wasm_html: Wasm renders the page HTML from compiled schema at_path, JS decodes one string; wasm_fields: JS calls one compiled at_path export per field",
    commit: execFileSync("git", ["-C", new URL("../../..", import.meta.url).pathname, "describe", "--always", "--dirty"], { encoding: "utf8" }).trim(),
  };
  console.log(JSON.stringify({ context, summary, results }, null, 1));
}
