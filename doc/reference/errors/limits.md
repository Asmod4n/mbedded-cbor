# cbor::limits

```cpp
#ifndef CBOR_STRING_LENGTH_DEFAULT
#define CBOR_STRING_LENGTH_DEFAULT SIZE_MAX
#endif

#ifndef CBOR_CONTAINER_ELEMENTS_DEFAULT
#define CBOR_CONTAINER_ELEMENTS_DEFAULT SIZE_MAX
#endif

struct limits {
    static constexpr std::size_t string_length = CBOR_STRING_LENGTH_DEFAULT;
    static constexpr std::size_t container_elements = CBOR_CONTAINER_ELEMENTS_DEFAULT;
};
```

```call-seq
cbor::limits::string_length      → std::size_t
cbor::limits::container_elements → std::size_t
```

```example
message: {}
call: cbor::limits::string_length
result: 4294967295
call: cbor::limits::container_elements
result: 4294967295
```

The limits of the build, in `<cbor/validity.hpp>`. A macro on the
command line sets a limit; a macro that the build does not set gets its
default. No call reads a limit at run time. A limit at `SIZE_MAX` is not
set, and its check is not in the program.

| Constant | Counts | Default | Error |
|---|---|---|---|
| `string_length` | the bytes of one text or byte string | `SIZE_MAX` | `string_length_exceeded` |
| `container_elements` | the elements of an array, or the pairs of a map | `SIZE_MAX` | `container_elements_exceeded` |

A limit counts what a call reads or decodes. An item that a call only
skips does not count. Every translation unit of one program uses the same
values.

Nesting has no fixed limit. A reader that recurses stops where the
stack of its thread ends: a call that comes within 16 KiB of the end
(32 KiB on Windows or with a sanitizer) returns `nesting_depth_exceeded`.
Each thread reads the bounds of its own stack once, at its first check:
from the system on Linux (glibc and musl), macOS and Windows, and from
the linker on WebAssembly without threads and on picolibc. Another
target does not compile. A frame outside these bounds, such as the
stack of a fiber or of a signal handler, is not checked. The first check
of a thread does not run in a signal handler. On Linux, a change of
`RLIMIT_STACK` after the first check, and a mapping that a program places
with `MAP_FIXED` below the stack of the main thread, are outside the
bound. On picolibc, a stack smaller than the reserve refuses every
nested item. `item_size`, `sequence_reader` and a path of child segments
do not recurse and read any depth. A deep value of a user type
(`structs`, `schema`) is destroyed and copied by its own recursion: a
thread with a smaller stack than the one that decoded it can overflow
there.

## Cost

None at run time for a limit. The stack check reads the bounds once
per thread and then compares the frame address with one limit at each
level.

## Example

```sh
g++ -DCBOR_STRING_LENGTH_DEFAULT=1024 ...
```

```cpp
// a build that sets no macro
cbor::limits::string_length;        // SIZE_MAX
cbor::limits::container_elements;   // SIZE_MAX
```

## See also

[error](error.md) · [../../basics.md](../../basics.md), section 4 · [../../advanced/basics.md](../../advanced/basics.md), sections 4 and 5
