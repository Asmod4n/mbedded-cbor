import { createServer } from "node:http";
import { readFileSync, writeFileSync } from "node:fs";
import { join, extname } from "node:path";
import { createRequire } from "node:module";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [playwright_dir, artifacts_dir, tree_dir, out_file, reps_text, arms_text, variants_text] = process.argv.slice(2);
const reps = Number(reps_text);
const require = createRequire(import.meta.url);
const { chromium } = require(playwright_dir);
const types = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm", ".json": "application/json" };
const server = createServer((req, res) => {
  const path = new URL(req.url, "http://x").pathname;
  const name = path.split("/").pop();
  const file = path.startsWith("/bench/") ? join(tree_dir, "tools/wasm/bench", name) : join(artifacts_dir, name);
  res.setHeader("Cross-Origin-Opener-Policy", "same-origin");
  res.setHeader("Cross-Origin-Embedder-Policy", "require-corp");
  res.setHeader("Content-Type", types[extname(file)] ?? "application/octet-stream");
  res.end(readFileSync(file));
});
await new Promise((r) => server.listen(0, "127.0.0.1", r));
const port = server.address().port;
const arm_names = arms_text ? arms_text.split(",") : ["leaves_cbor", "leaves_cbor_aa", "leaves_cbor_view", "leaves_json", "samples_cbor", "samples_cbor_aa", "samples_cbor_view", "samples_json"];
const variants = variants_text ? variants_text.split(",") : ["showcase"];
const arms = variants.flatMap((v) => arm_names.map((a) => `${v}:${a}`));
const browser = await chromium.launch();
const runs = Object.fromEntries(arms.map((a) => [a, []]));
for (let r = 0; r < reps; r++) {
  for (const arm of arms) {
    const page = await browser.newPage();
    page.on("pageerror", (e) => console.error(arm, e.message));
    const [variant, arm_name] = arm.split(":");
    await page.goto(`http://127.0.0.1:${port}/bench/flow.html?arm=${arm_name}&wasm=${variant.replace(/_aa$/, "")}.wasm`);
    await page.waitForFunction(() => window.bench_result, null, { timeout: 120000 });
    runs[arm].push(await page.evaluate(() => window.bench_result));
    await page.close();
  }
}
const median = (v) => { const s = [...v].sort((a, b) => a - b); return s[s.length >> 1]; };
const summary = Object.fromEntries(arms.map((a) => {
  const ns = runs[a].map((x) => x.ns);
  return [a, { median_ns: median(ns), min: Math.min(...ns), max: Math.max(...ns), check: runs[a][0].check }];
}));
const read = (cmd, args) => { try { return execFileSync(cmd, args, { encoding: "utf8" }).trim(); } catch (e) { return String(e); } };
const context = {
  commit: read("git", ["-C", tree_dir, "rev-parse", "HEAD"]),
  dirty: read("git", ["-C", tree_dir, "status", "--porcelain"]) !== "",
  browser: `chromium ${browser.version()} headless shell, playwright ${require(playwright_dir + "/package.json").version}, default launch flags`,
  isolated: runs[arms[0]][0].isolated,
  wasm_flags: "clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -flto -fno-exceptions -Wl,--strip-all,--gc-sections",
  wasm_bytes: Object.fromEntries(variants.map((v) => [v, readFileSync(join(artifacts_dir, v.replace(/_aa$/, "") + ".wasm")).length])),
  inputs: { cbor_bytes: readFileSync(join(artifacts_dir, "twitter.cbor")).length, json_bytes: readFileSync(join(artifacts_dir, "twitter.json")).length },
  json_library: "json-p3 2.2.2 iife",
  method: "each repetition of each arm is one cold flow in a fresh page, timed once; arms alternate; before the timer the CBOR bytes are a Uint8Array, the JSON text is a string, the wasm module is instantiated and json-p3 is loaded; values_*: load or parse, then the user id and the list of status ids; objects_*: load or parse, then four paths each compiled once and applied once, results as JS objects; leaves_*: an integer, a string and a Float64Array of 100 retweet counts; samples_*: 100000 doubles, CBOR as an RFC 8746 float64 little endian typed array (tag 86) given as a Float64Array view or copy, JSON as an array of numbers turned into a Float64Array; the inputs are built before the timer; *_view: JS writes the bytes into a buffer that wasm reserved and lazy::view takes it without a second copy, against lazy::copy in the other CBOR arms",
  kernel: os.release(), cpu: os.cpus()[0].model, cpus: os.cpus().length, user: os.userInfo().username,
  priority: os.getPriority(), node: process.version, repetitions: reps,
};
writeFileSync(out_file, JSON.stringify({ context, summary, runs }, null, 1));
await browser.close();
server.close();
