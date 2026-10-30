#include "schema_types.hpp"

namespace
{

class counting_resource : public std::pmr::memory_resource
{
    void *do_allocate(std::size_t const bytes, std::size_t const alignment) override
    {
        ++allocations;
        return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    }

    void do_deallocate(void *const p, std::size_t const bytes, std::size_t const alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(p, bytes, alignment);
    }

    bool do_is_equal(std::pmr::memory_resource const &other) const noexcept override
    {
        return this == &other;
    }

public:
    std::size_t allocations = 0;
};

} // namespace

// A plain struct has no allocator, so a std::pmr::vector cannot give its resource to it, but it can give it to each
// member that takes an allocator, as uses-allocator construction ([allocator.uses.construction]) does for one
// object. The test puts a counting resource as the default resource, so that a member that took the default
// resource shows as a count above zero. Each name is longer than any short string buffer, so each one allocates.
TEST_CASE("decode: a plain struct in a std::pmr::vector takes the resource of the vector for its members")
{
    std::string const long_name(40, 'n');
    std::string const bytes = schema_bytes(pmr_people{{{std::pmr::string(long_name)}, {std::pmr::string(long_name)}}});
    counting_resource fallback;
    counting_resource target;
    auto const owner = std::make_shared<std::string const>(bytes);
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded = form<pmr_people>::decode(owner, pmr_people{std::pmr::vector<pmr_named>(&target)});
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).people.size(), 2u);
    for (pmr_named const &p : (**decoded).people) {
        CHECK_EQ(p.n, long_name);
        CHECK_EQ(p.n.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
    CHECK_GT(target.allocations, 0u);
}

// Broken bytes give the error as a value, as with std::expected.
TEST_CASE("decode: an error is read as a value")
{
    std::string const bytes = *form<vehicle>::encode(sample_vehicle);
    std::string_view const cut = std::string_view(bytes).substr(0, 3);
    auto const r = form<vehicle>::decode(std::string(cut));
    REQUIRE_FALSE(r.has_value());
    CHECK_EQ(r.error(), error::too_little_data);
}

// decode(bytes) copies the message once, and every view in the result points into that copy. The message here is a
// temporary that is destroyed at the end of the full expression. The test exists because a view into the bytes of
// the caller would read freed memory, and the address sanitizer reports that read.
TEST_CASE("decode: the views of the result outlive the bytes of the caller")
{
    std::expected<cbor::ref<ticket>, cbor::error> const t = form<ticket>::decode(ticket_bytes());
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
    CHECK_EQ((*t)->row, 12u);
}

// decode(owner) copies nothing and keeps the owner. The caller releases its own pointer to the owner before
// it reads. The test exists because the result alone must keep the bytes alive.
TEST_CASE("decode: the result keeps the owner after the caller releases it")
{
    auto owner = std::make_shared<std::string const>(ticket_bytes());
    auto const t = form<ticket>::decode(std::move(owner));
    REQUIRE(t.has_value());
    CHECK_EQ((*t)->holder, sample_ticket.holder);
    REQUIRE_EQ((*t)->seats.size(), 2u);
    CHECK_EQ((*t)->seats.at(0), sample_ticket.seats.at(0));
    CHECK_EQ((*t)->seats.at(1), sample_ticket.seats.at(1));
}

namespace
{

probe const sample_probe{true, -300, 1ull << 40, 2.5, -1.5f, cbor::int128{-5}, {'x', 'y', 'z'}, "probe text",
                         {{17, 1.5f}, {18, 2.0f}}, -9, {300, 1800}, {{7, 8}}};

std::size_t probe_offset(std::size_t const size, std::size_t const member)
{
    return size - form<probe>::fixed_size() + member;
}

bool lies_inside(std::string_view const part, std::string_view const message)
{
    std::less<char const *> const before;
    return part.empty() || (!before(part.data(), message.data()) &&
                            !before(message.data() + message.size(), part.data() + part.size()));
}

// Reads every leaf path of probe, each through a new root and through a root with an owner, from a copy of the message. Gives the count of
// paths that gave a value.
std::size_t probe_paths_read(std::string_view const message)
{
    auto const block = std::make_shared<std::string const>(message);
    std::string_view const bytes = *block;
    std::size_t n = 0;
    auto const count = [&n](auto const &r) {
        if (r)
            ++n;
    };
    auto const owned = [&n, bytes](auto const &r) {
        if (!r)
            return;
        ++n;
        CHECK(lies_inside(*r, bytes));
    };
    count(at_path<probe, "$.flag">(bytes));
    count(at_path<probe, "$.i16">(bytes));
    count(at_path<probe, "$.u64">(bytes));
    count(at_path<probe, "$.f64">(bytes));
    count(at_path<probe, "$.f32">(bytes));
    count(at_path<probe, "$.i128">(bytes));
    count(at_path<probe, "$.code">(bytes));
    owned(at_path<probe, "$.code">(block));
    count(at_path<probe, "$.text">(bytes));
    owned(at_path<probe, "$.text">(block));
    for (std::size_t i = 0; i < 3; ++i) {
        count(at_path<probe, "$.tires[].diameter">(bytes, i));
        count(at_path<probe, "$.tires[].airPressure">(bytes, i));
        count(at_path<probe, "$.pair[]">(bytes, i));
    }
    count(at_path<probe, "$.maybe">(bytes));
    count(at_path<probe, "$.motor.horsepower">(bytes));
    count(at_path<probe, "$.motor.cc">(bytes));
    return n;
}

constexpr std::size_t probe_paths = 6 + 2 + 2 + 3 * 2 + 3;

// Reads every leaf path of probe through one root accessor, first in order and then in the reverse order, and the
// tires through an accessor of each tire. Gives the count of paths that gave a value in the first pass. Each value of
// a later pass is the value of the first pass.
std::size_t probe_lot_read(std::string_view const message)
{
    auto const block = std::make_shared<std::string const>(message);
    std::string_view const bytes = *block;
    auto opened = form<probe>::path(block);
    if (!opened)
        return 0;
    auto &lot = *opened;
    std::vector<std::optional<double>> first;
    auto const number = [](auto const &r) -> std::optional<double> {
        if (!r)
            return std::nullopt;
        if constexpr (requires { r->has_value(); })
            return r->has_value() ? std::optional<double>(static_cast<double>(**r)) : std::optional<double>(-1);
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(*r)>, cbor::int128>)
            return static_cast<double>(static_cast<std::int64_t>(*r));
        else
            return static_cast<double>(*r);
    };
    auto const text = [bytes](auto const &r) -> std::optional<double> {
        if (!r)
            return std::nullopt;
        CHECK(lies_inside(*r, bytes));
        return static_cast<double>(r->size());
    };
    auto const all = [&] {
        std::vector<std::optional<double>> v;
        v.push_back(number(lot.at_path<"$.flag">()));
        v.push_back(number(lot.at_path<"$.i16">()));
        v.push_back(number(lot.at_path<"$.u64">()));
        v.push_back(number(lot.at_path<"$.f64">()));
        v.push_back(number(lot.at_path<"$.f32">()));
        v.push_back(number(lot.at_path<"$.i128">()));
        v.push_back(text(lot.at_path<"$.code">()));
        v.push_back(text(lot.at_path<"$.text">()));
        for (std::size_t i = 0; i < 3; ++i) {
            v.push_back(number(lot.at_path<"$.tires[].diameter">(i)));
            v.push_back(number(lot.at_path<"$.tires[].airPressure">(i)));
            v.push_back(number(lot.at_path<"$.pair[]">(i)));
        }
        v.push_back(number(lot.at_path<"$.maybe">()));
        v.push_back(number(lot.at_path<"$.motor.horsepower">()));
        v.push_back(number(lot.at_path<"$.motor.cc">()));
        return v;
    };
    std::vector<std::optional<double>> const forward = all();
    std::vector<std::optional<double>> backward;
    for (std::size_t i = 3; i-- > 0;) {
        backward.push_back(number(lot.at_path<"$.tires[].airPressure">(i)));
        backward.push_back(number(lot.at_path<"$.tires[].diameter">(i)));
    }
    CHECK_EQ(backward[0], forward[8 + 3 * 2 + 1]);
    CHECK_EQ(backward[1], forward[8 + 3 * 2]);
    CHECK_EQ(backward[4], forward[8 + 1]);
    CHECK_EQ(backward[5], forward[8]);
    for (std::size_t i = 0; i < 3; ++i) {
        auto const tire = lot.at_path<"$.tires[]">(i);
        std::optional<double> const diameter = tire ? number(tire->at_path<"@.diameter">()) : std::nullopt;
        std::optional<double> const pressure = tire ? number(tire->at_path<"@.airPressure">()) : std::nullopt;
        CHECK_EQ(diameter, forward[8 + 3 * i]);
        CHECK_EQ(pressure, forward[8 + 3 * i + 1]);
    }
    CHECK_EQ(all(), forward);
    return static_cast<std::size_t>(std::ranges::count_if(forward, [](auto const &x) { return x.has_value(); }));
}

constexpr std::size_t probe_lot_paths = 8 + 3 * 2 + 3;

template <class A, cbor::fixed_string Path>
concept temporary_reads = requires(A &&a) { std::move(a).template at_path<Path>(); };

template <class A>
concept chain_reads = requires(A const &a) { (*a.template at_path<"$.tires[]">(0uz)).template at_path<"@.diameter">(); };

template <class R>
concept dereferenced_as_rvalue = requires(R &&r) { *std::move(r); } || requires(R &&r) { std::move(r).operator->(); };

} // namespace

