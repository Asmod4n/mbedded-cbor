# cbor::lazy::type

```cpp
cbor::result<cbor::type> type() const;
```

Gives the kind of the value at the lazy. A tag 2 or 3 whose magnitude
fits 64 bits gives `unsigned_integer` or `negative_integer`. A larger
bignum gives `unsigned_bignum` or `negative_bignum`. A typed array
(RFC 8746) gives `typed_array`. Every other tag gives `tag`; the call
does not pass it.

## Errors

`too_little_data` or `syntax_error`: the head is broken.

## Cost

O(1). O(bytes of the magnitude) for a bignum. No allocation.

## Example

```cpp
// bytes: a std::string with [1, "a", 2(h'05'), 1(0)]
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
doc->at(0)->type();   // cbor::type::unsigned_integer
doc->at(1)->type();   // cbor::type::text_string
doc->at(2)->type();   // cbor::type::unsigned_integer
doc->at(3)->type();   // cbor::type::tag
```

## See also

[get](get.md) · [clone](clone.md)
