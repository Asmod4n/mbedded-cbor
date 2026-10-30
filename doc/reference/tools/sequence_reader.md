# cbor::sequence_reader

```cpp
class sequence_reader
{
public:
    sequence_reader();
    sequence_reader(sequence_reader &&);
    sequence_reader &operator=(sequence_reader &&);

    template <borrowed_range_of_chars Piece>
    void feed(Piece piece);
    template <owning_range_of_chars Piece>
    void feed(Piece const &piece);
    template <owning_range_of_chars Piece>
    void feed(Piece &&piece);
    template <owning_range_of_chars Piece>
    void feed(std::shared_ptr<Piece const> piece);

    std::expected<lazy, error> next();
    bool empty() const;
};
```

```call-seq
cbor::sequence_reader r  → sequence_reader
r.feed(std::move(piece)) → void
r.next()                 → std::expected<lazy, error>
r.empty()                → bool
```

```example
bytes: 82 01 02 03
call: cbor::sequence_reader r
result: sequence_reader that holds no unread byte
call: auto first = bytes.substr(0, 2)
result: 2 bytes: 82 01
call: r.feed(std::move(first))
result: void
call: r.empty()
result: false
call: auto second = bytes.substr(2)
result: 2 bytes: 02 03
call: r.feed(std::move(second))
result: void
call: r.next()->size()
result: 2
call: r.next()->get<std::int64_t>()
result: 3
call: r.empty()
result: true
```

Reads a CBOR sequence (RFC 8742) whose bytes arrive in pieces, for
example from a socket. `feed` gives the reader the next piece. `next`
gives the next item as a `lazy` when all of its bytes are there, and
`too_little_data` while it waits. `empty` is true when the reader holds
no byte that no item took.

`feed(std::move(s))` takes a `std::string` without a copy.
`feed(shared_ptr)` holds the piece without a copy. `feed(view)` and
`feed(s)` copy the bytes. An item whole in a piece that the reader holds
is a view into that piece. The reader copies an item that the end of a
piece cuts, and only the bytes of that item.

Each item keeps its bytes alive after later feeds and after the reader
is gone. A char array, such as a literal, does not compile, because it
holds a NUL after its last byte. A `std::shared_ptr` to bytes that are
not const does not compile. The reader cannot be copied.

## Errors

- `too_little_data`: the item is not complete yet. Feed more bytes.
- `syntax_error`, `indefinite_length`: the bytes are not a well-formed
  item with definite lengths.
- `nesting_depth_exceeded`: the item is nested deeper than
  `limits::nesting_depth`.

After any other error than `too_little_data`, every call of `next` gives
that error, and `feed` keeps no byte. A null `std::shared_ptr` and a
call on a reader that was moved from throw `std::logic_error`.

## Cost

Each byte is scanned once. Memory O(n) of the bytes not yet read. A
feed and a `next` allocate nothing once the buffer has grown, except
`feed(std::move(s))`, which allocates the block that holds `s`.

## Example

```cpp
cbor::sequence_reader r;
r.feed(std::move(first));     // 82 01: the start of [1, 2]
r.next();                     // too_little_data
r.feed(std::move(second));    // 02 03
r.next()->size();             // 2
r.next()->get<std::int64_t>(); // 3
r.empty();                    // true
```

## See also

[sequence](sequence.md) · [../lazy/index.md](../lazy/index.md) · [../../runtime.md](../../runtime.md), section 7
