// Writes a described struct as Packed CBOR and reads it back. Reads a
// member of fixed size at an offset that the compiler computes. decode
// reads the whole struct into a cbor::ref.
// https://datatracker.ietf.org/doc/html/draft-ietf-cbor-packed-19
// https://www.rfc-editor.org/rfc/rfc8746
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <memory>
#include <new>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#ifdef __cpp_impl_reflection
#include <meta>
#elif __has_include(<boost/pfr.hpp>)
#include <boost/pfr.hpp>
#endif
#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#include "cbor_sets.hpp"
#include "notable_tags.hpp"
#include "packed_cbor.hpp"
#include "describe.hpp"
#include "encode.hpp"
#include "error.hpp"
#include "head.hpp"
#include "item_size.hpp"
#include "ref.hpp"
#include "rfc8746.hpp"
#include "rfc8943.hpp"
#include "rfc9535.hpp"
#include "validity.hpp"

namespace cbor
{

template <std::uint64_t Number, class T>
struct tagged_item {
    static_assert(!has_describe_members<T> && !(std::is_class_v<T> && std::is_aggregate_v<T> && !std::ranges::range<T>),
                  "the tag wrapper takes content that is not a struct; a struct carries its tag in cbor::describe_members.");
    static constexpr std::uint64_t number = Number;
    T content;
};

namespace detail
{

template <class T>
consteval std::size_t no_fixed_size()
{
    std::unreachable();
}

}

template <class Root>
consteval bool tags_registered();

template <class T>
class schema;

template <class E>
class typed_array_view;

template <class S>
struct fixed_element {};

namespace detail
{

template <class U>
inline constexpr bool is_std_variant = false;

template <class... A>
inline constexpr bool is_std_variant<std::variant<A...>> = true;

struct directory {
    std::size_t at;
    std::size_t count;
};

template <class Value, class Context, auto Element>
class indexing_iterator
{
    std::shared_ptr<void const> owner;
    std::string_view encoded;
    std::span<char const> list;
    std::size_t position = 0;
    std::uint64_t length = 0;
    [[no_unique_address]] Context context{};

    indexing_iterator(std::shared_ptr<void const> o, std::string_view const b, std::span<char const> const l, std::size_t const p,
                      std::uint64_t const n, Context const c)
        : owner(std::move(o)), encoded(b), list(l), position(p), length(n), context(c)
    {
    }

    Value read_element(std::size_t const at) const
    {
        auto element = [&] {
            if constexpr (std::same_as<Context, std::monostate>)
                return Element(list, length, at);
            else
                return Element(owner, encoded, list, length, at, context);
        }();
        validity::throw_logic_error_if(!element.has_value(), "cbor::schema: the iterator is outside of its list");
        return *std::move(element);
    }

    template <class>
    friend class cbor::schema;

    template <class>
    friend class cbor::typed_array_view;

public:
    using iterator_concept = std::random_access_iterator_tag;
    using iterator_category = std::input_iterator_tag;
    using value_type = Value;
    using difference_type = std::ptrdiff_t;

    indexing_iterator() = default;

    Value operator*() const
    {
        return read_element(position);
    }

    Value operator[](difference_type const n) const
    {
        return read_element(position + static_cast<std::size_t>(n));
    }

    indexing_iterator &operator++()
    {
        ++position;
        return *this;
    }

    indexing_iterator operator++(int)
    {
        indexing_iterator const was = *this;
        ++position;
        return was;
    }

    indexing_iterator &operator--()
    {
        --position;
        return *this;
    }

    indexing_iterator operator--(int)
    {
        indexing_iterator const was = *this;
        --position;
        return was;
    }

    indexing_iterator &operator+=(difference_type const n)
    {
        position += static_cast<std::size_t>(n);
        return *this;
    }

    indexing_iterator &operator-=(difference_type const n)
    {
        position -= static_cast<std::size_t>(n);
        return *this;
    }

    friend indexing_iterator operator+(indexing_iterator i, difference_type const n)
    {
        return i += n;
    }

    friend indexing_iterator operator+(difference_type const n, indexing_iterator i)
    {
        return i += n;
    }

    friend indexing_iterator operator-(indexing_iterator i, difference_type const n)
    {
        return i -= n;
    }

    friend difference_type operator-(indexing_iterator const &a, indexing_iterator const &b)
    {
        return static_cast<difference_type>(a.position - b.position);
    }

    friend bool operator==(indexing_iterator const &a, indexing_iterator const &b)
    {
        return a.position == b.position;
    }

    friend std::strong_ordering operator<=>(indexing_iterator const &a, indexing_iterator const &b)
    {
        return a.position <=> b.position;
    }
};

}

enum class description_place { inside, apart };

class reflection
{
    static constexpr std::size_t item_head = heads::initial_byte_size + sizeof(std::uint32_t);
    static constexpr std::size_t dynamic_type_sizes = heads::initial_byte_size + item_head;
    static constexpr std::size_t shared_first = 16;

    template <class T>
    struct fixed_length {};

    template <class E, std::size_t N>
    struct fixed_length<E[N]> {
        using element = std::remove_cv_t<E>;
        static constexpr std::size_t value = N;
    };

    template <class E, std::size_t N>
    struct fixed_length<std::array<E, N>> {
        using element = std::remove_cv_t<E>;
        static constexpr std::size_t value = N;
    };

    template <class E, std::size_t N>
        requires(N != std::dynamic_extent)
    struct fixed_length<std::span<E, N>> {
        using element = std::remove_cv_t<E>;
        static constexpr std::size_t value = N;
    };

    template <class U>
    static constexpr bool is_text_range = requires {
        requires std::ranges::contiguous_range<U>;
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, char> ||
                     std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, char8_t>;
    };

    template <class U>
    static constexpr bool is_byte_range = requires {
        requires std::ranges::contiguous_range<U>;
        requires std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, unsigned char> ||
                     std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>;
    };

    template <class U>
    static constexpr bool is_fixed_string = requires {
        typename fixed_length<U>::element;
        requires std::same_as<typename fixed_length<U>::element, char> ||
                     std::same_as<typename fixed_length<U>::element, char8_t> ||
                     std::same_as<typename fixed_length<U>::element, unsigned char> ||
                     std::same_as<typename fixed_length<U>::element, std::byte>;
    };

    template <class U>
    static constexpr bool has_fixed_underlying_type = std::is_enum_v<U> && requires { U{0}; };

    template <class U>
    static constexpr bool is_optional = requires(U const &v) {
        v.has_value();
        *v;
        typename U::value_type;
    } && !requires { typename U::error_type; };

    template <class U>
    static constexpr bool is_inline_optional = is_optional<U> && requires {
        requires has_describe_members<typename U::value_type>;
        requires !requires { fixed_length<typename U::value_type>::value; };
    };

    template <class U>
    static constexpr bool is_map = std::ranges::sized_range<U> && requires {
        typename U::key_type;
        typename U::mapped_type;
    };

    template <class U>
    static constexpr bool is_list = std::ranges::sized_range<U> && !is_text_range<U> && !is_byte_range<U> && !is_optional<U> &&
                                    !is_map<U> && !requires { fixed_length<U>::value; };

    template <class E>
    static consteval std::uint64_t typed_array_tag()
    {
        constexpr std::uint64_t f = std::is_floating_point_v<E> ? 1 : 0;
        constexpr std::uint64_t s = std::is_signed_v<E> && !std::is_floating_point_v<E> ? 1 : 0;
        constexpr std::uint64_t e = sizeof(E) > 1 ? 1 : 0;
        constexpr std::uint64_t ll = static_cast<std::uint64_t>(std::countr_zero(sizeof(E))) - f;
        return std::to_underlying(rfc8746::tag_number::typed_array_first) | f << std::to_underlying(rfc8746::bit_field::f) |
               s << std::to_underlying(rfc8746::bit_field::s) | e << std::to_underlying(rfc8746::bit_field::e) |
               ll << std::to_underlying(rfc8746::bit_field::ll);
    }

    template <class E>
    static constexpr bool is_typed_array_integer = requires {
        requires std::integral<E> && !std::same_as<E, bool> && !std::same_as<E, char> && !std::same_as<E, wchar_t> &&
                     !std::same_as<E, char8_t> && !std::same_as<E, char16_t> && !std::same_as<E, char32_t>;
        requires sizeof(E) <= sizeof(std::uint64_t);
    };

    template <class E>
    static constexpr bool is_typed_array_float = requires {
        requires std::is_floating_point_v<E> && std::numeric_limits<E>::is_iec559;
        requires(sizeof(E) == sizeof(std::uint16_t) && std::numeric_limits<E>::digits == heads::half_precision.significand_bits + 1) ||
                    (sizeof(E) == sizeof(float) && std::numeric_limits<E>::digits == std::numeric_limits<float>::digits) ||
                    (sizeof(E) == sizeof(double) &&
                     std::numeric_limits<E>::digits == std::numeric_limits<double>::digits);
    };

    template <class E>
    static constexpr bool is_typed_array_element = is_typed_array_integer<E> || is_typed_array_float<E>;

    template <class U>
    static constexpr bool is_typed_array = requires {
        requires is_list<U>;
        requires is_typed_array_element<std::remove_cv_t<std::ranges::range_value_t<U>>>;
    };

    template <class U>
#ifdef __SIZEOF_INT128__
    static constexpr bool is_wide_integer = std::same_as<U, int128> || std::same_as<U, uint128>;
#else
    static constexpr bool is_wide_integer = false;
#endif

    template <class U>
    static constexpr bool is_struct = has_describe_members<U> || (std::is_class_v<U> && std::is_aggregate_v<U>);

    template <class U, class... A>
    static consteval bool contains(std::tuple<std::type_identity<A>...>)
    {
        return (std::is_same_v<U, A> || ...);
    }

    template <class U, class... A>
    static consteval std::size_t index_in(std::tuple<std::type_identity<A>...>)
    {
        std::array<bool, sizeof...(A) + 1> const hits{std::is_same_v<U, A>..., true};
        return static_cast<std::size_t>(std::ranges::find(hits, true) - hits.begin());
    }

    template <auto Build>
    static constexpr auto static_bytes = [] {
        std::array<char, Build().size()> bytes{};
        std::ranges::copy(Build(), bytes.begin());
        return bytes;
    }();

    static consteval std::vector<unsigned char> key_encoded(major_type const major, std::uint64_t const argument,
                                                            std::string_view const text)
    {
        std::uint8_t const info = heads::preferred_argument_info(argument);
        std::size_t const bytes = heads::argument_size(info);
        std::vector<unsigned char> out{static_cast<unsigned char>(heads::initial_byte(major, info))};
        for (std::size_t k = bytes; k > 0; --k)
            out.push_back(static_cast<unsigned char>(argument >> (std::numeric_limits<std::uint8_t>::digits * (k - 1))));
        for (char const c : text)
            out.push_back(static_cast<unsigned char>(c));
        return out;
    }

    template <class E>
    CBOR_ALWAYS_INLINE static E read_typed_array_element(std::span<char const, sizeof(E)> const in)
    {
        std::array<char, sizeof(E)> little;
        std::ranges::copy(in, little.begin());
        if constexpr (std::endian::native == std::endian::big)
            std::ranges::reverse(little);
        return std::bit_cast<E>(little);
    }

    template <class E>
    CBOR_ALWAYS_INLINE static void write_typed_array_element(std::span<char, sizeof(E)> const out,
                                                             E const value)
    {
        auto little = std::bit_cast<std::array<char, sizeof(E)>>(value);
        if constexpr (std::endian::native == std::endian::big)
            std::ranges::reverse(little);
        std::ranges::copy(little, out.begin());
    }

    static consteval bool tag_taken_by_standard_type(std::uint64_t const tag)
    {
        return tag == std::to_underlying(rfc8949::tag_number::standard_date_time_string) ||
               tag == std::to_underlying(rfc8949::tag_number::epoch_based_date_time) ||
               tag == std::to_underlying(rfc8949::tag_number::unsigned_bignum) ||
               tag == std::to_underlying(rfc8949::tag_number::negative_bignum) ||
               tag == std::to_underlying(rfc8949::tag_number::encoded_cbor_data_item) ||
               tag == std::to_underlying(rfc8943::tag_number::days_since_epoch_date) ||
               tag == std::to_underlying(rfc8943::tag_number::full_date_string) ||
               tag == std::to_underlying(cbor_sets::tag_number::set) ||
               (tag >= std::to_underlying(rfc8746::tag_number::typed_array_first) &&
                tag <= std::to_underlying(rfc8746::tag_number::typed_array_last)) ||
               tag == std::to_underlying(validity::tag_number::fixed_array);
    }

    static consteval bool tag_invalid(std::uint64_t const tag)
    {
        return tag == std::numeric_limits<std::uint16_t>::max() || tag == std::numeric_limits<std::uint32_t>::max() ||
               tag == std::numeric_limits<std::uint64_t>::max() ||
               tag == std::to_underlying(notable_tags::tag_number::programming_aid_for_simple_values);
    }

    template <class Root>
    static consteval bool descriptions_valid()
    {
        constexpr auto check = []<class U>(std::type_identity<U>) {
            static_assert(has_describe_members<U>,
                          "cbor::schema takes each struct through cbor::describe_members<T>.");
            if constexpr (has_describe_members<U>) {
                static_assert(description_valid<U>());
                static_assert(base_described<U>(),
                              "cbor::schema takes a class with a base class only through "
                              "CBOR_DEFINE_DERIVED_TYPE_MEMBERS or describe_members<T>::base, which names its one "
                              "direct base class.");
#if !defined(__cpp_impl_reflection) && defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
                if constexpr (std::is_aggregate_v<U> && !describes_array<U>() && !has_described_base<U>)
                    static_assert(boost::pfr::tuple_size_v<U> == describe::members_count<U>,
                                  "cbor::schema lists each member of an aggregate; Boost.PFR counts another "
                                  "number of members.");
#endif
                if constexpr (tag_declared<U>())
                    static_assert(describe_members<U>::tag.has_value(),
                                  "cbor::schema takes a struct with a tag; cbor::untagged is for cbor::structs.");
                static_assert(std::apply([](auto const &...m) { return ((m.key.major == major_type::text_string) && ...); },
                                         describe_members<U>::members),
                              "cbor::schema takes a text key in cbor::describe_members; an integer key is for "
                              "cbor::structs.");
                static_assert(decode_form_valid<U>(),
                              "cbor::schema decodes a class through its default constructor, or through a "
                              "constructor that takes the described members in the order of the description, each "
                              "of a type with a default constructor.");
                static_assert(variant_members_valid<U>());
            }
        };
        if constexpr (is_struct<std::remove_cv_t<Root>>)
            std::apply([&](auto... type) { (check(type), ...); }, packing_table_of<Root>{});
        static_assert(tags_registered<Root>(), "cbor::schema takes one tag for one struct only.");
        return true;
    }

    template <class U>
    static consteval bool base_described()
    {
        if constexpr (has_described_base<U>) {
            using B = typename describe_members<U>::base;
            if constexpr (!std::is_class_v<B> || std::is_same_v<B, U> || !std::is_base_of_v<B, U>) {
                return false;
            } else {
#ifdef __cpp_impl_reflection
                constexpr auto bases =
                    std::define_static_array(std::meta::bases_of(^^U, std::meta::access_context::unprivileged()));
                return bases.size() == 1 && std::meta::dealias(std::meta::type_of(bases[0])) == std::meta::dealias(^^B);
#else
                return true;
#endif
            }
        } else {
#ifdef __cpp_impl_reflection
            return std::meta::bases_of(^^U, std::meta::access_context::unprivileged()).empty();
#else
            return true;
#endif
        }
    }

    template <class M>
    static consteval bool holds_variant()
    {
        using U = std::remove_cv_t<M>;
        if constexpr (detail::is_std_variant<U>)
            return true;
        else if constexpr (requires { fixed_length<U>::value; })
            return holds_variant<typename fixed_length<U>::element>();
        else if constexpr (is_struct<U>)
            return false;
        else if constexpr (is_optional<U>)
            return holds_variant<typename U::value_type>();
        else if constexpr (is_map<U>)
            return holds_variant<typename U::key_type>() || holds_variant<typename U::mapped_type>();
        else if constexpr (std::ranges::sized_range<U>)
            return holds_variant<std::ranges::range_value_t<U>>();
        else
            return false;
    }

    template <class A, class First>
    static consteval bool base_shared()
    {
        if constexpr (has_described_base<A> && has_described_base<First>)
            return std::is_same_v<typename describe_members<A>::base, typename describe_members<First>::base>;
        else
            return false;
    }

    template <class M>
    static consteval bool variant_member_valid()
    {
        using U = std::remove_cv_t<M>;
        if constexpr (detail::is_std_variant<U>) {
            [[maybe_unused]] constexpr auto alternatives = []<std::size_t... K>(std::index_sequence<K...>) {
                return std::tuple<std::type_identity<std::variant_alternative_t<K, U>>...>{};
            }(std::make_index_sequence<std::variant_size_v<U>>{});
            constexpr bool monostate_absent = !contains<std::monostate>(decltype(alternatives){});
            static_assert(monostate_absent, "cbor::schema takes no std::monostate in a std::variant.");
            constexpr bool described = []<std::size_t... K>(std::index_sequence<K...>) {
                return (has_describe_members<std::remove_cv_t<std::variant_alternative_t<K, U>>> && ...);
            }(std::make_index_sequence<std::variant_size_v<U>>{});
            static_assert(!monostate_absent || described,
                          "cbor::schema takes a std::variant of classes with cbor::describe_members only.");
            constexpr bool unique = []<std::size_t... K>(std::index_sequence<K...>) {
                return ((index_in<std::variant_alternative_t<K, U>>(decltype(alternatives){}) == K) && ...);
            }(std::make_index_sequence<std::variant_size_v<U>>{});
            static_assert(unique, "cbor::schema takes each alternative of a std::variant once.");
            if constexpr (monostate_absent && described) {
                constexpr bool shared = []<std::size_t... K>(std::index_sequence<K...>) {
                    return (base_shared<std::variant_alternative_t<K, U>, std::variant_alternative_t<0, U>>() && ...);
                }(std::make_index_sequence<std::variant_size_v<U>>{});
                static_assert(shared, "cbor::schema takes a std::variant of classes that name one and the same direct base "
                                      "class through CBOR_DEFINE_DERIVED_TYPE_MEMBERS or describe_members<T>::base.");
            }
        } else {
            static_assert(!holds_variant<U>(),
                          "cbor::schema takes a std::variant as a member of a described class only; a list, a map, an "
                          "array or a std::optional of a std::variant is not taken.");
        }
        return true;
    }

    template <class U>
    static consteval bool variant_members_valid()
    {
        return []<std::size_t... I>(std::index_sequence<I...>) {
            return (variant_member_valid<describe::member_t<U, I>>() && ...);
        }(std::make_index_sequence<describe::members_count<U>>{});
    }

    template <class U>
    static consteval bool tag_declared()
    {
        return requires {
            { describe_members<U>::tag } -> std::same_as<std::optional<cbor::tag> const &>;
        };
    }

    template <class U>
    static consteval std::optional<std::uint64_t> tag_number_of()
    {
        if constexpr (has_describe_members<U>) {
            if constexpr (tag_declared<U>())
                if (describe_members<U>::tag.has_value())
                    return describe_members<U>::tag->number;
        }
        return std::nullopt;
    }

    template <class M, class B>
    static std::type_identity<B> class_of(M B::*);

    template <class U, class P>
    static consteval bool member_of()
    {
        using C = typename decltype(class_of(P{}))::type;
        return std::is_base_of_v<C, U> && requires(U &u, P p) { u.*p; };
    }

