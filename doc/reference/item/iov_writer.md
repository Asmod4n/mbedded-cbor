# cbor::iov_writer

```cpp
class iov_writer;
```

```call-seq
cbor::iov_writer w; → iov_writer
```

```output
call: cbor::iov_writer w; cbor::encoder e(w);
call: e.text_string(text), with text a std::shared_ptr<std::string const> of 40 times "x"
call: auto const pieces = e.finish()
(*pieces)->size(): 2, the head 78 28 and *text with no copy
sent with writev: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

A target of [encoder](encoder.md) that collects pieces, not one buffer.
The encoder writes heads and short strings into blocks of 16 KiB. A text
or a byte string of 32 bytes or more that the program hands over with
its owner becomes a piece of its own, with no copy.
[finish](encoder_finish.md) gives the pieces as `struct iovec`, in a `ref`
that holds the blocks and the owners. The pieces are for sending only, as
[encode_iov](encode_iov.md) says. The class exists where `<sys/uio.h>`
exists.

The class has no public member function.

## Errors

A write after `finish` throws `std::logic_error`.

## Cost

The storage and its lists allocate. Each block of 16 KiB is one
allocation. A piece of its own adds one `std::shared_ptr` copy.

## Example

```cpp
// text: a std::shared_ptr<std::string const> with 40 times "x"
cbor::iov_writer w;
cbor::encoder e(w);
e.text_string(text);
auto const pieces = e.finish();
(*pieces)->size();   // 2: the head 78 28, then *text with no copy
// sent with writev: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
```

## See also

[encode_iov](encode_iov.md) · [encoder](encoder.md)
