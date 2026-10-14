import { readFileSync } from "node:fs";
import { showcase_instantiate } from "./loader.js";
const [wasm, doc] = process.argv.slice(2);
const s = await showcase_instantiate(readFileSync(wasm));
console.log("load", s.load(readFileSync(doc)));
for (const p of ["$.statuses[0].user.id", "$.statuses[?@.retweet_count > 100].id", "$..screen_name", "$.statuses[", "$[?"]) {
  const r = s.run(p);
  console.log(p, "=>", r.length > 300 ? r.slice(0, 300) + "..." : r);
}
console.log("edn", s.edn(new Uint8Array([0x82, 0x01, 0x63, 0x61, 0x62, 0x63])));