    template <class U>
    static consteval bool members_of_class()
    {
        return std::apply([](auto const &...m) { return (member_of<U, decltype(m.pointer)>() && ...); },
                          describe_members<U>::members);
    }

    template <class U>
    static consteval bool describes_array()
    {
        if constexpr (has_describe_members<U>)
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return (std::is_array_v<describe::member_t<U, I>> || ...);
            }(std::make_index_sequence<describe::members_count<U>>{});
        else
            return false;
    }

    template <class U>
    static consteval bool decode_form_valid()
    {
        if constexpr (std::is_default_constructible_v<U>)
            return true;
        else if constexpr (std::is_aggregate_v<U>)
            return false;
        else
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return (std::is_default_constructible_v<describe::member_t<U, I>> && ...) &&
                       std::is_constructible_v<U, describe::member_t<U, I>...>;
            }(std::make_index_sequence<describe::members_count<U>>{});
    }

    template <class U, std::size_t... I>
    static std::type_identity<std::tuple<describe::member_t<U, I>...>> arguments_type(std::index_sequence<I...>);

    template <class U>
    using arguments_of = typename decltype(arguments_type<U>(std::make_index_sequence<describe::members_count<U>>{}))::type;

    template <class U>
    static constexpr arguments_of<U> make_arguments()
    {
        return arguments_of<U>{};
    }

    template <class U, class Container>
    static constexpr arguments_of<U> make_arguments(Container const &container)
    {
        if constexpr (requires { container.get_allocator(); })
            return arguments_of<U>(std::allocator_arg, container.get_allocator());
        else
            return arguments_of<U>{};
    }

    template <class U, class P>
    static consteval bool converts_to_member_of()
    {
        return requires(typename decltype(class_of(P{}))::type *b) { static_cast<U *>(b); };
    }

    template <class U, std::size_t J, std::size_t L>
    static consteval bool pointers_differ()
    {
        constexpr auto a = std::get<J>(describe_members<U>::members).pointer;
        constexpr auto b = std::get<L>(describe_members<U>::members).pointer;
        using PA = std::remove_cvref_t<decltype(a)>;
        using PB = std::remove_cvref_t<decltype(b)>;
        using MA = std::remove_reference_t<decltype(std::declval<U &>().*a)>;
        using MB = std::remove_reference_t<decltype(std::declval<U &>().*b)>;
        if constexpr (J == L || !std::is_same_v<MA, MB>)
            return true;
        else if constexpr (converts_to_member_of<U, PA>() && converts_to_member_of<U, PB>())
            return static_cast<MA U::*>(a) != static_cast<MB U::*>(b);
        else if constexpr (std::is_same_v<PA, PB>)
            return a != b;
        else
            return true;
    }

    template <class U, std::size_t J, std::size_t... L>
    static consteval bool pointer_differs_from_all(std::index_sequence<L...>)
    {
        return (pointers_differ<U, J, L>() && ...);
    }

    template <class U>
    static consteval bool pointers_unique()
    {
        constexpr std::size_t n = std::tuple_size_v<std::remove_cvref_t<decltype(describe_members<U>::members)>>;
        return []<std::size_t... J>(std::index_sequence<J...>) {
            return (pointer_differs_from_all<U, J>(std::make_index_sequence<n>{}) && ...);
        }(std::make_index_sequence<n>{});
    }

    static consteval auto key_encoding_of(map_key const key)
    {
        return std::tuple(heads::initial_byte(key.major, heads::preferred_argument_info(key.argument)),
                          key.argument, key.text);
    }

    static consteval bool key_encoding_less(map_key const a, map_key const b)
    {
        return key_encoding_of(a) < key_encoding_of(b);
    }

    template <class... P>
    static consteval bool keys_unique(std::tuple<member<P>...> const &members)
    {
        std::vector<map_key> keys;
        std::apply([&](auto const &...m) { (keys.push_back(m.key), ...); }, members);
        std::ranges::sort(keys, key_encoding_less);
        return std::ranges::adjacent_find(keys, {}, key_encoding_of) == std::ranges::end(keys);
    }

    template <std::size_t N>
    static consteval std::array<std::size_t, N> keys_sorted(std::array<map_key, N> const &keys)
    {
        std::array<std::size_t, N> order;
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::sort(order, [&](std::size_t const a, std::size_t const b) { return key_encoding_less(keys[a], keys[b]); });
        return order;
    }

    template <class U>
    static constexpr std::array<std::size_t, describe::members_count<U>> key_order =
        keys_sorted([]<std::size_t... I>(std::index_sequence<I...>) {
            return std::array<map_key, sizeof...(I)>{describe::key_of<U, I>...};
        }(std::make_index_sequence<describe::members_count<U>>{}));

    template <class U, class F>
    CBOR_ALWAYS_INLINE static constexpr void for_each_field(F &&f)
    {
        [&]<std::size_t... K>(std::index_sequence<K...>) {
            (f(std::integral_constant<std::size_t, key_order<U>[K]>{}), ...);
        }(std::make_index_sequence<describe::members_count<U>>{});
    }

    template <class U, class P>
    static consteval std::size_t member_index_of(P const pointer)
    {
        std::size_t found = describe::members_count<U>;
        for_each_field<U>([&](auto i) {
            constexpr auto listed = std::get<decltype(i)::value>(describe_members<U>::members).pointer;
            if constexpr (std::is_same_v<std::remove_cv_t<decltype(listed)>, P>)
                if (listed == pointer)
                    found = decltype(i)::value;
        });
        return found;
    }

    template <class T>
    static consteval bool annotations_absent()
    {
#ifdef __cpp_impl_reflection
        std::meta::info const type = std::meta::dealias(^^T);
        if (annotated(type, ^^cbor::tag))
            return false;
        for (std::meta::info const m :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unprivileged()))
            if (annotated(m, ^^cbor::key))
                return false;
#endif
        return true;
    }

    template <class U>
    static consteval bool description_valid()
    {
        static_assert(tag_declared<U>(), "cbor::describe_members<T>::tag is a std::optional<cbor::tag>.");
        if constexpr (tag_declared<U>()) {
            static_assert(!describe_members<U>::tag.has_value() ||
                              !tag_taken_by_standard_type(describe_members<U>::tag->number),
                          "cbor::describe_members takes a tag that no standard type has.");
            static_assert(!describe_members<U>::tag.has_value() || !tag_invalid(describe_members<U>::tag->number),
                          "cbor::describe_members takes a tag that the IANA registry calls always invalid.");
        }
        static_assert(members_of_class<U>(), "cbor::describe_members lists a member of another class.");
        if constexpr (members_of_class<U>()) {
            static_assert(pointers_unique<U>(), "cbor::describe_members lists a member twice.");
            static_assert(reflected_members_listed<U>(),
                          "cbor::describe_members lists other members than reflection reads with unprivileged().");
        }
        static_assert(keys_unique(describe_members<U>::members), "cbor::describe_members gives two members the same key.");
        static_assert(annotations_absent<U>(),
                      "a type with cbor::describe_members takes its members, keys and tag from the description only.");
        return true;
    }

#ifdef __cpp_impl_reflection
    static consteval void add_public_members(std::meta::info const type, std::vector<std::meta::info> &out)
    {
        for (std::meta::info const b : std::meta::bases_of(type, std::meta::access_context::unprivileged()))
            add_public_members(std::meta::type_of(b), out);
        for (std::meta::info const m :
             std::meta::nonstatic_data_members_of(type, std::meta::access_context::unprivileged()))
            out.push_back(m);
    }

    template <class U>
    static consteval std::span<std::meta::info const> public_members()
    {
        std::vector<std::meta::info> out;
        add_public_members(std::meta::dealias(^^U), out);
        return std::define_static_array(out);
    }

    template <class U, std::meta::info R>
    static consteval bool listed_once()
    {
        return std::apply(
            [](auto const &...m) {
                auto const hit = [](auto const &e) {
                    if constexpr (std::is_same_v<decltype(e.pointer), decltype(&[:R:])>)
                        return e.pointer == &[:R:];
                    else
                        return false;
                };
                return (int{hit(m)} + ... + 0) == 1;
            },
            describe_members<U>::members);
    }
#endif

    template <class U>
    static consteval bool reflected_members_listed()
    {
#ifdef __cpp_impl_reflection
        static constexpr auto r = public_members<U>();
        constexpr std::size_t n = std::tuple_size_v<std::remove_cvref_t<decltype(describe_members<U>::members)>>;
        if constexpr (r.size() != n)
            return false;
        else
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return (listed_once<U, r[I]>() && ...);
            }(std::make_index_sequence<r.size()>{});
#else
        return true;
#endif
    }

#ifdef __cpp_impl_reflection
    static consteval std::string_view key_of(std::meta::info const r)
    {
        auto const name = std::meta::u8identifier_of(r);
        return std::define_static_string(std::string(name.begin(), name.end()));
    }

    static consteval bool annotated(std::meta::info const entity, std::meta::info const type)
    {
        return std::ranges::any_of(std::meta::annotations_of(entity), [type](std::meta::info const a) {
            return std::meta::remove_const(std::meta::type_of(a)) == type;
        });
    }

    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
        if constexpr (std::uses_allocator_v<E, Alloc> || !std::is_class_v<E> || !std::is_aggregate_v<E>) {
            return false;
        } else {
            if (!std::meta::bases_of(^^E, std::meta::access_context::unprivileged()).empty())
                return false;
            bool reached = false;
            template for (constexpr std::meta::info m :
                          std::define_static_array(std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::unprivileged()))) {
                using M = typename[:std::meta::type_of(m):];
                if constexpr (std::is_array_v<M> || std::is_reference_v<M>)
                    return false;
                else
                    reached = reached || std::uses_allocator_v<M, Alloc> || uses_allocator_aggregate<M, Alloc>();
            }
            return reached;
        }
    }

    template <class E, class Alloc>
    static constexpr E make_element(Alloc const &alloc, E &&from)
    {
        if constexpr (uses_allocator_aggregate<E, Alloc>()) {
            static constexpr auto members =
                std::define_static_array(std::meta::nonstatic_data_members_of(^^E, std::meta::access_context::unprivileged()));
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return E{make_element<typename[:std::meta::type_of(members[I]):]>(alloc, std::move(from.[:members[I]:]))...};
            }(std::make_index_sequence<members.size()>());
        } else {
            return std::make_obj_using_allocator<E>(alloc, std::move(from));
        }
    }

    template <class U>
    static consteval std::span<std::meta::info const> data_members()
    {
        std::vector<std::meta::info> members(std::from_range, std::meta::nonstatic_data_members_of(
                                                                  std::meta::dealias(^^U), std::meta::access_context::unprivileged()));
        std::ranges::sort(members, {}, [](std::meta::info const m) {
            std::string_view const key = key_of(m);
            return key_encoded(major_type::text_string, key.size(), key);
        });
        return std::define_static_array(members);
    }

    template <class U, std::meta::info M>
    static consteval std::size_t member_index_of()
    {
        return member_index_of<U>(&[:M:]);
    }
#elif defined(BOOST_PFR_ENABLED) && BOOST_PFR_ENABLED
    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
        if constexpr (std::uses_allocator_v<E, Alloc> || !std::is_class_v<E> || !std::is_aggregate_v<E> ||
                      std::ranges::range<E> || has_described_base<E>) {
            return false;
        } else if constexpr (describes_array<E>()) {
            return false;
        } else {
            return []<std::size_t... I>(std::index_sequence<I...>) {
                return ((!std::is_array_v<boost::pfr::tuple_element_t<I, E>> &&
                         !std::is_reference_v<boost::pfr::tuple_element_t<I, E>>) &&
                        ...) &&
                       (false || ... ||
                        (std::uses_allocator_v<boost::pfr::tuple_element_t<I, E>, Alloc> ||
                         uses_allocator_aggregate<boost::pfr::tuple_element_t<I, E>, Alloc>()));
            }(std::make_index_sequence<boost::pfr::tuple_size_v<E>>());
        }
    }

    template <class E, class Alloc>
    static constexpr E make_element(Alloc const &alloc, E &&from)
    {
        if constexpr (uses_allocator_aggregate<E, Alloc>()) {
            return [&]<std::size_t... I>(std::index_sequence<I...>) {
                return E{make_element<boost::pfr::tuple_element_t<I, E>>(alloc, std::move(boost::pfr::get<I>(from)))...};
            }(std::make_index_sequence<boost::pfr::tuple_size_v<E>>());
        } else {
            return std::make_obj_using_allocator<E>(alloc, std::move(from));
        }
    }
#else
    template <class E, class Alloc>
    static consteval bool uses_allocator_aggregate()
    {
        return false;
    }

    template <class E, class Alloc>
    static constexpr E make_element(Alloc const &alloc, E &&from)
    {
        return std::make_obj_using_allocator<E>(alloc, std::move(from));
    }
