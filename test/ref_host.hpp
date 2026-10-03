#pragma once

#include <cbor/cbor.hpp>

#include <cstdint>
#include <map>
#include <set>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace shared_test
{

// A value is a handle, as mrb_value is: two handles to one node are the same object.
struct node;
using handle = std::shared_ptr<node>;

// A registered object of the host: its tag number and the value that before_encode gives for it.
struct object {
    std::uint64_t tag;
    handle content;
};

struct node {
    std::variant<std::uint64_t, std::string, std::vector<handle>, std::vector<std::pair<handle, handle>>,
                 object>
        kind;
};

// The host keeps a weak reference to every node it makes. When the host ends, every node that only
// other nodes hold is garbage, as a tracing collector finds it, and is emptied; so a cycle that a
// failed decode left behind is freed, and a value the caller still holds keeps its whole graph.
struct ref_host {
    using value = handle;
    using identity = node const *;
    int before_encode_calls = 0;
    int after_decode_calls = 0;
    handle replacement;
    std::vector<std::weak_ptr<node>> made;

    handle make(node n)
    {
        auto h = std::make_shared<node>(std::move(n));
        made.push_back(h);
        return h;
    }

    ref_host() = default;
    ref_host(ref_host const &) = delete;
    ref_host &operator=(ref_host const &) = delete;

    ~ref_host()
    {
        std::vector<handle> live;
        for (auto const &w : made)
            if (auto h = w.lock())
                live.push_back(std::move(h));
        std::map<node const *, long> inner;
        auto const edges = [](node const &n, auto const &visit) {
            if (auto const *a = std::get_if<std::vector<handle>>(&n.kind))
                for (auto const &e : *a)
                    visit(e);
            else if (auto const *m = std::get_if<std::vector<std::pair<handle, handle>>>(&n.kind))
                for (auto const &[k, v] : *m) {
                    visit(k);
                    visit(v);
                }
            else if (auto const *o = std::get_if<object>(&n.kind))
                visit(o->content);
        };
        for (auto const &h : live)
            edges(*h, [&](handle const &e) {
                if (e)
                    ++inner[e.get()];
            });
        std::vector<node const *> todo;
        std::set<node const *> reached;
        for (auto const &h : live)
            if (h.use_count() - 1 > inner[h.get()])
                todo.push_back(h.get());
        while (!todo.empty()) {
            node const *n = todo.back();
            todo.pop_back();
            if (!reached.insert(n).second)
                continue;
            edges(*n, [&](handle const &e) {
                if (e)
                    todo.push_back(e.get());
            });
        }
        for (auto const &h : live)
            if (!reached.contains(h.get()))
                h->kind = std::uint64_t{0};
    }
};

inline bool tag_invoke(cbor::cyclic_data_structures_t, ref_host &)
{
    return true;
}

inline handle tag_invoke(cbor::unsigned_integer_decode_t, ref_host &host, std::uint64_t a)
{
    return host.make(node{a});
}

inline handle tag_invoke(cbor::negative_integer_decode_t, ref_host &host, std::uint64_t a)
{
    return host.make(node{a});
}

inline handle tag_invoke(cbor::unsigned_bignum_decode_t, ref_host &host, std::string_view m)
{
    return host.make(node{std::string(m)});
}

inline handle tag_invoke(cbor::negative_bignum_decode_t, ref_host &host, std::string_view m)
{
    return host.make(node{std::string(m)});
}

inline handle tag_invoke(cbor::byte_string_decode_t, ref_host &host, std::string_view b)
{
    return host.make(node{std::string(b)});
}

inline handle tag_invoke(cbor::text_string_decode_t, ref_host &host, std::string_view t)
{
    return host.make(node{std::string(t)});
}

inline handle tag_invoke(cbor::float_decode_t, ref_host &host, double)
{
    return host.make(node{std::uint64_t{0}});
}

inline handle tag_invoke(cbor::simple_value_decode_t, ref_host &host, std::uint8_t s)
{
    return host.make(node{std::uint64_t{s}});
}

inline handle tag_invoke(cbor::array_decode_t, ref_host &host, std::uint64_t)
{
    return host.make(node{std::vector<handle>{}});
}

// The node changes and the same handle comes back, so a reference into a container under
// construction stays valid.
inline handle tag_invoke(cbor::array_append_t, ref_host &, handle a, handle e)
{
    std::get<std::vector<handle>>(a->kind).push_back(std::move(e));
    return a;
}

inline handle tag_invoke(cbor::map_decode_t, ref_host &host, std::uint64_t)
{
    return host.make(node{std::vector<std::pair<handle, handle>>{}});
}

inline handle tag_invoke(cbor::map_insert_t, ref_host &, handle m, handle k, handle v)
{
    std::get<std::vector<std::pair<handle, handle>>>(m->kind).emplace_back(std::move(k), std::move(v));
    return m;
}

// Tag 5000 is registered in this host: tag_begin makes the empty object before its content.
inline std::optional<handle> tag_invoke(cbor::tag_begin_t, ref_host &host, std::uint64_t tag)
{
    if (tag != 5000)
        return std::nullopt;
    return host.make(node{object{5000, nullptr}});
}

inline handle tag_invoke(cbor::registered_decode_t, ref_host &, handle o, handle content)
{
    std::get<object>(o->kind).content = std::move(content);
    return o;
}

// The hook returns the replacement when the test set one, else the object itself.
inline handle tag_invoke(cbor::after_decode_t, ref_host &host, handle o)
{
    ++host.after_decode_calls;
    return host.replacement ? host.replacement : o;
}

inline handle tag_invoke(cbor::tag_decode_t, ref_host &, std::uint64_t, handle content)
{
    return content;
}

// The answers of this host to the questions of the encoder. A string key has no identity, as mruby
// copies an unfrozen String key; every other node is its own identity, an integer has none.
inline cbor::kind tag_invoke(cbor::kind_of_t, ref_host &, handle const &v)
{
    if (std::holds_alternative<std::uint64_t>(v->kind))
        return cbor::kind::unsigned_integer;
    if (std::holds_alternative<std::string>(v->kind))
        return cbor::kind::text_string;
    if (std::holds_alternative<std::vector<handle>>(v->kind))
        return cbor::kind::array;
    if (std::holds_alternative<object>(v->kind))
        return cbor::kind::registered;
    return cbor::kind::map;
}

inline std::uint64_t tag_invoke(cbor::registered_tag_t, ref_host &, handle const &v)
{
    return std::get<object>(v->kind).tag;
}

inline handle tag_invoke(cbor::before_encode_t, ref_host &host, handle const &v)
{
    ++host.before_encode_calls;
    return std::get<object>(v->kind).content;
}

inline std::uint64_t tag_invoke(cbor::unsigned_of_t, ref_host &, handle const &v)
{
    return std::get<std::uint64_t>(v->kind);
}

inline std::string_view tag_invoke(cbor::text_of_t, ref_host &, handle const &v)
{
    return std::get<std::string>(v->kind);
}

inline std::uint64_t tag_invoke(cbor::array_size_t, ref_host &, handle const &v)
{
    return std::get<std::vector<handle>>(v->kind).size();
}

inline handle const &tag_invoke(cbor::array_at_t, ref_host &, handle const &v, std::uint64_t i)
{
    return std::get<std::vector<handle>>(v->kind).at(i);
}

inline std::uint64_t tag_invoke(cbor::map_size_t, ref_host &, handle const &v)
{
    return std::get<std::vector<std::pair<handle, handle>>>(v->kind).size();
}

template <class F>
inline void tag_invoke(cbor::map_for_each_t, ref_host &, handle const &v, F const &f)
{
    for (auto const &[key, val] : std::get<std::vector<std::pair<handle, handle>>>(v->kind))
        f(key, val);
}

inline std::optional<node const *> tag_invoke(cbor::value_identity_t, ref_host &, handle const &v)
{
    if (std::holds_alternative<std::uint64_t>(v->kind))
        return std::nullopt;
    return v.get();
}

inline std::optional<node const *> tag_invoke(cbor::key_identity_t, ref_host &h, handle const &v)
{
    if (std::holds_alternative<std::string>(v->kind))
        return std::nullopt;
    return tag_invoke(cbor::value_identity, h, v);
}

} // namespace shared_test
