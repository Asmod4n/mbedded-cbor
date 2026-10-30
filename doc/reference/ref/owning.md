# cbor::owning<T>

```cpp
template <class T>
class owning {
public:
    T const &operator*() const &;
    void operator*() const && = delete;
    T const *operator->() const &;
    void operator->() const && = delete;
};
```

```call-seq
p.clone()  → owning<person>
*own       → person const &
own->name  → std::string_view
```

```example
message: {"name": "ann", "age": 30}
call: auto const p = cbor::structs<person>::decode(std::move(bytes)).value()
result: ref to person{"ann", 30}
call: auto const own = p.clone()
result: owning of person{"ann", 30}
call: own->name
result: "ann"
```

A value that holds its own copy of the bytes of each view in it.
[ref::clone](ref.md) makes it for a `T` that is not a single text or
byte view. The copy and the value share one `std::shared_ptr`, so a
view in it stays valid after the bytes of the `ref` end.

A copy of an `owning` shares the value and copies nothing. It has no
`==` and no public constructor. The value is `const`.

## Errors

None. `*` and `->` on a temporary `owning` do not compile.

## Cost

O(1) for `*`, `->` and copy. `ref::clone` is O(n): one `make_shared`,
and the buffer of the copied views.

## Example

```cpp
struct person {
    std::string_view name;
    int age;
};
// p: the cbor::ref<person> of {"name": "ann", "age": 30}
auto const own = p.clone();
use(own->name);   // "ann", valid after p ends
```

## See also

[ref](ref.md) · [../../basics.md](../../basics.md), section 2
