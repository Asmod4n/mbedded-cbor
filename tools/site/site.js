import { encoded, hexOf, bytesOf, instantiated } from "./examples.mjs";

const group = document.body.dataset.group;
const escaped = s => String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;");
const shown = (call, result) => '<span class="call">' + escaped(call) + "</span>\n" +
  '<span class="' + (/^(fault|unexpected)/.test(result) ? "err" : "ok") + '">' + escaped(result) + "</span>";
const module = fetch(document.querySelector('script[src^="site.js"]').src.replace(/site\.js.*$/, "examples.wasm"))
  .then(r => r.arrayBuffer()).then(instantiated);
const run = module.then(m => m.run);

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
  const list = consoleBox.querySelector("[data-completions]");
  let options = [], chosen = -1;
  const shut = () => { list.hidden = true; list.replaceChildren(); options = []; chosen = -1; entry.setAttribute("aria-expanded", "false"); };
  const take = text => { entry.value = text; shut(); entry.focus(); listed(); };
  const marked = () => options.forEach((o, i) => o.setAttribute("aria-selected", i === chosen ? "true" : "false"));
  const listed = async () => {
    let bytes;
    try { bytes = b.bytes(); } catch { shut(); return; }
    const found = await (await module).completed(bytes, [...history, entry.value].join("\n"), group);
    shut();
    if (!found.length || (found.length === 1 && found[0] === entry.value)) return;
    options = found.slice(0, 40).map((text, i) => {
      const o = document.createElement("button");
      o.type = "button";
      o.id = "co-option-" + i;
      o.setAttribute("role", "option");
      o.textContent = text.slice(entry.value.length - entry.value.match(/[A-Za-z0-9_:]*$/)[0].length);
      o.addEventListener("click", () => take(text));
      list.append(o);
      return Object.assign(o, { full: text });
    });
    list.hidden = false;
    entry.setAttribute("aria-expanded", "true");
  };
  entry.addEventListener("input", () => {
    if (/(->|\.|::)$/.test(entry.value) || !list.hidden) listed(); else shut();
  });
  entry.addEventListener("keydown", e => {
    if (e.key === "Tab" && !e.shiftKey) {
      if (options.length === 1) { e.preventDefault(); take(options[0].full); return; }
      if (options.length > 1) {
        e.preventDefault();
        const all = options.map(o => o.full);
        let common = all[0];
        for (const t of all) while (!t.startsWith(common)) common = common.slice(0, -1);
        if (common.length > entry.value.length) { entry.value = common; listed(); } else { chosen = (chosen + 1) % options.length; marked(); }
        return;
      }
      e.preventDefault();
      listed();
    } else if (e.key === "ArrowDown" && options.length) { e.preventDefault(); chosen = (chosen + 1) % options.length; marked(); }
    else if (e.key === "ArrowUp" && options.length) { e.preventDefault(); chosen = (chosen + options.length - 1) % options.length; marked(); }
    else if (e.key === "Enter" && chosen >= 0) { e.preventDefault(); take(options[chosen].full); }
    else if (e.key === "Escape") shut();
  });
  consoleBox.querySelector("[data-complete]").addEventListener("click", () => { entry.focus(); listed(); });
}
