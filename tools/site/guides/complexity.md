# Proofs of the complexity statements

A reference page states the cost of a call only where a proof stands here.
A proof counts the steps of the code and names the lines it uses. The line
numbers are those of the commit that adds or changes the proof. The C++
standard gives the cost of each standard library call, and a proof takes
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

1. `item_size` (include/cbor/item_size.hpp:166-173) calls `skip_item` once
   and then `distance` (validity.hpp:559-562), which subtracts two sizes:
   O(1).
2. `skip_item` (item_size.hpp:44-102) runs one loop (lines 47-100). Each
   pass decrements `pending` (line 48) and then reads exactly one head:
   through `try_decode_head` (head.hpp:310-342) or, when that gives no
   head, through `decode_head` (head.hpp:255-308).
3. A head that is read removes at least one byte from `d.encoded`. Every
   exit that gives a head removes `initial_byte_size` or more
   (head.hpp:267, 306, 338), and `initial_byte_size` is 1. A head that
   cannot be read ends the call with an error.
4. A pass reads one head, so a pass stands for one data item: a string,
   an array, a map, a tag, an integer, a float or a simple value. A
   successful call ends when `pending` is 0 (line 47), that is when every
   data item in the item is read. So it makes i passes. By step 3, i is at
   most n. A call that fails makes at most one pass more than the bytes it
   removed, so it also makes at most n + 1 passes.
5. Each pass does a fixed number of steps. None of them depends on the
   input size:
   - `try_decode_head` reads byte 0 only after `bytes_fit(1)`, and bytes 1
     to 8 only after `bytes_fit(9)` (head.hpp:312-317). It copies 8 bytes
     with `std::copy`: 8 assignments by [alg.copy]. It never reads past
     the view. `decode_head` reads at most 9 bytes in the same way.
   - `decode_byte_string` (head.hpp:344-353) checks the length, takes
     `std::span::first` ([span.overview]) and calls `remove_prefix`
     ([string.view.template.general]). It does not read the bytes of the
     string.
   - `checked_add`, `check_pending_items`, `check_definite_length`,
     `check_argument` and `check_simple_value` (validity.hpp) are each a
     fixed number of compares and arithmetic steps.
   - `marks.mark` of `no_marks` (item_size.hpp:36-41) does nothing.
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

1. The constructors (include/cbor/sequence.hpp:68-74, 82-88) read no
   item. A move of a `std::shared_ptr` is `noexcept`
   ([util.smartptr.shared.const], [util.smartptr.shared.assign]), so it
   does not allocate. A null or empty owner throws `std::logic_error`
   (validity.hpp:330-359); that is a wrong use, and the exception
   allocates.
2. `begin()` (sequence.hpp:165-168) calls `item_size` on the bytes once:
   O(i) of the first item, by the proof of item_size.
3. `++` (sequence.hpp:134-143) moves the start of the view past the item
   and calls `item_size` once on the rest: O(i) of the next item. The
   move is `detail::validity::subspan` (validity.hpp:566-574):
   `views::drop` of a `std::string_view`, which by [range.drop.overview]
   adds the offset to an iterator ([iterator.requirements.general]) and
   makes a `std::string_view` ([string.view.template.general]): O(1). Its
   `std::logic_error` path needs an offset after the end, and `*size` is
   at most the bytes of the view. After an item with an error, `++` makes
   the view empty with `std::span::last` ([span.overview]): O(1).
4. `*it` (sequence.hpp:123-128) gives a `substr` of the view
   ([string.view.template.general]): O(1). `it == end()`
   (sequence.hpp:155-158) calls `empty`: O(1). `end()` reads nothing.
5. A loop over k items calls `item_size` k + 1 times: once for each item
   and once on the empty rest. The last call fails at once with
   `too_little_data`: O(1). The items do not overlap, so the sum of their
   bytes is at most n, and the sum of their data items is at most n. So
   the loop is O(n).
6. The iterator holds a `std::string_view` and a
   `std::expected<std::size_t, error>`; the sequence holds a
   `std::shared_ptr` and a `std::string_view`. None of the calls in steps
   2 to 5 allocate, and item_size uses O(1) memory. So the memory is O(1).
