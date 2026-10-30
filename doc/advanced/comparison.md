# Compared with other libraries, in detail

This page adds the details to [../comparison.md](../comparison.md). It
shows six tasks in three kinds of code:

1. normal C++ on data that is already in memory;
2. mbedded-cbor;
3. other C++ libraries that have the feature.

Every snippet of kinds 1 and 3 was compiled with g++-16 -std=c++23
-Wall -Wextra -Werror and run in the podman image
mbedded-cbor-tw-compare. Every snippet of mbedded-cbor is a test case of
the library. A comment shows the input in `bytes` as text.

## 1. The libraries

The image mbedded-cbor-tw-compare holds these packages (`rpm -qa`):

| Library | Package | In the image |
|---|---|---|
| nlohmann/json | nlohmann_json-devel 3.12.0 | yes |
| libcbor | libcbor-devel 0.14.0 | yes |
| FlatBuffers | flatbuffers-devel 25.2.10 | yes |
| Cap'n Proto | libcapnp-devel 1.5.0 | yes |
| jsoncons | none | not in the image |
| TinyCBOR | none | not in the image |
| QCBOR | none | not in the image |

A library that is not in the image has no code in this document.
fxamacker/cbor (Go), ciborium (Rust), cbor2 (Python) and CBOR::XS (Perl)
are not C++, and this document does not compare them.

nlohmann/json reads CBOR with `from_cbor` into a `nlohmann::json` tree.
libcbor reads CBOR into a tree of `cbor_item_t`. FlatBuffers and
Cap'n Proto are not CBOR. They read their own wire format, and they need
a schema file and a code generator. The schema files of this document:

```
namespace fb;
table User { id:int; }
table Root { user:User; }
table Person { name:string; age:int; }
table Ints { values:[int]; }
table Names { values:[string]; }
table Doubles { values:[double]; }
```

```
@0xd1c2b3a4f5e6d7c8;
struct User { id @0 :Int32; }
struct Root { user @0 :User; }
struct Person { name @0 :Text; age @1 :Int32; }
struct Ints { values @0 :List(Int32); }
struct Doubles { values @0 :List(Float64); }
```

## 2. Read one field by path

The input is {"user": {"id": 42}}. The result is 42.

### C++ in memory

```cpp
std::map<std::string, std::map<std::string, int>> const doc{{"user", {{"id", 42}}}};
int const id = doc.at("user").at("id");
```

### mbedded-cbor

at_path reads the heads on the way to the field and decodes nothing else. The result is a std::expected.

```cpp
// bytes: {"user": {"id": 42}}
std::expected<std::int64_t, cbor::error> const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
```

### nlohmann/json

from_cbor decodes the whole input into a tree. at() throws std::out_of_range for a key that is not there.

```cpp
nlohmann::json const doc = nlohmann::json::from_cbor(bytes);
int const id = doc.at("user").at("id").get<int>();
// alternative: doc.at(nlohmann::json::json_pointer("/user/id"))
```

### libcbor

libcbor has no lookup of a key. The caller writes it.

```cpp
static cbor_item_t *map_get(cbor_item_t *map, std::string_view key)
{
    cbor_pair const *pairs = cbor_map_handle(map);
    for (std::size_t i = 0; i < cbor_map_size(map); i++) {
        cbor_item_t *k = pairs[i].key;
        if (cbor_isa_string(k) && cbor_string_length(k) == key.size() &&
            std::memcmp(cbor_string_handle(k), key.data(), key.size()) == 0)
            return pairs[i].value;
    }
    return nullptr;
}

cbor_load_result result;
cbor_item_t *doc = cbor_load(bytes, sizeof bytes, &result);
cbor_item_t *user = map_get(doc, "user");
cbor_item_t *id = user ? map_get(user, "id") : nullptr;
std::uint64_t const value = id ? cbor_get_int(id) : 0;
cbor_decref(&doc);
```

### FlatBuffers

```cpp
int const id = flatbuffers::GetRoot<fb::Root>(buf)->user()->id();
```

