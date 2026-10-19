# structs

`cbor::structs<T>` writes a C++ value as CBOR and reads it back, with the types of a member and the description of a class.

| Function | Does |
|---|---|
| [structs&lt;T&gt;::encode](encode.md) | writes a value as CBOR |
| [structs&lt;T&gt;::decode](decode.md) | reads a value from CBOR |
| [describe_members](describe_members.md) | gives the tag and the members of a class |
| [member](member.md) | gives the key of one member in a description |
| [CBOR_MEMBER](CBOR_MEMBER.md) | gives a member with its name as the key |
| [key](key.md) | gives a member of a `schema` struct another key; an integer key for a lazy |
| [skip](skip.md) | leaves a member of a `schema` struct out |
| [allowlist](allowlist.md) | writes only the allowed members of a `schema` struct |
| [allow](allow.md) | allows a member under `allowlist` |
| [Member types](types.md) | the C++ types and the CBOR items they give |
