# Console

Type calls on `doc`, the message below, or on `bytes`, its bytes. `doc`
is a `std::expected<cbor::lazy, cbor::error>`, so `->` reads the lazy, as
in C++. Each call runs in the library, compiled to WebAssembly. It is not
a C++ compiler: a loop or a new struct does not run here.

## Read a field by its key

[at](lazy/at.md) goes to the value of a key. A text comes back as a view
into the message.

```example
message: {"user": {"id": 42, "name": "ann"}}
call: doc->at("user")->at("name")->get<std::string_view>()
result: ref to "ann"
```

## Read an element by its index

[at](lazy/at.md) with a number goes to that element of an array.

```example
message: {"tags": ["a", "b", "c"]}
call: doc->at("tags")->at(1)->get<std::string_view>()
result: ref to "b"
```

## Read one value by its path

[at_path](lazy/at_path.md) reads one value by its JSONPath and skips the
rest of the message.

```example
message: {"user": {"id": 42, "name": "ann"}}
call: doc->at_path<"$.user.id">()->get<int>()
result: 42
```

## Read all values of a path

With `cbor::nodelist`, a path with `[*]` gives every value that matches.

```example
message: {"users": [{"name": "ann"}, {"name": "bob"}]}
call: doc->at_path<"$.users[*].name", cbor::nodelist>()->size()
result: 2
```

## Show the message as text

[diagnostic_notation](tools/diagnostic_notation.md) writes the bytes as
CBOR diagnostic notation.

```example
message: {"user": {"id": 42, "name": "ann"}}
call: cbor::diagnostic_notation(bytes).value()
result: "{\"user\": {\"id\": 42, \"name\": \"ann\"}}"
```

## Read a value with the wrong type

A value of another type comes back as a `cbor::error`, not as an
exception.

```example
message: {"user": {"id": 42, "name": "ann"}}
call: doc->at("user")->at("name")->get<int>()
result: unexpected cbor::error::incorrect_type
```

## What the console reads

- A call on `doc` or on `bytes`: `doc->at("user")`.
- `->` on a `std::expected`, `.` on a value.
- `get<T>()` with a type: `int`, `double`, `std::string_view`.
- `at_path<"…">()` with a JSONPath of RFC 9535.
- `cbor::key{1}` for a key that is a number.
- The Tab key or the button ⇥ shows the names that fit.
