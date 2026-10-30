# cbor::encoder

```cpp
template <class Target>
class encoder
{
public:
    explicit encoder(Target &target)
        requires(!pieces && !requires { typename Target::element_type; });
    explicit encoder(Target const target)
        requires requires { typename Target::element_type; };
    explicit encoder(Target &target)
        requires pieces;
    encoder(encoder const &) = delete;
    encoder &operator=(encoder const &) = delete;
};

template <class Target>
encoder(Target &) -> encoder<Target>;
template <class Element>
encoder(std::span<Element>) -> encoder<std::span<Element>>;
```

```call-seq
cbor::encoder e(out)                     → encoder<std::string>
cbor::encoder e{std::span<char>(buffer)} → encoder<std::span<char>>
cbor::encoder e(w)                       → encoder<iov_writer>
```

```example
bytes: a1 61 61 18 05
call: std::string out;
result: ""
call: cbor::encoder e(out);
result: encoder over a std::string
call: e.map(1)
result: void
call: e.text_string("a")
result: void
call: e.unsigned_integer(std::uint8_t{5})
result: void
call: e.finish()
result: 5
call: out
result: 5 bytes: a1 61 61 18 05
```

Writes CBOR one data item per call, in the order of the bytes. The
encoder counts nothing: the counts of [array](encoder_array.md) and
[map](encoder_map.md) come from the program. The target is one of
these:

- a container of bytes, for example `std::string`: the encoder appends;
- a `std::span` of bytes: the encoder writes into it and allocates
  nothing;
- an [iov_writer](iov_writer.md): the encoder gives pieces;
- an object with `allocate(std::size_t)`, which gives a writer with
  `append(std::string_view)` and `done(std::size_t)`.

The encoder does not write tags 28 and 29.

## Errors

Each call gives `std::expected<void, error>`. After an error, and after
[finish](encoder_finish.md), each call throws `std::logic_error`. A copy
does not compile.

## Cost

O(1) for each head, O(bytes) for each string. Over a `std::span<char>`
the encoder allocates nothing. Over a container, a writer or a span of
`std::byte`, it holds a buffer of 16 KiB. Over an `iov_writer`, it takes
one block of 16 KiB at once.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.map(1);
e.text_string("a");
e.unsigned_integer(std::uint8_t{5});
e.finish();   // 5: out holds {"a": 5}, a1 61 61 18 05
```

## See also

[finish](encoder_finish.md) · [encode](encode.md) · [../../runtime.md](../../runtime.md), section 6