// The whole message reads every path, and each value is the one that went in.
TEST_CASE("attack: the sample message reads every path")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    CHECK_EQ(probe_paths_read(bytes), probe_paths);
    CHECK_EQ(probe_lot_read(bytes), probe_lot_paths);
    CHECK_EQ(at_path<probe, "$.i16">(bytes), -300);
    CHECK((*at_path<probe, "$.i128">(bytes) == cbor::int128{-5}));
    CHECK_EQ(at_path<probe, "$.f64">(bytes), 2.5);
    CHECK_EQ(at_path<probe, "$.pair[]">(bytes, 1uz), 8u);
    CHECK_EQ(at_path<probe, "$.maybe">(bytes).value(), std::optional<std::int32_t>{-9});
}

// A text is a view into the bytes that the root holds. An accessor is read through an lvalue only, so each accessor
// on the way to a value has a name. A ref cannot be read through a temporary. An owner that is empty is a
// wrong use and throws. The test exists because each of these would otherwise let a view outlive its bytes.
TEST_CASE("attack: no text result outlives its bytes")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    using A = form<probe>::accessor<>;
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.text">()), std::expected<std::string_view, cbor::error>>);
    CHECK(std::same_as<decltype(std::declval<A const &>().at_path<"@.code">()), std::expected<std::string_view, cbor::error>>);
    CHECK(path_reads<probe, "$.text">);
    CHECK_FALSE(temporary_reads<A, "$.text">);
    CHECK_FALSE(temporary_reads<form<probe>::accessor<tire>, "@.diameter">);
    CHECK_FALSE(chain_reads<A>);
    CHECK_FALSE(dereferenced_as_rvalue<cbor::ref<probe>>);
    CHECK_THROWS_AS((void)form<probe>::path(std::shared_ptr<std::string const>{}), std::logic_error);
    CHECK_THROWS_AS((void)form<probe>::decode(std::shared_ptr<std::string const>{}), std::logic_error);
}

