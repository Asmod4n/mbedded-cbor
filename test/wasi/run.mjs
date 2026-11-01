// Runs a WASI program under node:wasi with its path as argv[0], which doctest reads, and the root directory
// opened, so that a test reads its test vectors by their absolute path.
import { readFile } from "node:fs/promises";
import { WASI } from "node:wasi";
const [program, ...args] = process.argv.slice(2);
const wasi = new WASI({ version: "preview1", args: [program, ...args], env: {}, preopens: { "/": "/" } });
const module = await WebAssembly.compile(await readFile(program));
process.exitCode = wasi.start(await WebAssembly.instantiate(module, wasi.getImportObject()));
