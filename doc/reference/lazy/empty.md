# cbor::lazy::empty

```cpp
std::expected<bool, error> empty() const;
```

```call-seq
doc->empty() → std::expected<bool, error>
```

```example
message: {"a": [], "b": [1]}
call: doc->at("a")->empty()
result: true
call: doc->at("b")->empty()
result: false
```

Gives `true` when [size](size.md) gives 0, and `false` when it gives
another count.

## Errors

The errors of [size](size.md).

## Cost

O(1). No allocation.

## Example

```cpp
// bytes: a std::string with {"a": [], "b": [1]}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
doc->at("a")->empty();   // true
doc->at("b")->empty();   // false
```

## See also

[size](size.md)
