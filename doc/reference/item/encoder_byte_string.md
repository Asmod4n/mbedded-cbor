# cbor::encoder::byte_string

```cpp
std::expected<void, error> byte_string(std::span<std::byte const> const bytes);
template <class V>
    requires std::same_as<V, std::vector<std::byte>>
std::expected<void, error> byte_string(V &&bytes);
std::expected<void, error> byte_string(std::shared_ptr<std::vector<std::byte> const> const &bytes);
std::expected<void, error> byte_string(ref<std::span<std::byte const>> const &bytes);
```

```call-seq
e.byte_string(bytes) → std::expected<void, error>
```

```example
bytes: 42 61 62
call: std::string out;
result: ""
call: cbor::encoder e(out);
result: encoder over a std::string
call: e.byte_string(std::as_bytes(std::span("ab", 2)))
result: void
call: e.finish()
result: 3
call: out
result: "Bab"
call: cbor::decode(out)
result: owning item of h'6162'
```

Writes `bytes` as a byte string, with the shortest head for the length.
Over an [iov_writer](iov_writer.md), the last three forms do not copy 32
bytes or more: the result keeps the moved vector or the owner. Over any
other target, each form copies the bytes.

## Errors

`no_buffer_space` over a span that is too short. The error of a writer.
A null `std::shared_ptr` throws `std::logic_error`. A call after an
error or after `finish` throws `std::logic_error`.

## Cost

O(bytes). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.byte_string(std::as_bytes(std::span("ab", 2)));
e.finish();   // out holds 42 61 62
```

## See also

[text_string](encoder_text_string.md) · [encoder](encoder.md)