// An owner is empty when it holds no object, whatever pointer it stores. An owner beside borrowed bytes compiles
// (owner decision 2026-10-17: an LMDB read transaction keeps the bytes of its value alive); an owner beside a string
// does not, because the string may die first. The test exists because each of these let a view outlive its bytes,
// and a check of the stored pointer refused an owner that holds the bytes.
TEST_CASE("attack: path and decode check that the owner holds an object")
{
    auto const bytes = std::make_shared<std::string const>(*form<probe>::encode(sample_probe));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { form<probe>::path(o, std::string(*o)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::path(o, std::string_view(*o)); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::decode(o, std::string_view(*o)); }; }(bytes)));
    CHECK_FALSE(([]<class O>(O const &) { return requires(O const &o) { form<probe>::decode(o, *o); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::path(o); }; }(bytes)));
    CHECK(([]<class O>(O const &) { return requires(O const &o) { form<probe>::decode(o); }; }(bytes)));
    std::shared_ptr<std::string const> const holds_nothing(std::shared_ptr<std::string const>{}, bytes.get());
    CHECK_THROWS_AS((void)form<probe>::path(holds_nothing), std::logic_error);
    CHECK_THROWS_AS((void)form<probe>::decode(holds_nothing), std::logic_error);
    std::shared_ptr<std::string const> const holds_bytes(std::shared_ptr<void const>(bytes), bytes.get());
    auto const root = form<probe>::path(holds_bytes);
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.i16">(), -300);
    auto const decoded = form<probe>::decode(holds_bytes);
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->i16, -300);
}

// Each prefix of the message is a cut message. The test exists because a reader that trusted an offset from the
// wire would read past the end of the cut. No prefix reads a value: the root lies at the end of the message.
TEST_CASE("attack: every prefix of a message is an error for every path")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t values = 0;
    for (std::size_t size = 0; size < bytes.size(); ++size)
        values += probe_paths_read(std::string_view(bytes).substr(0, size)) + probe_lot_read(std::string_view(bytes).substr(0, size));
    CHECK_EQ(values, 0u);
}

// A byte in front of the message or behind it moves the root away from its place at the end. The test exists because
// a reader that found the root by an offset from the start would read a record that is not there.
TEST_CASE("attack: a shifted root is incorrect_type")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    CHECK_EQ(at_path<probe, "$.i16">("\x00"s + bytes).error(), error::incorrect_type);
    CHECK_EQ(at_path<probe, "$.i16">(bytes + "\x00"s).error(), error::incorrect_type);
    CHECK_EQ(probe_paths_read("\x00"s + bytes), 0u);
    CHECK_EQ(probe_paths_read(bytes + "\x00"s), 0u);
    CHECK_EQ(probe_lot_read("\x00"s + bytes), 0u);
    CHECK_EQ(probe_lot_read(bytes + "\x00"s), 0u);
    CHECK_EQ(form<probe>::path(bytes + "\x00"s).error(), error::incorrect_type);
}

// RFC 8949 3: each fixed field has one head. The heads that are valid are written here from RFC 8949 3.3 and 3.4
// and the fixed widths of the schema: a bool is f4 or f5, an int16 is 19 or 39, a uint64 is 1b, a double fb, a
// float fa, an int128 the tag c2 or c3, a char[3] the text head 63. Every other head is incorrect_type. The test
// exists because a reader that ignored the head would read the argument of another type.
TEST_CASE("attack: every forged head of a fixed field is incorrect_type")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    auto const forged = [&bytes](std::size_t const member, int const head) {
        std::string f = bytes;
        f.at(probe_offset(f.size(), member)) = static_cast<char>(head);
        return f;
    };
    using S = form<probe>;
    for (int head = 0; head < 256; ++head) {
        CAPTURE(head);
        CHECK_EQ(at_path<probe, "$.flag">(forged(S::member_offset<&probe::flag>(), head)).has_value(), head == 0xf4 || head == 0xf5);
        CHECK_EQ(at_path<probe, "$.i16">(forged(S::member_offset<&probe::i16>(), head)).has_value(), head == 0x19 || head == 0x39);
        CHECK_EQ(at_path<probe, "$.u64">(forged(S::member_offset<&probe::u64>(), head)).has_value(), head == 0x1b);
        CHECK_EQ(at_path<probe, "$.f64">(forged(S::member_offset<&probe::f64>(), head)).has_value(), head == 0xfb);
        CHECK_EQ(at_path<probe, "$.f32">(forged(S::member_offset<&probe::f32>(), head)).has_value(), head == 0xfa);
        CHECK_EQ(at_path<probe, "$.i128">(forged(S::member_offset<&probe::i128>(), head)).has_value(), head == 0xc2 || head == 0xc3);
        CHECK_EQ(at_path<probe, "$.code">(forged(S::member_offset<&probe::code>(), head)).has_value(), head == 0x63);
        CHECK_EQ(at_path<probe, "$.pair[1]">(forged(S::member_offset<&probe::pair>(), head)).has_value(), head == 0x82);
    }
}

// Every byte of the root record is forged in turn, with values that flip a head, a length or a reference. The test
// exists because no forged byte may make a path read outside the message; the sanitizer and the check of each text
// see that.
TEST_CASE("attack: a forged byte anywhere in the root reads nothing outside the message")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const root = bytes.size() - form<probe>::fixed_size();
    for (std::size_t at = root; at < bytes.size(); ++at) {
        for (int const flip : {0x01, 0x20, 0x80, 0xff}) {
            std::string f = bytes;
            f.at(at) = static_cast<char>(f.at(at) ^ flip);
            probe_paths_read(f);
            probe_lot_read(f);
        }
    }
}

// A directory entry of the wire is forged to the edges of the message: before the shared items, inside the root,
// past the end, and to the largest u32. The test exists because an item that begins in the directory or ends in the
// root would overlap bytes that another path reads as another type.
TEST_CASE("attack: a directory entry out of range is an error")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"s);
    REQUIRE_NE(head, std::string::npos);
    std::size_t const root = bytes.size() - form<probe>::fixed_size();
    for (std::size_t j = 0; j < 3; ++j) {
        for (std::uint32_t const to : {0u, static_cast<std::uint32_t>(head), static_cast<std::uint32_t>(root),
                                       static_cast<std::uint32_t>(bytes.size()), 0x7fffffffu, 0xffffffffu}) {
            std::string f = bytes;
            f.replace(head + 5 + 4 * j, 4, std::string{static_cast<char>(to >> 24), static_cast<char>(to >> 16),
                                                       static_cast<char>(to >> 8), static_cast<char>(to)});
            CAPTURE(j);
            CAPTURE(to);
            CHECK_FALSE(form<probe>::decode(std::string(f)).has_value());
            CHECK_LT(probe_paths_read(f), probe_paths);
            CHECK_LT(probe_lot_read(f), probe_lot_paths);
        }
    }
}

