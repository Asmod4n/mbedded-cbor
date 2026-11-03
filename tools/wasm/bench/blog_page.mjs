import { createServer } from "node:http";
import { readFileSync, writeFileSync } from "node:fs";
import { join, extname } from "node:path";
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [playwright_dir, wasm_dir, mustache_mjs, posts_text, reps_text] = process.argv.slice(2);
const posts = Number(posts_text), reps = Number(reps_text ?? 10), pages = Math.ceil(posts / 10);
const require = createRequire(import.meta.url);
const { chromium } = require(playwright_dir);
const here = new URL(".", import.meta.url).pathname;

const memory = new WebAssembly.Memory({ initial: 2, maximum: 16384 });
const blog = (await WebAssembly.instantiate(await WebAssembly.compile(readFileSync(join(wasm_dir, "blog.wasm"))), { env: { memory }, wasi_snapshot_preview1: { fd_write: () => 0 } })).exports;
const pk = blog.server_encode(posts);
const cbor = Buffer.from(memory.buffer.slice(Number(pk >> 32n), Number(pk >> 32n) + Number(pk & 0xffffffffn)));
const authors = ["Ada", "Grace", "Linus", "Barbara", "Ken", "Margaret", "Dennis"];
const list = [];
for (let i = 0; i < posts; ++i) list.push({ id: i + 1, title: `Post number ${i + 1} <draft>`, author: authors[i % authors.length], summary: `Summary of post ${i + 1}: ` + "x".repeat(120), published: 1700000000 + i * 86400 });
const json = JSON.stringify({ posts: list });

const types = { ".html": "text/html", ".mjs": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };
const server = createServer((req, res) => {
  const path = new URL(req.url, "http://x").pathname;
  let body;
  if (path === "/") body = readFileSync(join(here, "blog_page.html"));
  else if (path === "/lib/mustache.mjs") body = readFileSync(mustache_mjs);
  else if (path === "/data/feed.json") body = json;
  else if (path === "/data/feed.cbor") body = cbor;
  else if (path.startsWith("/wasm/")) body = readFileSync(join(wasm_dir, path.slice(6)));
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Content-Type", types[extname(path)] ?? (path === "/" ? "text/html" : "application/octet-stream"));
  res.end(body);
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;
const names = ["json_literal", "json_literal_aa", "json_mustachejs", "wasm_html", "wasm_mustache", "wasm_mustache_aa"];
const results = Object.fromEntries(names.map((n) => [n, []]));
const browser = await chromium.launch();
for (let r = 0; r < reps; ++r)
  for (const n of r % 2 ? [...names].reverse() : names) {
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.goto(`http://127.0.0.1:${port}/?arm=${n}&pages=${pages}`);
    await page.waitForFunction(() => window.result !== undefined, null, { timeout: 120000 });
    results[n].push(await page.evaluate(() => window.result));
    await context.close();
  }
const version = browser.version();
await browser.close();
server.close();
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; };
const summary = Object.fromEntries(names.map((n) => {
  const R = results[n], m = (f) => +median(R.map(f)).toFixed(4);
  return [n, { dom_hash: R[0].dom_hash, init_ms: m((o) => o.init_ms), initial_first_ms: m((o) => o.initial_first_ms), initial_ms: m((o) => o.initial_ms), click_first_ms: m((o) => o.click_first_ms), click_ms: m((o) => o.click_ms) }];
}));
const context = {
  time: new Date().toISOString(), browser: `chromium ${version} headless (Playwright)`, node: process.version, kernel: os.release(), cpus: os.cpus().length, cpu: os.cpus()[0].model,
  posts, per_page: 10, reps, wire_bytes: { json: json.length, cbor: cbor.length },
  measured: "init = Wasm compile+instantiate (0 for JSON arms); initial = load (JSON.parse, or copy + schema path) + render page 0 + innerHTML + layout; click = render next page + innerHTML + layout; first = iteration 1 of 7 in a fresh page, warm = median of iterations 3-7",
  libraries: "mustache.js 4.2.0 (npm); mbedded-mustache " + execFileSync("git", ["-C", "/home/user/mbedded-mustache", "rev-parse", "--short", "HEAD"], { encoding: "utf8" }).trim() + " compiled with -fwasm-exceptions",
  commit: execFileSync("git", ["-C", join(here, "../../.."), "describe", "--always", "--dirty"], { encoding: "utf8" }).trim(),
};
console.log(JSON.stringify({ context, summary, results }, null, 1));
