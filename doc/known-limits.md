# Known limits

Status: decided means the owner or the specification fixed it. Open means
the behaviour can still change. Source: mbedded-cbor.md (parts 2 and 4),
the code and the tests, branch limits-at-no-cost.

## Shared references

| Limit | Status | Cause and where it is decided |
|---|---|---|
| Tag 29 forward references are refused (error sharedref_index_not_marked). | Decided | The marks of tag 28 are read once, up to the tag 29 that needs them. validity.hpp, shared.hpp. It differs from mruby-cbor. |
| decode() of [29(0), 28(1)] succeeds. | Open | decode does not enter an array, so it does not see the forward reference. Decoding through elements() finds it. |
| A cycle reaches only a binding that answers cyclic_data_structures with true. | Decided | Any other binding gets sharedref_not_complete. Encode without sharing gives cyclic_data_structure. |
| A document heavy in tag 29 costs more at its first tag 29. | Open | A fresh lazy reads every head once to find the marks. Measured 1.45 to 2.35 times slower. |
| Strings are shared by identity only. Tags 25 and 256 are not used. | Decided | They break lazy and at_path. |

## Threads and lazy

| Limit | Status | Cause and where it is decided |
|---|---|---|
| A lazy belongs to one thread. | Decided | The marks grow with no lock. Thread safety is the job of the application. |
| cbor::transfer hands a lazy to another thread. | Decided | Only a lazy that nothing else shares. Names are provisional. |
| A debug build throws std::logic_error on a foreign thread. | Decided | A release build does not check. cbor::is_thread_bound_v and is_sendable_v state it in the type. |
| lazy::decode through expected<lazy>::operator-> on a temporary gives a dangling reference. | Open | No overload sees the temporary. |
| A view outlives its owner in two forms with std::expected. | Open | Not closed. typed_array_view and schema::accessor keep their view after a move. |
| at_path(owner, encoded) trusts the owner. | Open | An overload on std::shared_ptr<std::string const> would close it. The owner decides. |

## Floats

| Limit | Status | Cause and where it is decided |
|---|---|---|
| NaN encodes as f97e00. The sign and the payload are lost. | Open | Encode writes the canonical half-precision NaN. Decode keeps sign and payload (head.hpp). The patch nan-encode.diff waits for the owner (RFC 8949 4.1). |
| float16 needs std::float16_t. | Decided | gcc 14.2 has it. clang 21 with libstdc++ 14 or libc++ 21 has none. |

## Compilers and reflection

| Limit | Status | Cause and where it is decided |
|---|---|---|
| clang builds have no reflection. databind and schema need g++. | Decided | They use __cpp_impl_reflection and annotations. clang 23 has no reflection. gcc 16 does not define __cpp_annotations. |
| g++-16 -fsanitize=undefined: verify_path and at_path(binding) do not compile for keys longer than 15 bytes. | Open | Not analysed. |
| clang ASan Debug overflows 8 MiB in value_decode at depth 1024. | Open | The stack frame per level is large in this build. |
| bench/schema.cpp does not compile. | Open | The bench source is older than the schema API. Not repaired. |

## Limits and memory

| Limit | Status | Cause and where it is decided |
|---|---|---|
| Nesting depth default is 128. The bound is 512. | Decided | CBOR_NESTING_DEPTH_DEFAULT and nesting_depth_limit in validity.hpp. Setting above 512 throws std::logic_error. A default above the bound fails a static_assert. |
| string_length, container_elements and input_bytes are SIZE_MAX by default. | Decided | validity.hpp macros. No limit is in force until the application sets one. A count in the input reserves nothing. |
| Memory is bounded by the memory resource of the caller (std::pmr). | Decided | decoded_bytes is removed. databind::decode and schema::decode read into a target with its allocator. |
| A plain struct element of a std::pmr::vector takes its members from the default resource. | Open | The allocator does not reach a type that has none. |
| schema::decode lets std::bad_alloc through. | Open | databind maps it to not_enough_memory. The owner decides whether schema does the same. |
| Limits are one global object. | Decided | An exception to the functional rule. Each entry reads it once. |

## Checks in the code

| Limit | Status | Cause and where it is decided |
|---|---|---|
| The "enough bytes left" check is written by hand in lazy.hpp, schema.hpp, diagnostic_notation.hpp and head.hpp. | Open | validity::check_pending_items should be the one place. |
| The has_value() sites (item_end.hpp, query.hpp) repeat the error that validity chooses. | Open | The owner decides the form. |

## Input the decoder refuses or accepts

| Limit | Status | Cause and where it is decided |
|---|---|---|
| Indefinite length is refused (error indefinite_length). | Decided | head.hpp, additional information 31. |
| UTF-8 in text strings is not checked. | Decided | The decoder returns the bytes of the string. No error for invalid UTF-8. |
| Duplicate keys are checked only in the deterministic profile. | Decided | validity.hpp gives duplicate_key against the neighbour key. Any other read stops at the first matching key (RFC 8949 5.6). |
| Keys 2(h'01') and 1 are distinct by RFC 8949 5.6.1, but decode turns both into 1. | Open | Not tested for databind with std::map. |
| validity::check_sorted_keys_unique has no caller. | Open | Written before the profile used it. |
| Simple values 24 to 31 are refused. | Decided | validity::check_simple_value. |
| Tag 55799 inside tag 24 is not skipped. | Open | Cost not measured. |
| Tag 24: the place of the top-level item of the embedded item is not decided. | Open | |

## Cost

| Limit | Status | Cause and where it is decided |
|---|---|---|
| Some functions cost O(n^2) or worse. | Open | doc/complexity.md lists them. Exponential first: generic_read and databind::read with tag 29; keys_equivalent on maps nested as keys. |
| The checked form of the limits on floats with clang is 1.24 to 1.31 times slower. | Open | The no-limit form costs nothing. Measurement 2026-10-10-045701Z.json. |