// Two directory entries are forged to the same offset, so two items would share their bytes. In key order the items
// of probe are text, maybe and tires; each pair of neighbours is forged once. The text item of login
// is the last item; its length is forged to reach into the root record behind it. The test exists because a text
// that reached into the root would give the bytes of the root's fields as text.
TEST_CASE("attack: overlapping items are an error")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"s);
    REQUIRE_NE(head, std::string::npos);
    std::string same = bytes;
    same.replace(head + 9, 4, bytes.substr(head + 5, 4));
    CHECK_FALSE(at_path<probe, "$.text">(same).has_value());
    CHECK_FALSE(at_path<probe, "$.maybe">(same).has_value());
    std::string next = bytes;
    next.replace(head + 13, 4, bytes.substr(head + 9, 4));
    CHECK_FALSE(at_path<probe, "$.maybe">(next).has_value());
    CHECK_FALSE(at_path<probe, "$.tires[0].diameter">(next).has_value());

    std::string const one = schema_bytes(login{5, true, "ab"});
    std::size_t const text = one.find("\x7a\x00\x00\x00\x02" "ab"s);
    REQUIRE_NE(text, std::string::npos);
    std::string into_root = one;
    into_root.replace(text + 1, 4, std::string{'\x00', '\x00', '\x00', static_cast<char>(2 + form<login>::fixed_size())});
    CHECK_EQ(at_path<login, "$.name">(into_root).error(), error::too_little_data);
    CHECK_EQ(at_path<login, "$.id">(into_root), 5u);
}

// Each item of a packed message starts with an RFC 8949 head: a typed array with the tag head d8 56 and the head
// 5a of its byte string, a text with the head 7a. A four-byte argument is read big endian (RFC 8949 Section 3).
// A directory entry is forged so that the head of the last item no longer fits before the root record. The test
// exists because the read of that head must stop at the bytes that are left and give too_little_data.
TEST_CASE("attack: an item head that does not fit before the root is too_little_data")
{
    auto const big_endian = [](std::size_t const x) {
        return std::string{static_cast<char>(x >> 24), static_cast<char>(x >> 16), static_cast<char>(x >> 8),
                           static_cast<char>(x)};
    };
    std::string const array = schema_bytes(doubles{{1.0, 2.0}});
    std::size_t const head = array.find("\xd8\x56\x5a\x00\x00\x00\x10"sv);
    REQUIRE_NE(head, std::string::npos);
    std::size_t const entry = array.find(big_endian(head));
    REQUIRE_LT(entry, head);
    std::string cut_array = array;
    cut_array.replace(entry, 4, big_endian(array.size() - form<doubles>::fixed_size() - 3));
    CHECK_EQ(at_path<doubles, "$.v[0]">(cut_array).error(), error::too_little_data);

    std::string const text = schema_bytes(login{5, true, "ab"});
    std::size_t const name = text.find("\x7a\x00\x00\x00\x02" "ab"sv);
    REQUIRE_NE(name, std::string::npos);
    std::size_t const name_entry = text.find(big_endian(name));
    REQUIRE_LT(name_entry, name);
    std::string cut_text = text;
    cut_text.replace(name_entry, 4, big_endian(text.size() - form<login>::fixed_size() - 3));
    CHECK_EQ(at_path<login, "$.name">(cut_text).error(), error::too_little_data);
}

// The directory is a byte string with a four-byte argument (RFC 8949 Section 3.1, major type 2). Its length is
// forged far past the end of the message, and the count in the prefix is forged to match it. The test exists
// because the read of the directory must stop at the bytes that are left and give too_little_data.
TEST_CASE("attack: a directory longer than the message is too_little_data")
{
    auto const big_endian = [](std::size_t const x) {
        return std::string{static_cast<char>(x >> 24), static_cast<char>(x >> 16), static_cast<char>(x >> 8),
                           static_cast<char>(x)};
    };
    std::string const bytes = schema_bytes(login{5, true, "ab"});
    std::size_t const directory = bytes.find("\x5a\x00\x00\x00\x04"sv, 8);
    REQUIRE_NE(directory, std::string::npos);
    std::size_t const forged_length = 0x00fffff0;
    std::size_t const first =
        (std::size_t{static_cast<std::uint8_t>(bytes[4])} << 24 | std::size_t{static_cast<std::uint8_t>(bytes[5])} << 16 |
         std::size_t{static_cast<std::uint8_t>(bytes[6])} << 8 | std::size_t{static_cast<std::uint8_t>(bytes[7])}) -
        1;
    std::string forged = bytes;
    forged.replace(4, 4, big_endian(first + forged_length / 4));
    forged.replace(directory + 1, 4, big_endian(forged_length));
    CHECK_EQ(form<login>::path(std::string(forged)).error(), error::too_little_data);
    CHECK_EQ(form<login>::decode(std::string(forged)).error(), error::too_little_data);
}

// A vector of fixed-size structs refers to the description of its element in the header. The reference of the
// tires is forged to name another table entry. The test exists because a path into the vector must check that
// reference, while a path that does not touch the vector stays readable.
TEST_CASE("attack: a forged reference of a fixed-size vector is incorrect_type")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    std::size_t const head = bytes.find("\xd9\x80\x69\x82"s);
    REQUIRE_NE(head, std::string::npos);
    std::string forged = bytes;
    forged.at(head + 4) = static_cast<char>(forged.at(head + 4) + 1);
    CHECK_EQ(at_path<probe, "$.tires[1].diameter">(forged).error(), error::incorrect_type);
    CHECK_EQ(at_path<probe, "$.text">(forged), at_path<probe, "$.text">(bytes));
    auto lot = *form<probe>::path(std::string(forged));
    for (int pass = 0; pass < 2; ++pass) {
        CHECK_EQ(lot.at_path<"$.tires[].diameter">(0uz).error(), error::incorrect_type);
        CHECK_EQ(lot.at_path<"$.tires[].airPressure">(1uz).error(), error::incorrect_type);
    }
    CHECK_EQ(form<probe>::decode(std::string(forged)).error(), error::incorrect_type);
}

