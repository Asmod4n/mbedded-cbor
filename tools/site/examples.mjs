const textEncoder = new TextEncoder();
const textDecoder = new TextDecoder();

const head = (major, n) => {
  if (n < 24) return [major << 5 | n];
  if (n < 0x100) return [major << 5 | 24, n];
  if (n < 0x10000) return [major << 5 | 25, n >> 8, n & 255];
  if (n < 0x100000000) return [major << 5 | 26, n >>> 24, (n >>> 16) & 255, (n >>> 8) & 255, n & 255];
  const b = new DataView(new ArrayBuffer(8));
  b.setBigUint64(0, BigInt(n));
  return [major << 5 | 27, ...new Uint8Array(b.buffer)];
};

export const encoded = v => {
  if (v === null) return [0xf6];
  if (v === true) return [0xf5];
  if (v === false) return [0xf4];
  if (typeof v === "number") {
    if (Number.isSafeInteger(v)) return v >= 0 ? head(0, v) : head(1, -1 - v);
    const d = new DataView(new ArrayBuffer(9));
    d.setUint8(0, 0xfb);
    d.setFloat64(1, v);
    return [...new Uint8Array(d.buffer)];
  }
  if (typeof v === "string") {
    const s = [...textEncoder.encode(v)];
    return [...head(3, s.length), ...s];
  }
  if (Array.isArray(v)) return [...head(4, v.length), ...v.flatMap(encoded)];
  const keys = Object.keys(v);
  return [...head(5, keys.length), ...keys.flatMap(k => [...encoded(k), ...encoded(v[k])])];
};

export const hexOf = bytes => [...bytes].map(b => b.toString(16).padStart(2, "0")).join(" ");

export const bytesOf = hex => {
  const t = hex.replace(/\s+/g, "");
  if (t.length % 2 || /[^0-9a-f]/i.test(t)) throw new Error("the bytes are not hex");
  return Uint8Array.from(t.match(/../g) ?? [], h => parseInt(h, 16));
};

export const instantiated = async source => {
  const module = await WebAssembly.compile(source);
  let memory;
  const imports = {};
  for (const i of WebAssembly.Module.imports(module))
    (imports[i.module] ??= {})[i.name] = () => { throw new Error("the module called " + i.name); };
  Object.assign(imports.wasi_snapshot_preview1 ??= {}, {
    environ_sizes_get: (count, size) => {
      const d = new DataView(memory.buffer);
      d.setUint32(count, 0, true);
      d.setUint32(size, 0, true);
      return 0;
    },
    environ_get: () => 0,
    random_get: (p, n) => { crypto.getRandomValues(new Uint8Array(memory.buffer, p, n)); return 0; },
  });
  const made = async () => {
    const instance = await WebAssembly.instantiate(module, imports);
    memory = instance.exports.memory;
    instance.exports._initialize?.();
    return instance.exports;
  };
  let x = await made();
  const put = data => {
    const p = x.buffer(data.length || 1);
    new Uint8Array(memory.buffer, p, data.length).set(data);
    return p;
  };
  return async (bytes, lines) => {
    const text = textEncoder.encode(lines);
    try {
      const b = put(bytes), l = put(text);
      const p = x.run(b, bytes.length, l, text.length);
      x.release(b);
      x.release(l);
      const n = new DataView(memory.buffer).getUint32(p, true);
      const out = textDecoder.decode(new Uint8Array(memory.buffer, p + 4, n));
      x.release(p);
      return out.split("\n").slice(0, -1);
    } catch (e) {
      x = await made();
      return ["fault: the call ended the module: " + e.message];
    }
  };
};
