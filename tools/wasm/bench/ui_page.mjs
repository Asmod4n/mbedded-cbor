import { createServer } from "node:http";
import { readFileSync, writeFileSync } from "node:fs";
import { join, extname } from "node:path";
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [playwright_dir, wasm_dir, rows_text, reps_text] = process.argv.slice(2);
const rows = Number(rows_text), reps = Number(reps_text ?? 10);
const require = createRequire(import.meta.url);
const { chromium } = require(playwright_dir);
const here = new URL(".", import.meta.url).pathname;

const memory = new WebAssembly.Memory({ initial: 2, maximum: 16384 });
const ui = (await WebAssembly.instantiate(await WebAssembly.compile(readFileSync(join(wasm_dir, "ui.wasm"))), { env: { memory }, wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }) })).exports;
const pk = ui.server_encode(rows);
const cbor = Buffer.from(memory.buffer.slice(Number(pk >> 32n), Number(pk >> 32n) + Number(pk & 0xffffffffn)));
const p = ui.alloc(cbor.length); new Uint8Array(memory.buffer).set(cbor, p); ui.ui_load(p, cbor.length);
const hq = ui.ui_html();
const sk = ui.schema_encode(rows);
const schema_cbor = Buffer.from(memory.buffer.slice(Number(sk >> 32n), Number(sk >> 32n) + Number(sk & 0xffffffffn)));
const html = Buffer.from(memory.buffer.slice(Number(hq >> 32n), Number(hq >> 32n) + Number(hq & 0xffffffffn))).toString("utf8");
const names = ["Ada", "Grace", "Linus", "Barbara", "Ken", "Margaret", "Dennis"];
const screen = [{ type: "table", head: ["Name <user>", "Amount", "Time"], rows: [] }, { type: "form", action: "/send", fields: [] }];
for (let r = 0; r < rows; ++r) screen[0].rows.push([`${names[r % names.length]} & co ${r}`, (r * 37) % 10000, { type: "time", epoch: 1700000000 + r * 86400 }]);
for (let f = 0; f < 5; ++f) screen[1].fields.push({ type: "field", name: `field${f}`, label: `Field ${f}`, input: f % 2 ? "number" : "text" });
const json = JSON.stringify(screen);

const types = { ".html": "text/html", ".mjs": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };
const server = createServer((req, res) => {
  const path = new URL(req.url, "http://x").pathname;
  let body;
  if (path === "/") body = readFileSync(join(here, "ui_page.html"));
  else if (path === "/data/screen.html") body = html;
  else if (path === "/data/screen.json") body = json;
  else if (path === "/data/screen.cbor") body = cbor;
  else if (path === "/data/screen.schema.cbor") body = schema_cbor;
  else if (path.startsWith("/wasm/")) body = readFileSync(join(wasm_dir, path.slice(6)));
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Content-Type", types[extname(path)] ?? (path === "/" ? "text/html" : "application/octet-stream"));
  res.end(body);
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;
const arms = ["htmx", "htmx_aa", "json_ui", "json_ui_aa", "wasm_ui", "wasm_schema", "wasm_schema_aa"];
const results = Object.fromEntries(arms.map((n) => [n, []]));
const browser = await chromium.launch();
for (let r = 0; r < reps; ++r)
  for (const n of r % 2 ? [...arms].reverse() : arms) {
    const context = await browser.newContext();
    const page = await context.newPage();
    await page.goto(`http://127.0.0.1:${port}/?arm=${n}`);
    await page.waitForFunction(() => window.result !== undefined, null, { timeout: 120000 });
    results[n].push(await page.evaluate(() => window.result));
    await context.close();
  }
const version = browser.version();
await browser.close();
server.close();
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; };
const summary = Object.fromEntries(arms.map((n) => {
  const R = results[n], m = (f) => +median(R.map(f)).toFixed(4);
  return [n, { dom_hash: R[0].dom_hash, init_ms: m((o) => o.init_ms), first_ms: m((o) => o.first_ms), warm_ms: m((o) => o.warm_ms) }];
}));
const context = {
  time: new Date().toISOString(), browser: `chromium ${version} headless (Playwright)`, node: process.version, kernel: os.release(), cpus: os.cpus().length, cpu: os.cpus()[0].model,
  rows, form_fields: 5, reps, wire_bytes: { html: Buffer.byteLength(html), json: Buffer.byteLength(json), cbor_tags: cbor.length, cbor_schema: schema_cbor.length },
  ui_tags: "provisional: 40100 table, 40101 form, 40102 field, 1 epoch time (RFC 8949) in wasm_ui; schema types screen 40105, table 40100, row 40104, moment 40103, form 40101, field 40102 in wasm_schema",
  measured: "init = Wasm compile+instantiate (0 for the others); time = from data in hand to DOM: htmx innerHTML of server HTML; json_ui JSON.parse + JS renderer + innerHTML; wasm_ui copy + cbor::decode + render in Wasm + TextDecoder + innerHTML; wasm_schema copy + schema path + render with compiled at_path + TextDecoder + innerHTML; each + forced layout; first = iteration 1 of 7 in a fresh page, warm = median of 3-7",
  commit: execFileSync("git", ["-C", join(here, "../../.."), "describe", "--always", "--dirty"], { encoding: "utf8" }).trim(),
};
console.log(JSON.stringify({ context, summary, results }, null, 1));
