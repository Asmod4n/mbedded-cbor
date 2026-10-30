#pragma once

#include "lazy.hpp"

namespace site
{

inline result notation_made(call const &c)
{
    auto const bytes = text_argument(c, 0);
    if (!bytes || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::diagnostic_notation takes the bytes of one item");
    return value_of(cbor::diagnostic_notation(*bytes));
}

inline result size_read(call const &c)
{
    auto const bytes = text_argument(c, 0);
    if (!bytes || c.arguments.size() != 1) [[unlikely]]
        return std::unexpected("cbor::item_size takes the bytes as one text");
    return value_of(cbor::item_size(*bytes));
}

inline result string_substr(call const &c)
{
    auto const self = self_of<std::string>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    if (c.arguments.empty() || c.arguments.size() > 2) [[unlikely]]
        return std::unexpected("substr takes a position and a count");
    auto const pos = unsigned_argument(c, 0);
    if (!pos) [[unlikely]]
        return std::unexpected(pos.error());
    if (*pos > (*self)->size()) [[unlikely]]
        return std::unexpected("the position is after the end of the string");
    if (c.arguments.size() == 1)
        return value_of((*self)->substr(static_cast<std::size_t>(*pos)));
    auto const count = unsigned_argument(c, 1);
    if (!count) [[unlikely]]
        return std::unexpected(count.error());
    return value_of((*self)->substr(static_cast<std::size_t>(*pos), static_cast<std::size_t>(*count)));
}

inline result sequence_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const owner = argument_of<std::shared_ptr<std::string const>>(c, 0, "a std::shared_ptr<std::string const>");
    if (!owner) [[unlikely]]
        return std::unexpected(owner.error());
    return value_of(cbor::sequence(**owner));
}

inline result sequence_begin(call const &c)
{
    auto const self = self_of<cbor::sequence>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return view_of(*c.self, (*self)->begin());
}

inline result sequence_empty(call const &c)
{
    auto const self = self_of<cbor::sequence>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return value_of((*self)->empty());
}

inline result iterator_star(call const &c)
{
    auto const self = self_of<cbor::sequence::iterator>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    auto const item = ***self;
    if (!item) [[unlikely]]
        return value_of(item);
    value out = view_of(*c.self, *item);
    out.expected = true;
    return out;
}

inline result reader_made(call const &c)
{
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(cbor::sequence_reader{});
}

inline result reader_feed(call const &c)
{
    auto *const self = c.self == nullptr ? nullptr : changed_if<cbor::sequence_reader>(*c.self);
    if (self == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    if (auto const ok = arguments_counted(c, 1); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    auto const piece = argument_of<std::string>(c, 0, "a std::string");
    if (!piece) [[unlikely]]
        return std::unexpected(piece.error());
    self->feed(std::string(**piece));
    return value_of(std::monostate{});
}

inline result reader_next(call const &c)
{
    auto *const self = c.self == nullptr ? nullptr : changed_if<cbor::sequence_reader>(*c.self);
    if (self == nullptr) [[unlikely]]
        return std::unexpected("the call needs an object of its class");
    if (auto const ok = arguments_counted(c, 0); !ok) [[unlikely]]
        return std::unexpected(ok.error());
    return value_of(self->next());
}

inline result reader_empty(call const &c)
{
    auto const self = self_of<cbor::sequence_reader>(c);
    if (!self) [[unlikely]]
        return std::unexpected(self.error());
    return value_of((*self)->empty());
}

inline constexpr entry tools_functions[] = {
    {"cbor::diagnostic_notation()", notation_made},
    {"cbor::item_size()", size_read},
    {"std::string.substr()", string_substr},
    {"cbor::sequence()", sequence_made},
    {"cbor::sequence{}", sequence_made},
    {"cbor::sequence.begin()", sequence_begin},
    {"cbor::sequence.empty()", sequence_empty},
    {"cbor::sequence::iterator.operator*()", iterator_star},
    {"cbor::sequence_reader{}", reader_made},
    {"cbor::sequence_reader.feed()", reader_feed, true},
    {"cbor::sequence_reader.next()", reader_next, true},
    {"cbor::sequence_reader.empty()", reader_empty},
};

}
