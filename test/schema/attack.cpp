#include "types.hpp"

namespace schema_types
{


template <class = void>
std::size_t probe_offset(std::size_t const size, std::size_t const member)
{
    return size - form<probe>::fixed_size() + member;
}

// Reads every leaf path of probe, each through a new root and through a root with an owner, from a copy of the message. Gives the count of
// paths that gave a value.
template <class = void>
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
template <class = void>
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
#ifdef CBOR_HAS_INT128
        else if constexpr (std::same_as<std::remove_cvref_t<decltype(*r)>, cbor::int128>)
            return static_cast<double>(static_cast<std::int64_t>(*r));
#endif
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

} // namespace schema_types

// The whole message reads every path, and each value is the one that went in.
TEST_CASE("attack: the sample message reads every path")
{
    std::string const bytes = *form<probe>::encode(sample_probe);
    CHECK_EQ(probe_paths_read(bytes), probe_paths);
    CHECK_EQ(probe_lot_read(bytes), probe_lot_paths);
    CHECK_EQ(at_path<probe, "$.i16">(bytes), -300);
    CHECK((*at_path<probe, "$.i128">(bytes) == wide_integer{-5}));
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
#ifdef CBOR_HAS_INT128
        CHECK_EQ(at_path<probe, "$.i128">(forged(S::member_offset<&probe::i128>(), head)).has_value(), head == 0xc2 || head == 0xc3);
#else
        CHECK_EQ(at_path<probe, "$.i128">(forged(S::member_offset<&probe::i128>(), head)).has_value(), head == 0x1b || head == 0x3b);
#endif
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

namespace schema_types
{

// Reads every form of a variant member and counts the values that came back.
template <class = void>
std::size_t read_pet(std::string_view const bytes)
{
    std::size_t n = 0;
    n += form<home>::decode(std::string(bytes)).has_value();
    n += form<home>::unverified_decode(std::string(bytes)).has_value();
    if (auto const doc = form<home>::path(std::string(bytes))) {
        if (auto const pet = doc->at_path<"$.pet">()) {
            n += 1;
            if (auto const d = pet->get<dog>())
                n += d->at_path<"@.name">().has_value();
            if (auto const c = pet->get<cat>())
                n += c->at_path<"@.toys[1]">().has_value();
        }
    }
    return n;
}

} // namespace schema_types

// A message cut at any byte reads no value. Each byte flipped reads no memory outside of the message; ASan
// and UBSan of this build show such a read.
TEST_CASE("attack: a variant member under a cut or a changed byte")
{
    for (home const &h : {home_with_dog(), home_with_cat()}) {
        std::string const bytes = *form<home>::encode(h);
        CHECK_EQ(read_pet(bytes), 4u);
        std::size_t values = 0;
        for (std::size_t size = 0; size < bytes.size(); ++size)
            values += read_pet(std::string_view(bytes).substr(0, size));
        CHECK_EQ(values, 0u);
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            std::string f = bytes;
            f.at(at) = static_cast<char>(f.at(at) ^ 0x41);
            read_pet(f);
        }
    }
}

// A tag that names no alternative is incorrect_type. A record whose tag head fits but whose values do not is
// too_little_data. A directory entry that points one byte into the record finds no tag of an alternative there,
// and decode, which reads the items in their order, finds the entry off its place.
TEST_CASE("attack: an unknown tag, a cut record and a wrong directory entry of a variant member")
{
    std::string const bytes = *form<home>::encode(home_with_dog());
    auto const at = bytes.find("\xd9\x06\xcc"sv);
    REQUIRE_NE(at, std::string::npos);
    std::string unknown = bytes;
    unknown[at + 2] = '\xcf';
    CHECK_EQ(form<home>::decode(std::string(unknown)).error(), error::incorrect_type);
    CHECK_EQ(form<home>::path(std::string(unknown))->at_path<"$.pet">().error(), error::incorrect_type);
    std::string other_root = bytes;
    other_root[at + 2] = '\xce';
    CHECK_EQ(form<home>::decode(std::string(other_root)).error(), error::incorrect_type);
    std::size_t const head = bytes.find("\x5a\x00\x00\x00\x0c"sv);
    REQUIRE_NE(head, std::string::npos);
    auto const big_endian = [](std::size_t const x) {
        return std::string{static_cast<char>(x >> 24), static_cast<char>(x >> 16), static_cast<char>(x >> 8),
                           static_cast<char>(x)};
    };
    REQUIRE_EQ(bytes.substr(head + 5, 4), big_endian(at));
    std::string cut = bytes;
    cut.replace(head + 9, 4, big_endian(at + 4));
    CHECK_EQ(form<home>::path(std::string(cut))->at_path<"$.pet">().error(), error::too_little_data);
    std::string moved = bytes;
    moved.replace(head + 5, 4, big_endian(at + 1));
    CHECK_EQ(form<home>::path(std::string(moved))->at_path<"$.pet">().error(), error::incorrect_type);
    CHECK_EQ(form<home>::decode(std::string(moved)).error(), error::syntax_error);
}

