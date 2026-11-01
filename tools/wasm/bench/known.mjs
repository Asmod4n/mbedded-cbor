import { readFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import { join } from "node:path";
import os from "node:os";

const [wasm_file, data_dir, p3_file, mode, arm] = process.argv.slice(2);
const median = (a) => { const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; };

const ops = {
  twitter_one: { doc: "twitter", js: (j) => j.statuses[50].user.screen_name.length, p3: "$.statuses[50].user.screen_name", wasm: "twitter_one" },
  twitter_users: { doc: "twitter", js: (j) => j.statuses.map((s) => s.user.id).length, p3: "$.statuses[*].user.id", wasm: "twitter_users" },
  twitter_filter: { doc: "twitter", js: (j) => j.statuses.filter((s) => s.user.screen_name === "IwiAlohomora").length, p3: "$.statuses[?@.user.screen_name == 'IwiAlohomora'].id_str", wasm: "twitter_filter" },
  canada_sum: { doc: "canada", js: (j) => { let s = 0; for (const r of j.features[0].geometry.coordinates) for (const p of r) s += p[0]; return s; }, p3: "$.features[0].geometry.coordinates[*][*][0]", wasm: "canada_sum", sum: true },
  citm_areas: { doc: "citm_catalog", js: (j) => { let n = 0; for (const p of j.performances) for (const c of p.seatCategories) n += c.areas.length; return n; }, p3: "$.performances[*].seatCategories[*].areas[*].areaId", wasm: "citm_areas" },
};

async function arm_of(name) {
  const [op_name, kind] = name.replace(/_aa$/, "").split(":");
  const op = ops[op_name];
  if (kind === "json_js") {
    const text = readFileSync(join(data_dir, op.doc + ".json"), "utf8");
    return () => op.js(JSON.parse(text));
  }
  if (kind === "json_p3") {
    const { jsonpath } = await import(p3_file);
    const text = readFileSync(join(data_dir, op.doc + ".json"), "utf8");
    const path = jsonpath.compile(op.p3);
    return () => { const nodes = path.query(JSON.parse(text)); return op.sum ? nodes.reduce((a, n) => a + n.value, 0) : nodes.length; };
  }
  const bytes = new Uint8Array(readFileSync(join(data_dir, op.doc + ".cbor")));
  const memory = new WebAssembly.Memory({ initial: 2, maximum: 16384 });
  const module = await WebAssembly.compile(readFileSync(wasm_file));
  const x = (await WebAssembly.instantiate(module, { env: { memory }, wasi_snapshot_preview1: { fd_write: () => 0 } })).exports;
  return () => {
    const p = x.alloc(bytes.length);
    new Uint8Array(memory.buffer).set(bytes, p);
    x.load(p, bytes.length);
    x.release(p);
    return x[op.wasm]();
  };
}

if (mode === "arm") {
  const run = await arm_of(arm);
  const times = [];
  let result;
  for (let k = 0; k < 7; ++k) { const t0 = performance.now(); result = run(); times.push(performance.now() - t0); }
  console.log(JSON.stringify({ arm, result, first_ms: times[0], ms: median(times.slice(2)) }));
} else if (mode === "run") {
  const reps = Number(process.env.REPS ?? 10);
  const names = Object.keys(ops).flatMap((o) => [`${o}:json_js`, `${o}:json_p3`, `${o}:wasm`]).concat(["twitter_users:json_js_aa", "twitter_users:wasm_aa"]);
  const results = Object.fromEntries(names.map((n) => [n, []]));
  for (let r = 0; r < reps; ++r)
    for (const n of r % 2 ? [...names].reverse() : names)
      results[n].push(JSON.parse(execFileSync(process.execPath, [process.argv[1], wasm_file, data_dir, p3_file, "arm", n], { encoding: "utf8" })));
  const summary = Object.fromEntries(names.map((n) => {
    const ms = results[n].map((o) => o.ms);
    return [n, { result: results[n][0].result, first_median_ms: +median(results[n].map((o) => o.first_ms)).toFixed(3), median_ms: +median(ms).toFixed(3), spread: +(Math.max(...ms) / Math.min(...ms) - 1).toFixed(3) }];
  }));
  const context = {
    time: new Date().toISOString(), node: process.version, v8: process.versions.v8, kernel: os.release(), cpus: os.cpus().length, cpu: os.cpus()[0].model,
    reps, iterations_per_process: "7, first and median of the last 5; each iteration includes JSON.parse or the copy into Wasm memory and the load",
    data: "nativejson-benchmark canada.json, citm_catalog.json, twitter.json (miloyip/nativejson-benchmark master); CBOR by python3-cbor2 (Debian sid) from the same JSON",
    jsonpath_library: "json-p3 3.0.2 (npm)",
    commit: execFileSync("git", ["-C", new URL("../../..", import.meta.url).pathname, "describe", "--always", "--dirty"], { encoding: "utf8" }).trim(),
  };
  console.log(JSON.stringify({ context, summary, results }, null, 1));
}
