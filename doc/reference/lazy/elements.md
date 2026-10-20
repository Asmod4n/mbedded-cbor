# cbor::lazy::elements

```cpp
cbor::result<lazy_elements> elements() const;
```

Gives the elements of an array, or of a typed array, as a view. Each
element of the view is a `cbor::result<lazy>`. The call reads
only the head. The view reads one element at each step.

## Errors

- `not_indexable`: the value is not an array or a typed array.
- `indefinite_length`: the array has an indefinite length.
- `too_little_data`: the count is larger than the bytes that are left.

## Cost

O(1). No allocation. A full loop is O(bytes of the array).

## Example

```cpp
// bytes: a std::string with [1, 2, 3]
auto const array = cbor::lazy::take_ownership(std::move(bytes));
int sum = 0;
for (auto const element : *array->elements())
    if (element)
        sum += element->get<int>().value_or(0);
use(sum);   // 6
```

## See also

[lazy_elements](lazy_elements.md) · [entries](entries.md) · [size](size.md)
