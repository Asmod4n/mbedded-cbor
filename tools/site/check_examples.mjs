import { readFileSync, readdirSync } from "node:fs";
import { encoded, bytesOf, instantiated } from "./examples.mjs";

const [wasm, work, header] = process.argv.slice(2);
const { run, completed } = await instantiated(readFileSync(wasm));
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
// A completion offers only calls that fit the value of the document: its indexes, its keys and the types that
// get<> reads from it. Each expected list is written from the API of cbor::lazy and the types of RFC 8949, not
// from the code that makes the completions.
const array = { doc: [1, "two", { k: 3 }] };
const map = { doc: { name: "bob", age: 30, tags: ["a", "b"] } };
const text = { doc: "hello" };
const number = { doc: 30 };
const keys = { doc: { 'a"b': 1, "a\\b": 2, "a'b": 3, "größe": 4, "x\ny": 5, "t\tz": 6, "c\rr": 7 } };
const old = { doc: { a: 1 } };
const integers = ["int", "long", "unsigned", "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t", "std::uint8_t",
  "std::uint16_t", "std::uint32_t", "std::uint64_t", "std::size_t", "double"];
const cases = [
  // The cases of the first form of the console stay, in the form that ends in a call that runs.
  [old, "doc->ty", ["doc->type()"]],
  [old, "doc.", ["doc.has_value()", "doc.value()", "doc.value_or("]],
  [old, "cbor::lazy::ta", ["cbor::lazy::take_ownership("]],
  [old, "auto const x = doc\nx->si", ["x->size()"]],
  [old, "std::move(doc->ty", ["std::move(doc->type()", "std::move(doc->type())"]],
  [old, "auto const y = doc->em", ["auto const y = doc->empty()"]],
  [old, "doc->at_path<\"$.a", ["doc->at_path<\"$.a\">()"]],
  [old, "by", ["bytes"]],
  // An array takes indexes and paths with indexes; it has no entries and no scalar to get.
  [array, "doc->", ["doc->at(0)", "doc->at_path<\"$\">()", "doc->size()", "doc->empty()", "doc->type()", "doc->elements()",
    "doc->clone()", "doc->at(1)", "doc->at(2)", "doc->at_path<\"$[0]\">()", "doc->at_path<\"$[1]\">()",
    "doc->at_path<\"$[2]\">()", "doc->at_path<\"$[*]\", cbor::nodelist>()"]],
  [array, "doc->at(", ["doc->at(0)", "doc->at(1)", "doc->at(2)"]],
  [array, "doc->get<", []],
  [array, "doc->at(1)->get<", ["doc->at(1)->get<std::string_view>()"]],
  [array, "(*doc)[", ["(*doc)[0]", "(*doc)[1]", "(*doc)[2]"]],
  [array, "doc[", []],
  [array, "auto const i = 1\ndoc->at(i", ["doc->at(i)"]],
  // A map takes its keys; the parent's keys are offered beside a path that names one of them.
  [map, "doc->at(\"", ["doc->at(\"name\")", "doc->at(\"age\")", "doc->at(\"tags\")"]],
  [map, "doc->at(\"n", ["doc->at(\"name\")"]],
  [map, "doc->get<", []],
  [map, "doc->at(\"tags\")->", ["doc->at(\"tags\")->at(0)", "doc->at(\"tags\")->at_path<\"$\">()", "doc->at(\"tags\")->size()",
    "doc->at(\"tags\")->empty()", "doc->at(\"tags\")->type()", "doc->at(\"tags\")->elements()", "doc->at(\"tags\")->clone()",
    "doc->at(\"tags\")->at(1)", "doc->at(\"tags\")->at_path<\"$[0]\">()", "doc->at(\"tags\")->at_path<\"$[1]\">()",
    "doc->at(\"tags\")->at_path<\"$[*]\", cbor::nodelist>()"]],
  [map, "doc->at_path<\"$.", ["doc->at_path<\"$.name\">()", "doc->at_path<\"$.age\">()", "doc->at_path<\"$.tags\">()"]],
  [map, "doc->at_path<\"$.n", ["doc->at_path<\"$.name\">()"]],
  [map, "doc->at_path<\"$.tags", ["doc->at_path<\"$.tags\">()", "doc->at_path<\"$.tags[0]\">()", "doc->at_path<\"$.tags[1]\">()",
    "doc->at_path<\"$.tags[*]\", cbor::nodelist>()"]],
  [map, "doc->at_path<\"$.tags[", ["doc->at_path<\"$.tags[0]\">()", "doc->at_path<\"$.tags[1]\">()",
    "doc->at_path<\"$.tags[*]\", cbor::nodelist>()"]],
  [map, "doc->at(doc->at(\"n", ["doc->at(doc->at(\"name\")"]],
  [map, "std::move(doc->at(\"n", ["std::move(doc->at(\"name\")", "std::move(doc->at(\"name\"))"]],
  [map, "cbor::key{doc->ty", ["cbor::key{doc->type()"]],
  [map, "doc->clone()", ["doc->clone().value()->type", "doc->clone().value()->get<cbor::type::map>()",
    "doc->clone().value()->additional_information"]],
  [map, "doc->clone()->", []],
  [map, "doc->nothing", []],
  // A text gives only a std::string_view; it has no index and no key.
  [text, "doc->get<", ["doc->get<std::string_view>()"]],
  [text, "doc->at(", []],
  // An integer is read by every integer type that holds 30, and by double.
  [number, "doc->get<", integers.map(t => `doc->get<${t}>()`)],
  // Keys with characters that the console string and the JSONPath escape; a key with CR has no console escape.
  [keys, "doc->at(\"a", ["doc->at(\"a\\\"b\")", "doc->at(\"a\\\\b\")", "doc->at(\"a'b\")"]],
  [keys, "doc->at(\"x", ["doc->at(\"x\\ny\")"]],
  [keys, "doc->at(\"t", ["doc->at(\"t\\tz\")"]],
  [keys, "doc->at(\"c", []],
  [keys, "doc->at_path<\"$.g", ["doc->at_path<\"$.größe\">()"]],
  [keys, "doc->at_path<\"$['a", ["doc->at_path<\"$['a\\\"b']\">()", "doc->at_path<\"$['a\\\\\\\\b']\">()",
    "doc->at_path<\"$['a\\\\'b']\">()"]],
];
// Every offered line runs, unless it still has an open bracket, which the user closes next.
const open = line => { let depth = 0, quoted = false; for (let i = 0; i < line.length; i++) { const c = line[i];
  if (quoted) { if (c === "\\") i++; else if (c === '"') quoted = false; continue; }
  if (c === "-" && line[i + 1] === ">") { i++; continue; }
  if (c === '"') quoted = true; else if ("([{<".includes(c)) depth++; else if (")]}>".includes(c)) depth--; } return depth > 0 || quoted; };
