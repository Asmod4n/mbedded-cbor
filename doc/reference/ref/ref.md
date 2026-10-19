# cbor::ref<T>

```cpp
template <class T>
class ref {
public:
    ref(ref const &) = default;
    ref &operator=(ref const &) = default;
    ref(ref &&o) noexcept(std::is_nothrow_move_constructible_v<T> &&
                          std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>;
    ref &operator=(ref &&o) noexcept(std::is_nothrow_move_assignable_v<T> &&
                                     std::is_nothrow_default_constructible_v<T>)
        requires std::is_default_constructible_v<T>;

    std::conditional_t<is_view<T>, T, T const &> operator*() const &;
    void operator*() const && = delete;
    T const *operator->() const &;
    void operator->() const && = delete;

    std::string clone() const requires std::same_as<T, std::string_view>;
    std::vector<std::byte> clone() const requires std::same_as<T, std::span<std::byte const>>;
    owning<T> clone() const
        requires(!std::same_as<T, std::string_view> && !std::same_as<T, std::span<std::byte const>>);

    friend bool operator==(ref const &a, ref const &b) requires std::equality_comparable<T>;
    friend bool operator==(ref const &a, T const &b) requires std::equality_comparable<T>;
};
```

A value and a `std::shared_ptr` to the owner of the bytes that it views.
`*r` gives a view by value and any other `T` as `T const &`. `r->`
reaches the members. `==` compares the values. A copy shares the owner
and copies no bytes. A move leaves `T{}` and no owner behind.

A view in the value stays valid while one `ref` with that owner lives.
A view copied out of the `ref` does not keep the bytes. `clone` copies
the bytes: into a `std::string`, a byte vector, or an
[owning](owning.md). Only the library makes a `ref`.

## Errors

None. `*` and `->` on a temporary `ref` do not compile.

## Cost

O(1) for `*`, `->`, copy and move; a copy changes an atomic count.
`clone` is O(n) and allocates.

## Example

```cpp
// bytes: a std::string with "abc"
auto const r = cbor::structs<std::string_view>::decode(std::move(bytes));
if (r) {
    std::string_view const v = **r;   // "abc", valid while *r lives
    *r == "abc";                       // true
}
```

## See also

[owning](owning.md) · [../structs/decode.md](../structs/decode.md) · [../../basics.md](../../basics.md), section 2