// A value variant under every cut and every changed byte reads no memory outside the message (ASan, UBSan). A
// string length past its item: the verified decode checks the order of the items and gives syntax_error while
// the length ends inside the items, too_little_data past the root; the unverified decode keeps the check of the
// end of the items and gives the same; the path form reads to the next directory entry and gives
// too_little_data.
TEST_CASE("attack: value and text alternatives of a variant member")
{
    for (setting const &s : sample_settings()) {
        std::string const bytes = *form<setting>::encode(s);
        for (std::size_t size = 0; size < bytes.size(); ++size) {
            CHECK_FALSE(form<setting>::decode(bytes.substr(0, size)).has_value());
            CHECK_FALSE(form<setting>::unverified_decode(bytes.substr(0, size)).has_value());
        }
        for (std::size_t at = 0; at < bytes.size(); ++at) {
            std::string f = bytes;
            f.at(at) = static_cast<char>(f.at(at) ^ 0x41);
            (void)form<setting>::decode(std::string(f));
            (void)form<setting>::unverified_decode(std::string(f));
            if (auto const doc = form<setting>::unverified_path(std::string(f)))
                if (auto const v = doc->at_path<"$.value">()) {
                    (void)v->get<std::string>();
                    (void)v->get<std::int32_t>();
                    (void)v->get<std::vector<std::byte>>();
                }
        }
    }
    // The text "ab" of pet is the first item; note holds the bytes of an empty text and "y". The length of pet
    // grows from 2 to 7 and swallows the head of note, so the next read starts at the empty text and ends before
    // the root, at "y".
    std::string const swallowed = *form<shelter>::encode(shelter{std::string("ab"), "\x7a\x00\x00\x00\x00y"s});
    std::size_t const pet = swallowed.find("\x7a\x00\x00\x00\x02" "ab"sv);
    REQUIRE_NE(pet, std::string::npos);
    std::string inside = swallowed;
    inside[pet + 4] = '\x07';
    CHECK_EQ(form<shelter>::decode(std::string(inside)).error(), error::syntax_error);
    CHECK_EQ(form<shelter>::unverified_decode(std::string(inside)).error(), error::syntax_error);
    CHECK_EQ(form<shelter>::path(std::string(inside))->at_path<"$.pet">().error(), error::too_little_data);
    // name "n" is the first item, the text "ab" of value the last; its length grows past the root.
    std::string const bytes = *form<setting>::encode(setting{"n", std::string("ab")});
    std::size_t const head = bytes.size() - form<setting>::fixed_size() - 7;
    REQUIRE_EQ(bytes.substr(head, 7), "\x7a\x00\x00\x00\x02" "ab"sv);
    std::string past = bytes;
    past[head + 3] = '\x7f';
    CHECK_EQ(form<setting>::decode(std::string(past)).error(), error::too_little_data);
    CHECK_EQ(form<setting>::unverified_decode(std::string(past)).error(), error::too_little_data);
    std::string unknown = bytes;
    unknown[head] = '\x7b';
    CHECK_EQ(form<setting>::decode(std::string(unknown)).error(), error::incorrect_type);
}
