import { readFileSync } from "node:fs";
import { showcase_instantiate } from "./loader.js";
import { leaf_reader } from "./leaf.js";
const [wasm, doc] = process.argv.slice(2);
const s = await showcase_instantiate(readFileSync(wasm));
console.log("load", s.load(readFileSync(doc)));
for (const p of ["$.statuses[0].user.id", "$.statuses[?@.retweet_count > 100].id", "$..screen_name", "$.statuses[", "$[?"]) {
  const r = s.run(p);
  console.log(p, "=>", r.length > 300 ? r.slice(0, 300) + "..." : r);
}
console.log("edn", s.edn(new Uint8Array([0x82, 0x01, 0x63, 0x61, 0x62, 0x63])));
console.log("js_values", JSON.stringify(s.js_values("$.statuses[0:2].user.screen_name")));
const leaf = leaf_reader(s.exports);
console.log("leaf integer", leaf.integer("$.statuses[0].user.id"), "string", leaf.string("$.statuses[0].user.screen_name"),
  "numbers", leaf.numbers("$.statuses[*].retweet_count").length);