// Each prefix and each forged byte of a message of several cars is read through a new root and an accessor of each
// car. The test exists because an accessor that trusted a list it read once would read a later item past the end.
TEST_CASE("attack: an accessor reads nothing outside a cut or forged message of several cars")
{
    std::string const bytes = schema_bytes(sample_lot());
    std::size_t differ = 0;
    auto const other = [](auto const &a, auto const &b) { return a.has_value() != b.has_value() || (a && *a != *b); };
    auto const read = [&differ, other](std::string_view const m) {
        auto opened = form<parking_lot>::path(std::make_shared<std::string const>(m));
        std::size_t n = 0;
        if (!opened)
            return n;
        auto &lot = *opened;
        for (std::size_t i = 6; i-- > 0;) {
            auto const make = lot.at_path<"$.cars[].make">(i);
            auto const seats = lot.at_path<"$.cars[].seats">(i);
            auto const pressure = lot.at_path<"$.cars[].wheels[].airPressure">(i, 1uz);
            n += std::size_t{make.has_value()} + std::size_t{seats.has_value()} + std::size_t{lot.at_path<"$.cars[].note">(i).has_value()} + std::size_t{pressure.has_value()};
            auto const car = lot.at_path<"$.cars[]">(i);
            if (!car)
                continue;
            differ += std::size_t{other(car->at_path<"@.make">(), make)} + std::size_t{other(car->at_path<"@.seats">(), seats)};
            auto const wheel = car->at_path<"@.wheels[]">(1uz);
            if (wheel)
                differ += other(wheel->at_path<"@.airPressure">(), pressure);
        }
        n += lot.at_path<"$.notes[]">(1uz).has_value();
        n += lot.at_path<"$.rows[][]">(2uz, 0uz).has_value();
        return n;
    };
    CHECK_EQ(read(bytes), 5u * 3 + 2 + 2);
    std::size_t values = 0;
    for (std::size_t size = 0; size < bytes.size(); ++size)
        values += read(std::string_view(bytes).substr(0, size));
    CHECK_EQ(values, 0u);
    for (std::size_t at = 0; at < bytes.size(); ++at) {
        std::string f = bytes;
        f.at(at) = static_cast<char>(f.at(at) ^ 0x41);
        read(f);
    }
    CHECK_EQ(differ, 0u);
}

// The value category of the bytes says what decode and path do. A moved std::string becomes the owner and is not
// copied; an lvalue and a const rvalue are copied once. The result stays valid after the caller reuses or destroys
// its buffer.
TEST_CASE("decode and path: an rvalue string is moved, everything else is copied")
{
    std::string const name(40, 'n');
    std::string const message = schema_bytes(login{7, true, name});
    auto range = [](std::string_view const inner, char const *const begin, std::size_t const size) {
        return std::less_equal<>{}(begin, inner.data()) && std::less_equal<>{}(inner.data() + inner.size(), begin + size);
    };

    auto buffer = std::make_unique<std::string>(message);
    char const *data = buffer->data();
    auto const moved = form<login>::path(std::move(*buffer));
    REQUIRE(moved.has_value());
    auto const moved_name = moved->at_path<"$.name">();
    REQUIRE(moved_name.has_value());
    CHECK(range(*moved_name, data, message.size()));
    buffer->assign(message.size(), '\0');
    buffer.reset();
    CHECK_EQ(*moved_name, name);

    std::string lvalue = message;
    auto const copied = form<login>::path(std::string(lvalue));
    REQUIRE(copied.has_value());
    auto const copied_name = copied->at_path<"$.name">();
    REQUIRE(copied_name.has_value());
    CHECK_FALSE(range(*copied_name, lvalue.data(), lvalue.size()));
    CHECK_EQ(lvalue, message);
    lvalue.assign(message.size(), '\0');
    CHECK_EQ(*copied_name, name);

    std::string const constant = message;
    // Owner decision 2026-10-17: no form copies the input. A const string, a named string, a view and a literal
    // would need a copy, so none of them compiles.
    auto const takes = []<class S>(S &&) { return requires(S &&s) { form<login>::decode(std::forward<S>(s)); }; };
    auto const opens = []<class S>(S &&) { return requires(S &&s) { form<login>::path(std::forward<S>(s)); }; };
    CHECK_FALSE(opens(std::move(constant)));
    CHECK_FALSE(takes(std::move(constant)));
    CHECK_FALSE(takes(lvalue));
    CHECK_FALSE(takes(std::string_view(message)));
    CHECK_FALSE(takes(""));
    CHECK_FALSE(opens(""));

    auto decode_buffer = std::make_unique<std::string>(message);
    auto const decoded = form<login>::decode(std::move(*decode_buffer));
    decode_buffer.reset();
    REQUIRE(decoded.has_value());
    CHECK_EQ((*decoded)->name, name);
    CHECK_EQ(form<login>::decode(std::string()).error(), error::too_little_data);
    CHECK_EQ(form<login>::path(std::string()).error(), error::too_little_data);
}