### Cap'n Proto

```cpp
capnp::FlatArrayMessageReader reader(words);
int const id = reader.getRoot<Root>().getUser().getId();
```

## 3. Read and write a struct

The struct holds a name and an age. The code writes it and reads it back.

### C++ in memory

```cpp
struct person {
    std::string name;
    int age;
};
person const p{"ann", 30};
person const copy = p;
```

### mbedded-cbor

structs reads the members of an aggregate by C++26 reflection on g++ 16, and through Boost.PFR on every other compiler. The struct needs no macro and no tag.

```cpp
struct person {
    std::string name;
    int age;
};

auto bytes = cbor::structs<person>::encode(person{"ann", 30});
auto const back = cbor::structs<person>::decode(std::move(*bytes));
// (*back)->name is "ann", (*back)->age is 30
```

### nlohmann/json

A macro names each member once more.

```cpp
struct person {
    std::string name;
    int age;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(person, name, age)

std::vector<std::uint8_t> const bytes = nlohmann::json::to_cbor(person{"ann", 30});
person const back = nlohmann::json::from_cbor(bytes).get<person>();
```

### libcbor

libcbor is a C library. Its headers in /usr/include/cbor declare functions on `cbor_item_t` and none on a type of the caller, so it has no mapping of a struct. This document shows no code for it.

### FlatBuffers

The struct is the table Person of the schema file. flatc writes its C++ type.

```cpp
flatbuffers::FlatBufferBuilder b;
b.Finish(fb::CreatePerson(b, b.CreateString("ann"), 30));
auto const *back = flatbuffers::GetRoot<fb::Person>(b.GetBufferPointer());
std::string const name = back->name()->str();
int const age = back->age();
```

### Cap'n Proto

The struct is Person of the schema file. capnp compile writes its C++ type.

```cpp
capnp::MallocMessageBuilder m;
auto p = m.initRoot<Person>();
p.setName("ann");
p.setAge(30);
auto const words = capnp::messageToFlatArray(m);
capnp::FlatArrayMessageReader reader(words);
auto const back = reader.getRoot<Person>();
std::string const name = back.getName().cStr();
int const age = back.getAge();
```

## 4. Iterate an array

The input is [1, 2, 3]. The result is the sum 6.

### C++ in memory

```cpp
std::vector<int> const v{1, 2, 3};
int sum = 0;
for (int const x : v)
    sum += x;
```

### mbedded-cbor

elements() gives a forward range. Each element is a std::expected<cbor::lazy, cbor::error>.

```cpp
auto const doc = cbor::lazy::take_ownership(std::move(bytes)); // [1, 2, 3]
std::int64_t sum = 0;
for (auto const e : *doc->elements())
    sum += e->get<std::int64_t>().value_or(0);
```

### nlohmann/json

```cpp
int sum = 0;
for (auto const &x : nlohmann::json::from_cbor(bytes))
    sum += x.get<int>();
```

### libcbor

```cpp
cbor_load_result result;
cbor_item_t *doc = cbor_load(bytes, sizeof bytes, &result);
std::uint64_t sum = 0;
for (std::size_t i = 0; i < cbor_array_size(doc); i++)
    sum += cbor_get_int(cbor_array_handle(doc)[i]);
cbor_decref(&doc);
```

### FlatBuffers

```cpp
int sum = 0;
for (int const x : *flatbuffers::GetRoot<fb::Ints>(buf)->values())
    sum += x;
```

### Cap'n Proto

```cpp
capnp::FlatArrayMessageReader reader(words);
int sum = 0;
for (int const x : reader.getRoot<Ints>().getValues())
    sum += x;
```

## 5. Shared values

A shared value is one value that two places of a document hold. CBOR
writes it with tag 28 (mark the value) and tag 29 (a reference to the
n-th marked value). The input is [28("x"), 29(0)], and both elements are
"x".

### C++ in memory

```cpp
auto const x = std::make_shared<std::string const>("x");
std::vector<std::shared_ptr<std::string const>> const v{x, x};
bool const same = v[0] == v[1];
```

