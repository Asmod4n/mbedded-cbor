# The bytes that cbor::schema::encode writes

A message is Packed CBOR (`refs/draft-ietf-cbor-packed-19.txt`):

```
113([table, rump])
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
