# cbor::encoder::finish

```cpp
auto finish()
    -> std::expected<std::conditional_t<pieces, ref<std::span<std::string_view const>>, std::size_t>, error>;
```

```call-seq
e.finish() → std::expected<std::size_t, error>
e.finish() → std::expected<ref<std::span<std::string_view const>>, error>
```

```example
bytes: 63 61 62 63
call: std::array<char, 16> buffer{};
result: 16 bytes: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
call: cbor::encoder e{std::span<char>(buffer)};
result: encoder over a std::span<char>
call: e.text_string("abc")
result: void
call: e.finish()
result: 4
call: buffer
result: 16 bytes: 63 61 62 63 00 00 00 00 00 00 00 00 00 00 00 00
```

Ends the encoder. It writes the buffer of the encoder to the target and
calls `done` of a writer. It gives the count of bytes written, or the
pieces over an [iov_writer](iov_writer.md). After the call, each call
of the encoder throws `std::logic_error`.

## Errors

`no_buffer_space`, `value_too_large`, `not_enough_memory`, `io_error`:
the error of the target. A second `finish` throws `std::logic_error`.

## Cost

O(bytes in the buffer of the encoder), at most 16 KiB.

## Example

```cpp
std::array<char, 16> buffer{};
cbor::encoder e{std::span<char>(buffer)};
e.text_string("abc");
e.finish();   // 4: buffer starts with 63 61 62 63
e.array(0);   // throws std::logic_error
```

## See also

[encoder](encoder.md) · [iov_writer](iov_writer.md)
