const typed_array_types = {
  64: Uint8Array, 68: Uint8ClampedArray, 69: Uint16Array, 70: Uint32Array, 71: BigUint64Array,
  72: Int8Array, 77: Int16Array, 78: Int32Array, 79: BigInt64Array, 85: Float32Array, 86: Float64Array,
};
const decoder = new TextDecoder();
const encoder = new TextEncoder();

export function leaf_reader(x) {
  const call = (f, path) => {
    const t = encoder.encode(path);
    const p = x.alloc(t.length);
    new Uint8Array(x.memory.buffer, p, t.length).set(t);
    const r = f(p, t.length);
    x.release(p);
    const e = x.last_error();
    if (e !== 0) throw new Error("cbor error " + e);
    return r;
  };
  const at = (r) => Number(r >> 32n);
  const size = (r) => Number(r & 0xffffffffn);
  return {
    number: (path) => call(x.path_double, path),
    integer: (path) => call(x.path_int, path),
    string: (path) => { const r = call(x.path_string, path); return decoder.decode(new Uint8Array(x.memory.buffer, at(r), size(r))); },
    bytes: (path) => { const r = call(x.path_bytes, path); return new Uint8Array(x.memory.buffer, at(r), size(r)); },
    typed_array: (path) => {
      const r = call(x.path_typed_array, path);
      const type = typed_array_types[x.typed_array_tag()];
      if (type === undefined) throw new Error("typed array tag " + x.typed_array_tag() + " has no JS view");
      const n = size(r) / type.BYTES_PER_ELEMENT;
      if (at(r) % type.BYTES_PER_ELEMENT === 0) return new type(x.memory.buffer, at(r), n);
      return new type(x.memory.buffer.slice(at(r), at(r) + size(r)));
    },
    numbers: (path) => { const r = call(x.path_doubles, path); return new Float64Array(x.memory.buffer, at(r), size(r)); },
  };
}
