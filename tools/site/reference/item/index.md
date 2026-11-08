# item

`cbor::item` is a decoded tree, and `cbor::encoder` writes CBOR one data item per call. `decode` copies every string into one memory zone.

| Function | Does |
|---|---|
| [item](item.md) | the type: one node of a decoded tree |
| [item::get](get.md) | gives the value of a node of a known kind |
| [decode](decode.md) | decodes one top-level item into a tree |
| [encoded_cbor_data_item](encoded_cbor_data_item.md) | gives the tree inside a tag 24 |
| [encode](encode.md) | writes a tree into a container, a span or a writer |
| [encode_iov](encode_iov.md) | writes a tree into a list of pieces |
| [encoder](encoder.md) | the type: writes one data item per call |
| [encoder::unsigned_integer](unsigned_integer.md) | writes an unsigned integer |
| [encoder::negative_integer](negative_integer.md) | writes a negative integer |
| [encoder::byte_string](byte_string.md) | writes a byte string |
| [encoder::text_string](text_string.md) | writes a text string |
| [encoder::array](array.md) | writes the head of an array |
| [encoder::map](map.md) | writes the head of a map |
| [encoder::tag](tag.md) | writes the head of a tag |
| [encoder::simple_value](simple_value.md) | writes a simple value |
| [encoder::floating_point](floating_point.md) | writes a float |
| [encoder::typed_array](typed_array.md) | writes a typed array |
| [encoder::finish](finish.md) | ends the encoder and gives the result |
| [iov_writer](iov_writer.md) | the target that collects pieces |