### mbedded-cbor

Tags 28 and 29 are plain tags in a lazy, in a path and in a tree decode. Only `cbor::structs` decode resolves them. No writer of the library adds them by itself. `encode` of an item writes a tag 28 or 29 that the item holds, and a program writes them with `encoder::tag` ([runtime.md](runtime.md)).

```cpp
// owner: a std::shared_ptr<std::string const> with [28("x"), 29(0)]
auto const second = cbor::lazy::view(owner)->at(1);
// second->type() is cbor::type::tag, and second->get<std::uint64_t>() is 0

auto const v = cbor::structs<std::vector<std::string>>::decode(owner);
// v.error() is cbor::error::incorrect_type
```

### nlohmann/json

nlohmann/json has no shared values. from_cbor with cbor_tag_handler_t::ignore drops the tags, and 29(0) becomes the number 0. The default handler gives a parse_error for a tag.

```cpp
std::vector<std::uint8_t> const bytes{0x82, 0xd8, 0x1c, 0x61, 'x', 0xd8, 0x1d, 0x00}; // [28("x"), 29(0)]
auto const doc = nlohmann::json::from_cbor(bytes, true, true, nlohmann::json::cbor_tag_handler_t::ignore);
// doc is ["x", 0]: the tags are dropped, 29(0) stays the number 0
```

### libcbor

libcbor has no shared values. It gives tag 29 as a tag item around the number 0.

```cpp
std::uint8_t const bytes[] = {0x82, 0xd8, 0x1c, 0x61, 'x', 0xd8, 0x1d, 0x00}; // [28("x"), 29(0)]
cbor_load_result result;
cbor_item_t *doc = cbor_load(bytes, sizeof bytes, &result);
cbor_item_t *second = cbor_array_handle(doc)[1];
bool const is_tag = cbor_isa_tag(second) && cbor_tag_value(second) == 29;
// the second element stays tag 29 around the number 0
cbor_decref(&doc);
```

### FlatBuffers

FlatBuffers is not CBOR and has no tag 28. A builder can store one offset in two places, and both places read the same bytes.

```cpp
flatbuffers::FlatBufferBuilder b;
auto const x = b.CreateString("x");
b.Finish(fb::CreateNames(b, b.CreateVector(std::vector{x, x})));
auto const *v = flatbuffers::GetRoot<fb::Names>(b.GetBufferPointer())->values();
bool const same = v->Get(0) == v->Get(1);
```

### Cap'n Proto

This document has no snippet for Cap'n Proto. Whether Cap'n Proto can point two places at one value was not checked.

## 6. A number array

The input is the numbers 1.5, 2.5 and 3.5 as binary64. The result is the sum 7.5.

### C++ in memory

```cpp
std::vector<double> const store{1.5, 2.5, 3.5};
std::span<double const> const values = store;
double const sum = std::accumulate(values.begin(), values.end(), 0.0);
```

### mbedded-cbor

cbor::schema reads the struct from its description, which CBOR_DEFINE_TYPE writes. It writes a std::vector<double> as an RFC 8746 typed array. view gives a typed_array_view, a random access range over the bytes of the input, with no copy.

```cpp
struct series {
    std::vector<double> values;
};

CBOR_DEFINE_TYPE(series, cbor::tag{1900}, CBOR_MEMBER(series, values));

auto const root = cbor::schema<series>::path(std::move(bytes));
auto const values = root->view<"$.values">();
double const sum = std::accumulate(values->begin(), values->end(), 0.0);
```

### nlohmann/json

get<std::vector<double>>() copies the numbers out of the tree.

```cpp
auto const values = nlohmann::json::from_cbor(bytes).get<std::vector<double>>();
double const sum = std::accumulate(values.begin(), values.end(), 0.0);
```

### libcbor

The input of this snippet holds the numbers as binary16, because they fit. cbor_float_get_float reads each width.

