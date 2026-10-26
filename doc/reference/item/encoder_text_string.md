# cbor::encoder::text_string

```cpp
std::expected<void, error> text_string(std::string_view const text);
template <class S>
    requires std::same_as<S, std::string>
std::expected<void, error> text_string(S &&text);
std::expected<void, error> text_string(std::shared_ptr<std::string const> const &text);
std::expected<void, error> text_string(ref<std::string_view> const &text);
```

Writes `text` as a text string, with the shortest head for the length.
The call does not check UTF-8. Over an [iov_writer](iov_writer.md), the
last three forms do not copy a text of 32 bytes or more: the result
keeps the moved string or the owner. Over any other target, each form
copies the text.

## Errors

`no_buffer_space` over a span that is too short. The error of a writer.
A null `std::shared_ptr` throws `std::logic_error`. A call after an
error or after `finish` throws `std::logic_error`.

## Cost

O(bytes of the text). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.text_string("abc");
e.finish();   // out holds 63 61 62 63
```

## See also

[byte_string](encoder_byte_string.md) · [iov_writer](iov_writer.md)
