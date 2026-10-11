# Compared with other libraries

This page compares mbedded-cbor with four C++ libraries that read and
write CBOR or a binary format of their own: nlohmann/json, libcbor,
FlatBuffers and Cap'n Proto. Each code example was compiled and run.

| Task | mbedded-cbor | nlohmann/json | libcbor | FlatBuffers | Cap'n Proto |
|---|---|---|---|---|---|
| Format | CBOR | JSON; CBOR through `from_cbor` and `to_cbor` | CBOR | its own | its own |
| Read one field | a path; the rest is not decoded | decodes the whole input first | decodes the whole input; no lookup of a key | direct | direct |
| Write a struct | no extra code (C++26 reflection) | a macro that names each member | no mapping of a struct | a schema file and a code generator | a schema file and a code generator |
| Shared values, tags 28 and 29 | read and written | not read | not read | one offset in two places | not checked |
| A list of numbers with no copy | `cbor::packed` | copies | reads each number | direct | direct |
| Broken input | a `std::expected` error | an exception, or a discarded value | an error code | the Verifier; without it, no check | an exception |

## Read one field

```cpp
// mbedded-cbor; bytes: {"user": {"id": 42}}
auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);   // *id == 42
```

```cpp
// nlohmann/json
nlohmann::json const doc = nlohmann::json::from_cbor(bytes);
int const id = doc.at("user").at("id").get<int>();
```

## Write a struct and read it back

```cpp
// mbedded-cbor
struct person {
    std::string name;
    int age;
};

auto const bytes = cbor::structs<person>::encode(person{"ann", 30});
auto const back = cbor::structs<person>::decode(*bytes);
// (*back)->name is "ann", (*back)->age is 30
```

```cpp
// nlohmann/json
struct person {
    std::string name;
    int age;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(person, name, age)

std::vector<std::uint8_t> const bytes = nlohmann::json::to_cbor(person{"ann", 30});
person const back = nlohmann::json::from_cbor(bytes).get<person>();
```

`encode` and `decode` return a `std::expected`, and `*` takes the value
([structs.md](structs.md)).

## Broken input

```cpp
// mbedded-cbor; bytes: [1, 2] without its last byte
auto const r = cbor::at_path<"$[1]", std::int64_t>(bytes);
bool const broken = !r && r.error() == cbor::error::too_little_data;
```

```cpp
// nlohmann/json
auto const doc = nlohmann::json::from_cbor(bytes, true, false);
bool const broken = doc.is_discarded();
```

More: the code of every library for six tasks, the lines of code of
each, and a measurement are in
[advanced/comparison.md](advanced/comparison.md).
