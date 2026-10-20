# cbor::lazy::take_ownership

```cpp
template <owning_range_of_chars Encoded>
static cbor::result<lazy> take_ownership(Encoded &&encoded);
```

Makes the lazy of the top-level item in `encoded`. The bytes move into a
new owner; nothing is copied. Every lazy, view and `ref` that comes from
the result holds that owner, so the bytes stay alive as long as one of
them lives.

`Encoded` is a container of `char` that owns its bytes, for example
`std::string`. An lvalue, a `std::string_view` and a literal do not
compile.

## Errors

None from the bytes. The call reads only the first head, to skip one
tag 55799. A step on the lazy reports broken input ([at](at.md),
[get](get.md)).

## Cost

O(1). One allocation: the owner, which holds the moved container.

## Example

```cpp
// bytes: a std::string with {"id": 42}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
if (doc)
    use(doc->at("id")->get<std::int64_t>());   // 42
```

## See also

[view](view.md) · [at](at.md) · [../../basics.md](../../basics.md), section 2
