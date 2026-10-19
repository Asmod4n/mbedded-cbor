# errors

The error values of the library, their conversion to `std::error_code`,
and the limits of a build.

| Function | Does |
|---|---|
| [error](error.md) | names each fault that a call returns |
| [category](category.md) | gives the one error category of `cbor::error` |
| [error_category](error_category.md) | gives the name, the text and the `std::errc` of each error |
| [make_error_code](make_error_code.md) | makes a `std::error_code` from a `cbor::error` |
| [std::is_error_code_enum](is_error_code_enum.md) | lets a `cbor::error` convert to `std::error_code` |
| [limits](limits.md) | gives the limits of the build and the macros that set them |
