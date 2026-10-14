const errno_success = 0;

export async function showcase_instantiate(wasm_bytes) {
  let memory;
  const decoder = new TextDecoder();
  const encoder = new TextEncoder();
  const wasi_snapshot_preview1 = {
    fd_write: (fd, iovs, iovs_len, nwritten) => {
      const view = new DataView(memory.buffer);
      let text = "";
      let written = 0;
      for (let i = 0; i < iovs_len; i++) {
        const ptr = view.getUint32(iovs + i * 8, true);
        const len = view.getUint32(iovs + i * 8 + 4, true);
        text += decoder.decode(new Uint8Array(memory.buffer, ptr, len));
        written += len;
      }
      (fd === 2 ? console.error : console.log)(text);
      view.setUint32(nwritten, written, true);
      return errno_success;
    },
  };
  let stack = [], keys = [], built = [], key;
  const text_at = (ptr, len) => decoder.decode(new Uint8Array(memory.buffer, ptr, len));
  const put = (v) => {
    if (!stack.length) { built.push(v); return; }
    const top = stack[stack.length - 1];
    if (Array.isArray(top)) top.push(v); else top[key] = v;
  };
  const js = {
    value_null: () => put(null), value_bool: (b) => put(!!b), value_number: (v) => put(v),
    value_string: (ptr, len) => put(text_at(ptr, len)),
    array_begin: () => { const a = []; put(a); stack.push(a); keys.push(key); },
    object_begin: () => { const o = {}; put(o); stack.push(o); keys.push(key); },
    key_string: (ptr, len) => { key = text_at(ptr, len); }, key_number: (v) => { key = String(v); },
    container_end: () => { stack.pop(); key = keys.pop(); },
  };
  const { instance } = await WebAssembly.instantiate(wasm_bytes, { wasi_snapshot_preview1, js });
  const e = instance.exports;
  memory = e.memory;
  e._initialize();
  const copied_in = (bytes) => {
    const ptr = e.alloc(bytes.length);
    new Uint8Array(memory.buffer, ptr, bytes.length).set(bytes);
    return ptr;
  };
  const called = (f, bytes) => {
    const ptr = copied_in(bytes);
    const r = f(ptr, bytes.length);
    e.release(ptr);
    return r;
  };
  const text_of = (packed) =>
    decoder.decode(new Uint8Array(memory.buffer, Number(packed >> 32n), Number(packed & 0xffffffffn)));
  return {
    exports: e,
    load: (bytes) => called(e.load, bytes),
    run: (path) => text_of(called(e.run, encoder.encode(path))),
    edn: (bytes) => text_of(called(e.edn, bytes)),
    compile: (path) => called(e.compile, encoder.encode(path)),
    js_values: (path) => {
      const c = called(e.compile, encoder.encode(path));
      if (c !== 0) return c;
      built = [];
      const n = e.compiled_js_values();
      const r = built;
      built = [];
      return n < 0 ? -n : r;
    },
  };
}