// The bytes of the vector come from the wire. The head of the vector, the description in the header, the length
// of the byte string and every bool are checked before a value is read.
TEST_CASE("at_path and decode: a broken fixed-size vector is an error")
{
    std::string const bytes = schema_bytes(cloud{{{1.0f, 0.5f}, {2.0f, -1.0f}}});
    std::size_t const head = bytes.find("\xd9\x80\x69\x82\xe2\x5a"s);
    REQUIRE_NE(head, std::string::npos);
    std::string reference = bytes;
    reference[head + 4] = '\xe3';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(reference).error(), error::incorrect_type);
    CHECK_EQ(form<cloud>::decode(std::string(reference)).error(), error::incorrect_type);
    std::string description = bytes;
    description[description.find("\x18\x55"s) + 1] = '\x56';
    CHECK_EQ(form<cloud>::path(std::string(description)).error(), error::incorrect_type);
    CHECK_EQ(form<cloud>::decode(std::string(description)).error(), error::incorrect_type);
    std::string partial = bytes;
    partial[head + 9] = '\x0f';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(partial).error(), error::inadmissible_type_for_tag_content);
    CHECK_EQ(form<cloud>::decode(std::string(partial)).error(), error::inadmissible_type_for_tag_content);
    std::string longer = bytes;
    longer[head + 9] = '\x18';
    CHECK_EQ(at_path<cloud, "$.points[0].x">(longer).error(), error::too_little_data);
    CHECK_EQ(form<cloud>::decode(std::string(longer)).error(), error::too_little_data);
    std::string const flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    std::string two = flags;
    two[element + 2] = '\x02';
    CHECK_EQ(at_path<gauges, "$.g[0].on">(two).error(), error::incorrect_type);
    CHECK_EQ(at_path<gauges, "$.g[0].shade">(two), color::white);
    CHECK_EQ(form<gauges>::decode(std::string(two)).error(), error::incorrect_type);
}

namespace
{

template <class T>
using unverified_root = std::remove_cvref_t<decltype(*form<T>::unverified_path(std::string()))>;

// Reads every kind of part through the unverified forms and returns nothing: the sweep below only needs each call
// to return.
void unverified_reads(std::string const &bytes)
{
    auto const owner = std::make_shared<std::string const>(bytes);
    (void)form<everything>::unverified_at_path<"$.big">(bytes);
    (void)form<everything>::unverified_at_path<"$.engines[1].cc">(bytes);
    (void)form<everything>::unverified_at_path<"$.maybe">(bytes);
    (void)form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz);
    (void)form<everything>::unverified_at_path<"$.tires[].airPressure">(bytes, 1uz);
    (void)form<everything>::unverified_decode(owner);
    auto const root = form<everything>::unverified_path(owner);
    if (!root)
        return;
    (void)root->at_path<"$.text">();
    if (auto const inner = root->at_path<"$.inner">(); inner && *inner)
        (void)(*inner)->at_path<"@.x">();
    (void)root->at_path<"$.names[1]">();
    if (auto const track = root->view<"$.track">())
        for (double const d : *track)
            (void)d;
    if (auto const owners = root->at_path<"$.owners">())
        (void)owners->size();
    if (auto const gauges = root->at_path<"$.gauges">())
        for (auto const g : *gauges) {
            (void)g.at_path<"@.on">();
            (void)g.at_path<"@.range[1]">();
        }
    if (auto const tires = root->at_path<"$.tires">())
        for (std::size_t i = 0; i < tires->size() + 1; ++i)
            (void)tires->at_path<"@[].diameter">(i);
}

} // namespace

// The verified decode checks the head of a fixed string and of a std::array as the verified path does. Before,
// only the path checked them, so a forged head gave incorrect_type to a path and a value to decode.
TEST_CASE("decode: the head of a fixed string and of a std::array is checked")
{
    std::string const bytes = schema_bytes(sample_vehicle);
    std::size_t const code = bytes.find("\x64" "ABCD"s);
    REQUIRE_NE(code, std::string::npos);
    std::string text_head = bytes;
    text_head[code] = '\x44';
    CHECK_EQ(at_path<vehicle, "$.code">(text_head).error(), error::incorrect_type);
    CHECK_EQ(form<vehicle>::decode(std::string(text_head)).error(), error::incorrect_type);
    std::string const spare = schema_bytes(sample_everything);
    std::size_t const engines = spare.find("\x82\xd9\x05\xdd"s);
    REQUIRE_NE(engines, std::string::npos);
    std::string array_head = spare;
    array_head[engines] = '\x83';
    CHECK_EQ(at_path<everything, "$.engines[1].cc">(array_head).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::decode(std::string(array_head)).error(), error::incorrect_type);
}

// Owner decision 2026-10-18: the unverified_ forms read a message from a trusted peer without the checks of its
// data, as FlatBuffers does without its Verifier. On a message that the encoder wrote they give what the verified
// forms give.
TEST_CASE("unverified: the same results as the verified forms on a message that the encoder wrote")
{
    std::string const bytes = schema_bytes(sample_everything);
    CHECK_EQ(form<everything>::unverified_at_path<"$.big">(bytes), at_path<everything, "$.big">(bytes));
    CHECK_EQ(form<everything>::unverified_at_path<"$.engines[1].cc">(bytes), 1200u);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 1uz), -6);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[].airPressure">(bytes, 1uz), 3.0f);
    CHECK_EQ(form<everything>::unverified_at_path<"$.maybe">(bytes).value(), std::optional<std::int32_t>{-9});
    auto const root = form<everything>::unverified_path(std::string(bytes));
    REQUIRE(root.has_value());
    CHECK_EQ(root->at_path<"$.text">(), "text"sv);
    CHECK_EQ(root->at_path<"$.names[1]">(), "bc"sv);
    auto const inner = root->at_path<"$.inner">();
    REQUIRE(inner.has_value());
    REQUIRE(inner->has_value());
    CHECK_EQ((*inner)->at_path<"@.y">(), 2);
    auto const track = root->view<"$.track">();
    REQUIRE(track.has_value());
    CHECK_EQ(track->at(1), 2.0);
    auto const gauges = root->at_path<"$.gauges">();
    REQUIRE(gauges.has_value());
    std::vector<int> xs;
    for (auto const g : *gauges)
        xs.push_back(*g.at_path<"@.at.x">());
    CHECK_EQ(xs, (std::vector<int>{3, -6}));
    CHECK_EQ(reencoded(form<everything>::unverified_decode(std::string(bytes))), bytes);
    auto const owner = std::make_shared<std::string const>(schema_bytes(sample_vehicle));
    CHECK_EQ(reencoded(form<vehicle>::unverified_decode(owner)), *owner);
}

