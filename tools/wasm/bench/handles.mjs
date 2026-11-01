import { readFileSync, writeFileSync } from "node:fs";
import { execFileSync } from "node:child_process";
import os from "node:os";

const [wasm_file, mode, arm, extra] = process.argv.slice(2);
const count = 1000000;

function document_of(kind) {
  const head = [0x9a, (count >>> 24) & 255, (count >>> 16) & 255, (count >>> 8) & 255, count & 255];
  const size = kind === "int" ? 3 : 9;
  const out = new Uint8Array(head.length + count * size);
  out.set(head, 0);
  let at = head.length;
  for (let i = 0; i < count; ++i) {
    if (kind === "int") {
      const v = 1000 + (i % 1000);
      out[at] = 0x19; out[at + 1] = v >> 8; out[at + 2] = v & 255;
    } else {
      out[at] = 0x68;
      const s = String(10000000 + (i % 1000));
      for (let k = 0; k < 8; ++k) out[at + 1 + k] = s.charCodeAt(k);
    }
    at += size;
  }
  return out;
}

async function instance_of() {
  const memory = new WebAssembly.Memory({ initial: 2, maximum: 16384 });
  const module = await WebAssembly.compile(readFileSync(wasm_file));
  const x = (await WebAssembly.instantiate(module, { env: { memory }, wasi_snapshot_preview1: { fd_write: () => 0 } })).exports;
  return { x, memory };
}

function loaded(x, memory, bytes) {
  const p = x.alloc(bytes.length);
  new Uint8Array(memory.buffer).set(bytes, p);
  return x.doc_load(p, bytes.length);
}

const median = (a) => { const s = [...a].sort((p, q) => p - q); return s[s.length >> 1]; };

const arms = {
  offsets_int(x, memory) {
    const doc = loaded(x, memory, document_of("int"));
    return () => {
      let sum = 0n, off = x.arr_first(doc);
      for (let i = 0; i < count; ++i) { sum += x.get_int(doc, off); off = x.arr_next(doc, off); }
      return sum;
    };
  },
  handles_int(x, memory) {
    const doc = loaded(x, memory, document_of("int"));
    const registry = new FinalizationRegistry((h) => x.h_release(h));
    return () => {
      let sum = 0n, off = x.arr_first(doc);
      const kept = new Array(count);
      for (let i = 0; i < count; ++i) {
        const w = { h: x.h_new(doc, off) };
        registry.register(w, w.h, w);
        kept[i] = w;
        off = x.arr_next(doc, off);
      }
      for (let i = 0; i < count; ++i) sum += x.h_get_int(kept[i].h);
      for (let i = 0; i < count; ++i) { registry.unregister(kept[i]); x.h_release(kept[i].h); }
      return sum;
    };
  },
  text_global(x, memory) {
    const doc = loaded(x, memory, document_of("text"));
    const decoder = new TextDecoder();
    let view = new Uint8Array(memory.buffer);
    return () => {
      let total = 0, off = x.arr_first(doc);
      for (let i = 0; i < count; ++i) {
        const packed = x.get_text(doc, off);
        if (view.buffer !== memory.buffer) view = new Uint8Array(memory.buffer);
        const p = Number(packed >> 32n), n = Number(packed & 0xffffffffn);
        total += decoder.decode(view.subarray(p, p + n)).length;
        off = x.arr_next(doc, off);
      }
      return total;
    };
  },
  text_new(x, memory) {
    const doc = loaded(x, memory, document_of("text"));
    const decoder = new TextDecoder();
    return () => {
      let total = 0, off = x.arr_first(doc);
      for (let i = 0; i < count; ++i) {
        const packed = x.get_text(doc, off);
        const p = Number(packed >> 32n), n = Number(packed & 0xffffffffn);
        total += decoder.decode(new Uint8Array(memory.buffer, p, n)).length;
        off = x.arr_next(doc, off);
      }
      return total;
    };
  },
};

if (mode === "facts") {
  const { x, memory } = await instance_of();
  const rss0 = process.memoryUsage().rss;
  const before = memory.buffer;
  const view = new Uint8Array(memory.buffer);
  const doc = loaded(x, memory, document_of("int"));
  const first = x.arr_first(doc);
  const text_before = x.get_int(doc, first);
  const pages_before = memory.buffer.byteLength / 65536;
  const big = x.alloc(64 * 1024 * 1024);
  const pages_after = memory.buffer.byteLength / 65536;
  const text_after = x.get_int(doc, first);
  const rss1 = process.memoryUsage().rss;
  new Uint8Array(memory.buffer).fill(1, big, big + 64 * 1024 * 1024);
  const rss2 = process.memoryUsage().rss;
  console.log(JSON.stringify({
    maximum_pages: 16384, pages_before, pages_after,
    value_same_after_grow: text_before === text_after, offset_of_first_element: first,
    old_buffer_detached: before.byteLength === 0, old_view_length_after_grow: view.length,
    rss_mib_after_load: +(rss0 / 1048576).toFixed(1), rss_mib_after_grow_64mib_untouched: +(rss1 / 1048576).toFixed(1),
    rss_mib_after_writing_64mib: +(rss2 / 1048576).toFixed(1),
  }));
} else if (mode === "arm") {
  const { x, memory } = await instance_of();
  const run = arms[arm.replace(/_aa$/, "")](x, memory);
  const times = [];
  for (let k = 0; k < 7; ++k) { const t0 = performance.now(); run(); times.push(performance.now() - t0); }
  console.log(JSON.stringify({ arm, ms: median(times.slice(2)), rss_mib: +(process.memoryUsage().rss / 1048576).toFixed(1) }));
} else if (mode === "run") {
  const reps = Number(extra ?? 10);
  const names = ["offsets_int", "offsets_int_aa", "handles_int", "text_global", "text_global_aa", "text_new"];
  const results = Object.fromEntries(names.map((n) => [n, []]));
  for (let r = 0; r < reps; ++r) {
    const order = r % 2 ? [...names].reverse() : names;
    for (const n of order) {
      const out = JSON.parse(execFileSync(process.execPath, [process.argv[1], wasm_file, "arm", n], { encoding: "utf8" }));
      results[n].push(out);
    }
  }
  const facts = JSON.parse(execFileSync(process.execPath, [process.argv[1], wasm_file, "facts"], { encoding: "utf8" }));
  const summary = Object.fromEntries(names.map((n) => {
    const ms = results[n].map((o) => o.ms);
    return [n, { median_ms: +median(ms).toFixed(2), spread: +(Math.max(...ms) / Math.min(...ms) - 1).toFixed(3), rss_mib: median(results[n].map((o) => o.rss_mib)) }];
  }));
  const context = {
    time: new Date().toISOString(), node: process.version, v8: process.versions.v8, kernel: os.release(), cpus: os.cpus().length,
    cpu: os.cpus()[0].model, elements: count, reps, iterations_per_process: "7, median of the last 5",
    commit: execFileSync("git", ["-C", new URL("../../..", import.meta.url).pathname, "describe", "--always", "--dirty"], { encoding: "utf8" }).trim(),
  };
  console.log(JSON.stringify({ context, facts, summary, results }, null, 1));
}
