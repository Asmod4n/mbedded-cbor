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

// A registered object of the binding: its tag number and the value that before_encode gives for it.
struct object {
    std::uint64_t tag;
    handle content;
};

struct node {
    std::variant<std::uint64_t, std::string, std::vector<handle>, std::vector<std::pair<handle, handle>>,
                 object>
        kind;
};

// The binding keeps a weak reference to every node it makes. When the binding ends, every node that only
// other nodes hold is garbage, as a tracing collector finds it, and is emptied; so a cycle that a
// failed decode left behind is freed, and a value the caller still holds keeps its whole graph.
struct ref_binding {
    using value = handle;

    handle unsigned_integer_decode(std::uint64_t const a)
    {
        return make(node{a});
    }

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

    ref_binding() = default;
    ref_binding(ref_binding const &) = delete;
    ref_binding &operator=(ref_binding const &) = delete;

    ~ref_binding()
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

    handle negative_integer_decode(std::uint64_t a)
    {
        return make(node{a});
    }

    handle unsigned_bignum_decode(std::string_view m)
    {
        return make(node{std::string(m)});
    }

    handle negative_bignum_decode(std::string_view m)
    {
        return make(node{std::string(m)});
    }

    handle byte_string_decode(std::string_view b)
    {
        return make(node{std::string(b)});
    }

    handle text_string_decode(std::string_view t)
    {
        return make(node{std::string(t)});
    }

    handle float_decode(double)
    {
        return make(node{std::uint64_t{0}});
    }

    handle simple_value_decode(std::uint8_t s)
    {
        return make(node{std::uint64_t{s}});
    }

    handle array_decode(std::uint64_t)
    {
        return make(node{std::vector<handle>{}});
    }

    // The node changes and the same handle comes back, so a reference into a container under
    // construction stays valid.
    handle array_append(handle a, handle e)
    {
        std::get<std::vector<handle>>(a->kind).push_back(std::move(e));
        return a;
    }

    handle map_decode(std::uint64_t)
    {
        return make(node{std::vector<std::pair<handle, handle>>{}});
    }

    handle map_insert(handle m, handle k, handle v)
    {
        std::get<std::vector<std::pair<handle, handle>>>(m->kind).emplace_back(std::move(k), std::move(v));
        return m;
    }

    handle tag_decode(std::uint64_t, handle content)
    {
        return content;
    }

    bool cyclic_data_structures()
    {
        return true;
    }

    std::optional<handle> tag_begin(std::uint64_t tag)
    {
        if (tag != 5000)
            return std::nullopt;
        return make(node{object{5000, nullptr}});
    }

    handle registered_decode(handle o, handle content)
    {
        std::get<object>(o->kind).content = std::move(content);
        return o;
    }

    handle after_decode(handle o)
    {
        ++after_decode_calls;
        return replacement ? replacement : o;
    }

    cbor::kind kind_of(handle const &v)
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

    std::uint64_t registered_tag(handle const &v)
    {
        return std::get<object>(v->kind).tag;
    }

    handle before_encode(handle const &v)
    {
        ++before_encode_calls;
        return std::get<object>(v->kind).content;
    }

    std::uint64_t unsigned_of(handle const &v)
    {
        return std::get<std::uint64_t>(v->kind);
    }

    std::string_view text_of(handle const &v)
    {
        return std::get<std::string>(v->kind);
    }

    std::uint64_t array_size(handle const &v)
    {
        return std::get<std::vector<handle>>(v->kind).size();
    }

    handle const & array_at(handle const &v, std::uint64_t i)
    {
        return std::get<std::vector<handle>>(v->kind).at(i);
    }

    std::uint64_t map_size(handle const &v)
    {
        return std::get<std::vector<std::pair<handle, handle>>>(v->kind).size();
    }

    template <class F>
    void map_for_each(handle const &v, F const &f)
    {
        for (auto const &[key, val] : std::get<std::vector<std::pair<handle, handle>>>(v->kind))
            f(key, val);
    }

    std::optional<node const *> value_identity(handle const &v)
    {
        if (std::holds_alternative<std::uint64_t>(v->kind))
            return std::nullopt;
        return v.get();
    }

    std::optional<node const *> key_identity(handle const &v)
    {
        if (std::holds_alternative<std::string>(v->kind))
            return std::nullopt;
        return value_identity(v);
    }
};


// Tag 5000 is registered in this binding: tag_begin makes the empty object before its content.
// The hook returns the replacement when the test set one, else the object itself.
// The answers of this binding to the questions of the encoder. A string key has no identity, as mruby
// copies an unfrozen String key; every other node is its own identity, an integer has none.
} // namespace shared_test
