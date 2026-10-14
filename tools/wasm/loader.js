const errno_badf = 8;
const errno_success = 0;

export async function showcase_instantiate(wasm_bytes) {
  let memory;
  const decoder = new TextDecoder();
  const encoder = new TextEncoder();
  const wasi_snapshot_preview1 = {
    fd_close: () => errno_badf,
    fd_seek: () => errno_badf,
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
  const { instance } = await WebAssembly.instantiate(wasm_bytes, { wasi_snapshot_preview1 });
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
  };
}
