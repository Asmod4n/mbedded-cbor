# cbor::lazy::at

```cpp
std::expected<lazy, error> at(std::string_view key) const;
std::expected<lazy, error> at(key k) const;
std::expected<lazy, error> at(std::size_t index) const;
```

```call-seq
doc->at("key")            → std::expected<lazy, error>
doc->at(cbor::key{7})      → std::expected<lazy, error>
doc->at(1)                 → std::expected<lazy, error>
```

```example
bytes: a2 64 63 61 72 73 82 a1 62 68 70 05 a1 62 68 70 07 07 65 73 65 76 65 6e
call: doc->at("cars")->at(1)->at("hp")->get<int>()
result: 7
call: doc->at(cbor::key{7})->get<std::string_view>()
result: ref to "seven"
call: doc->at("cars")->size()
result: 2
call: doc->at("cars")->at(0)->at("hp")->get<int>()
result: 5
```

Gives the lazy of one child. `at(key)` gives the value of the first pair
of a map whose key is the text `key`. `at(cbor::key{n})` gives the value
of the first pair whose key is the integer `n`; a bignum key that holds
`n` matches too. `at(index)` gives the element of an array or of a typed
array, counted from 0.

A text key does not match a byte string with the same bytes. The call
passes a tag that it does not interpret, and reads into a tag 24. It
reads no value; [get](get.md) does.

## Errors

- `key_not_found`: no pair has the key.
- `index_out_of_bounds`: `index` is not less than the count.
- `not_indexable`: a key on a value that is not a map, or an index on a
  value that is not an array.
- `too_little_data`, `syntax_error`, `indefinite_length`: the bytes up to
  the child are broken.

## Cost

A key: O(bytes up to the pair), at most O(bytes of the map). An index:
O(bytes before the element); O(1) in a typed array. No allocation.

## Example

```cpp
// bytes: a std::string with {"cars": [{"hp": 5}, {"hp": 7}], 7: "seven"}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
doc->at("cars")->at(1)->at("hp")->get<int>();   // 7
doc->at(cbor::key{7})->get<std::string_view>(); // a ref to "seven"
doc->at("bus").error();                         // key_not_found
doc->at(0).error();                             // not_indexable
```

## See also

[operator[]](operator_subscript.md) · [at_path](at_path.md) · [get](get.md) · [entries](entries.md)