```cpp
cbor_load_result result;
cbor_item_t *doc = cbor_load(bytes, sizeof bytes, &result);
double sum = 0;
for (std::size_t i = 0; i < cbor_array_size(doc); i++)
    sum += cbor_float_get_float(cbor_array_handle(doc)[i]);
cbor_decref(&doc);
```

### FlatBuffers

```cpp
auto const *values = flatbuffers::GetRoot<fb::Doubles>(buf)->values();
double const sum = std::accumulate(values->begin(), values->end(), 0.0);
```

### Cap'n Proto

```cpp
capnp::FlatArrayMessageReader reader(words);
auto const values = reader.getRoot<Doubles>().getValues();
double const sum = std::accumulate(values.begin(), values.end(), 0.0);
```

## 7. Broken input and errors

The input stops before its end. Each snippet tells that the input is broken.

### C++ in memory

Data in memory is not parsed. The snippet shows the standard form of an error: a std::expected.

```cpp
auto const parse = [](std::string const &s) -> std::expected<int, std::errc> {
    if (s.empty())
        return std::unexpected(std::errc::invalid_argument);
    return std::stoi(s);
};
auto const r = parse("");
```

### mbedded-cbor

Each read gives a std::expected<T, cbor::error>. A wrong use that the compiler cannot see throws std::logic_error ([basics.md](basics.md)).

```cpp
// bytes: [1, 2] without its last byte
auto const r = cbor::at_path<"$[1]", std::int64_t>(bytes);
bool const broken = !r && r.error() == cbor::error::too_little_data;
```

### nlohmann/json

```cpp
std::vector<std::uint8_t> const cut{0x82, 0x01};
auto const doc = nlohmann::json::from_cbor(cut, true, false);
bool const broken = doc.is_discarded();
// with allow_exceptions true: nlohmann::json::parse_error
```

### libcbor

```cpp
std::uint8_t const cut[] = {0x82, 0x01};
cbor_load_result result;
cbor_item_t *doc = cbor_load(cut, sizeof cut, &result);
bool const broken = doc == nullptr && result.error.code == CBOR_ERR_NOTENOUGHDATA;
```

### FlatBuffers

`whole` holds a complete Person buffer.

```cpp
std::vector<std::uint8_t> const cut(whole.begin(), whole.end() - 8);
flatbuffers::Verifier verifier(cut.data(), cut.size());
bool const broken = !verifier.VerifyBuffer<fb::Person>(nullptr);
// without the Verifier, GetRoot reads the cut buffer with no check
```

### Cap'n Proto

`whole` holds a complete Person message. A read of a broken message throws kj::Exception.

```cpp
auto const cut = whole.asPtr().slice(0, whole.size() - 1);
bool broken = false;
try {
    capnp::FlatArrayMessageReader reader(cut);
    (void)reader.getRoot<Person>().getName();
} catch (kj::Exception const &) {
    broken = true;
}
```

## 8. Lines of code

The count is the lines of each snippet above. A blank line and a line
that holds only a comment do not count. A declaration that the task
needs (a struct, a macro, a helper) counts in the task. A schema file
does not count.

| Task | Library | Lines |
|---|---|---|
| path | C++ in memory | 2 |
| path | mbedded-cbor | 1 |
| path | nlohmann/json | 2 |
| path | libcbor | 17 |
| path | FlatBuffers | 1 |
| path | Cap'n Proto | 2 |
| struct | C++ in memory | 6 |
| struct | mbedded-cbor | 6 |
| struct | nlohmann/json | 7 |
| struct | FlatBuffers | 5 |
| struct | Cap'n Proto | 9 |
| iterate | C++ in memory | 4 |
| iterate | mbedded-cbor | 4 |
| iterate | nlohmann/json | 3 |
| iterate | libcbor | 6 |
| iterate | FlatBuffers | 3 |
| iterate | Cap'n Proto | 4 |
| shared | C++ in memory | 3 |
| shared | mbedded-cbor | 1 |
| shared | FlatBuffers | 5 |
| numbers | C++ in memory | 3 |
| numbers | mbedded-cbor | 7 |
| numbers | nlohmann/json | 2 |
| numbers | libcbor | 6 |
| numbers | FlatBuffers | 2 |
| numbers | Cap'n Proto | 3 |
| errors | C++ in memory | 6 |
| errors | mbedded-cbor | 2 |
| errors | nlohmann/json | 3 |
| errors | libcbor | 4 |
| errors | FlatBuffers | 3 |
| errors | Cap'n Proto | 8 |

