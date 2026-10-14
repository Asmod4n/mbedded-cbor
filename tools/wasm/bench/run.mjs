import { createServer } from "node:http";
import { readFileSync, writeFileSync } from "node:fs";
import { join, extname } from "node:path";
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [playwright_dir, artifacts_dir, tree_dir, out_file, reps_text] = process.argv.slice(2);
const reps = Number(reps_text);
const require = createRequire(import.meta.url);
const { chromium } = require(playwright_dir);
const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };
const server = createServer((req, res) => {
  const path = new URL(req.url, "http://x").pathname;
  const name = path.split("/").pop();
  let file;
  if (["showcase.wasm", "twitter.cbor", "twitter.json"].includes(name)) file = join(artifacts_dir, name);
  else if (path.startsWith("/bench/")) file = join(tree_dir, "tools/wasm/bench", name);
  else file = join(tree_dir, "tools/wasm", name);
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Content-Type", types[extname(file)] ?? "application/octet-stream");
  res.end(readFileSync(file));
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;

const arms = ["json_parse", "json_parse_aa", "json_value", "json_ids", "cbor_value_literal", "cbor_value_literal_aa",
  "cbor_value_runtime", "cbor_ids_literal", "cbor_ids_runtime", "empty_call", "first_json", "first_cbor"];
const browser = await chromium.launch();
const runs = Object.fromEntries(arms.map((a) => [a, []]));
for (let r = 0; r < reps; r++) {
  for (const arm of arms) {
    const page = await browser.newPage();
    await page.goto(`http://127.0.0.1:${port}/bench/page.html?arm=${arm}`);
    await page.waitForFunction(() => window.bench_result, null, { timeout: 120000 });
    runs[arm].push(await page.evaluate(() => window.bench_result));
    await page.close();
  }
}
const median = (v) => { const s = [...v].sort((a, b) => a - b); return s[s.length >> 1]; };
const summary = Object.fromEntries(arms.map((a) => {
  const ns = runs[a].map((x) => x.ns_per_op);
  return [a, { median_ns_per_op: median(ns), min: Math.min(...ns), max: Math.max(...ns), check: runs[a][0].check }];
}));
const read = (cmd, args) => { try { return execFileSync(cmd, args, { encoding: "utf8" }).trim(); } catch (e) { return String(e); } };
const context = {
  commit: read("git", ["-C", tree_dir, "rev-parse", "HEAD"]),
  dirty: read("git", ["-C", tree_dir, "status", "--porcelain"]) !== "",
  browser: `chromium ${browser.version()} headless shell, playwright ${require(playwright_dir + "/package.json").version}, default launch flags`,
  isolated: runs.json_parse[0].isolated,
  wasm_flags: "clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -fno-exceptions",
  wasm_bytes: readFileSync(join(artifacts_dir, "showcase.wasm")).length,
  inputs: { cbor_bytes: readFileSync(join(artifacts_dir, "twitter.cbor")).length, json_bytes: readFileSync(join(artifacts_dir, "twitter.json")).length },
  method: "each repetition of each arm runs in a fresh page; arms alternate; a loop arm doubles its iteration count until one batch takes at least 200 ms; a first_* arm times one call in a fresh page; the CBOR bytes are in wasm memory and the JSON text is a JS string before the timer starts",
  kernel: os.release(), cpu: os.cpus()[0].model, cpus: os.cpus().length, user: os.userInfo().username,
  priority: os.getPriority(), node: process.version, repetitions: reps,
};
writeFileSync(out_file, JSON.stringify({ context, summary, runs }, null, 1));
await browser.close();
server.close();
