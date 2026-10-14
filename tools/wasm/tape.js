const op = { null: 0, false: 1, true: 2, number: 3, string: 4, key: 5, array: 6, object: 7 };
const short_string_bytes = 64;

export function tape_values_built(memory, packed, numbers_at, shapes) {
  const count = Number(packed & 0xffffffffn);
  const tape = new Uint32Array(memory.buffer, Number(packed >> 32n), count * 3);
  const numbers = new Float64Array(memory.buffer, numbers_at);
  const u8 = new Uint8Array(memory.buffer);
  const decoder = new TextDecoder();
  const keys = new Map();
  const constructors = new Map();
  let i = 0;
  const string_read = (at, length) => {
    if (length < short_string_bytes) {
      let s = "";
      for (let k = at; k < at + length; k++) {
        const c = u8[k];
        if (c > 127) return decoder.decode(u8.subarray(at, at + length));
        s += String.fromCharCode(c);
      }
      return s;
    }
    return decoder.decode(u8.subarray(at, at + length));
  };
  const key_read = (at, length) => {
    let h = length;
    for (let k = at; k < at + length; k++) h = (h * 31 + u8[k]) | 0;
    const hit = keys.get(h);
    if (hit !== undefined && hit.length === length) {
      let same = true;
      for (let k = 0; k < length; k++) if (u8[hit.at + k] !== u8[at + k]) { same = false; break; }
      if (same) return hit.text;
    }
    const text = string_read(at, length);
    if (hit === undefined) keys.set(h, { at, length, text });
    return text;
  };
  const next = () => {
    const t = tape[i], a = tape[i + 1], b = tape[i + 2];
    i += 3;
    switch (t) {
      case op.object: {
        if (shapes) {
          const names = new Array(a), values = new Array(a);
          for (let k = 0; k < a; k++) { names[k] = key_read(tape[i + 1], tape[i + 2]); i += 3; values[k] = next(); }
          const shape = names.join("\u0000");
          let make = constructors.get(shape);
          if (make === undefined) {
            make = new Function("v", "return {" + names.map((n, k) => JSON.stringify(n) + ":v[" + k + "]").join(",") + "}");
            constructors.set(shape, make);
          }
          return make(values);
        }
        const o = {};
        for (let k = 0; k < a; k++) { const name = key_read(tape[i + 1], tape[i + 2]); i += 3; o[name] = next(); }
        return o;
      }
      case op.array: { const r = new Array(a); for (let k = 0; k < a; k++) r[k] = next(); return r; }
      case op.number: return numbers[a];
      case op.string: return string_read(a, b);
      case op.true: return true;
      case op.false: return false;
      default: return null;
    }
  };
  const out = [];
  while (i < count * 3) out.push(next());
  return out;
}
