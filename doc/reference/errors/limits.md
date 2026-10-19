# cbor::limits

```cpp
#ifndef CBOR_NESTING_DEPTH_DEFAULT
#define CBOR_NESTING_DEPTH_DEFAULT 128
#endif

#ifndef CBOR_STRING_LENGTH_DEFAULT
#define CBOR_STRING_LENGTH_DEFAULT SIZE_MAX
#endif

#ifndef CBOR_CONTAINER_ELEMENTS_DEFAULT
#define CBOR_CONTAINER_ELEMENTS_DEFAULT SIZE_MAX
#endif

struct limits {
    static constexpr std::size_t nesting_depth = CBOR_NESTING_DEPTH_DEFAULT;
    static constexpr std::size_t string_length = CBOR_STRING_LENGTH_DEFAULT;
    static constexpr std::size_t container_elements = CBOR_CONTAINER_ELEMENTS_DEFAULT;
};
```

The limits of the build, in `<cbor/validity.hpp>`. A macro on the
command line sets a limit; a macro that the build does not set gets its
default. No call reads a limit at run time. A limit at `SIZE_MAX` is not
set, and its check is not in the program.

| Constant | Counts | Default | Error |
|---|---|---|---|
| `nesting_depth` | the arrays, maps and tags around an item | 128, at most 512 | `nesting_depth_exceeded` |
| `string_length` | the bytes of one text or byte string | `SIZE_MAX` | `string_length_exceeded` |
| `container_elements` | the elements of an array, or the pairs of a map | `SIZE_MAX` | `container_elements_exceeded` |

A limit counts what a call reads or decodes. An item that a call only
skips does not count. Every translation unit of one program uses the same
values.

## Errors

A `CBOR_NESTING_DEPTH_DEFAULT` above 512 does not compile.

## Cost

None at run time.

## Example

```sh
g++ -DCBOR_NESTING_DEPTH_DEFAULT=16 -DCBOR_STRING_LENGTH_DEFAULT=1024 ...
```

```cpp
// a build that sets no macro
cbor::limits::nesting_depth;        // 128
cbor::limits::string_length;        // SIZE_MAX
cbor::limits::container_elements;   // SIZE_MAX
```

## See also

[error](error.md) · [../../basics.md](../../basics.md), section 4 · [../../advanced/basics.md](../../advanced/basics.md), sections 4 and 5
