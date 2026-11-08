# Proofs of the complexity statements

A reference page states the cost of a call only where a proof stands here.
A proof counts the steps of the code and names the functions it uses. A
test with an input of 100000 data items stands beside each proof: a form
that is quadratic in that input does not end in the time of the test run.
The C++ standard gives the cost of each standard library call, and a proof takes
that cost as given and names the clause:

- [string.view.template.general]: every member function of
  `basic_string_view` is O(1) unless otherwise specified.
- [span.overview]: every member function of `span` is constant time.
- [alg.copy]: `std::copy` does exactly N assignments.
- [iterator.requirements.general]: an iterator operation is constant time
  (amortized).
- [range.drop.overview]: `views::drop` of a `basic_string_view` gives
  `T(ranges::begin(E) + min(size, n), ranges::end(E))`.

## item_size

Statement: O(i) time, where i is the number of data items in the item,
and i is at most n, the number of bytes of the item. O(1) memory. No
allocation.

Proof.

1. `cbor::item_size` (include/cbor/item_size.hpp) calls
   `well_formedness::skip_item` once and then
   `detail::validity::distance`, which subtracts two sizes:
   O(1).
2. `skip_item` runs one loop, `while (pending != 0)`. Each pass
   decrements `pending` and then reads exactly one head: through
   `heads::decoder::try_decode_head` (include/cbor/head.hpp) or, when that
   gives no head, through `heads::decoder::decode_head`.
3. A head that is read removes at least one byte from `d.encoded`. Every
   exit of the two functions that gives a head calls
   `encoded.remove_prefix` with `initial_byte_size` or more, and
   `initial_byte_size` is 1. A head that
   cannot be read ends the call with an error.
4. A pass reads one head, so a pass stands for one data item: a string,
   an array, a map, a tag, an integer, a float or a simple value. A
   successful call ends when `pending` is 0, that is when every
   data item in the item is read. So it makes i passes. By step 3, i is at
   most n. A call that fails makes at most one pass more than the bytes it
   removed, so it also makes at most n + 1 passes.
5. Each pass does a fixed number of steps. None of them depends on the
   input size:
   - `try_decode_head` reads byte 0 only after `bytes_fit(1)`, and bytes 1
     to 8 only after `bytes_fit(9)`. It copies 8 bytes
     with `std::copy`: 8 assignments by [alg.copy]. It never reads past
     the view. `decode_head` reads at most 9 bytes in the same way.
   - `heads::decoder::decode_byte_string` checks the length, takes
     `std::span::first` ([span.overview]) and calls `remove_prefix`
     ([string.view.template.general]). It does not read the bytes of the
     string.
   - `checked_add`, `check_pending_items`, `check_definite_length`,
     `check_argument` and `check_simple_value` (include/cbor/validity.hpp) are each a
     fixed number of compares and arithmetic steps.
   - `marks.mark` of `well_formedness::no_marks` does nothing.
   So the time is O(i), and O(n) because i is at most n.
6. The memory of the call is its local variables: `pending`, `added`, a
   head and the decoder, which holds a `std::string_view`. Their number
   does not depend on the input. Nothing in the call recurses. So the
   memory is O(1). No reachable line makes a `std::string`, a container
   or an exception, or calls an allocation function.

## sequence

Statement: `begin()` and each `++` are O(i) of the item they read, at most
the bytes of that item. A loop over the whole sequence is O(n), n the
bytes of the sequence. O(1) memory. No allocation, except a wrong use of
a constructor, which throws.

The time of the constructors has no statement: they move a
`std::shared_ptr`, and the standard gives no complexity for that move.

Proof.

1. The constructors of `cbor::sequence` (include/cbor/sequence.hpp) read no
   item. A move of a `std::shared_ptr` is `noexcept`
   ([util.smartptr.shared.const], [util.smartptr.shared.assign]), so it
   does not allocate. A null or empty owner throws `std::logic_error`
   (`detail::validity::throw_logic_error_if_null` and
   `throw_logic_error_if_empty`); that is a wrong use, and the exception
   allocates.
2. `begin()` calls `item_size` on the bytes once:
   O(i) of the first item, by the proof of item_size.
3. `++` of `sequence::iterator` moves the start of the view past the item
   and calls `item_size` once on the rest: O(i) of the next item. The
   move is `detail::validity::subspan`:
   `views::drop` of a `std::string_view`, which by [range.drop.overview]
   adds the offset to an iterator ([iterator.requirements.general]) and
   makes a `std::string_view` ([string.view.template.general]): O(1). Its
   `std::logic_error` path needs an offset after the end, and `*size` is
   at most the bytes of the view. After an item with an error, `++` makes
   the view empty with `std::span::last` ([span.overview]): O(1).
4. `*it` gives a `substr` of the view
   ([string.view.template.general]): O(1). `it == end()`
   calls `empty`: O(1). `end()` reads nothing.
5. A loop over k items calls `item_size` k + 1 times: once for each item
   and once on the empty rest. The last call fails at once with
   `too_little_data`: O(1). The items do not overlap, so the sum of their
   bytes is at most n, and the sum of their data items is at most n. So
   the loop is O(n).
6. The iterator holds a `std::string_view` and a
   `std::expected<std::size_t, error>`; the sequence holds a
   `std::shared_ptr` and a `std::string_view`. None of the calls in steps
   2 to 5 allocate, and item_size uses O(1) memory. So the memory is O(1).
