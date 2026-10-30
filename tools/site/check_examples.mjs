import { readFileSync, readdirSync } from "node:fs";
import { encoded, bytesOf, instantiated } from "./examples.mjs";

const [wasm, work, header] = process.argv.slice(2);
const run = await instantiated(readFileSync(wasm));
const flat = s => s.replace(/\s+/g, " ").trim();
let wrong = 0, count = 0;
for (const file of readdirSync(work).filter(f => /^examples-.*\.json$/.test(f))) {
  const group = file.slice("examples-".length, -".json".length);
  const dump = group === "index" ? "" : flat(readFileSync(`${work}/${group}.txt`, "utf8"));
  for (const e of JSON.parse(readFileSync(`${work}/${file}`, "utf8"))) {
    const bytes = e.message !== undefined ? Uint8Array.from(encoded(JSON.parse(e.message))) : bytesOf(e.bytes);
    const out = await run(bytes, e.calls.map(c => c.call).join("\n"), group);
    e.calls.forEach((c, i) => {
      count++;
      if (out[i] !== c.result) {
        wrong++;
        console.error(`${e.page}: ${c.call}\n  page:   ${c.result}\n  module: ${out[i]}`);
      }
      for (const text of [c.call, c.result])
        if (!dump.includes(flat(text))) {
          wrong++;
          console.error(`${e.page}: lynx does not show ${text}`);
        }
    });
  }
}
const names = [...readFileSync(header, "utf8").matchAll(/^    ([a-z_]+)(?: = 1)?,?$/gm)].map(m => m[1]);
const back = await run(new Uint8Array([0]), names.map(n => `cbor::error::${n}`).join("\n"), "errors");
names.forEach((n, i) => {
  count++;
  if (back[i] !== `cbor::error::${n}`) {
    wrong++;
    console.error(`the error ${n} reads back as ${back[i]}`);
  }
});
console.log(`examples: ${count} checks, ${wrong} differ`);
process.exit(wrong ? 1 : 0);