for (const [{ doc }, lines, expected] of cases) {
  count++;
  const bytes = Uint8Array.from(encoded(doc));
  const { found, trapped } = await completed(bytes, lines, "lazy");
  const history = lines.split("\n").slice(0, -1);
  if (trapped || JSON.stringify(found) !== JSON.stringify(expected)) {
    wrong++;
    console.error(`completion of ${JSON.stringify(lines)} on ${JSON.stringify(doc)}${trapped ? " trapped" : ""}\n  want: ${JSON.stringify(expected)}\n  got:  ${JSON.stringify(found)}`);
  }
  for (const line of found.filter(l => !open(l) && !l.endsWith("("))) {
    const result = (await run(bytes, [...history, line].join("\n"), "lazy")).at(-1);
    if (result.startsWith("fault")) {
      wrong++;
      console.error(`the completion ${JSON.stringify(line)} runs to ${result}`);
    }
  }
}
// A document of 10,000 elements still offers every member, not only the first 40 indexes.
{
  count++;
  const bytes = Uint8Array.from(encoded(Array.from({ length: 10000 }, (_, i) => i)));
  const { found } = await completed(bytes, "doc->", "lazy");
  if (!found.includes("doc->size()") || found.length !== 40) {
    wrong++;
    console.error(`completion of doc-> on 10,000 elements: ${JSON.stringify(found)}`);
  }
}
console.log(`examples: ${count} checks, ${wrong} differ`);
process.exit(wrong ? 1 : 0);