// The unverified forms skip the checks of the data: a forged class tag, a forged head of a number, a forged tag of
// a typed array and a forged head of a fixed-size vector give a value where the verified forms give incorrect_type.
TEST_CASE("unverified: a forged type byte gives a value, not an error")
{
    std::string const bytes = schema_bytes(sample_everything);
    auto const forged = [&bytes](std::string const &find, std::size_t const at, char const to) {
        std::size_t const where = bytes.rfind(find);
        REQUIRE_NE(where, std::string::npos);
        std::string out = bytes;
        out[where + at] = to;
        return out;
    };
    std::string const root_tag = forged("\xd9\x06\x7c"s, 2, '\x7d');
    CHECK_EQ(at_path<everything, "$.flag">(root_tag).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::unverified_at_path<"$.flag">(root_tag), true);
    CHECK(form<everything>::unverified_decode(std::string(root_tag)).has_value());
    std::string const track_tag = forged("\xd8\x56\x5a"s, 1, '\x55');
    auto const verified = form<everything>::path(std::string(track_tag));
    REQUIRE(verified.has_value());
    CHECK_EQ(verified->view<"$.track">().error(), error::incorrect_type);
    auto const unverified = form<everything>::unverified_path(std::string(track_tag));
    REQUIRE(unverified.has_value());
    CHECK_EQ(unverified->view<"$.track">()->at(0), 1.0);
    std::string const vector_head = forged("\xd9\x80\x69"s, 1, '\x81');
    CHECK_EQ(at_path<everything, "$.gauges[1].on">(vector_head).error(), error::incorrect_type);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[1].on">(vector_head), false);
}

// The unverified forms keep every check of a bound: a cut message, an index past the end and a length past the
// end give the same error in both forms.
TEST_CASE("unverified: a bound is checked as in the verified forms")
{
    std::string const bytes = schema_bytes(sample_everything);
    CHECK_EQ(form<everything>::unverified_path(bytes.substr(0, 20)).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_decode(bytes.substr(0, 20)).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[].at.x">(bytes, 2uz).error(),
             error::index_out_of_bounds);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[].diameter">(bytes, 2uz).error(),
             error::index_out_of_bounds);
    std::string longer = bytes;
    std::size_t vectors = 0;
    for (std::size_t head = longer.find("\xd9\x80\x69"s); head != std::string::npos;
         head = longer.find("\xd9\x80\x69"s, head + 1), ++vectors)
        longer.replace(longer.find('\x5a', head) + 1, 4, "\xff\xff\xff\xff"s);
    CHECK_EQ(vectors, 2);
    CHECK_EQ(form<everything>::at_path<"$.gauges[0].on">(longer).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.gauges[0].on">(longer).error(), error::too_little_data);
    CHECK_EQ(form<everything>::unverified_at_path<"$.tires[0].diameter">(longer).error(), error::too_little_data);
}

// Every accessor that an unverified accessor gives is unverified, and every accessor that a verified accessor
// gives is verified, so a read never changes its form in the middle of a path.
TEST_CASE("unverified: the form of an accessor passes to its parts")
{
    using unverified = unverified_root<everything>;
    using verified = form<everything>::accessor<>;
    CHECK(std::same_as<unverified, form<everything>::accessor<everything, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.gauges">())>,
                       form<everything>::accessor<std::vector<gauge>, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.gauges[]">(0uz))>,
                       form<everything>::accessor<cbor::fixed_element<gauge>, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<unverified const &>().at_path<"$.engines[0]">())>,
                       form<everything>::accessor<engine, false>>);
    CHECK(std::same_as<std::remove_cvref_t<decltype(*std::declval<verified const &>().at_path<"$.gauges[]">(0uz))>,
                       form<everything>::accessor<cbor::fixed_element<gauge>>>);
}

// The unverified forms read where the data says, so the test changes every byte of a message that holds every kind
// of part, and cuts it at every length. Each call must return; in the build with AddressSanitizer and
// UndefinedBehaviorSanitizer no read leaves the input.
TEST_CASE("unverified: every changed byte and every cut stays inside the input")
{
    std::string const bytes = schema_bytes(sample_everything);
    for (std::size_t i = 0; i < bytes.size(); ++i)
        for (char const to : {'\x00', '\xff', static_cast<char>(~bytes[i]), '\x7f', '\x80'}) {
            std::string changed = bytes;
            changed[i] = to;
            unverified_reads(changed);
        }
    for (std::size_t n = 0; n < bytes.size(); ++n)
        unverified_reads(bytes.substr(0, n));
    std::size_t const directory = bytes.find('\x5a');
    REQUIRE_NE(directory, std::string::npos);
    for (std::uint32_t const entry : {0u, 1u, 0x7fffffffu, 0xffffffffu, static_cast<std::uint32_t>(bytes.size())})
        for (std::size_t at = directory + 5; at + 4 <= bytes.size() && at < directory + 5 + 4 * 8; at += 4) {
            std::string changed = bytes;
            for (std::size_t k = 0; k < 4; ++k)
                changed[at + k] = static_cast<char>(entry >> (24 - 8 * k));
            unverified_reads(changed);
        }
    for (std::uint32_t const length : {0u, 3u, 4u, 0xffffffffu}) {
        std::string changed = bytes;
        for (std::size_t k = 0; k < 4; ++k)
            changed[directory + 1 + k] = static_cast<char>(length >> (24 - 8 * k));
        unverified_reads(changed);
    }
}

// A bool in a fixed-size struct is one byte. The verified form refuses a byte other than 0 and 1; the unverified
// form reads any byte other than 0 as true.
TEST_CASE("unverified: a bool byte 2 reads true")
{
    std::string const flags = schema_bytes(gauges{{{true, color::white, {-2, 3}, {1, 0x0203}}}});
    std::size_t const element = flags.find("\x03\xfe\x01\x01\x00"s);
    REQUIRE_NE(element, std::string::npos);
    std::string two = flags;
    two[element + 2] = '\x02';
    CHECK_EQ(at_path<gauges, "$.g[0].on">(two).error(), error::incorrect_type);
    CHECK_EQ(form<gauges>::unverified_at_path<"$.g[0].on">(two), true);
    auto const back = form<gauges>::unverified_decode(std::string(two));
    REQUIRE(back.has_value());
    CHECK((*back)->g[0].on);
}