The shared row of mbedded-cbor counts the lazy form. The structs form
is 1 line. The shared rows of nlohmann/json and libcbor are left out,
because their snippets show that the feature is absent.

## 9. Measurements

`bench/results/2026-10-09-025729Z.json` holds the only measurement in
bench/results that runs mbedded-cbor, FlatBuffers and Cap'n Proto on the
same data with the commit of a recent tree (f2b1e56). The operations are
in `bench/compare.cpp`. The mbedded-cbor arm uses `cbor::schema`
(Packed CBOR), not the at_path and lazy forms of sections 2 and 4.
FlatBuffers runs without its Verifier. The build is g++-16 16.2.0,
-O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie,
podman image mbedded-cbor-tw-compare, 10 rounds. Each value is the
median in nanoseconds per operation.

One field, `$.cars[500].engine.cc` of cars (the carsales data):

| Library | ns | Spread |
|---|---|---|
| mbedded-cbor schema | 13.0 | 0.50 |
| FlatBuffers | 2.4 | 0.24 |
| Cap'n Proto | 189.5 | 0.41 |

One element, `$.values[30000]` of floats (60000 binary64):

| Library | ns | Spread |
|---|---|---|
| mbedded-cbor schema | 6.0 | 0.19 |
| FlatBuffers | 1.3 | 0.50 |
| Cap'n Proto | 136.5 | 0.44 |

The sum of every element of floats (60000 binary64), the task of
section 6:

| Library | ns | Spread |
|---|---|---|
| mbedded-cbor schema | 36063 | 0.23 |
| FlatBuffers | 34761 | 0.24 |
| Cap'n Proto | 37714 | 0.23 |

The A/A difference of a cell lies between 0.07 and 10.9 percent (field
`aa_percent` of the file). The mbedded-cbor sum reads each element
through `i.at_path<"@[]">()`, not through the `view` of section 6.

bench/results has no measurement of nlohmann/json, libcbor, the plain
at_path form, the lazy elements form, structs, shared values or broken
input against another library in this image. This document gives no
number for them.

## 10. Other decoders read what mbedded-cbor writes

`test/interop/run.sh` writes values with every public writer:
`cbor::encoder`, `cbor::encode(item)`, `structs<T>::encode` and
`schema<T>::encode`. The values are the examples of RFC 8949 Appendix A,
the cbor-wg test vectors, the bench corpora, every integer and float
width, tags 28 and 29, and typed arrays. Each decoder below reads the
output. The script then compares the value with the value that was
written. `test/run.sh full` runs it, and the CI runs it at each push.
The decoders are the packages of Debian sid.

| Decoder | Version | Reads all output | Does not support |
|---|---|---|---|
| TinyCBOR | 7.0 | yes | |
| cbor2 (Python) | 6.1.2 | yes | |
| jsoncons | 1.5.0 | no | simple values other than 20 to 23 |
| CBOR::XS (Perl) | 1.87 | no | simple values other than 20 to 23; undefined as a map key |
| libcbor | 0.10.2 | no | tags 6 to 20, so no Packed CBOR; simple values other than 20 to 23 |
| nlohmann/json | 3.11.3 | no | a tag on an item that is not a byte string; undefined and simple values other than 20 to 22, so no Packed CBOR; map keys that are not text |

RFC 8949 section 5.2 lets a decoder refuse a value that it does not
support. Where a decoder supports a value, it reads the same value that
mbedded-cbor wrote. libcbor 0.14.0 reads tag 6 and so reads Packed CBOR.
A decoder that does not know a tag gives the tag number and its content
(RFC 8949 section 3.4). jsoncons gives the content without the tag
number.
