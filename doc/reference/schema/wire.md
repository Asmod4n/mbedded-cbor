# The bytes that cbor::schema::encode writes

A message is Packed CBOR (`refs/draft-ietf-cbor-packed-19.txt`):

```
113([table, rump])
```

```call-seq
cbor::schema<T>::encode(value) → 113([table, rump])
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: doc->at(1)
result: lazy of 1801(128([_2 6(0_2), 5_0, 6(-1_2), 6(1_2)]))
call: doc
result: lazy of 113([[_2 114(["make", "seats", "tires", "track"]), 114(["diameter", "pressure"]), [1800, [["diameter", 69], ["pressure", 85]]], h'000000710000007b00000091'_2, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, undefined, "Volvo"_2, 32873([simple(2), h'110000002040120000001040'_2]), 86(h'000000000000f03f00000000000000400000000000000840'_2)], 1801(128([_2 6(0_2), 5_0, 6(-1_2), 6(1_2)]))])
```

| Part | Bytes |
|---|---|
| tag 113 | Basic-Packed-CBOR, on an array of two |
| table | an array with a count of 4 bytes |
| 114([keys]) | the record function: the keys of one struct, one entry for each struct |
| description | `[class tag, [[key, RFC 8746 tag], ...]]`: one entry for each element type of a vector of fixed-size structs |
| directory | a byte string of 4-byte offsets of the parts of variable size |
| fillers | `undefined`, so that the parts start at index 16 or later |
| parts | the texts, byte strings, lists and maps |
| rump | the root record, at the end of the message |

A record is the class tag ([tag](tag.md)), a straight reference tag 128
to 135 (or `6([n, ...])`) to its 114 entry, and an array with a count of
4 bytes of its fields. Each field has a head of fixed width. A field of
variable size is `6(N)`, a shared reference with a 4-byte `N`, into the
table.

A list of a number is an RFC 8746 typed array, little endian. A
`std::vector<double>` is tag 86.

A vector of fixed-size structs is `32873([simple(j), h'...'])`. `j` is
the index of its description in the table; from index 16 on, `6(N)`
stands in place of `simple(j)`. The byte string holds the elements
with no padding, little endian. Tag 32873 is provisional: the IANA
registry does not have it yet.

A decoder of Packed CBOR reads the message as a map with the keys of
the members.

## See also

[encode](encode.md) · [fixed_size](fixed_size.md) · [member_offset](member_offset.md) · [../../advanced/schema.md](../../advanced/schema.md), section 1