// A class with no default constructor is built by its constructor, which takes the described members in the order
// of the description, as mrb_cpp_new of mruby-c-ext-helpers forwards its arguments to new T(...). The wire does not
// show how a class is built, so the same description gives the bytes of an aggregate. The key order (unit before
// value) differs from the order of the description, so a mix-up of the two orders shows here.
TEST_CASE("decode: a class with a constructor takes its described members in the order of the description")
{
    std::string const bytes = schema_bytes(reading{7, "kg"});
    CHECK_EQ(bytes, schema_bytes(reading_plain{7, "kg"}));
    auto const back = form<reading>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    CHECK_EQ((**back).value, 7u);
    CHECK_EQ((**back).unit, "kg");
    auto const unverified = form<reading>::unverified_decode(std::make_shared<std::string const>(bytes));
    REQUIRE(unverified.has_value());
    CHECK_EQ((**unverified).unit, "kg");
    CHECK_EQ(at_path<reading, "$.unit">(bytes), "kg");
}

// Each place that makes an element builds a class with a constructor the same way: an element of a vector, the
// value of an optional and the value of a map.
TEST_CASE("decode: a class with a constructor in a vector, an optional and a map")
{
    station const full{{reading{1, "a"}, reading{2, "bb"}}, reading{3, "ccc"}, {{9, reading{4, "dddd"}}}};
    std::string const bytes = schema_bytes(full);
    auto const back = form<station>::decode(std::string(bytes));
    REQUIRE(back.has_value());
    REQUIRE_EQ((**back).readings.size(), 2u);
    CHECK_EQ((**back).readings[1].value, 2u);
    CHECK_EQ((**back).readings[1].unit, "bb");
    REQUIRE((**back).first.has_value());
    CHECK_EQ((**back).first->unit, "ccc");
    REQUIRE_EQ((**back).by_hour.size(), 1u);
    CHECK_EQ((**back).by_hour.at(9).unit, "dddd");
    CHECK_EQ(at_path<station, "$.readings[1].unit">(bytes), "bb");
    CHECK_EQ(reencoded(form<station>::decode(std::string(bytes))), bytes);
    auto const empty = form<station>::decode(schema_bytes(station{{}, std::nullopt, {}}));
    REQUIRE(empty.has_value());
    CHECK((**empty).readings.empty());
    CHECK_FALSE((**empty).first.has_value());
}

// The arguments of the constructor are made with the allocator of the vector (std::tuple with std::allocator_arg,
// [tuple.cnstr]), so a std::pmr member keeps the resource of the vector through the move into the class.
TEST_CASE("decode: a class with a constructor in a std::pmr::vector takes the resource of the vector")
{
    std::string const long_unit(40, 'u');
    pmr_station value;
    value.readings.emplace_back(std::pmr::string(long_unit));
    value.readings.emplace_back(std::pmr::string(long_unit));
    std::string const bytes = schema_bytes(value);
    counting_resource fallback;
    counting_resource target;
    auto const owner = std::make_shared<std::string const>(bytes);
    std::pmr::memory_resource *const was = std::pmr::set_default_resource(&fallback);
    auto const decoded = form<pmr_station>::decode(owner, pmr_station{std::pmr::vector<pmr_reading>(&target)});
    std::pmr::set_default_resource(was);
    REQUIRE(decoded.has_value());
    REQUIRE_EQ((**decoded).readings.size(), 2u);
    for (pmr_reading const &r : (**decoded).readings) {
        CHECK_EQ(r.unit, long_unit);
        CHECK_EQ(r.unit.get_allocator().resource(), &target);
    }
    CHECK_EQ(fallback.allocations, 0u);
}

// The root of a message stands at its end. A message with a missing end gave incorrect_type, because the last bytes
// are not the root. Every verified entry gives too_little_data for each length that cuts the message.
TEST_CASE("a message cut at any length is too_little_data")
{
    std::string const bytes = schema_bytes(reading{7, "kg"});
    std::string const apart_bytes = *form<byte_fields>::template encode<cbor::description_place::apart>(byte_fields_value());
    std::string const whole = *form<byte_fields>::encode(byte_fields_value());
    for (std::size_t n = 0; n < whole.size(); ++n) {
        CAPTURE(n);
        std::string const cut = whole.substr(0, n);
        CHECK_EQ(form<byte_fields>::decode(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::decode(std::make_shared<std::string const>(cut), byte_fields{}).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::path(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::at_path<"$.list[1]">(cut).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::at_path<"$.list[]">(cut, 1uz).error(), error::too_little_data);
    }
    for (std::size_t n = 0; n < bytes.size(); ++n) {
        CAPTURE(n);
        CHECK_EQ(form<reading>::decode(bytes.substr(0, n)).error(), error::too_little_data);
    }
    for (std::size_t n = 0; n < apart_bytes.size(); ++n) {
        CAPTURE(n);
        std::string const cut = apart_bytes.substr(0, n);
        CHECK_EQ(form<byte_fields>::template decode<cbor::description_place::apart>(std::string(cut)).error(), error::too_little_data);
        CHECK_EQ(form<byte_fields>::template at_path<"$.list[1]", cbor::description_place::apart>(cut).error(), error::too_little_data);
    }
}

// Only a cut message of the type is too_little_data: a message of another type, a message with a byte after it and
// text that is not CBOR keep their error.
TEST_CASE("a whole message of another type keeps its error")
{
    std::string const whole = *form<byte_fields>::encode(byte_fields_value());
    CHECK_EQ(form<reading>::decode(std::string(whole)).error(), error::incorrect_type);
    CHECK_EQ(form<byte_fields>::decode(whole + "\x01").error(), error::incorrect_type);
    std::string const text(whole.size(), '{');
    CHECK_EQ(form<byte_fields>::decode(std::string(text)).error(), error::incorrect_type);
}