#endif

    template <class E, class Container>
    static constexpr E make_element(Container const &container)
    {
        if constexpr (requires { container.get_allocator(); }) {
            if constexpr (uses_allocator_aggregate<E, decltype(container.get_allocator())>())
                return make_element<E>(container.get_allocator(), E{});
            else
                return std::make_obj_using_allocator<E>(container.get_allocator());
        } else {
            return E{};
        }
    }

    template <class M>
    static consteval auto fixed_leaf_of()
    {
        if constexpr (std::same_as<M, bool>)
            return std::type_identity<std::uint8_t>{};
        else if constexpr (has_fixed_underlying_type<M>)
            return std::type_identity<std::underlying_type_t<M>>{};
        else
            return std::type_identity<M>{};
    }

    template <class M>
    using fixed_leaf_t = typename decltype(fixed_leaf_of<M>())::type;

    template <class M>
    static constexpr bool is_fixed_leaf = is_typed_array_element<fixed_leaf_t<M>>;

    template <class M>
    static consteval bool fixed_member_valid()
    {
        if constexpr (is_fixed_leaf<M>) {
            return true;
        } else if constexpr (requires { fixed_length<M>::value; }) {
            if constexpr (is_fixed_string<M>)
                return false;
            else
                return fixed_length<M>::value > 0 && is_fixed_leaf<std::remove_cv_t<typename fixed_length<M>::element>>;
        } else {
            return fixed_struct_valid<M>();
        }
    }

    template <class S>
    static consteval bool fixed_struct_valid()
    {
        if constexpr (!has_describe_members<S> || !std::is_default_constructible_v<S> || requires { fixed_length<S>::value; }) {
            return false;
        } else {
            bool valid = describe::members_count<S> > 0;
            for_each_field<S>([&](auto i) { valid = valid && fixed_member_valid<describe::member_t<S, decltype(i)::value>>(); });
            return valid;
        }
    }

    template <class U>
    static constexpr bool is_fixed_vector = requires {
        requires is_list<U> && !is_typed_array<U>;
        requires fixed_struct_valid<std::remove_cv_t<std::ranges::range_value_t<U>>>();
    };

    template <class U>
    static constexpr bool is_record_list = requires {
        requires is_list<U> && !is_typed_array<U> && !is_fixed_vector<U>;
        requires is_struct<std::remove_cv_t<std::ranges::range_value_t<U>>> &&
                     !is_list<std::remove_cv_t<std::ranges::range_value_t<U>>> &&
                     !is_map<std::remove_cv_t<std::ranges::range_value_t<U>>> &&
                     !is_optional<std::remove_cv_t<std::ranges::range_value_t<U>>>;
    };

    template <class M>
    static consteval std::size_t fixed_width()
    {
        if constexpr (is_fixed_leaf<M>) {
            return sizeof(fixed_leaf_t<M>);
        } else if constexpr (requires { fixed_length<M>::value; }) {
            return fixed_length<M>::value * sizeof(fixed_leaf_t<std::remove_cv_t<typename fixed_length<M>::element>>);
        } else {
            std::size_t width = 0;
            for_each_field<M>([&](auto i) { width += fixed_width<describe::member_t<M, decltype(i)::value>>(); });
            return width;
        }
    }

    template <class S, std::size_t Member>
    static consteval std::size_t fixed_offset()
    {
        std::size_t at = 0;
        bool before = true;
        for_each_field<S>([&](auto i) {
            before = before && decltype(i)::value != Member;
            if (before)
                at += fixed_width<describe::member_t<S, decltype(i)::value>>();
        });
        return at;
    }

    template <class S>
    static consteval bool fixed_layout_native()
    {
        if constexpr (std::endian::native != std::endian::little || !std::is_trivially_copyable_v<S> ||
                      sizeof(S) != fixed_width<S>()) {
            return false;
        } else {
            bool same = true;
            for_each_field<S>([&](auto i) {
                constexpr std::size_t I = decltype(i)::value;
                using M = std::remove_cv_t<describe::member_t<S, I>>;
                if constexpr (!std::is_trivially_copyable_v<M> || sizeof(M) != fixed_width<M>()) {
                    same = false;
                } else {
                    constexpr std::size_t at = fixed_offset<S, I>();
                    std::array<std::byte, sizeof(S)> bytes{};
                    std::ranges::fill(std::span(bytes).subspan(at, sizeof(M)), std::byte{1});
                    auto const probe = std::bit_cast<S>(bytes);
                    auto const read = std::bit_cast<std::array<std::byte, sizeof(M)>>(describe::get<I>(probe));
                    same = same && std::ranges::all_of(read, [](std::byte const b) { return b == std::byte{1}; });
                    if constexpr (fixed_struct_valid<M>())
                        same = same && fixed_layout_native<M>();
                }
            });
            return same;
        }
    }

    template <class S>
    static consteval void encode_description(std::vector<char> &encoded)
    {
        heads::append_head(encoded, major_type::array, 2);
        heads::append_head(encoded, major_type::unsigned_integer, *tag_number_of<S>());
        heads::append_head(encoded, major_type::array, describe::members_count<S>);
        for_each_field<S>([&](auto i) {
            using M = describe::member_t<S, decltype(i)::value>;
            constexpr std::string_view key = describe::key_of<S, decltype(i)::value>.text;
            constexpr bool counted = !is_fixed_leaf<M> && requires { fixed_length<M>::value; };
            heads::append_head(encoded, major_type::array, counted ? 3 : 2);
            heads::append_head(encoded, major_type::text_string, key.size());
            encoded.insert(encoded.end(), key.begin(), key.end());
            if constexpr (is_fixed_leaf<M>) {
                heads::append_head(encoded, major_type::unsigned_integer, typed_array_tag<fixed_leaf_t<M>>());
            } else if constexpr (counted) {
                using L = fixed_leaf_t<std::remove_cv_t<typename fixed_length<M>::element>>;
                heads::append_head(encoded, major_type::unsigned_integer, typed_array_tag<L>());
                heads::append_head(encoded, major_type::unsigned_integer, fixed_length<M>::value);
            } else {
                encode_description<M>(encoded);
            }
        });
    }

    template <class U, std::size_t K, class Lists>
    static consteval auto collect_description_members(Lists const lists)
    {
        if constexpr (K == describe::members_count<U>)
            return lists;
        else
            return collect_description_members<U, K + 1>(
                collect_description_types<describe::member_t<U, key_order<U>[K]>>(lists));
    }

    template <class V, std::size_t K, class Lists>
    static consteval auto collect_description_alternatives(Lists const lists)
    {
        if constexpr (K == std::variant_size_v<V>)
            return lists;
        else
            return collect_description_alternatives<V, K + 1>(collect_description_types<std::variant_alternative_t<K, V>>(lists));
    }

    template <class T, class Visited, class Types>
    static consteval auto collect_description_types(std::pair<Visited, Types> const lists)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (detail::is_std_variant<U>) {
            return collect_description_alternatives<U, 0>(lists);
        } else if constexpr (requires { fixed_length<U>::value; }) {
            return collect_description_types<typename fixed_length<U>::element>(lists);
        } else if constexpr (has_describe_members<U>) {
            if constexpr (contains<U>(Visited{}))
                return lists;
            else
                return collect_description_members<U, 0>(
                    std::pair(std::tuple_cat(lists.first, std::tuple<std::type_identity<U>>{}), lists.second));
        } else if constexpr (is_inline_optional<U> || is_optional<U>) {
            return collect_description_types<typename U::value_type>(lists);
        } else if constexpr (is_map<U>) {
            return collect_description_types<typename U::mapped_type>(collect_description_types<typename U::key_type>(lists));
        } else if constexpr (is_fixed_vector<U>) {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            if constexpr (contains<E>(Types{}))
                return lists;
            else
                return std::pair(lists.first, std::tuple_cat(lists.second, std::tuple<std::type_identity<E>>{}));
        } else if constexpr (std::ranges::sized_range<U>) {
            return collect_description_types<std::ranges::range_value_t<U>>(lists);
        } else {
            return lists;
        }
    }

    template <class Root>
    using description_types_of = decltype(collect_description_types<Root>(std::pair<std::tuple<>, std::tuple<>>{}).second);

    template <class Root>
    friend consteval bool cbor::tags_registered();

    template <class T, class Root>
    static consteval std::size_t fixed_size();

    template <class T, std::size_t Member, class Root>
    static consteval std::size_t member_offset();

    static constexpr std::size_t inline_optional_head = 2;

    static constexpr std::size_t typed_array_head = heads::initial_byte_size + sizeof(std::uint8_t) + item_head;


    static constexpr void encode_fixed_width_head(std::vector<char> &encoded, major_type const major, std::size_t const width)
    {
        heads::append_head(
            encoded, major,
            static_cast<std::uint8_t>(std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                                      std::countr_zero(width)),
            0);
    }

    template <class Root, class T>
    static consteval void encode_zero_initialized(std::vector<char> &encoded)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            heads::append_head(encoded, major_type::simple_float, std::to_underlying(simple_value::false_value));
        } else if constexpr (std::is_enum_v<U>) {
            encode_zero_initialized<Root, std::underlying_type_t<U>>(encoded);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            heads::append_head(encoded, major_type::tag,
                               std::to_underlying(rfc8949::tag_number::unsigned_bignum));
            heads::append_head(encoded, major_type::byte_string, sizeof(U));
            encoded.resize(encoded.size() + sizeof(U));
#endif
        } else if constexpr (std::is_integral_v<U>) {
            encode_fixed_width_head(encoded, major_type::unsigned_integer, sizeof(U));
        } else if constexpr (std::is_floating_point_v<U>) {
            constexpr int digits = std::numeric_limits<U>::digits;
#if defined(__STDCPP_FLOAT16_T__)
            if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
                encode_fixed_width_head(encoded, major_type::simple_float, sizeof(std::float16_t));
            else
#endif
            if constexpr (
#if defined(__STDCPP_BFLOAT16_T__)
                digits == std::numeric_limits<std::bfloat16_t>::digits ||
#endif
                digits == std::numeric_limits<float>::digits)
                encode_fixed_width_head(encoded, major_type::simple_float, sizeof(float));
            else if constexpr (digits == std::numeric_limits<double>::digits)
                encode_fixed_width_head(encoded, major_type::simple_float, sizeof(double));
            else {
                heads::append_head(encoded, major_type::tag,
                                   std::to_underlying(rfc8746::tag_number::float128_big_endian));
                heads::append_head(encoded, major_type::byte_string, sizeof(uint128));
                encoded.resize(encoded.size() + sizeof(uint128));
            }
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t>) {
                heads::append_head(encoded, major_type::text_string, n);
                encoded.resize(encoded.size() + n);
            } else if constexpr (std::same_as<E, unsigned char> || std::same_as<E, std::byte>) {
                heads::append_head(encoded, major_type::byte_string, n);
                encoded.resize(encoded.size() + n);
            } else {
                heads::append_head(encoded, major_type::array, n);
                for (std::size_t i = 0; i < n; ++i)
                    encode_zero_initialized<Root, E>(encoded);
            }
        } else if constexpr (has_describe_members<U>) {
            heads::append_head(encoded, major_type::tag, *tag_number_of<U>());
            encode_straight_reference<Root, U>(encoded);
            encode_fixed_width_head(encoded, major_type::array, sizeof(std::uint32_t));
            auto const count = heads::big_endian(static_cast<std::uint32_t>(describe::members_count<U>));
            std::ranges::copy(count, std::span(encoded).template last<sizeof(std::uint32_t)>().begin());
            for_each_field<U>([&](auto i) { encode_zero_initialized<Root, describe::member_t<U, decltype(i)::value>>(encoded); });
        } else if constexpr (is_inline_optional<U>) {
            heads::append_head(encoded, major_type::array, 2);
            heads::append_head(encoded, major_type::simple_float, std::to_underlying(simple_value::false_value));
            encode_zero_initialized<Root, typename U::value_type>(encoded);
        } else {
            heads::append_head(encoded, major_type::tag, std::to_underlying(packed_cbor::tag_number::reference));
            encode_fixed_width_head(encoded, major_type::unsigned_integer, sizeof(std::uint32_t));
        }
    }

    template <class Root, class T>
    static consteval std::span<char const> zero_initialized()
    {
        return static_bytes<[]() consteval {
            std::vector<char> encoded;
            encode_zero_initialized<Root, T>(encoded);
            return encoded;
        }>;
    }

    template <class U>
    static consteval std::span<char const> record_keys()
    {
        return static_bytes<[]() consteval {
            std::vector<char> encoded;
            heads::append_head(encoded, major_type::tag, std::to_underlying(packed_cbor::tag_number::record_function));
            heads::append_head(encoded, major_type::array, describe::members_count<U>);
            for_each_field<U>([&](auto i) {
                constexpr std::string_view key = describe::key_of<U, decltype(i)::value>.text;
                heads::append_head(encoded, major_type::text_string, key.size());
                encoded.insert(encoded.end(), key.begin(), key.end());
            });
            return encoded;
        }>;
    }

    template <class U, std::size_t K, class Types>
    static consteval auto collect_record_members(Types const types)
    {
        if constexpr (K == describe::members_count<U>)
            return types;
        else
            return collect_record_members<U, K + 1>(collect_record_types<describe::member_t<U, key_order<U>[K]>>(types));
    }

    template <class V, std::size_t K, class Types>
    static consteval auto collect_record_alternatives(Types const types)
    {
        if constexpr (K == std::variant_size_v<V>)
            return types;
        else
            return collect_record_alternatives<V, K + 1>(collect_record_types<std::variant_alternative_t<K, V>>(types));
    }

    template <class T, class Types>
    static consteval auto collect_record_types(Types const types)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (detail::is_std_variant<U>) {
            return collect_record_alternatives<U, 0>(types);
        } else if constexpr (requires { fixed_length<U>::value; }) {
            return collect_record_types<typename fixed_length<U>::element>(types);
        } else if constexpr (is_struct<U>) {
            if constexpr (contains<U>(Types{}))
                return types;
            else if constexpr (has_describe_members<U>)
                return collect_record_members<U, 0>(std::tuple_cat(types, std::tuple<std::type_identity<U>>{}));
            else
                return std::tuple_cat(types, std::tuple<std::type_identity<U>>{});
        } else if constexpr (is_inline_optional<U> || is_optional<U>) {
            return collect_record_types<typename U::value_type>(types);
        } else if constexpr (is_map<U>) {
            return collect_record_types<typename U::mapped_type>(collect_record_types<typename U::key_type>(types));
        } else if constexpr (std::ranges::sized_range<U>) {
            return collect_record_types<std::ranges::range_value_t<U>>(types);
        } else {
            return types;
        }
    }

    template <class Root>
    using packing_table_of = decltype(collect_record_types<Root>(std::tuple<>{}));

    template <class Root>
    static consteval std::size_t shared_first_of()
    {
        return std::max(shared_first, std::tuple_size_v<packing_table_of<Root>> + std::tuple_size_v<description_types_of<Root>> + 1);
    }

    template <class Root>
    static consteval std::size_t fillers_of()
    {
        return shared_first_of<Root>() - 1 - std::tuple_size_v<packing_table_of<Root>> - std::tuple_size_v<description_types_of<Root>>;
    }

    template <class Root, class S>
    static consteval std::span<char const> fixed_array_head()
    {
        return static_bytes<[]() consteval {
            std::vector<char> encoded;
            heads::append_head(encoded, major_type::tag, std::to_underlying(validity::tag_number::fixed_array));
            heads::append_head(encoded, major_type::array, 2);
            std::size_t const j = std::tuple_size_v<packing_table_of<Root>> + index_in<S>(description_types_of<Root>{});
            if (j < shared_first) {
                heads::append_head(encoded, major_type::simple_float, j);
            } else {
                std::size_t const m = j - shared_first;
                heads::append_head(encoded, major_type::tag, std::to_underlying(packed_cbor::tag_number::reference));
                heads::append_head(encoded, static_cast<major_type>(m & 1), m >> 1);
            }
            encoded.push_back(heads::initial_byte(major_type::byte_string,
                                                  std::to_underlying(rfc8949::additional_information::four_byte_argument)));
            return encoded;
        }>;
    }

    static constexpr std::uint64_t straight_argument_count =
        std::to_underlying(packed_cbor::tag_number::straight_argument_last) -
        std::to_underlying(packed_cbor::tag_number::straight_argument_first) + 1;

    template <class Root, class U>
    static consteval std::uint64_t argument_index_of()
    {
        return index_in<std::remove_cv_t<U>>(packing_table_of<Root>{});
    }

    template <class Root, class U>
    static consteval void encode_straight_reference(std::vector<char> &encoded)
    {
        constexpr std::uint64_t i = argument_index_of<Root, U>();
        if constexpr (i < straight_argument_count) {
            heads::append_head(encoded, major_type::tag,
                               std::to_underlying(packed_cbor::tag_number::straight_argument_first) + i);
        } else {
            heads::append_head(encoded, major_type::tag, std::to_underlying(packed_cbor::tag_number::reference));
            heads::append_head(encoded, major_type::array, 2);
            heads::append_head(encoded, major_type::unsigned_integer, i - straight_argument_count);
        }
    }

    template <class Root, class U>
    static consteval std::size_t straight_reference_size()
    {
        std::vector<char> encoded;
        encode_straight_reference<Root, U>(encoded);
        return encoded.size();
    }

    template <class Root>
    static consteval std::span<char const> description_of()
    {
        return static_bytes<[]() consteval {
            std::vector<char> encoded;
            std::apply(
                [&]<class... U>(std::type_identity<U>...) {
                    (encoded.insert(encoded.end(), record_keys<U>().begin(), record_keys<U>().end()), ...);
                },
                packing_table_of<Root>{});
            std::apply([&]<class... U>(std::type_identity<U>...) { (encode_description<U>(encoded), ...); },
                       description_types_of<Root>{});
            return encoded;
        }>;
    }

    template <class Root, description_place D>
    static consteval std::span<char const> packing_prefix_of()
    {
        return static_bytes<[]() consteval {
            std::vector<char> encoded;
            heads::append_head(encoded, major_type::tag, std::to_underlying(packed_cbor::tag_number::basic_packed_cbor));
            heads::append_head(encoded, major_type::array, 2);
            encode_fixed_width_head(encoded, major_type::array, sizeof(std::uint32_t));
            if constexpr (D == description_place::inside) {
                constexpr auto description = description_of<Root>();
                encoded.insert(encoded.end(), description.begin(), description.end());
            }
            return encoded;
        }>;
    }

    template <class Root, description_place D>
    static consteval std::size_t table_first_of()
    {
        if constexpr (D == description_place::inside)
            return shared_first_of<Root>();
        else
            return shared_first_of<Root>() - std::tuple_size_v<packing_table_of<Root>> - std::tuple_size_v<description_types_of<Root>>;
    }

    template <class U>
    static constexpr auto float_bits(U const value)
    {
        constexpr int digits = std::numeric_limits<U>::digits;
#if defined(__STDCPP_FLOAT16_T__)
        if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
            return std::bit_cast<std::uint16_t>(value);
        else
#endif
#if defined(__STDCPP_BFLOAT16_T__)
        if constexpr (digits == std::numeric_limits<std::bfloat16_t>::digits)
            return std::uint32_t{std::bit_cast<std::uint16_t>(value)} << std::numeric_limits<std::uint16_t>::digits;
        else
#endif
        if constexpr (digits == std::numeric_limits<float>::digits)
            return std::bit_cast<std::uint32_t>(value);
        else if constexpr (digits == std::numeric_limits<double>::digits)
            return std::bit_cast<std::uint64_t>(value);
#if defined(__SIZEOF_INT128__) && defined(__STDCPP_FLOAT128_T__)
        else
            return std::bit_cast<uint128>(static_cast<std::float128_t>(value));
#endif
    }

    template <class Root>
    struct second_item {
        std::size_t items = 0;
        std::size_t bytes = 0;
        bool overflow = false;

        void add_bytes(std::size_t const n)
        {
            auto const sum = validity::checked_add(bytes, n);
            overflow |= !sum;
            bytes = sum.value_or(bytes);
        }

        void add_block(std::size_t const count, std::size_t const size)
        {
            auto const block = validity::checked_mul(count, size);
            overflow |= !block;
            add_bytes(block.value_or(0));
        }

        template <class E, class R>
        void add_elements(R const &range)
        {
            if constexpr (fixed_struct_valid<std::remove_cv_t<E>>()) {
                items += 1;
                add_bytes(fixed_array_head<Root, std::remove_cv_t<E>>().size() + sizeof(std::uint32_t));
                add_block(std::ranges::size(range), fixed_width<std::remove_cv_t<E>>());
                return;
            } else if constexpr (is_typed_array_element<std::remove_cv_t<E>>) {
                items += 1;
                add_bytes(typed_array_head);
                add_block(std::ranges::size(range), sizeof(E));
                return;
            }
            items += 1;
            add_bytes(item_head);
            add_block(std::ranges::size(range), fixed_size<E, Root>());
            if constexpr (!std::is_arithmetic_v<E> && !std::is_enum_v<E>)
                for (auto const &e : range)
                    add<E>(e);
        }

        template <class T>
        void add(T const &value)
        {
            using U = std::remove_cv_t<T>;
            if constexpr (std::is_arithmetic_v<U> || std::is_enum_v<U> || is_wide_integer<U>) {
            } else if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                if constexpr (!std::is_arithmetic_v<E> && !std::same_as<E, std::byte>)
                    for (auto const &e : value)
                        add<E>(e);
            } else if constexpr (has_describe_members<U>) {
                for_each_field<U>([&] CBOR_ALWAYS_INLINE(auto i) {
                    add<describe::member_t<U, decltype(i)::value>>(describe::get<decltype(i)::value>(value));
                });
            } else if constexpr (detail::is_std_variant<U>) {
                validity::throw_logic_error_if(value.valueless_by_exception(),
                                               "cbor::schema::encode: the std::variant holds no value");
                items += 1;
                std::visit(
                    [&]<class A>(A const &held) {
                        add_bytes(fixed_size<A, Root>());
                        add<A>(held);
                    },
                    value);
            } else if constexpr (is_inline_optional<U>) {
                if (value.has_value())
                    add<typename U::value_type>(*value);
            } else if constexpr (is_optional<U>) {
                items += 1;
                add_bytes(item_head);
                if (value.has_value()) {
                    add_bytes(fixed_size<typename U::value_type, Root>());
                    add<typename U::value_type>(*value);
                }
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                items += 1;
                add_bytes(item_head + std::ranges::size(value));
            } else if constexpr (is_map<U>) {
                items += 1;
                add_bytes(item_head);
                add_block(std::ranges::size(value),
                          fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>());
                for (auto const &[k, v] : value) {
                    add<typename U::key_type>(k);
                    add<typename U::mapped_type>(v);
                }
            } else {
                add_elements<std::ranges::range_value_t<U>>(value);
            }
        }
    };

    template <class Root, class E>
    CBOR_ALWAYS_INLINE static void copy_zero_initialized(std::span<char, fixed_size<E, Root>()> const field)
    {
        constexpr std::size_t n = fixed_size<E, Root>();
        static_assert(zero_initialized<Root, E>().size() >= n, "The zero-initialized encoding must hold the whole field.");
        std::span<char const, n> const from = zero_initialized<Root, E>().template first<n>();
        std::copy(from.begin(), from.end(), field.begin());
    }

    struct encode_cursor {
        std::size_t position;
        std::size_t index;
    };

    template <class Root, description_place D>
    static consteval std::size_t directory_at()
    {
        return packing_prefix_of<Root, D>().size() + item_head;
    }

    template <class Root, class E, bool Exact, description_place D, class R>
    CBOR_ALWAYS_INLINE static encode_cursor encode_elements(std::span<char> const out, std::size_t const data, R const &range,
                                       encode_cursor position)
    {
        constexpr std::size_t size = fixed_size<E, Root>();
        std::size_t at = data;
        for (auto const &e : range) {
            auto const field = out.subspan(at).template first<size>();
            copy_zero_initialized<Root, E>(field);
            position = encode_value<Root, E, Exact, D>(out, field, e, position);
            at += size;
        }
        return position;
    }

    template <class Root, bool Exact, description_place D, class V>
    CBOR_ALWAYS_INLINE static encode_cursor encode_reference(std::span<char> const out, std::span<char, dynamic_type_sizes> const field,
                                        V const &value, encode_cursor c)
    {
        using U = std::remove_cv_t<V>;
        std::size_t const j = c.index++;
        std::size_t const item = c.position;
        std::size_t const m = j + shared_first_of<Root>() - shared_first;
        heads::write_u32(out, directory_at<Root, D>() + sizeof(std::uint32_t) * j, item);
        field[1] =
            heads::initial_byte(static_cast<major_type>(m & 1),
                                std::to_underlying(rfc8949::additional_information::four_byte_argument));
        auto const n = heads::big_endian(static_cast<std::uint32_t>(m >> 1));
        std::ranges::copy(n, field.template last<sizeof(std::uint32_t)>().begin());
        std::size_t const data = item + item_head;
        if constexpr (detail::is_std_variant<U>) {
            std::visit(
                [&]<class A>(A const &held) {
                    auto const record = out.subspan(item).template first<fixed_size<A, Root>()>();
                    copy_zero_initialized<Root, A>(record);
                    c.position = item + fixed_size<A, Root>();
                    c = encode_value<Root, A, Exact, D>(out, record, held, c);
                },
                value);
        } else if constexpr (is_optional<U>) {
            using E = typename U::value_type;
            heads::write_item_head(out, item, major_type::array, value.has_value() ? 1 : 0);
            c.position = data;
            if (value.has_value()) {
                c.position += fixed_size<E, Root>();
                auto const element = out.subspan(data).template first<fixed_size<E, Root>()>();
                copy_zero_initialized<Root, E>(element);
                c = encode_value<Root, E, Exact, D>(out, element, *value, c);
            }
        } else if constexpr (is_text_range<U> || is_byte_range<U>) {
            std::size_t const length = std::ranges::size(value);
            heads::write_item_head(out, item, is_text_range<U> ? major_type::text_string : major_type::byte_string, length);
            auto const from = std::as_bytes(std::span(value));
            std::copy(from.begin(), from.end(), std::as_writable_bytes(out.subspan(data, length)).begin());
            c.position = data + length;
        } else if constexpr (is_map<U>) {
            using K = typename U::key_type;
            using M = typename U::mapped_type;
            std::size_t const length = std::ranges::size(value);
            heads::write_item_head(out, item, major_type::map, length);
            c.position = data + length * (fixed_size<K, Root>() + fixed_size<M, Root>());
            std::size_t at = data;
            for (auto const &[k, v] : value) {
                auto const key = out.subspan(at).template first<fixed_size<K, Root>()>();
                copy_zero_initialized<Root, K>(key);
                c = encode_value<Root, K, Exact, D>(out, key, k, c);
                at += fixed_size<K, Root>();
                auto const mapped = out.subspan(at).template first<fixed_size<M, Root>()>();
                copy_zero_initialized<Root, M>(mapped);
                c = encode_value<Root, M, Exact, D>(out, mapped, v, c);
                at += fixed_size<M, Root>();
            }
        } else if constexpr (is_typed_array<U>) {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            std::size_t const length = std::ranges::size(value);
            out[item] = heads::initial_byte(
                major_type::tag, std::to_underlying(rfc8949::additional_information::one_byte_argument));
            out[item + heads::initial_byte_size] = static_cast<char>(typed_array_tag<E>());
            heads::write_item_head(out, item + heads::initial_byte_size + sizeof(std::uint8_t), major_type::byte_string,
                                   length * sizeof(E));
            std::size_t const elements = item + typed_array_head;
            c.position = elements + length * sizeof(E);
            if constexpr (std::endian::native == std::endian::little && std::ranges::contiguous_range<U>) {
                auto const from = std::as_bytes(std::span(value));
                std::ranges::copy(from, std::as_writable_bytes(out.subspan(elements, from.size())).begin());
            } else {
                std::size_t at = elements;
                for (E const e : value) {
                    write_typed_array_element<E>(out.subspan(at).template first<sizeof(E)>(), e);
                    at += sizeof(E);
                }
            }
        } else if constexpr (is_fixed_vector<U>) {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            static constexpr auto head = fixed_array_head<Root, E>();
            constexpr std::size_t width = fixed_width<E>();
            std::size_t const length = std::ranges::size(value);
            std::ranges::copy(head, out.subspan(item).template first<head.size()>().begin());
            heads::write_u32(out, item + head.size(), length * width);
            std::size_t at = item + head.size() + sizeof(std::uint32_t);
            c.position = at + length * width;
            if constexpr (fixed_layout_native<E>() && std::ranges::contiguous_range<U>) {
                auto const from = std::as_bytes(std::span(value));
                std::ranges::copy(from, std::as_writable_bytes(out.subspan(at, from.size())).begin());
            } else {
                for (auto const &e : value) {
                    write_fixed_struct<E>(out.subspan(at).template first<width>(), e);
                    at += width;
                }
            }
        } else {
            using E = std::ranges::range_value_t<U>;
            std::size_t const length = std::ranges::size(value);
            heads::write_item_head(out, item, major_type::array, length);
            c.position = data + length * fixed_size<E, Root>();
            c = encode_elements<Root, E, Exact, D>(out, data, value, c);
        }
        return c;
    }

    template <class S>
    CBOR_ALWAYS_INLINE static void write_fixed_struct(std::span<char, fixed_width<S>()> const out, S const &value)
    {
        for_each_field<S>([&] CBOR_ALWAYS_INLINE(auto i) {
            constexpr std::size_t I = decltype(i)::value;
            using M = describe::member_t<S, I>;
            constexpr std::size_t at = fixed_offset<S, I>();
            if constexpr (is_fixed_leaf<M>) {
                using L = fixed_leaf_t<M>;
                write_typed_array_element<L>(out.template subspan<at, sizeof(L)>(), static_cast<L>(describe::get<I>(value)));
            } else if constexpr (requires { fixed_length<M>::value; }) {
                using L = fixed_leaf_t<std::remove_cv_t<typename fixed_length<M>::element>>;
                for (std::size_t k = 0; k < fixed_length<M>::value; ++k)
                    write_typed_array_element<L>(out.subspan(at + k * sizeof(L)).template first<sizeof(L)>(),
                                                 static_cast<L>(describe::get<I>(value)[k]));
            } else {
                write_fixed_struct<M>(out.template subspan<at, fixed_width<M>()>(), describe::get<I>(value));
            }
        });
    }

    template <class Root, class T, bool Exact, description_place D>
    CBOR_ALWAYS_INLINE static encode_cursor encode_value(std::span<char> const out, std::span<char, fixed_size<T, Root>()> const field,
                                    T const &value, encode_cursor position)
    {
        using U = std::remove_cv_t<T>;
        if constexpr (std::same_as<U, bool>) {
            field.front() = heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::false_value) + value);
        } else if constexpr (std::is_enum_v<U>) {
            position = encode_value<Root, std::underlying_type_t<U>, Exact, D>(out, field, std::to_underlying(value), position);
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            uint128 magnitude = static_cast<uint128>(value);
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = static_cast<uint128>(value >> std::numeric_limits<int128>::digits);
                field.front() = heads::initial_byte(major_type::tag,
                                                    std::to_underlying(rfc8949::tag_number::unsigned_bignum) +
                                                        static_cast<std::uint64_t>(sign & 1));
                magnitude ^= sign;
            }
            auto const bytes = heads::big_endian(magnitude);
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
#endif
        } else if constexpr (std::unsigned_integral<U>) {
            auto const bytes = heads::big_endian(value);
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(value >> std::numeric_limits<U>::digits);
            field.front() =
                heads::initial_byte(static_cast<major_type>(sign & 1),
                                    std::to_underlying(rfc8949::additional_information::one_byte_argument) +
                                        std::countr_zero(sizeof(U)));
            auto const bytes = heads::big_endian(static_cast<M>(static_cast<M>(value) ^ sign));
            std::copy(bytes.begin(), bytes.end(), field.template last<sizeof(U)>().begin());
        } else if constexpr (std::is_floating_point_v<U>) {
            auto const bits = heads::big_endian(float_bits(value));
            std::copy(bits.begin(), bits.end(), field.template last<bits.size()>().begin());
        } else if constexpr (requires { fixed_length<U>::value; }) {
            using E = typename fixed_length<U>::element;
            constexpr std::size_t n = fixed_length<U>::value;
            if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                          std::same_as<E, std::byte>) {
                auto const from = std::as_bytes(std::span(value));
                std::copy(from.begin(), from.end(), std::as_writable_bytes(field.template last<n>()).begin());
            } else {
                constexpr std::size_t head = heads::head_size(n);
                static_assert(validity::checked_mul(n, fixed_size<E, Root>())
                                      .and_then([](std::size_t const elements) {
                                          return validity::checked_add(head, elements);
                                      })
                                      .value_or(std::numeric_limits<std::size_t>::max()) <=
                                  fixed_size<U, Root>(),
                              "The elements must lie inside the field.");
                std::size_t at = head;
                for (auto const &e : value) {
                    position = encode_value<Root, E, Exact, D>(out, field.subspan(at).template first<fixed_size<E, Root>()>(), e, position);
                    at += fixed_size<E, Root>();
                }
            }
        } else if constexpr (has_describe_members<U>) {
            for_each_field<U>([&] CBOR_ALWAYS_INLINE(auto i) {
                constexpr std::size_t I = decltype(i)::value;
                using M = describe::member_t<U, I>;
                position = encode_value<Root, M, Exact, D>(
                    out, field.template subspan<member_offset<U, I, Root>(), fixed_size<M, Root>()>(), describe::get<I>(value), position);
            });
        } else if constexpr (is_inline_optional<U>) {
            if (value.has_value()) {
                using E = typename U::value_type;
                field.template subspan<1, 1>().front() =
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::true_value));
                position = encode_value<Root, E, Exact, D>(out, field.template subspan<inline_optional_head, fixed_size<E, Root>()>(), *value,
                                           position);
            }
        } else {
            position = encode_reference<Root, Exact, D>(out, field, value, position);
        }
        return position;
    }

    template <class U>
    static consteval std::size_t member_named(std::string_view const name)
    {
        std::size_t found = describe::members_count<U>;
        for_each_field<U>([&](auto i) {
            if (describe::key_of<U, decltype(i)::value>.text == name)
                found = decltype(i)::value;
        });
        return found;
    }

    static consteval std::size_t step_end(std::string_view const path, std::size_t const at)
    {
        return static_cast<std::size_t>(std::ranges::find_first_of(path | std::views::drop(at + 1), std::string_view(".[")) -
                                        path.begin());
    }

    static consteval std::size_t index_end(std::string_view const path, std::size_t const at)
    {
        return static_cast<std::size_t>(std::ranges::find(path | std::views::drop(at + 1), ']') - path.begin());
    }

    template <class T>
    static consteval std::size_t index_of(std::string_view const digits)
    {
        std::size_t value = 0;
        auto const [end, ec] = std::from_chars(digits.data(), std::to_address(digits.end()), value);
        if (ec != std::errc{} || end != std::to_address(digits.end())) [[unlikely]]
            return detail::no_fixed_size<T>();
        return value;
    }

    template <class T>
    static T read_fixed_value(std::span<char const, fixed_size<T, T>()> const field)
    {
        using U = std::remove_cv_t<T>;
        auto const head = static_cast<unsigned char>(field.front());
        if constexpr (std::same_as<U, bool>) {
            return (head & 1) != 0;
        } else if constexpr (has_fixed_underlying_type<U>) {
            return static_cast<U>(read_fixed_value<std::underlying_type_t<U>>(field));
#ifdef __SIZEOF_INT128__
        } else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>) {
            uint128 const magnitude = heads::read_unsigned128(field.template last<sizeof(U)>());
            if constexpr (std::same_as<U, int128>) {
                uint128 const sign = -static_cast<uint128>(head & 1);
                return static_cast<U>(magnitude ^ sign);
            } else {
                return magnitude;
            }
#endif
        } else if constexpr (std::unsigned_integral<U>) {
            return heads::read_unsigned<U>(field.template last<sizeof(U)>());
        } else if constexpr (std::signed_integral<U>) {
            using M = std::make_unsigned_t<U>;
            M const sign = static_cast<M>(-static_cast<M>((head >> rfc8949::additional_information_bits) & 1));
            return static_cast<U>(heads::read_unsigned<M>(field.template last<sizeof(M)>()) ^ sign);
        } else {
            using B = decltype(float_bits(U{}));
#if defined(__SIZEOF_INT128__) && defined(__STDCPP_FLOAT128_T__)
            if constexpr (std::same_as<B, uint128>) {
                auto const bits = heads::read_unsigned128(field.template last<sizeof(B)>());
                return static_cast<U>(std::bit_cast<std::float128_t>(bits));
            } else
#endif
            {
                auto const bits = heads::read_unsigned<B>(field.template last<sizeof(B)>());
#if defined(__STDCPP_FLOAT16_T__)
                using F = std::conditional_t<sizeof(B) == sizeof(std::uint16_t), std::float16_t,
                                             std::conditional_t<sizeof(B) == sizeof(std::uint32_t), float,
                                                                double>>;
#if defined(__STDCPP_BFLOAT16_T__)
                if constexpr (std::same_as<U, std::bfloat16_t>)
                    if (static_cast<std::uint16_t>(bits) == 0)
                        return std::bit_cast<std::bfloat16_t>(static_cast<std::uint16_t>(bits >> std::numeric_limits<std::uint16_t>::digits));
#endif
                return static_cast<U>(std::bit_cast<F>(bits));
#else
                if constexpr (sizeof(B) == sizeof(std::uint16_t))
                    return static_cast<U>(heads::float_decode_binary16(bits));
                else
                    return static_cast<U>(
                        std::bit_cast<std::conditional_t<sizeof(B) == sizeof(std::uint32_t), float,
                                                         double>>(bits));
#endif
            }
        }
    }

    template <class T>
    CBOR_ALWAYS_INLINE static bool fixed_head_valid(std::span<char const, fixed_size<T, T>()> const field)
    {
        using U = std::remove_cv_t<T>;
        auto const head = static_cast<unsigned char>(field.front());
        if constexpr (std::same_as<U, bool>) {
            return (head | 1) == static_cast<unsigned char>(heads::initial_byte(
                                     major_type::simple_float, std::to_underlying(simple_value::true_value)));
        } else if constexpr (has_fixed_underlying_type<U>) {
            return fixed_head_valid<std::underlying_type_t<U>>(field);
        } else {
            constexpr unsigned char expected = static_cast<unsigned char>(zero_initialized<void, U>()[0]);
            if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>)
                static_assert(fixed_size<T, T>() > 1, "The field must hold the second byte that is read.");
            if constexpr (std::signed_integral<U> || std::same_as<U, int128>) {
                if constexpr (std::same_as<U, int128>)
                    return (head & 0xfe) == expected && static_cast<unsigned char>(field[1]) == static_cast<unsigned char>(zero_initialized<void, U>()[1]);
                else
                    return (head & static_cast<unsigned char>(~(1 << rfc8949::additional_information_bits))) == expected;
            } else if constexpr (std::same_as<U, uint128>) {
                return head == expected && static_cast<unsigned char>(field[1]) == static_cast<unsigned char>(zero_initialized<void, U>()[1]);
            } else {
                return head == expected;
            }
        }
    }

    template <class Root, class U, std::size_t N>
    CBOR_ALWAYS_INLINE static bool class_tag_valid(std::span<char const, N> const field)
    {
        constexpr std::size_t n = heads::head_size(*tag_number_of<U>());
        static_assert(N != std::dynamic_extent && N >= n, "The field must hold the whole tag head.");
        static_assert(zero_initialized<Root, U>().size() >= n, "The zero-initialized encoding must hold the whole tag head.");
        std::span<char const, n> const expected = zero_initialized<Root, U>().template first<n>();
        return std::ranges::equal(field.template first<n>(), expected);
    }

    template <class Root, class U>
    CBOR_ALWAYS_INLINE static bool fixed_length_head_valid(std::span<char const, fixed_size<U, Root>()> const field)
    {
        constexpr std::size_t head = heads::head_size(fixed_length<U>::value);
        static_assert(zero_initialized<Root, U>().size() >= head, "The zero-initialized encoding must hold the whole head.");
        std::span<char const, head> const expected = zero_initialized<Root, U>().template first<head>();
        return std::ranges::equal(field.template first<head>(), expected);
    }

    struct reference {
        std::size_t data;
        std::size_t length;
    };

    struct variant_reference {
        std::size_t data;
        std::size_t size;
        std::size_t index;
    };

    struct item_bounds {
        std::size_t at;
        std::size_t end;
    };

    static constexpr char reference_tag_byte =
        heads::initial_byte(major_type::tag, std::to_underlying(packed_cbor::tag_number::reference));

    template <class Root, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> read_shared_index(std::span<char const, dynamic_type_sizes> const field)
    {
        constexpr std::size_t fillers = shared_first_of<Root>() - shared_first;
        auto const info = static_cast<unsigned char>(field[1]);
        if constexpr (Verified)
            if (field[0] != reference_tag_byte ||
                (info & static_cast<unsigned char>(~(1 << rfc8949::additional_information_bits))) != std::to_underlying(rfc8949::additional_information::four_byte_argument))
                [[unlikely]]
                return std::unexpected(error::incorrect_type);
        std::size_t const m = 2 * std::size_t{heads::read_unsigned<std::uint32_t>(field.subspan<2, sizeof(std::uint32_t)>())} + ((info >> rfc8949::additional_information_bits) & 1);
        if constexpr (Verified)
            if (m < fillers) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
        return m - fillers;
    }

    template <class Root, class S, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<reference, error> read_fixed_array(std::string_view const encoded, std::size_t const item,
                                                                               std::size_t const end)
    {
        static constexpr auto head = fixed_array_head<Root, S>();
        constexpr std::size_t data = head.size() + sizeof(std::uint32_t);
        if (auto const fits = validity::check_bytes_left(item + data, end); !fits) [[unlikely]]
            return std::unexpected(fits.error());
        if constexpr (Verified)
            if (!std::ranges::equal(std::span<char const>(encoded).subspan(item).template first<head.size()>(), head)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        std::size_t const length = heads::read_unsigned<std::uint32_t>(
            std::span<char const>(encoded).subspan(item + head.size()).template first<sizeof(std::uint32_t)>());
        if (auto const fits = validity::check_bytes_left(length, end - item - data); !fits) [[unlikely]]
            return std::unexpected(fits.error());
        if constexpr (Verified)
            if (error const r = validity::check_whole_elements(length, fixed_width<S>()).error_or(error{}); r != error{}) [[unlikely]]
                return std::unexpected(r);
        if (error const r = validity::check_argument<true>(major_type::byte_string, length).error_or(error{}); r != error{})
            [[unlikely]]
            return std::unexpected(r);
        return reference{item + data, length / fixed_width<S>()};
    }

    template <class M, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<M, error> read_fixed_leaf(std::span<char const, sizeof(fixed_leaf_t<M>)> const in)
    {
        using L = fixed_leaf_t<M>;
        L const value = read_typed_array_element<L>(in);
        if constexpr (std::same_as<M, bool>) {
            if constexpr (Verified)
                if (value > 1) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            return value != 0;
        } else {
            return static_cast<M>(value);
        }
    }

    template <class Root, major_type Major, class E, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    read_item_reference(std::string_view const encoded, std::size_t const item, std::size_t const end,
                        std::size_t const element)
    {
        if constexpr (fixed_struct_valid<E>()) {
            return read_fixed_array<Root, E, Verified>(encoded, item, end);
        } else if constexpr (is_typed_array_element<E>) {
            static_assert(validity::typed_array_element_size(typed_array_tag<E>()) == sizeof(E),
                          "The elements of the typed array tag of E are sizeof(E) bytes.");
            if (auto const fits = validity::check_bytes_left(item + typed_array_head, end); !fits) [[unlikely]]
                return std::unexpected(fits.error());
            static constexpr std::array head{
                heads::initial_byte(major_type::tag,
                                    std::to_underlying(rfc8949::additional_information::one_byte_argument)),
                static_cast<char>(typed_array_tag<E>()),
                heads::initial_byte(major_type::byte_string,
                                    std::to_underlying(rfc8949::additional_information::four_byte_argument))};
            if constexpr (Verified)
                if (!std::ranges::equal(std::span<char const>(encoded).subspan(item).template first<head.size()>(), head)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            std::size_t const size =
                heads::read_unsigned<std::uint32_t>(std::span<char const>(encoded).subspan(item + head.size()).template first<sizeof(std::uint32_t)>());
            if (auto const fits = validity::check_bytes_left(size, end - item - typed_array_head); !fits) [[unlikely]]
                return std::unexpected(fits.error());
            if constexpr (Verified)
                if (!validity::check_typed_array(typed_array_tag<E>(), size)) [[unlikely]]
                    return std::unexpected(error::inadmissible_type_for_tag_content);
            if (error const r =
                    validity::check_argument<true>(major_type::byte_string, size).error_or(error{});
                r != error{}) [[unlikely]]
                return std::unexpected(r);
            return reference{item + typed_array_head, size / sizeof(E)};
        } else {
            if (auto const fits = validity::check_bytes_left(item + item_head, end); !fits) [[unlikely]]
                return std::unexpected(fits.error());
            if constexpr (Verified)
                if (encoded[item] !=
                    heads::initial_byte(Major,
                                        std::to_underlying(rfc8949::additional_information::four_byte_argument)))
                    [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            std::size_t const length =
                heads::read_unsigned<std::uint32_t>(std::span<char const>(encoded).subspan(item + 1).template first<sizeof(std::uint32_t)>());
            auto const size = validity::checked_mul(length, element);
            if (!size || !validity::bytes_fit(*size, end - item - item_head)) [[unlikely]]
                return std::unexpected(error::too_little_data);
            if (error const r = validity::check_argument<true>(Major, length).error_or(error{}); r != error{})
                [[unlikely]]
                return std::unexpected(r);
            return reference{item + item_head, length};
        }
    }

    CBOR_ALWAYS_INLINE static std::size_t directory_entry(std::string_view const encoded, detail::directory const dir, std::size_t const j)
    {
        return heads::read_unsigned<std::uint32_t>(
            std::span<char const>(encoded).subspan(dir.at + sizeof(std::uint32_t) * j).template first<sizeof(std::uint32_t)>());
    }

    template <class Root, major_type Major, class E = void, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    read_reference(std::string_view const encoded, std::span<char const, dynamic_type_sizes> const field,
                   detail::directory const dir, std::size_t const element)
    {
        auto const j = read_shared_index<Root, Verified>(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        return read_item<Root, Major, E, Verified>(encoded, dir, *j, element);
    }

    template <class Root>
    CBOR_ALWAYS_INLINE static std::expected<item_bounds, error> read_item_bounds(std::string_view const encoded,
                                                                                 detail::directory const dir, std::size_t const j)
    {
        if (!validity::check_index(j, dir.count)) [[unlikely]]
            return std::unexpected(error::unpopulated_table_index);
        constexpr std::size_t fillers = fillers_of<Root>();
        std::size_t const items_at = dir.at + sizeof(std::uint32_t) * dir.count + fillers;
        std::size_t const items_end = encoded.size() - fixed_size<Root, Root>();
        std::size_t const item = directory_entry(encoded, dir, j);
        std::size_t const end = j + 1 < dir.count ? directory_entry(encoded, dir, j + 1) : items_end;
        if (auto const inside = validity::check_subrange(item, end, items_at, items_end); !inside) [[unlikely]]
            return std::unexpected(inside.error());
        return item_bounds{item, end};
    }

    template <class Root, major_type Major, class E = void, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<reference, error>
    read_item(std::string_view const encoded, detail::directory const dir, std::size_t const j,
              std::size_t const element)
    {
        auto const bounds = read_item_bounds<Root>(encoded, dir, j);
        if (!bounds) [[unlikely]]
            return std::unexpected(bounds.error());
        return read_item_reference<Root, Major, E, Verified>(encoded, bounds->at, bounds->end, element);
    }

    template <class Root, class V, std::size_t K = 0>
    CBOR_ALWAYS_INLINE static std::expected<variant_reference, error>
    read_variant_item(std::string_view const encoded, std::size_t const item, std::size_t const end,
                      error const fault = error::incorrect_type)
    {
        if constexpr (K == std::variant_size_v<V>) {
            return std::unexpected(fault);
        } else {
            using A = std::variant_alternative_t<K, V>;
            constexpr std::size_t n = heads::head_size(*tag_number_of<A>());
            if (!validity::check_bytes_left(item + n, end)) [[unlikely]]
                return read_variant_item<Root, V, K + 1>(encoded, item, end, error::too_little_data);
            if (!class_tag_valid<Root, A>(std::span<char const>(encoded).subspan(item).template first<n>()))
                return read_variant_item<Root, V, K + 1>(encoded, item, end, fault);
            if (auto const fits = validity::check_bytes_left(item + fixed_size<A, Root>(), end); !fits) [[unlikely]]
                return std::unexpected(fits.error());
            return variant_reference{item, fixed_size<A, Root>(), K};
        }
    }

    template <class Root, class V, bool Verified = true>
    CBOR_ALWAYS_INLINE static std::expected<variant_reference, error>
    read_variant_reference(std::string_view const encoded, std::span<char const, dynamic_type_sizes> const field,
                           detail::directory const dir)
    {
        auto const j = read_shared_index<Root, Verified>(field);
        if (!j) [[unlikely]]
            return std::unexpected(j.error());
        auto const bounds = read_item_bounds<Root>(encoded, dir, *j);
        if (!bounds) [[unlikely]]
            return std::unexpected(bounds.error());
        return read_variant_item<Root, V>(encoded, bounds->at, bounds->end);
    }

    template <class T, description_place D>
    static bool packing_prefix_valid(std::string_view const encoded)
    {
        static constexpr auto prefix = packing_prefix_of<T, D>();
        return validity::bytes_fit(prefix.size() + heads::initial_byte_size, encoded.size()) &&
               std::ranges::equal(std::span(encoded).template first<4>(), std::span(prefix).template first<4>()) &&
               std::ranges::equal(std::span(encoded).template subspan<8, prefix.size() - 8>(),
                                  std::span(prefix).template subspan<8>()) &&
               encoded[prefix.size()] ==
                   heads::initial_byte(major_type::byte_string,
                                       std::to_underlying(rfc8949::additional_information::four_byte_argument));
    }

    template <class T, description_place D>
    static error too_little_data_if_truncated(std::string_view const encoded, error const e)
    {
        if (e == error::too_little_data || !packing_prefix_valid<T, D>(encoded) || !validity::bytes_fit(fixed_size<T, T>(), encoded.size()))
            return e;
        if (class_tag_valid<T, T>(std::span<char const>(encoded).subspan(encoded.size() - fixed_size<T, T>()).template first<fixed_size<T, T>()>()))
            return e;
        heads::decoder<true> d{encoded};
        if (well_formedness::skip_items(d, 1).error_or(error{}) != error::too_little_data)
            return e;
        return error::too_little_data;
    }

    template <class T, bool Verified, description_place D>
    static std::expected<detail::directory, error> read_directory(std::string_view const encoded)
    {
        static constexpr auto prefix = packing_prefix_of<T, D>();
        constexpr std::size_t fillers = fillers_of<T>();
        static_assert(prefix.size() >= 8, "The packing prefix must hold the eight bytes that are compared and read.");
        constexpr auto least_size = validity::checked_add(directory_at<T, D>(), fillers).and_then([](std::size_t const head) {
            return validity::checked_add(head, fixed_size<T, T>());
        });
        static_assert(least_size.has_value(), "The least size of an encoded item must fit in std::size_t.");
        constexpr std::size_t least = *least_size;
        if (auto const fits = validity::check_bytes_left(least, encoded.size()); !fits) [[unlikely]]
            return std::unexpected(fits.error());
        if constexpr (Verified)
            if (!packing_prefix_valid<T, D>(encoded)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        std::size_t const length =
            heads::read_unsigned<std::uint32_t>(std::span<char const>(encoded)
                                                    .subspan(prefix.size() + heads::initial_byte_size)
                                                    .template first<sizeof(std::uint32_t)>());
        std::size_t const count = length / sizeof(std::uint32_t);
        if constexpr (Verified)
            if (length % sizeof(std::uint32_t) != 0 ||
                heads::read_unsigned<std::uint32_t>(std::span<char const>(encoded)
                                                        .subspan(sizeof(std::uint32_t))
                                                        .template first<sizeof(std::uint32_t)>()) !=
                    table_first_of<T, D>() + count) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        if (auto const fits = validity::check_bytes_left(length, encoded.size() - least); !fits) [[unlikely]]
            return std::unexpected(fits.error());
        static constexpr auto undefined = [] {
            std::array<char, fillers> a{};
            a.fill(static_cast<char>(heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined))));
            return a;
        }();
        if constexpr (Verified)
            if (!std::ranges::equal(std::span(encoded).subspan(directory_at<T, D>() + length).template first<fillers>(), undefined)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        return detail::directory{directory_at<T, D>(), count};
    }

    static consteval std::size_t count_index_arguments(std::string_view const path)
    {
        constexpr std::string_view index_argument_mark = "[]";
        if (path.empty())
            return 0;
        return static_cast<std::size_t>(std::ranges::distance(path | std::views::split(index_argument_mark))) - 1;
    }

    template <fixed_string Path>
    static consteval std::size_t index_arguments()
    {
        return count_index_arguments(Path.view());
    }

    template <fixed_string Path, std::size_t At>
    static consteval std::size_t index_argument()
    {
        return index_arguments<Path>() - count_index_arguments(Path.view().substr(At));
    }

    template <class T, fixed_string Path, std::size_t At>
    static consteval bool path_valid()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_optional<U>)
                return path_valid<typename U::value_type, Path, At>();
            else
                return true;
        } else if constexpr (path[At] == '.') {
            if constexpr (!has_describe_members<U> || requires { fixed_length<U>::value; } || is_optional<U>) {
                return false;
            } else {
                constexpr std::size_t end = step_end(path, At);
                constexpr std::size_t m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
                if constexpr (m == describe::members_count<U>)
                    return false;
                else
                    return path_valid<describe::member_t<U, m>, Path, end>();
            }
        } else if constexpr (path[At] == '[' && index_end(path, At) < path.size()) {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (is_byte_range<U>) {
                if constexpr (close + 1 != path.size())
                    return false;
                else if constexpr (requires { fixed_length<U>::value; })
                    return close == At + 1 ||
                           index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1))) < fixed_length<U>::value;
                else
                    return true;
            } else if constexpr (is_fixed_string<U> || is_text_range<U> || is_map<U> || is_optional<U>) {
                return false;
            } else if constexpr (requires { fixed_length<U>::value; }) {
                if constexpr (close != At + 1 && index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1))) >= fixed_length<U>::value)
                    return false;
                else
                    return path_valid<typename fixed_length<U>::element, Path, close + 1>();
            } else if constexpr (std::ranges::sized_range<U>) {
                return path_valid<std::ranges::range_value_t<U>, Path, close + 1>();
            } else {
                return false;
            }
        } else {
            return false;
        }
    }

    template <class Root, class T, fixed_string Path, std::size_t At, bool Verified = true>
    static consteval auto path_result()
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_optional<U>)
                return std::type_identity<
                    std::optional<typename decltype(path_result<Root, typename U::value_type, Path, At, Verified>())::type>>{};
            else if constexpr (is_fixed_string<U> || is_text_range<U> || is_byte_range<U>)
                return std::type_identity<std::string_view>{};
            else if constexpr ((std::is_class_v<U> || std::is_array_v<U>) && !std::same_as<U, int128> && !std::same_as<U, uint128>)
                return std::type_identity<typename cbor::schema<Root>::template accessor<U, Verified>>{};
            else
                return std::type_identity<U>{};
        } else if constexpr (path[At] == '.') {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::size_t m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            return path_result<Root, describe::member_t<U, m>, Path, end, Verified>();
        } else {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (is_byte_range<U>)
                return std::type_identity<std::remove_cv_t<std::ranges::range_value_t<U>>>{};
            else if constexpr (requires { fixed_length<U>::value; })
                return path_result<Root, typename fixed_length<U>::element, Path, close + 1, Verified>();
            else if constexpr (is_fixed_vector<U>)
                return fixed_path_result<Root, std::remove_cv_t<std::ranges::range_value_t<U>>, Path, close + 1, Verified>();
            else
                return path_result<Root, std::ranges::range_value_t<U>, Path, close + 1, Verified>();
        }
    }

    template <class Root, class S, fixed_string Path, std::size_t At, bool Verified = true>
    static consteval auto fixed_path_result()
    {
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            return std::type_identity<typename cbor::schema<Root>::template accessor<fixed_element<S>, Verified>>{};
        } else {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::size_t m = member_named<S>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            using M = describe::member_t<S, m>;
            if constexpr (fixed_struct_valid<M>()) {
                return fixed_path_result<Root, M, Path, end, Verified>();
            } else if constexpr (is_fixed_leaf<M>) {
                return std::type_identity<M>{};
            } else {
                static_assert(end < path.size(), "a path into an element of a fixed-size vector ends at a number");
                return std::type_identity<std::remove_cv_t<typename fixed_length<M>::element>>{};
            }
        }
    }

    template <class Root, class S, fixed_string Path, std::size_t At, bool Verified = true>
    CBOR_ALWAYS_INLINE static auto walk_fixed(std::string_view const encoded, std::span<char const, fixed_width<S>()> const element,
                                              detail::directory const floor,
                                              std::array<std::size_t, index_arguments<Path>()> const &indexes)
        -> std::expected<typename decltype(fixed_path_result<Root, S, Path, At, Verified>())::type, error>
    {
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            return typename cbor::schema<Root>::template accessor<fixed_element<S>, Verified>(encoded, element, floor);
        } else {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::size_t m = member_named<S>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            using M = describe::member_t<S, m>;
            auto const member = element.template subspan<fixed_offset<S, m>(), fixed_width<M>()>();
            if constexpr (fixed_struct_valid<M>()) {
                return walk_fixed<Root, M, Path, end, Verified>(encoded, member, floor, indexes);
            } else if constexpr (is_fixed_leaf<M>) {
                return read_fixed_leaf<M, Verified>(member.template first<sizeof(fixed_leaf_t<M>)>());
            } else {
                using L = std::remove_cv_t<typename fixed_length<M>::element>;
                constexpr std::size_t close = index_end(path, end);
                std::size_t j;
                if constexpr (close == end + 1)
                    j = std::get<index_argument<Path, end>()>(indexes);
                else
                    j = index_of<S>(std::string_view(std::span(path).subspan(end + 1, close - end - 1)));
                auto const position = validity::check_index(j, fixed_length<M>::value);
                if (!position) [[unlikely]]
                    return std::unexpected(position.error());
                return read_fixed_leaf<L, Verified>(
                    validity::element_span(member, *position, sizeof(fixed_leaf_t<L>)).template first<sizeof(fixed_leaf_t<L>)>());
            }
        }
    }

    CBOR_ALWAYS_INLINE static std::span<char const> list_span(std::string_view const encoded, reference const r,
                                                              std::size_t const stride)
    {
        return std::span<char const>(encoded).subspan(r.data).first(r.length * stride);
    }

    template <class E>
    CBOR_ALWAYS_INLINE static std::expected<E, error> typed_array_element_at(std::span<char const> const list,
                                                                             std::uint64_t const length, std::size_t const i)
    {
        auto const position = validity::check_index(i, length);
        if (!position) [[unlikely]]
            return std::unexpected(position.error());
        return read_typed_array_element<E>(validity::element_span(list, *position, sizeof(E)).template first<sizeof(E)>());
    }

    template <class Root, class S, bool Verified>
    CBOR_ALWAYS_INLINE static std::expected<typename cbor::schema<Root>::template accessor<fixed_element<S>, Verified>, error>
    fixed_element_at(std::shared_ptr<void const> const &owner, std::string_view const encoded, std::span<char const> const list,
                     std::uint64_t const length, std::size_t const i, detail::directory const dir)
    {
        auto const position = validity::check_index(i, length);
        if (!position) [[unlikely]]
            return std::unexpected(position.error());
        typename cbor::schema<Root>::template accessor<fixed_element<S>, Verified> element(
            encoded, validity::element_span(list, *position, fixed_width<S>()).template first<fixed_width<S>()>(), dir);
        element.owner = owner;
        return element;
    }

    template <class Root, class E, bool Verified>
    CBOR_ALWAYS_INLINE static std::expected<typename cbor::schema<Root>::template accessor<E, Verified>, error>
    record_element_at(std::shared_ptr<void const> const &owner, std::string_view const encoded, std::span<char const> const list,
                      std::uint64_t const length, std::size_t const i, detail::directory const dir)
    {
        auto const position = validity::check_index(i, length);
        if (!position) [[unlikely]]
            return std::unexpected(position.error());
        return typename cbor::schema<Root>::template accessor<E, Verified>(
            owner, encoded, validity::element_span(list, *position, fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(), dir);
    }

    template <class Root, class U, bool Verified>
    static consteval auto list_iterator_result()
    {
        if constexpr (is_fixed_vector<U>) {
            using S = std::remove_cv_t<std::ranges::range_value_t<U>>;
            return std::type_identity<detail::indexing_iterator<typename cbor::schema<Root>::template accessor<fixed_element<S>, Verified>,
                                                                detail::directory, &fixed_element_at<Root, S, Verified>>>{};
        } else if constexpr (is_record_list<U>) {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            return std::type_identity<detail::indexing_iterator<typename cbor::schema<Root>::template accessor<E, Verified>,
                                                                detail::directory, &record_element_at<Root, E, Verified>>>{};
        } else {
            return std::type_identity<void>{};
        }
    }

    template <class Root, class T, fixed_string Path, std::size_t At, bool Verified = true>
    CBOR_ALWAYS_INLINE static auto
    walk_path(std::string_view const encoded, std::span<char const, fixed_size<T, Root>()> const field,
              detail::directory const floor, std::array<std::size_t, index_arguments<Path>()> const &indexes)
        -> std::expected<typename decltype(path_result<Root, T, Path, At, Verified>())::type, error>
    {
        using U = std::remove_cv_t<T>;
        constexpr std::string_view path = Path.view();
        if constexpr (At == path.size()) {
            if constexpr (is_fixed_string<U>) {
                constexpr std::size_t n = fixed_length<U>::value;
                if constexpr (Verified)
                    if (!fixed_length_head_valid<Root, U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                return std::string_view(field.template last<n>());
            } else if constexpr (is_inline_optional<U>) {
                using E = typename U::value_type;
                using X = typename decltype(path_result<Root, E, Path, At, Verified>())::type;
                if (field.template subspan<1, 1>().front() !=
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::true_value)))
                    return std::optional<X>{};
                auto const x = walk_path<Root, E, Path, At, Verified>(
                    encoded, field.template subspan<inline_optional_head, fixed_size<E, Root>()>(), floor,
                    indexes);
                if (!x) [[unlikely]]
                    return std::unexpected(x.error());
                return std::optional<X>{*x};
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r =
                    read_reference<Root, major_type::array, void, Verified>(encoded, field, floor, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if constexpr (Verified)
                    if (r->length > 1) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                using X = typename decltype(path_result<Root, E, Path, At, Verified>())::type;
                if (r->length == 0)
                    return std::optional<X>{};
                auto const x = walk_path<Root, E, Path, At, Verified>(
                    encoded,
                    std::span<char const>(encoded).subspan(r->data).template first<fixed_size<E, Root>()>(),
                    floor, indexes);
                if (!x) [[unlikely]]
                    return std::unexpected(x.error());
                return std::optional<X>{*x};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                constexpr major_type major = is_text_range<U> ? major_type::text_string : major_type::byte_string;
                auto const r = read_reference<Root, major, void, Verified>(encoded, field, floor, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return std::string_view(std::span(encoded).subspan(r->data, r->length));
            } else if constexpr (is_map<U>) {
                auto const r = read_reference<Root, major_type::map, void, Verified>(
                    encoded, field, floor,
                    fixed_size<typename U::key_type, Root>() + fixed_size<typename U::mapped_type, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U, Verified>(encoded, field, floor, *r);
            } else if constexpr (is_typed_array<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                auto const r = read_reference<Root, major_type::array, E, Verified>(encoded, field, floor, sizeof(E));
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U, Verified>(encoded, field, floor, *r);
            } else if constexpr (is_fixed_vector<U>) {
                using S = std::remove_cv_t<std::ranges::range_value_t<U>>;
                auto const r = read_reference<Root, major_type::array, S, Verified>(encoded, field, floor, fixed_width<S>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U, Verified>(encoded, field, floor, *r);
            } else if constexpr (is_list<U>) {
                auto const r = read_reference<Root, major_type::array, void, Verified>(
                    encoded, field, floor, fixed_size<std::ranges::range_value_t<U>, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U, Verified>(encoded, field, floor, *r);
            } else if constexpr (detail::is_std_variant<U>) {
                auto const r = read_variant_reference<Root, U, Verified>(encoded, field, floor);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                return typename cbor::schema<Root>::template accessor<U, Verified>(encoded, *r, floor);
            } else if constexpr ((std::is_class_v<U> || std::is_array_v<U>) && !std::same_as<U, int128> && !std::same_as<U, uint128>) {
                return typename cbor::schema<Root>::template accessor<U, Verified>(encoded, field, floor);
            } else {
                if constexpr (Verified)
                    if (!fixed_head_valid<U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                return read_fixed_value<U>(field);
            }
        } else if constexpr (path[At] == '.') {
            constexpr std::size_t end = step_end(path, At);
            constexpr std::size_t m = member_named<U>(std::string_view(std::span(path).subspan(At + 1, end - At - 1)));
            using M = describe::member_t<U, m>;
            if constexpr (Verified)
                if (!class_tag_valid<Root, U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            return walk_path<Root, M, Path, end, Verified>(
                encoded, field.template subspan<member_offset<U, m, Root>(), fixed_size<M, Root>()>(), floor,
                indexes);
        } else if constexpr (is_byte_range<U>) {
            using B = std::remove_cv_t<std::ranges::range_value_t<U>>;
            constexpr std::size_t close = index_end(path, At);
            std::size_t i;
            if constexpr (close == At + 1)
                i = std::get<index_argument<Path, At>()>(indexes);
            else
                i = index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1)));
            if constexpr (requires { fixed_length<U>::value; }) {
                constexpr std::size_t n = fixed_length<U>::value;
                if constexpr (Verified)
                    if (!fixed_length_head_valid<Root, U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                auto const position = validity::check_index(i, n);
                if (!position) [[unlikely]]
                    return std::unexpected(position.error());
                return std::bit_cast<B>(validity::element_span(field.template last<n>(), *position, 1).front());
            } else {
                auto const r = read_reference<Root, major_type::byte_string, void, Verified>(encoded, field, floor, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                auto const position = validity::check_index(i, r->length);
                if (!position) [[unlikely]]
                    return std::unexpected(position.error());
                return std::bit_cast<B>(validity::element_span(list_span(encoded, *r, 1), *position, 1).front());
            }
        } else {
            constexpr std::size_t close = index_end(path, At);
            if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                constexpr std::size_t head = heads::head_size(n);
                static_assert(zero_initialized<Root, U>().size() >= head, "The zero-initialized encoding must hold the whole head.");
                static_assert(validity::checked_mul(n, fixed_size<E, Root>())
                                      .and_then([](std::size_t const elements) {
                                          return validity::checked_add(head, elements);
                                      })
                                      .value_or(std::numeric_limits<std::size_t>::max()) <=
                                  fixed_size<U, Root>(),
                              "The elements must lie inside the field.");
                if constexpr (Verified)
                    if (!fixed_length_head_valid<Root, U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                auto const elements = field.template subspan<head, n * fixed_size<E, Root>()>();
                if constexpr (close == At + 1) {
                    auto const position = validity::check_index(std::get<index_argument<Path, At>()>(indexes), n);
                    if (!position) [[unlikely]]
                        return std::unexpected(position.error());
                    return walk_path<Root, E, Path, close + 1, Verified>(
                        encoded, validity::element_span(elements, *position, fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(),
                        floor, indexes);
                } else {
                    constexpr validity::index position =
                        validity::check_index(index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1))), n).value();
                    return walk_path<Root, E, Path, close + 1, Verified>(
                        encoded, validity::element_span(elements, position, fixed_size<E, Root>()).template first<fixed_size<E, Root>()>(),
                        floor, indexes);
                }
            } else {
                using E = std::ranges::range_value_t<U>;
                std::size_t i;
                if constexpr (close == At + 1)
                    i = std::get<index_argument<Path, At>()>(indexes);
                else
                    i = index_of<T>(std::string_view(std::span(path).subspan(At + 1, close - At - 1)));
                if constexpr (is_typed_array<U>) {
                    using F = std::remove_cv_t<E>;
                    auto const r =
                        read_reference<Root, major_type::array, F, Verified>(encoded, field, floor, sizeof(F));
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    return typed_array_element_at<F>(list_span(encoded, *r, sizeof(F)), r->length, i);
                } else if constexpr (is_fixed_vector<U>) {
                    using S = std::remove_cv_t<E>;
                    auto const r = read_reference<Root, major_type::array, S, Verified>(encoded, field, floor, fixed_width<S>());
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    auto const position = validity::check_index(i, r->length);
                    if (!position) [[unlikely]]
                        return std::unexpected(position.error());
                    return walk_fixed<Root, S, Path, close + 1, Verified>(
                        encoded,
                        validity::element_span(list_span(encoded, *r, fixed_width<S>()), *position, fixed_width<S>())
                            .template first<fixed_width<S>()>(),
                        floor, indexes);
                } else {
                    auto const r =
                        read_reference<Root, major_type::array, void, Verified>(encoded, field, floor, fixed_size<E, Root>());
                    if (!r) [[unlikely]]
                        return std::unexpected(r.error());
                    auto const position = validity::check_index(i, r->length);
                    if (!position) [[unlikely]]
                        return std::unexpected(position.error());
                    return walk_path<Root, E, Path, close + 1, Verified>(
                        encoded,
                        validity::element_span(list_span(encoded, *r, fixed_size<E, Root>()), *position, fixed_size<E, Root>())
                            .template first<fixed_size<E, Root>()>(),
                        floor, indexes);
                }
            }
        }
    }

    template <bool Verified>
    struct decode_cursor {
        std::string_view encoded;
        detail::directory dir;
        std::size_t index;
        std::size_t at;
        std::size_t end;

        template <class Root, major_type Major, class E = void>
        CBOR_ALWAYS_INLINE std::expected<reference, error> take_reference(std::span<char const, dynamic_type_sizes> const field,
                                                                          std::size_t const element)
        {
            auto const j = read_shared_index<Root, Verified>(field);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (!validity::check_index(*j, dir.count)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            if constexpr (Verified) {
                if (*j != index) [[unlikely]]
                    return std::unexpected(error::unpopulated_table_index);
                if (directory_entry(encoded, dir, *j) != at) [[unlikely]]
                    return std::unexpected(error::syntax_error);
            }
            auto const r = read_item_reference<Root, Major, E, Verified>(encoded, at, end, element);
            if (!r) [[unlikely]]
                return r;
            index += 1;
            at = r->data + r->length * element;
            return r;
        }

        template <class Root, class V>
        CBOR_ALWAYS_INLINE std::expected<variant_reference, error>
        take_variant_reference(std::span<char const, dynamic_type_sizes> const field)
        {
            auto const j = read_shared_index<Root, Verified>(field);
            if (!j) [[unlikely]]
                return std::unexpected(j.error());
            if (!validity::check_index(*j, dir.count)) [[unlikely]]
                return std::unexpected(error::unpopulated_table_index);
            if constexpr (Verified) {
                if (*j != index) [[unlikely]]
                    return std::unexpected(error::unpopulated_table_index);
                if (directory_entry(encoded, dir, *j) != at) [[unlikely]]
                    return std::unexpected(error::syntax_error);
            }
            auto const r = read_variant_item<Root, V>(encoded, at, end);
            if (!r) [[unlikely]]
                return r;
            index += 1;
            at = r->data + r->size;
            return r;
        }

        template <class Root, class V, std::size_t K>
        std::expected<void, error> read_alternative(V &out, variant_reference const r, std::size_t const depth,
                                                    std::size_t const depth_max)
        {
            using A = std::variant_alternative_t<K, V>;
            auto const record = std::span<char const>(encoded).subspan(r.data).template first<fixed_size<A, Root>()>();
            if constexpr (std::is_default_constructible_v<A>) {
                return read_value<Root>(out.template emplace<K>(), record, depth, depth_max);
            } else {
                auto made = read_constructed<Root, A>(make_arguments<A>(), record, depth, depth_max);
                if (!made) [[unlikely]]
                    return std::unexpected(made.error());
                out.template emplace<K>(std::move(*made));
                return {};
            }
        }

        template <class Root, class T>
        std::expected<void, error> read_value(T &out, std::span<char const, fixed_size<T, Root>()> const field, std::size_t const depth,
                                              std::size_t const depth_max)
        {
            using U = std::remove_cv_t<T>;
            if constexpr (has_describe_members<U>) {
                if constexpr (Verified)
                    if (!class_tag_valid<Root, U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                std::expected<void, error> done;
                for_each_field<U>([&] CBOR_ALWAYS_INLINE(auto i) {
                    constexpr std::size_t I = decltype(i)::value;
                    using M = describe::member_t<U, I>;
                    if (done)
                        done = read_value<Root>(describe::get<I>(out),
                                                field.template subspan<member_offset<U, I, Root>(), fixed_size<M, Root>()>(), depth,
                                                depth_max);
                });
                return done;
            } else if constexpr (detail::is_std_variant<U>) {
                auto const r = take_variant_reference<Root, U>(field);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const nesting = validity::check_nesting_depth(depth + 1, depth_max); !nesting) [[unlikely]]
                    return std::unexpected(nesting.error());
                return [&]<std::size_t... K>(std::index_sequence<K...>) {
                    std::expected<void, error> done;
                    ((r->index == K ? (void)(done = read_alternative<Root, U, K>(out, *r, depth + 1, depth_max)) : void()), ...);
                    return done;
                }(std::make_index_sequence<std::variant_size_v<U>>{});
            } else if constexpr (is_fixed_string<U>) {
                if constexpr (Verified)
                    if (!fixed_length_head_valid<Root, U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                auto const text = field.template last<fixed_length<U>::value>();
                std::ranges::transform(text, std::ranges::begin(out), [](char const c) {
                    return static_cast<typename fixed_length<U>::element>(c);
                });
                return {};
            } else if constexpr (requires { fixed_length<U>::value; }) {
                using E = typename fixed_length<U>::element;
                constexpr std::size_t n = fixed_length<U>::value;
                if constexpr (Verified)
                    if (!fixed_length_head_valid<Root, U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                std::expected<void, error> done;
                auto const elements = field.template subspan<heads::head_size(n), n * fixed_size<E, Root>()>();
                auto const read_element = [&]<std::size_t I>() {
                    constexpr validity::index position = validity::check_index(I, n).value();
                    return read_value<Root>(std::span(out).template subspan<I, 1>().front(),
                                            validity::element_span(elements, position, fixed_size<E, Root>())
                                                .template first<fixed_size<E, Root>()>(),
                                            depth, depth_max);
                };
                [&]<std::size_t... I>(std::index_sequence<I...>) {
                    ((done = done ? read_element.template operator()<I>() : done), ...);
                }(std::make_index_sequence<n>{});
                return done;
            } else if constexpr (is_inline_optional<U>) {
                using E = typename U::value_type;
                if (field.template subspan<1, 1>().front() !=
                    heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::true_value))) {
                    out.reset();
                    return {};
                }
                auto const inner = field.template subspan<inline_optional_head, fixed_size<E, Root>()>();
                if constexpr (std::is_default_constructible_v<E>) {
                    return read_value<Root>(out.emplace(), inner, depth, depth_max);
                } else {
                    auto made = read_constructed<Root, E>(make_arguments<E>(), inner, depth, depth_max);
                    if (!made) [[unlikely]]
                        return std::unexpected(made.error());
                    out.emplace(std::move(*made));
                    return {};
                }
            } else if constexpr (is_optional<U>) {
                using E = typename U::value_type;
                auto const r = take_reference<Root, major_type::array>(field, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if constexpr (Verified)
                    if (r->length > 1) [[unlikely]]
                        return std::unexpected(error::syntax_error);
                if (r->length == 0) {
                    out.reset();
                    return {};
                }
                if (auto const nesting = validity::check_nesting_depth(depth + 1, depth_max); !nesting) [[unlikely]]
                    return std::unexpected(nesting.error());
                auto const inner = std::span<char const>(encoded).subspan(r->data).template first<fixed_size<E, Root>()>();
                if constexpr (std::is_default_constructible_v<E>) {
                    E element{};
                    if (auto const e = read_value<Root>(element, inner, depth + 1, depth_max); !e) [[unlikely]]
                        return e;
                    out = std::move(element);
                } else {
                    auto made = read_constructed<Root, E>(make_arguments<E>(), inner, depth + 1, depth_max);
                    if (!made) [[unlikely]]
                        return std::unexpected(made.error());
                    out.emplace(std::move(*made));
                }
                return {};
            } else if constexpr (is_text_range<U> || is_byte_range<U>) {
                auto const r = take_reference<Root, is_text_range<U> ? major_type::text_string : major_type::byte_string>(field, 1);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                std::string_view const part{std::span(encoded).subspan(r->data, r->length)};
                if constexpr (std::same_as<std::remove_cv_t<std::ranges::range_value_t<U>>, std::byte>) {
                    auto const raw = std::as_bytes(std::span(part));
                    if constexpr (std::ranges::view<U>)
                        out = U(raw.begin(), raw.end());
                    else
                        out.assign(raw.begin(), raw.end());
                } else if constexpr (std::ranges::view<U>) {
                    out = U(part.begin(), part.end());
                } else {
                    out.assign(part.begin(), part.end());
                }
                return {};
            } else if constexpr (is_map<U>) {
                using K = typename U::key_type;
                using V = typename U::mapped_type;
                constexpr std::size_t pair = fixed_size<K, Root>() + fixed_size<V, Root>();
                auto const r = take_reference<Root, major_type::map>(field, pair);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                out.clear();
                auto const entries = list_span(encoded, *r, pair);
                for (std::size_t i = 0; i < r->length; ++i) {
                    auto const entry = validity::element_span(entries, *validity::check_index(i, r->length), pair).template first<pair>();
                    if constexpr (std::is_default_constructible_v<K> && std::is_default_constructible_v<V>) {
                        K key = make_element<K>(out);
                        V value = make_element<V>(out);
                        if (auto const e = read_value<Root>(key, entry.template first<fixed_size<K, Root>()>(), depth + 1, depth_max);
                            !e) [[unlikely]]
                            return e;
                        if (auto const e = read_value<Root>(value, entry.template last<fixed_size<V, Root>()>(), depth + 1, depth_max);
                            !e) [[unlikely]]
                            return e;
                        out.try_emplace(std::move(key), std::move(value));
                    } else {
                        auto key = read_element<Root, K>(out, entry.template first<fixed_size<K, Root>()>(), depth + 1, depth_max);
                        if (!key) [[unlikely]]
                            return std::unexpected(key.error());
                        auto value = read_element<Root, V>(out, entry.template last<fixed_size<V, Root>()>(), depth + 1, depth_max);
                        if (!value) [[unlikely]]
                            return std::unexpected(value.error());
                        out.try_emplace(std::move(*key), std::move(*value));
                    }
                }
                return {};
            } else if constexpr (is_typed_array<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                auto const r = take_reference<Root, major_type::array, E>(field, sizeof(E));
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                std::span<char const> const all = list_span(encoded, *r, sizeof(E)).first(r->length * sizeof(E));
                auto const elements =
                    std::views::iota(std::size_t{0}, r->length) | std::views::transform([all](std::size_t const i) {
                        std::array<char, sizeof(E)> bytes;
                        std::ranges::copy_n(std::ranges::next(all.begin(), static_cast<std::ptrdiff_t>(i * sizeof(E))),
                                            sizeof(E), bytes.begin());
                        return read_typed_array_element<E>(bytes);
                    });
                if constexpr (requires { out.assign_range(elements); }) {
                    out.assign_range(elements);
                } else {
                    out.clear();
                    out.reserve(r->length);
                    for (E const e : elements)
                        out.push_back(e);
                }
                return {};
            } else if constexpr (is_fixed_vector<U>) {
                using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                constexpr std::size_t width = fixed_width<E>();
                auto const r = take_reference<Root, major_type::array, E>(field, width);
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                auto const elements = list_span(encoded, *r, width);
                out.clear();
                out.reserve(r->length);
                for (std::size_t i = 0; i < r->length; ++i) {
                    E element = make_element<E>(out);
                    if (auto const e = read_fixed_struct<E>(
                            element, validity::element_span(elements, *validity::check_index(i, r->length), width).template first<width>());
                        !e) [[unlikely]]
                        return e;
                    out.push_back(std::move(element));
                }
                return {};
            } else if constexpr (std::ranges::sized_range<U>) {
                using E = std::ranges::range_value_t<U>;
                auto const r = take_reference<Root, major_type::array>(field, fixed_size<E, Root>());
                if (!r) [[unlikely]]
                    return std::unexpected(r.error());
                if (auto const c = validity::check_nesting_depth(r->length != 0 ? depth + 1 : depth, depth_max); !c) [[unlikely]]
                    return std::unexpected(c.error());
                auto const entries = list_span(encoded, *r, fixed_size<E, Root>());
                out.clear();
                out.reserve(r->length);
                for (std::size_t i = 0; i < r->length; ++i) {
                    auto const entry = validity::element_span(entries, *validity::check_index(i, r->length), fixed_size<E, Root>())
                                           .template first<fixed_size<E, Root>()>();
                    if constexpr (std::is_default_constructible_v<E>) {
                        E element = make_element<E>(out);
                        if (auto const e = read_value<Root>(element, entry, depth + 1, depth_max); !e) [[unlikely]]
                            return e;
                        out.push_back(std::move(element));
                    } else {
                        auto element = read_constructed<Root, E>(make_arguments<E>(out), entry, depth + 1, depth_max);
                        if (!element) [[unlikely]]
                            return std::unexpected(element.error());
                        out.push_back(std::move(*element));
                    }
                }
                return {};
            } else {
                if constexpr (Verified)
                    if (!fixed_head_valid<U>(field)) [[unlikely]]
                        return std::unexpected(error::incorrect_type);
                out = read_fixed_value<U>(field);
                return {};
            }
        }

        template <class Root, class U, class Arguments>
        std::expected<U, error> read_constructed(Arguments arguments, std::span<char const, fixed_size<U, Root>()> const field,
                                                 std::size_t const depth, std::size_t const depth_max)
        {
            if constexpr (Verified)
                if (!class_tag_valid<Root, U>(field)) [[unlikely]]
                    return std::unexpected(error::incorrect_type);
            std::expected<void, error> done;
            for_each_field<U>([&](auto i) {
                constexpr std::size_t I = decltype(i)::value;
                using M = describe::member_t<U, I>;
                if (done)
                    done = read_value<Root>(std::get<I>(arguments),
                                            field.template subspan<member_offset<U, I, Root>(), fixed_size<M, Root>()>(), depth,
                                            depth_max);
            });
            if (!done) [[unlikely]]
                return std::unexpected(done.error());
            return std::make_from_tuple<U>(std::move(arguments));
        }

        template <class Root, class E, class Container>
        std::expected<E, error> read_element(Container const &container, std::span<char const, fixed_size<E, Root>()> const field,
                                             std::size_t const depth, std::size_t const depth_max)
        {
            if constexpr (std::is_default_constructible_v<E>) {
                E element = make_element<E>(container);
                if (auto const e = read_value<Root>(element, field, depth, depth_max); !e) [[unlikely]]
                    return std::unexpected(e.error());
                return element;
            } else {
                return read_constructed<Root, E>(make_arguments<E>(container), field, depth, depth_max);
            }
        }

        template <class S>
        static std::expected<void, error> read_fixed_struct(S &out, std::span<char const, fixed_width<S>()> const in)
        {
            std::expected<void, error> done;
            for_each_field<S>([&] CBOR_ALWAYS_INLINE(auto i) {
                constexpr std::size_t I = decltype(i)::value;
                using M = describe::member_t<S, I>;
                constexpr std::size_t at = fixed_offset<S, I>();
                if (done) {
                    if constexpr (is_fixed_leaf<M>) {
                        auto const v = read_fixed_leaf<M, Verified>(in.template subspan<at, sizeof(fixed_leaf_t<M>)>());
                        if (v)
                            describe::get<I>(out) = *v;
                        else [[unlikely]]
                            done = std::unexpected(v.error());
                    } else if constexpr (requires { fixed_length<M>::value; }) {
                        using L = std::remove_cv_t<typename fixed_length<M>::element>;
                        for (std::size_t k = 0; k < fixed_length<M>::value && done; ++k) {
                            auto const v = read_fixed_leaf<L, Verified>(
                                validity::element_span(in.template subspan<at, fixed_width<M>()>(),
                                                       *validity::check_index(k, fixed_length<M>::value), sizeof(fixed_leaf_t<L>))
                                    .template first<sizeof(fixed_leaf_t<L>)>());
                            if (v)
                                describe::get<I>(out)[k] = *v;
                            else [[unlikely]]
                                done = std::unexpected(v.error());
                        }
                    } else {
                        done = read_fixed_struct<M>(describe::get<I>(out), in.template subspan<at, fixed_width<M>()>());
                    }
                }
            });
            return done;
        }
    };

    template <class T, bool Verified, description_place D, class Target>
    static std::expected<void, error> read_root(Target &out, std::string_view const encoded, std::size_t const depth_max)
    {
        auto const dir = read_directory<T, Verified, D>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        std::size_t const root = encoded.size() - fixed_size<T, T>();
        auto const field = std::span<char const>(encoded).subspan(root).template first<fixed_size<T, T>()>();
        if constexpr (Verified)
            if (!class_tag_valid<T, T>(field)) [[unlikely]]
                return std::unexpected(error::incorrect_type);
        constexpr std::size_t fillers = fillers_of<T>();
        decode_cursor<Verified> c{encoded, *dir, 0, dir->at + sizeof(std::uint32_t) * dir->count + fillers, root};
        if constexpr (std::is_same_v<Target, T>) {
            if (auto const r = c.template read_value<T>(out, field, 0, depth_max); !r) [[unlikely]]
                return std::unexpected(r.error());
        } else {
            auto made = c.template read_constructed<T, T>(make_arguments<T>(), field, 0, depth_max);
            if (!made) [[unlikely]]
                return std::unexpected(made.error());
            out.emplace(std::move(*made));
        }
        if (c.at != root) [[unlikely]]
            return std::unexpected(error::syntax_error);
        if constexpr (Verified)
            if (c.index != dir->count) [[unlikely]]
                return std::unexpected(error::syntax_error);
        return {};
    }

    template <class>
    friend class cbor::schema;

    template <class T, description_place D>
    static std::expected<std::size_t, std::errc> encoded_size(second_item<T> const &second)
    {
        constexpr auto fixed_part =
            validity::checked_add(directory_at<T, D>(), fillers_of<T>())
                .and_then([](std::size_t const head) { return validity::checked_add(head, fixed_size<T, T>()); });
        static_assert(fixed_part.has_value(), "The fixed part of an encoded item must fit in std::size_t.");
        constexpr std::size_t fixed = *fixed_part;
        if (second.overflow) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        auto const directory = validity::checked_mul(second.items, sizeof(std::uint32_t));
        if (!directory) [[unlikely]]
            return directory;
        auto const second_size = validity::checked_add(*directory, second.bytes);
        if (!second_size) [[unlikely]]
            return second_size;
        auto const size = validity::checked_add(fixed, *second_size);
        if (!size || !std::in_range<std::uint32_t>(*size)) [[unlikely]]
            return std::unexpected(std::errc::value_too_large);
        return size;
    }

    template <bool Exact, description_place D, class T>
    CBOR_ALWAYS_INLINE static std::size_t write_encoded(std::span<char> const encoded, T const &value, second_item<T> const &second)
    {
        static constexpr auto prefix = packing_prefix_of<T, D>();
        constexpr std::size_t fillers = fillers_of<T>();
        std::size_t const size = encoded.size() - (Exact ? 0 : heads::head_padding);
        std::ranges::copy(prefix, encoded.template first<prefix.size()>().begin());
        heads::write_u32(encoded, 4, table_first_of<T, D>() + second.items);
        heads::write_item_head(encoded, prefix.size(), major_type::byte_string, sizeof(std::uint32_t) * second.items);
        std::size_t const data = directory_at<T, D>() + sizeof(std::uint32_t) * second.items;
        std::ranges::fill(encoded.subspan(data, fillers),
                          heads::initial_byte(major_type::simple_float, std::to_underlying(simple_value::undefined)));
        auto const root = encoded.subspan(size - fixed_size<T, T>()).template first<fixed_size<T, T>()>();
        copy_zero_initialized<T, T>(root);
        encode_value<T, T, Exact, D>(encoded, root, value, encode_cursor{data + fillers, 0});
        return size;
    }

    template <class>
    friend class cbor::typed_array_view;

    friend class generic;

    friend class pfr;
};

template <class E>
class typed_array_view
{
    std::shared_ptr<void const> owner;
    std::span<char const> bytes;

    typed_array_view(std::shared_ptr<void const> o, std::span<char const> const b) : owner(std::move(o)), bytes(b)
    {
    }

    template <class>
    friend class schema;

public:
    typed_array_view(typed_array_view const &) = default;

    typed_array_view(typed_array_view &&other) noexcept
        : owner(std::move(other.owner)), bytes(std::exchange(other.bytes, std::span<char const>()))
    {
    }

    typed_array_view &operator=(typed_array_view const &) = default;

    typed_array_view &operator=(typed_array_view &&other) noexcept
    {
        owner = std::move(other.owner);
        bytes = std::exchange(other.bytes, std::span<char const>());
        return *this;
    }

    ~typed_array_view() = default;

    using iterator = detail::indexing_iterator<E, std::monostate, &reflection::typed_array_element_at<E>>;

    CBOR_ALWAYS_INLINE std::size_t size() const
    {
        return bytes.size() / sizeof(E);
    }

    CBOR_ALWAYS_INLINE bool empty() const
    {
        return bytes.empty();
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE iterator begin(this Self &&self)
    {
        return iterator(self.owner, std::string_view(), self.bytes, 0, self.size(), {});
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE iterator end(this Self &&self)
    {
        return iterator(self.owner, std::string_view(), self.bytes, self.size(), self.size(), {});
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE iterator cbegin(this Self &&self)
    {
        return self.begin();
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE iterator cend(this Self &&self)
    {
        return self.end();
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> rbegin(this Self &&self)
    {
        return std::reverse_iterator<iterator>(self.end());
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> rend(this Self &&self)
    {
        return std::reverse_iterator<iterator>(self.begin());
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> crbegin(this Self &&self)
    {
        return self.rbegin();
    }

    template <class Self>
        requires std::is_lvalue_reference_v<Self>
    CBOR_ALWAYS_INLINE std::reverse_iterator<iterator> crend(this Self &&self)
    {
        return self.rend();
    }

    CBOR_ALWAYS_INLINE E front() const
    {
        auto const element = reflection::typed_array_element_at<E>(bytes, size(), 0);
        validity::throw_logic_error_if(!element.has_value(), "cbor::typed_array_view::front: the view is empty");
        return *element;
    }

    CBOR_ALWAYS_INLINE E back() const
    {
        auto const element = reflection::typed_array_element_at<E>(bytes, size(), size() - 1);
        validity::throw_logic_error_if(!element.has_value(), "cbor::typed_array_view::back: the view is empty");
        return *element;
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> first(std::size_t const n) const &
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(owner, bytes.first(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> first(std::size_t const n) &&
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(std::move(owner), std::exchange(bytes, std::span<char const>()).first(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> last(std::size_t const n) const &
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(owner, bytes.last(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<typed_array_view, error> last(std::size_t const n) &&
    {
        if (error const c = validity::check_count(n, size()).error_or(error{}); c != error{}) [[unlikely]]
            return std::unexpected(c);
        return typed_array_view(std::move(owner), std::exchange(bytes, std::span<char const>()).last(n * sizeof(E)));
    }

    CBOR_ALWAYS_INLINE std::expected<E, error> at(std::size_t const i) const
    {
        return reflection::typed_array_element_at<E>(bytes, size(), i);
    }

    CBOR_ALWAYS_INLINE E operator[](std::size_t const i) const
    {
        auto const element = reflection::typed_array_element_at<E>(bytes, size(), i);
        validity::throw_logic_error_if(!element.has_value(), "cbor::typed_array_view::operator[]: the index is outside of the view");
        return *element;
    }
};

template <class Root>
consteval bool tags_registered()
{
    std::vector<std::uint64_t> numbers;
    bool tagged = true;
    std::apply(
        [&]<class... U>(std::type_identity<U>...) {
            (
                [&] {
                    constexpr auto number = reflection::tag_number_of<U>();
                    tagged = tagged && number.has_value();
                    if constexpr (number.has_value())
                        numbers.push_back(*number);
                }(),
                ...);
        },
        reflection::packing_table_of<Root>{});
    if (!tagged)
        return false;
    std::ranges::sort(numbers);
    return std::ranges::adjacent_find(numbers) == std::ranges::end(numbers);
}

template <class T, class Root>
consteval std::size_t reflection::fixed_size()
{
    using U = std::remove_cv_t<T>;
    constexpr std::size_t initial_byte_size = heads::initial_byte_size;
    if constexpr (std::same_as<U, bool>)
        return initial_byte_size;
    else if constexpr (reflection::has_fixed_underlying_type<U>)
        return fixed_size<std::underlying_type_t<U>, Root>();
#ifdef __SIZEOF_INT128__
    else if constexpr (std::same_as<U, int128> || std::same_as<U, uint128>)
        return heads::head_size(std::to_underlying(rfc8949::tag_number::negative_bignum)) +
               heads::head_size(sizeof(U)) + sizeof(U);
#endif
    else if constexpr (std::is_integral_v<U> && std::has_single_bit(sizeof(U)) && sizeof(U) <= sizeof(std::uint64_t))
        return initial_byte_size + sizeof(U);
    else if constexpr (std::is_floating_point_v<U>) {
        constexpr int digits = std::numeric_limits<U>::digits;
#if defined(__STDCPP_FLOAT16_T__)
        if constexpr (digits == std::numeric_limits<std::float16_t>::digits)
            return initial_byte_size + sizeof(std::float16_t);
        else
#endif
        if constexpr (
#if defined(__STDCPP_BFLOAT16_T__)
            digits == std::numeric_limits<std::bfloat16_t>::digits ||
#endif
            digits == std::numeric_limits<float>::digits)
            return initial_byte_size + sizeof(float);
        else if constexpr (digits == std::numeric_limits<double>::digits)
            return initial_byte_size + sizeof(double);
#if defined(__SIZEOF_INT128__) && defined(__STDCPP_FLOAT128_T__)
        else if constexpr (digits == heads::extended_precision_digits ||
                           digits == std::numeric_limits<std::float128_t>::digits)
            return heads::head_size(std::to_underlying(rfc8746::tag_number::float128_big_endian)) +
                   heads::head_size(sizeof(std::float128_t)) + sizeof(std::float128_t);
#endif
        else
            return detail::no_fixed_size<T>();
    } else if constexpr (requires { reflection::fixed_length<U>::value; }) {
        using E = typename reflection::fixed_length<U>::element;
        constexpr std::size_t n = reflection::fixed_length<U>::value;
        if constexpr (std::same_as<E, char> || std::same_as<E, char8_t> || std::same_as<E, unsigned char> ||
                      std::same_as<E, std::byte>) {
            constexpr auto size = validity::checked_add(heads::head_size(n), n);
            static_assert(size.has_value(), "The fixed size must fit in std::size_t.");
            return *size;
        } else {
            constexpr std::size_t element = fixed_size<E, Root>();
            constexpr auto size = validity::checked_mul(n, element).and_then([](std::size_t const elements) {
                return validity::checked_add(heads::head_size(n), elements);
            });
            static_assert(size.has_value(), "The fixed size must fit in std::size_t.");
            return *size;
        }
    } else if constexpr (detail::is_std_variant<U>) {
        return reflection::dynamic_type_sizes;
    } else if constexpr (has_describe_members<U>) {
        std::size_t size = heads::head_size(*reflection::tag_number_of<U>()) + reflection::straight_reference_size<Root, U>() +
                           reflection::item_head;
        bool fits = true;
        reflection::for_each_field<U>([&](auto i) {
            constexpr std::size_t member = fixed_size<describe::member_t<U, decltype(i)::value>, Root>();
            auto const sum = validity::checked_add(size, member);
            fits = fits && sum.has_value();
            size = sum.value_or(size);
        });
        return fits ? size : detail::no_fixed_size<U>();
    } else if constexpr (reflection::is_inline_optional<U>) {
        constexpr std::size_t value = fixed_size<typename U::value_type, Root>();
        constexpr auto size = validity::checked_add(reflection::inline_optional_head, value);
        static_assert(size.has_value(), "The fixed size must fit in std::size_t.");
        return *size;
    }
    else if constexpr (requires(U const &v) {
                           v.has_value();
                           *v;
                           typename U::value_type;
                       } && !requires { typename U::error_type; })
        return reflection::dynamic_type_sizes;
    else if constexpr (std::ranges::sized_range<U>)
        return reflection::dynamic_type_sizes;
    else
        return detail::no_fixed_size<T>();
}

template <class T, std::size_t Member, class Root>
consteval std::size_t reflection::member_offset()
{
    using U = std::remove_cv_t<T>;
    std::size_t offset = heads::head_size(*reflection::tag_number_of<U>()) + reflection::straight_reference_size<Root, U>() +
                         reflection::item_head;
    bool before = true;
    reflection::for_each_field<U>([&](auto i) {
        before = before && decltype(i)::value != Member;
        if (before)
            offset += fixed_size<describe::member_t<U, decltype(i)::value>, Root>();
    });
    return Member < describe::members_count<U> ? offset : detail::no_fixed_size<T>();
}

template <class T>
class schema
{
    static_assert(reflection::descriptions_valid<T>());

    template <class X>
    static constexpr bool value_holds_no_view = std::is_trivially_copyable_v<X> && !std::same_as<X, std::string_view> &&
                                                !std::same_as<X, std::optional<std::string_view>>;

public:
    template <class U = T>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static consteval std::size_t fixed_size()
    {
        return reflection::fixed_size<U, T>();
    }

    template <class U = T>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static constexpr std::span<char const> description()
    {
        return reflection::description_of<U>();
    }

    template <auto Member, class U = T>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static consteval std::size_t member_offset()
    {
#ifdef __cpp_impl_reflection
        if constexpr (std::is_same_v<decltype(Member), std::meta::info>)
            return reflection::member_offset<U, reflection::member_index_of<std::remove_cv_t<U>, Member>(), T>();
        else
#endif
            return reflection::member_offset<U, reflection::member_index_of<std::remove_cv_t<U>>(Member), T>();
    }

    template <class U = T, bool Verified = true>
    class accessor
    {
        static constexpr bool listed = reflection::is_list<U> || reflection::is_map<U>;

        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::span<char const, reflection::fixed_size<U, T>()> field;
        detail::directory dir;
        [[no_unique_address]] std::conditional_t<listed, reflection::reference, std::monostate> items;

        accessor(std::string_view const b, std::span<char const, reflection::fixed_size<U, T>()> const f, detail::directory const d)
            requires(!listed)
            : encoded(b), field(f), dir(d)
        {
        }

        accessor(std::string_view const b, std::span<char const, reflection::fixed_size<U, T>()> const f, detail::directory const d,
                 reflection::reference const r)
            requires(listed)
            : encoded(b), field(f), dir(d), items(r)
        {
        }

        accessor(std::shared_ptr<void const> o, std::string_view const b, std::span<char const, reflection::fixed_size<U, T>()> const f,
                 detail::directory const d)
            requires(!listed)
            : owner(std::move(o)), encoded(b), field(f), dir(d)
        {
        }

        static consteval std::size_t stride()
            requires(reflection::is_fixed_vector<U> || reflection::is_record_list<U>)
        {
            using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
            if constexpr (reflection::is_fixed_vector<U>)
                return reflection::fixed_width<E>();
            else
                return reflection::fixed_size<E, T>();
        }

        friend class schema;

        friend class reflection;

        template <class, bool>
        friend class accessor;

    public:
        accessor(accessor const &) = default;

        accessor(accessor &&other) noexcept
            : owner(std::move(other.owner)), encoded(std::exchange(other.encoded, std::string_view())), field(other.field),
              dir(other.dir), items(std::exchange(other.items, {}))
        {
        }

        accessor &operator=(accessor const &) = default;

        accessor &operator=(accessor &&other) noexcept
        {
            owner = std::move(other.owner);
            encoded = std::exchange(other.encoded, std::string_view());
            field = other.field;
            dir = other.dir;
            items = std::exchange(other.items, {});
            return *this;
        }

        ~accessor() = default;

        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(((Path.view().starts_with('@') && reflection::path_valid<U, Path, 1>()) ||
                      (Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>())) &&
                     sizeof...(Index) == reflection::index_arguments<Path>() && std::is_lvalue_reference_v<Self>)
        CBOR_ALWAYS_INLINE auto at_path(this Self &&self, Index const... indexes)
        {
            validity::throw_logic_error_if_empty(self.owner, "cbor::schema::accessor::at_path: the accessor was moved from");
            auto result = [&] {
                std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
                constexpr std::string_view path = Path.view();
                if constexpr (path.starts_with('$') && std::same_as<U, T>) {
                    return reflection::walk_path<T, T, Path, 1, Verified>(self.encoded, self.field, self.dir, i);
                } else if constexpr (path.starts_with('$')) {
                    constexpr std::size_t root = reflection::fixed_size<T, T>();
                    return reflection::walk_path<T, T, Path, 1, Verified>(self.encoded,
                                                                std::span<char const>(self.encoded)
                                                                    .subspan(self.encoded.size() - root)
                                                                    .template first<root>(),
                                                                self.dir, i);
                } else if constexpr (reflection::is_typed_array<U> && path.size() > 1) {
                    using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                    constexpr std::size_t close = reflection::index_end(path, 1);
                    std::size_t at;
                    if constexpr (close == 2)
                        at = std::get<0>(i);
                    else
                        at = reflection::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                    return reflection::typed_array_element_at<E>(reflection::list_span(self.encoded, self.items, sizeof(E)),
                                                                  self.items.length, at);
                } else if constexpr (reflection::is_fixed_vector<U> && path.size() > 1) {
                    using E = std::remove_cv_t<std::ranges::range_value_t<U>>;
                    constexpr std::size_t close = reflection::index_end(path, 1);
                    using X = typename decltype(reflection::fixed_path_result<T, E, Path, close + 1, Verified>())::type;
                    std::size_t at;
                    if constexpr (close == 2)
                        at = std::get<0>(i);
                    else
                        at = reflection::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                    constexpr std::size_t width = reflection::fixed_width<E>();
                    auto const position = validity::check_index(at, self.items.length);
                    if (!position) [[unlikely]]
                        return std::expected<X, error>(std::unexpect, position.error());
                    return reflection::walk_fixed<T, E, Path, close + 1, Verified>(
                        self.encoded,
                        validity::element_span(reflection::list_span(self.encoded, self.items, width), *position, width)
                            .template first<width>(),
                        self.dir, i);
                } else if constexpr (reflection::is_list<U> && path.size() > 1) {
                    using E = std::ranges::range_value_t<U>;
                    constexpr std::size_t close = reflection::index_end(path, 1);
                    using X = typename decltype(reflection::path_result<T, E, Path, close + 1, Verified>())::type;
                    std::size_t at;
                    if constexpr (close == 2)
                        at = std::get<0>(i);
                    else
                        at = reflection::index_of<U>(std::string_view(std::span(path).subspan(2, close - 2)));
                    constexpr std::size_t width = reflection::fixed_size<E, T>();
                    auto const position = validity::check_index(at, self.items.length);
                    if (!position) [[unlikely]]
                        return std::expected<X, error>(std::unexpect, position.error());
                    return reflection::walk_path<T, E, Path, close + 1, Verified>(
                        self.encoded,
                        validity::element_span(reflection::list_span(self.encoded, self.items, width), *position, width)
                            .template first<width>(),
                        self.dir, i);
                } else {
                    return reflection::walk_path<T, U, Path, 1, Verified>(self.encoded, self.field, self.dir, i);
                }
            
            }();
            if constexpr (requires { result->owner; })
                if (result)
                    result->owner = self.owner;
            if constexpr (requires { (*result)->owner; })
                if (result && *result)
                    (*result)->owner = self.owner;
            return result;
        }

        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(std::same_as<U, T> && Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>() &&
                     sizeof...(Index) == reflection::index_arguments<Path>())
        auto view(this Self &&self, Index const... indexes)
        {
            using X = typename decltype(reflection::path_result<T, T, Path, 1, Verified>())::type;
            using E = typename decltype([]<class V>(std::type_identity<accessor<V, Verified>>) {
                static_assert(reflection::is_typed_array<V>, "cbor::schema::accessor::view: the path does not end at a typed array");
                return std::type_identity<std::remove_cv_t<std::ranges::range_value_t<V>>>{};
            }(std::type_identity<X>{}))::type;
            auto const list = self.template at_path<Path>(indexes...);
            if (!list) [[unlikely]]
                return std::expected<typed_array_view<E>, error>(std::unexpect, list.error());
            return std::expected<typed_array_view<E>, error>(
                typed_array_view<E>(std::forward_like<Self>(self.owner), reflection::list_span(self.encoded, list->items, sizeof(E))));
        }

        std::size_t size() const
            requires(listed)
        {
            return items.length;
        }

        using iterator = typename decltype(reflection::list_iterator_result<T, U, Verified>())::type;

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        iterator begin(this Self &&self)
        {
            validity::throw_logic_error_if_empty(self.owner, "cbor::schema::accessor::begin: the accessor was moved from");
            return iterator(self.owner, self.encoded, reflection::list_span(self.encoded, self.items, stride()), 0, self.items.length,
                            self.dir);
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        iterator end(this Self &&self)
        {
            return iterator(self.owner, self.encoded, reflection::list_span(self.encoded, self.items, stride()), self.items.length,
                            self.items.length, self.dir);
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        iterator cbegin(this Self &&self)
        {
            return self.begin();
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        iterator cend(this Self &&self)
        {
            return self.end();
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        std::reverse_iterator<iterator> rbegin(this Self &&self)
        {
            return std::reverse_iterator<iterator>(self.end());
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        std::reverse_iterator<iterator> rend(this Self &&self)
        {
            return std::reverse_iterator<iterator>(self.begin());
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        std::reverse_iterator<iterator> crbegin(this Self &&self)
        {
            return self.rbegin();
        }

        template <class Self>
            requires(std::is_lvalue_reference_v<Self> && (reflection::is_fixed_vector<U> || reflection::is_record_list<U>))
        std::reverse_iterator<iterator> crend(this Self &&self)
        {
            return self.rend();
        }
    };

    template <class S, bool Verified>
    class accessor<fixed_element<S>, Verified>
    {
        std::shared_ptr<void const> owner;
        std::string_view encoded;
        std::span<char const, reflection::fixed_width<S>()> element;
        detail::directory dir;

        accessor(std::string_view const b, std::span<char const, reflection::fixed_width<S>()> const e, detail::directory const d)
            : encoded(b), element(e), dir(d)
        {
        }

        friend class schema;

        friend class reflection;

        template <class, bool>
        friend class accessor;

    public:
        template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
            requires(((Path.view().starts_with('@') && reflection::path_valid<S, Path, 1>()) ||
                      (Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>())) &&
                     sizeof...(Index) == reflection::index_arguments<Path>() && std::is_lvalue_reference_v<Self>)
        CBOR_ALWAYS_INLINE auto at_path(this Self &&self, Index const... indexes)
        {
            validity::throw_logic_error_if_empty(self.owner, "cbor::schema::accessor::at_path: the accessor was moved from");
            auto result = [&] {
                std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
                if constexpr (Path.view().starts_with('$')) {
                    constexpr std::size_t root = reflection::fixed_size<T, T>();
                    return reflection::walk_path<T, T, Path, 1, Verified>(
                        self.encoded, std::span<char const>(self.encoded).subspan(self.encoded.size() - root).template first<root>(),
                        self.dir, i);
                } else {
                    return reflection::walk_fixed<T, S, Path, 1, Verified>(self.encoded, self.element, self.dir, i);
                }
            }();
            if constexpr (requires { result->owner; })
                if (result)
                    result->owner = self.owner;
            if constexpr (requires { (*result)->owner; })
                if (result && *result)
                    (*result)->owner = self.owner;
            return result;
        }
    };

    template <bool Verified, class... A>
    class accessor<std::variant<A...>, Verified>
    {
        std::shared_ptr<void const> owner;
        std::string_view encoded;
        reflection::variant_reference held;
        detail::directory dir;

        accessor(std::string_view const b, reflection::variant_reference const h, detail::directory const d)
            : encoded(b), held(h), dir(d)
        {
        }

        friend class schema;

        friend class reflection;

        template <class, bool>
        friend class accessor;

    public:
        accessor(accessor const &) = default;

        accessor(accessor &&other) noexcept
            : owner(std::move(other.owner)), encoded(std::exchange(other.encoded, std::string_view())), held(other.held),
              dir(other.dir)
        {
        }

        accessor &operator=(accessor const &) = default;

        accessor &operator=(accessor &&other) noexcept
        {
            owner = std::move(other.owner);
            encoded = std::exchange(other.encoded, std::string_view());
            held = other.held;
            dir = other.dir;
            return *this;
        }

        ~accessor() = default;

        template <class Self>
            requires std::is_lvalue_reference_v<Self>
        std::size_t index(this Self &&self)
        {
            validity::throw_logic_error_if_empty(self.owner, "cbor::schema::accessor::index: the accessor was moved from");
            return self.held.index;
        }

        template <class E, class Self>
            requires(std::is_lvalue_reference_v<Self> && (std::same_as<E, A> || ...))
        std::expected<accessor<E, Verified>, error> get(this Self &&self)
        {
            validity::throw_logic_error_if_empty(self.owner, "cbor::schema::accessor::get: the accessor was moved from");
            constexpr std::size_t k = reflection::index_in<E>(std::tuple<std::type_identity<A>...>{});
            if (self.held.index != k) [[unlikely]]
                return std::unexpected(error::incorrect_type);
            constexpr std::size_t n = reflection::fixed_size<E, T>();
            return accessor<E, Verified>(self.owner, self.encoded,
                                         std::span<char const>(self.encoded).subspan(self.held.data).template first<n>(), self.dir);
        }
    };

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, true>, error> path(Encoded &&encoded)
    {
        return path_with_owner<true, D>(std::make_shared<Encoded const>(std::move(encoded)));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, true>, error> path(std::shared_ptr<Encoded const> encoded)
    {
        return path_with_owner<true, D>(std::move(encoded));
    }

    template <description_place D = description_place::inside, borrowed_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, true>, error> path(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        return path_with_owner<true, D>(std::move(owner), std::string_view(std::span<char const>(encoded)));
    }

    template <fixed_string Path, description_place D = description_place::inside, std::convertible_to<std::size_t>... Index>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && Path.view().starts_with('$') &&
                 reflection::path_valid<T, Path, 1>() && sizeof...(Index) == reflection::index_arguments<Path>() &&
                 value_holds_no_view<typename decltype(reflection::path_result<T, T, Path, 1>())::type>)
    static auto at_path(std::string_view const encoded, Index const... indexes)
    {
        return at_path_with_directory<Path, true, D>(encoded, indexes...);
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static std::expected<ref<T>, error> decode(Encoded &&encoded)
    {
        return decode_with_owner<true, D>(std::move(encoded));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static std::expected<ref<T>, error> decode(std::shared_ptr<Encoded const> encoded)
    {
        return decode_with_owner<true, D>(std::move(encoded));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && std::is_default_constructible_v<T>)
    static std::expected<ref<T>, error> decode(std::shared_ptr<Encoded const> encoded, T target)
    {
        return decode_with_owner<true, D>(std::move(encoded), std::move(target));
    }

    template <description_place D = description_place::inside, borrowed_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static std::expected<ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        return decode_with_owner<true, D>(std::move(owner), std::string_view(std::span<char const>(encoded)));
    }

    template <description_place D = description_place::inside, borrowed_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && std::is_default_constructible_v<T>)
    static std::expected<ref<T>, error> decode(std::shared_ptr<void const> owner, Encoded const &encoded, T target)
    {
        return decode_with_owner<true, D>(std::move(owner), std::string_view(std::span<char const>(encoded)),
                                     std::move(target));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, false>, error> unverified_path(Encoded &&encoded)
    {
        return path_with_owner<false, D>(std::make_shared<Encoded const>(std::move(encoded)));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, false>, error> unverified_path(std::shared_ptr<Encoded const> encoded)
    {
        return path_with_owner<false, D>(std::move(encoded));
    }

    template <description_place D = description_place::inside, borrowed_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, false>, error> unverified_path(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        return path_with_owner<false, D>(std::move(owner), std::string_view(std::span<char const>(encoded)));
    }

    template <fixed_string Path, description_place D = description_place::inside, std::convertible_to<std::size_t>... Index>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && Path.view().starts_with('$') &&
                 reflection::path_valid<T, Path, 1>() && sizeof...(Index) == reflection::index_arguments<Path>() &&
                 value_holds_no_view<typename decltype(reflection::path_result<T, T, Path, 1>())::type>)
    static auto unverified_at_path(std::string_view const encoded, Index const... indexes)
    {
        return at_path_with_directory<Path, false, D>(encoded, indexes...);
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static std::expected<ref<T>, error> unverified_decode(Encoded &&encoded)
    {
        return decode_with_owner<false, D>(std::move(encoded));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<Encoded const> encoded)
    {
        return decode_with_owner<false, D>(std::move(encoded));
    }

    template <description_place D = description_place::inside, owning_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && std::is_default_constructible_v<T>)
    static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<Encoded const> encoded, T target)
    {
        return decode_with_owner<false, D>(std::move(encoded), std::move(target));
    }

    template <description_place D = description_place::inside, borrowed_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<void const> owner, Encoded const &encoded)
    {
        return decode_with_owner<false, D>(std::move(owner), std::string_view(std::span<char const>(encoded)));
    }

    template <description_place D = description_place::inside, borrowed_range_of_chars Encoded>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && std::is_default_constructible_v<T>)
    static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<void const> owner, Encoded const &encoded, T target)
    {
        return decode_with_owner<false, D>(std::move(owner), std::string_view(std::span<char const>(encoded)),
                                     std::move(target));
    }

    template <description_place D = description_place::inside>
    CBOR_ALWAYS_INLINE static std::expected<std::string, error> encode(T const &value)
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    {
        reflection::second_item<T> second;
        second.add(value);
        auto const size = reflection::encoded_size<T, D>(second);
        if (!size) [[unlikely]]
            return std::unexpected(validity::writer_error(size.error()));
        auto const sum = validity::checked_add(*size, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        std::string out(padded, '\0');
        out.resize(reflection::write_encoded<false, D>(std::span<char>(out), value, second));
        return out;
    }

    template <description_place D = description_place::inside, class Target>
        requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
    CBOR_ALWAYS_INLINE static std::expected<std::size_t, error> encode(T const &value, Target &&target)
    {
        using U = std::remove_cvref_t<Target>;
        reflection::second_item<T> second;
        second.add(value);
        auto const size = reflection::encoded_size<T, D>(second);
        if (!size) [[unlikely]]
            return std::unexpected(validity::writer_error(size.error()));
        auto const sum = validity::checked_add(*size, heads::head_padding);
        if (!sum) [[unlikely]]
            return std::unexpected(validity::writer_error(sum.error()));
        std::size_t const padded = *sum;
        if constexpr (encoding::byte_container<U> && requires { requires std::same_as<std::ranges::range_value_t<U>, char>; }) {
            std::size_t const at = std::ranges::size(target);
            auto const total = validity::checked_add(at, padded);
            if (!total) [[unlikely]]
                return std::unexpected(validity::writer_error(total.error()));
            target.reserve(*total);
            target.insert(target.end(), padded, char{});
            reflection::write_encoded<false, D>(std::span<char>(target).subspan(at), value, second);
            target.resize(at + *size);
            return *size;
        } else if constexpr (!encoding::byte_container<U> && requires { std::span<char>(target); }) {
            std::span<char> const out(target);
            if (validity::bytes_fit(padded, out.size())) {
                reflection::write_encoded<false, D>(out.first(padded), value, second);
                return *size;
            }
        }
        decltype(auto) message = encoding::message_of(target, *size);
        if constexpr (requires { std::span<char>(message); }) {
            std::span<char> const out(message);
            if (!validity::bytes_fit(*size, out.size())) [[unlikely]]
                return std::unexpected(error::no_buffer_space);
            reflection::write_encoded<true, D>(out.first(*size), value, second);
            if (auto const r = message.done(*size); !r) [[unlikely]]
                return std::unexpected(validity::writer_error(r.error()));
            return *size;
        } else {
            std::string encoded(padded, '\0');
            encoded.resize(reflection::write_encoded<false, D>(std::span<char>(encoded), value, second));
            if (auto const r = message.append(encoded); !r) [[unlikely]]
                return std::unexpected(validity::writer_error(r.error()));
            if (auto const r = message.done(encoded.size()); !r) [[unlikely]]
                return std::unexpected(validity::writer_error(r.error()));
            return encoded.size();
        }
    }

private:
    template <fixed_string Path, bool Verified, description_place D, std::convertible_to<std::size_t>... Index>
    static auto at_path_with_directory(std::string_view const encoded, Index const... indexes)
    {
        using X = typename decltype(reflection::path_result<T, T, Path, 1, Verified>())::type;
        auto const dir = reflection::read_directory<T, Verified, D>(encoded);
        if (!dir) [[unlikely]]
            return std::expected<X, error>(std::unexpect, dir.error());
        std::array<std::size_t, sizeof...(Index)> const i{static_cast<std::size_t>(indexes)...};
        std::expected<X, error> read =
            reflection::walk_path<T, T, Path, 1, Verified>(encoded,
                                                           std::span<char const>(encoded)
                                                               .subspan(encoded.size() - fixed_size())
                                                               .template first<fixed_size()>(),
                                                           *dir, i);
        if constexpr (Verified)
            if (!read) [[unlikely]]
                return std::expected<X, error>(std::unexpect, reflection::too_little_data_if_truncated<T, D>(encoded, read.error()));
        return read;
    }

    template <bool Verified, description_place D, owning_range_of_chars Encoded>
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, Verified>, error> path_with_owner(std::shared_ptr<Encoded const> encoded)
    {
        validity::throw_logic_error_if_null(encoded, "cbor::schema::path: the encoded data item is null");
        std::string_view const bytes{std::span<char const>(*encoded)};
        return path_with_owner<Verified, D>(std::shared_ptr<void const>(std::move(encoded)), bytes);
    }

    template <bool Verified, description_place D>
    CBOR_ALWAYS_INLINE static std::expected<accessor<T, Verified>, error> path_with_owner(std::shared_ptr<void const> owner, std::string_view const encoded)
    {
        validity::throw_logic_error_if_empty(
            owner, "cbor::schema::path: the owner of the encoded data item is empty");
        auto const dir = reflection::read_directory<T, Verified, D>(encoded);
        if (!dir) [[unlikely]]
            return std::unexpected(dir.error());
        auto const root = std::span<char const>(encoded).subspan(encoded.size() - fixed_size()).template first<fixed_size()>();
        if constexpr (Verified)
            if (!reflection::class_tag_valid<T, T>(root)) [[unlikely]]
                return std::unexpected(reflection::too_little_data_if_truncated<T, D>(encoded, error::incorrect_type));
        return accessor<T, Verified>(std::move(owner), encoded, root, *dir);
    }

    template <bool Verified, description_place D, owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode_with_owner(Encoded &&encoded)
    {
#if defined(__cpp_exceptions)
        try {
#endif
            return decode_with_owner<Verified, D>(std::make_shared<Encoded const>(std::move(encoded)));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }

    template <bool Verified, description_place D, owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<Encoded const> encoded)
    {
        validity::throw_logic_error_if_null(encoded, "cbor::schema::decode: the encoded data item is null");
        std::string_view const bytes{std::span<char const>(*encoded)};
        return decode_with_owner<Verified, D>(std::shared_ptr<void const>(std::move(encoded)), bytes);
    }

    template <bool Verified, description_place D>
    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<void const> owner, std::string_view const encoded)
    {
        if constexpr (std::is_default_constructible_v<T>) {
            return decode_with_owner<Verified, D>(std::move(owner), encoded, T{});
        } else {
            validity::throw_logic_error_if_empty(owner, "cbor::schema::decode: the owner of the encoded data item is empty");
#if defined(__cpp_exceptions)
            try {
#endif
                std::optional<T> made;
                if (auto const r = reflection::read_root<T, Verified, D>(made, encoded, limits::nesting_depth); !r) [[unlikely]]
                    return std::unexpected(Verified ? reflection::too_little_data_if_truncated<T, D>(encoded, r.error()) : r.error());
                return ref<T>(std::move(owner), std::move(*made));
#if defined(__cpp_exceptions)
            } catch (std::bad_alloc const &) {
                return std::unexpected(error::not_enough_memory);
            }
#endif
        }
    }

    template <bool Verified, description_place D, owning_range_of_chars Encoded>
    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<Encoded const> encoded, T &&target)
    {
        validity::throw_logic_error_if_null(encoded, "cbor::schema::decode: the encoded data item is null");
        std::string_view const bytes{std::span<char const>(*encoded)};
        return decode_with_owner<Verified, D>(std::shared_ptr<void const>(std::move(encoded)), bytes, std::move(target));
    }

    template <bool Verified, description_place D>
    static std::expected<ref<T>, error> decode_with_owner(std::shared_ptr<void const> owner,
                                                         std::string_view const encoded, T &&target)
    {
        validity::throw_logic_error_if_empty(owner,
                                             "cbor::schema::decode: the owner of the encoded data item is empty");
#if defined(__cpp_exceptions)
        try {
#endif
            if (auto const r = reflection::read_root<T, Verified, D>(target, encoded, limits::nesting_depth); !r)
                [[unlikely]]
                return std::unexpected(Verified ? reflection::too_little_data_if_truncated<T, D>(encoded, r.error()) : r.error());
            return ref<T>(std::move(owner), std::move(target));
#if defined(__cpp_exceptions)
        } catch (std::bad_alloc const &) {
            return std::unexpected(error::not_enough_memory);
        }
#endif
    }
};

}
