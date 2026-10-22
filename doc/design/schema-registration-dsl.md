# Schema registration DSL — design proposal

Status: design proposal; implementation is not included in this commit.

## Goal

Replace native C++ annotations in the schema API with an explicit registration DSL. A user must be able to register an existing class or struct, including a type from a third-party library, without editing its definition, deriving a wrapper, or adding annotations to its declaration.

The registered tag is a compile-time property of the type. Schema construction should obtain it statically, not by runtime lookup.

## Proposed public syntax

Illustrative API:

    struct Point {
        float x;
        float y;
    };

    CBOR_SCHEMA(Point, 1001);

The same form should work for a type the user does not own:

    CBOR_SCHEMA(QPoint, 1002);

The macro name and exact syntax remain open until the implementation is checked against the existing schema machinery.

## Required compile-time behavior

If a type is used in a position where the schema requires a registered CBOR type but no registration exists, compilation must fail. There must be no silent fallback and no deferred runtime error.

This requirement must be applied at the actual schema boundary: not every C++ type appearing as a member necessarily needs its own CBOR tag. Primitive fields and ordinary member fields must continue to be handled according to the existing schema rules. The implementation must distinguish a type that is a schema node requiring registration from a member type that is merely encoded as part of its parent.

Nested types must work naturally. If a registered schema type contains another type that itself requires schema registration, the compiler should validate both requirements while instantiating the schema.

## Keep tag registration and member description separate

The repository already has cbor::describe_members<T>, cbor::member, cbor::map_key, and CBOR_MEMBER(T, member) in include/cbor/describe.hpp.

The registration DSL should define the type-level tag. Member description remains responsible for describing fields and their CBOR keys when the implementation cannot discover them automatically. This avoids creating a parallel metadata system.

A tag-only macro cannot discover arbitrary members on compilers without C++ reflection. The non-reflection path must therefore continue to use an explicit member description where required. On reflection-enabled compilers, schema generation may discover members at compile time, subject to the current implementation's rules.

## Design constraints

1. Preserve the existing CBOR wire representation and validation behavior.
2. Support classes and structs, including third-party types.
3. Require no inheritance and no wrapper type.
4. Resolve tags at compile time with no runtime registry lookup or allocation.
5. Reject missing required registrations at compile time.
6. Diagnose conflicting registrations and invalid tag values at compile time where practical.
7. Do not require registrations for primitive or ordinary member types unless the existing schema model specifically treats them as schema nodes.
8. Remove native annotations from the target API rather than requiring users to annotate their original class definitions. Decide whether a temporary compatibility period is needed.

## Suggested implementation sequence

1. Identify the exact schema-instantiation boundary where a registered tag is mandatory.
2. Introduce one compile-time trait as the source of truth for a type's registered tag.
3. Make schema generation consume that trait and issue a focused static assertion when registration is missing.
4. Preserve the existing describe_members<T> mechanism for explicit member descriptions.
5. Add compile-time tests for registered and unregistered types, nested types, third-party types, and primitive/member types.
6. Compare encoded bytes and decode behavior with the existing implementation.
7. Remove the old annotation-based API and update the documentation once migration behavior is decided.

## Acceptance criteria

- A registered user-defined class can be used as a schema type.
- A registered third-party class can be used without modifying its declaration.
- Missing required registration causes a compile-time error with an actionable diagnostic.
- Nested schema types are checked recursively where the schema rules require it.
- Ordinary fields do not accidentally require independent registration.
- Existing member descriptions remain usable and do not duplicate tag metadata.
- The encoded CBOR representation remains unchanged for equivalent schemas.
