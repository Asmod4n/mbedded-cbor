# structs

`cbor::structs<T>` writes a C++ value as CBOR and reads it back, with the types of a member and the description of a class.

The examples use the types in [test/doc_types.hpp](../../../test/doc_types.hpp), namespace `reference_structs`.

| Function | Does |
|---|---|
| [structs&lt;T&gt;::encode](encode.md) | writes a value as CBOR |
| [structs&lt;T&gt;::decode](decode.md) | reads a value from CBOR |
| [describe_members](describe_members.md) | gives the tag and the members of a class |
| [member](member.md) | gives the key of one member in a description |
| [CBOR_MEMBER](CBOR_MEMBER.md) | gives a member with its name as the key |
| [CBOR_DEFINE_TYPE_MEMBERS](CBOR_DEFINE_TYPE_MEMBERS.md) | describes a class from the names of its members |
| [Member types](types.md) | the C++ types and the CBOR items they give |
