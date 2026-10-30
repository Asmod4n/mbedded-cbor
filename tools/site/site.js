import { encoded, hexOf, bytesOf, instantiated } from "./examples.mjs";

const group = document.body.dataset.group;
const escaped = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;");
const shown = (call, result) => '<span class="call">' + escaped(call) + "</span>\n" +
  '<span class="' + (/^(fault|unexpected)/.test(result) ? "err" : "ok") + '">' + escaped(result) + "</span>";
const run = fetch(document.querySelector('script[src^="site.js"]').src.replace(/site\.js.*$/, "examples.wasm"))
  .then(r => r.arrayBuffer()).then(instantiated);

const boxOf = box => {
  const message = box.querySelector("[data-message]"), notation = box.querySelector("[data-notation]");
  const hex = box.querySelector("[data-bytes]");
  const bytes = () => bytesOf(hex.value);
  const fromMessage = () => {
    try { hex.value = hexOf(encoded(JSON.parse(message.value))); message.removeAttribute("aria-invalid"); }
    catch { message.setAttribute("aria-invalid", "true"); }
  };
  const toNotation = async () => {
    if (!notation) return;
    try {
      const text = (await (await run)(bytes(), "cbor::diagnostic_notation(bytes).value()", group))[0];
      try { notation.value = JSON.parse(text); } catch { notation.value = text; }
    }
    catch (e) { notation.value = e.message; }
  };
  if (message) { message.addEventListener("input", fromMessage); if (!hex.value) fromMessage(); }
  hex.addEventListener("input", toNotation);
  toNotation();
  return { message, hex, bytes, fromMessage };
};

document.querySelectorAll(".play:not([data-console])").forEach(box => {
  const b = boxOf(box), calls = box.querySelector("[data-calls]"), out = box.querySelector("[data-out]");
  const button = box.querySelector("button.run");
  const go = async () => {
    let bytes;
    try { bytes = b.bytes(); } catch (e) { out.innerHTML = shown("bytes", "fault: " + e.message); return; }
    const lines = calls.value.split("\n").filter(l => l.trim());
    const results = await (await run)(bytes, lines.join("\n"), group);
    out.innerHTML = lines.map((l, i) => shown(l, results[i] ?? "")).join("\n");
  };
  button.hidden = false;
  button.addEventListener("click", go);
  [b.message, b.hex, calls].forEach(el => el?.addEventListener("input", go));
});

const consoleBox = document.querySelector(".play[data-console]");
if (consoleBox) {
  const b = boxOf(consoleBox), log = consoleBox.querySelector(".log");
  const form = consoleBox.querySelector("form"), entry = form.querySelector("input");
  const history = [];
  form.addEventListener("submit", async e => {
    e.preventDefault();
    const line = entry.value;
    if (!line.trim()) return;
    let bytes;
    try { bytes = b.bytes(); } catch (err) { log.insertAdjacentHTML("beforeend", shown(line, "fault: " + err.message) + "\n"); return; }
    const results = await (await run)(bytes, [...history, line].join("\n"), group);
    const last = results[results.length - 1] ?? "";
    if (!last.startsWith("fault")) history.push(line);
    log.insertAdjacentHTML("beforeend", shown("» " + line, last) + "\n");
    log.scrollTop = log.scrollHeight;
    entry.select();
  });
  [b.message, b.hex].forEach(el => el?.addEventListener("input", () => { history.length = 0; }));
}
