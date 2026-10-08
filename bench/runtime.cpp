#include <benchmark/benchmark.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "value.hpp"

static std::uint64_t bytes_sum(std::string_view const s)
{
    return std::transform_reduce(s.begin(), s.end(), std::uint64_t{0}, std::plus<>{},
                                 [](char const c) { return std::uint64_t(std::uint8_t(c)); });
}

#if defined(ARM_READ) || defined(ARM_MB_DECODE) || defined(ARM_MB_PATH_ALL)
struct read_binding : cbor::binding<std::uint64_t> {
    std::uint64_t unsigned_integer_decode(std::uint64_t a) { return a; }
    std::uint64_t negative_integer_decode(std::uint64_t a) { return a; }
    std::uint64_t unsigned_bignum_decode(std::string_view m) { return bytes_sum(m); }
    std::uint64_t negative_bignum_decode(std::string_view m) { return bytes_sum(m); }
    std::uint64_t byte_string_decode(std::string_view s) { return bytes_sum(s); }
    std::uint64_t text_string_decode(std::string_view s) { return bytes_sum(s); }
    std::uint64_t float_decode(double f) { return std::bit_cast<std::uint64_t>(f); }
    std::uint64_t simple_value_decode(std::uint8_t s) { return s; }
    std::uint64_t array_decode(std::uint64_t n) { return n; }
    std::uint64_t array_append(std::uint64_t a, std::uint64_t e) { return a + e; }
    std::uint64_t map_decode(std::uint64_t n) { return n; }
    std::uint64_t map_insert(std::uint64_t m, std::uint64_t k, std::uint64_t v) { return m + k + v; }
    std::uint64_t tag_decode(std::uint64_t t, std::uint64_t c) { return t + c; }
};
#endif

#if defined(ARM_LC_PREALLOC) || defined(ARM_LC_READ) || defined(ARM_LC_DECODE)
#include <cbor.h>
#endif

#if defined(ARM_JC_CLEAR) || defined(ARM_JC_READ)
#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
#endif

#if defined(ARM_VG_READ)
#include <cbor_decoder.h>
struct span_decoder final : cbor_decoder {
    std::uint8_t const *p, *end;
    std::uint8_t get_byte() override
    {
        if (p == end)
            throw cbor_decoder_exception("end");
        return *p++;
    }
};
static std::uint64_t vg_read(span_decoder &d, int const depth)
{
    if (depth > 128)
        throw cbor_decoder_exception("depth");
    cbor_object const o = d.read();
    if (o.is_bytes() || o.is_string()) {
        std::uint64_t const n = o.raw_value();
        if (n > std::uint64_t(d.end - d.p))
            throw cbor_decoder_exception("end");
        std::uint64_t const sum = bytes_sum(std::string_view(reinterpret_cast<char const *>(d.p), n));
        d.p += n;
        return sum;
    }
    std::uint64_t sum = o.raw_value();
    if (o.is_array())
        for (std::uint64_t i = o.as_array(); i > 0; --i)
            sum += vg_read(d, depth + 1);
    else if (o.is_map())
        for (std::uint64_t i = o.as_map(); i > 0; --i)
            sum += vg_read(d, depth + 1) + vg_read(d, depth + 1);
    else if (o.is_tag())
        sum += vg_read(d, depth + 1);
    return sum;
}
#endif

#if defined(ARM_VG_RAW)
#include <cbor_encoder.h>
struct vg_out final : cbor_encoder {
    std::vector<std::uint8_t> v;
    std::uint8_t *p = nullptr;
    void put_byte(std::uint8_t b) override { *p++ = b; }
    void payload(std::string_view s)
    {
        std::memcpy(p, s.data(), s.size());
        p += s.size();
    }
    void neg(std::uint64_t a) { write_type_and_value(1, a); }
};
static void vg_encode(vg_out &o, bench::value const &x)
{
    bench::binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: o.write_uint(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: o.neg(b.unsigned_of(x) - 1); break;
    case cbor::kind::floating_point: o.write_float_shortest(b.float_of(x)); break;
    case cbor::kind::simple_value: o.write_simple(b.simple_of(x)); break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); o.write_string_header(t.size()); o.payload(t); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); o.write_bytes_header(t.size()); o.payload(t); } break;
    case cbor::kind::array: { auto const n = b.array_size(x); o.write_array(n); for (std::uint64_t i = 0; i < n; ++i) vg_encode(o, b.array_at(x, i)); } break;
    case cbor::kind::map: o.write_map(b.map_size(x)); b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { vg_encode(o, k); vg_encode(o, v); }); break;
    case cbor::kind::registered: o.write_tag(b.registered_tag(x)); vg_encode(o, b.before_encode(x)); break;
    default: std::abort();
    }
}
#endif

#if defined(ARM_MP_REUSE) || defined(ARM_MP_READ)
#define ARM_MP 1
#include <msgpack.hpp>
static void mp_pack(msgpack::packer<msgpack::sbuffer> &pk, bench::value const &x)
{
    bench::binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: pk.pack_uint64(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: {
        std::uint64_t const n = b.unsigned_of(x) - 1;
        if (n <= std::uint64_t(INT64_MAX)) {
            pk.pack_int64(-1 - std::int64_t(n));
        } else {
            msgpack::sbuffer body;
            msgpack::packer<msgpack::sbuffer>(body).pack_uint64(n);
            pk.pack_ext(body.size(), 2);
            pk.pack_ext_body(body.data(), body.size());
        }
    } break;
    case cbor::kind::floating_point: pk.pack_double(b.float_of(x)); break;
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); if (v == 20) pk.pack_false(); else if (v == 21) pk.pack_true(); else pk.pack_nil(); } break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); pk.pack_str(std::uint32_t(t.size())); pk.pack_str_body(t.data(), std::uint32_t(t.size())); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); pk.pack_bin(std::uint32_t(t.size())); pk.pack_bin_body(t.data(), std::uint32_t(t.size())); } break;
    case cbor::kind::array: { auto const n = b.array_size(x); pk.pack_array(std::uint32_t(n)); for (std::uint64_t i = 0; i < n; ++i) mp_pack(pk, b.array_at(x, i)); } break;
    case cbor::kind::map: pk.pack_map(std::uint32_t(b.map_size(x))); b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { mp_pack(pk, k); mp_pack(pk, v); }); break;
    case cbor::kind::registered: {
        msgpack::sbuffer body;
        msgpack::packer<msgpack::sbuffer> inner(body);
        inner.pack_uint64(b.registered_tag(x));
        mp_pack(inner, b.before_encode(x));
        pk.pack_ext(body.size(), 1);
        pk.pack_ext_body(body.data(), body.size());
    } break;
    default: std::abort();
    }
}
#endif

#if defined(ARM_FB_REUSE) || defined(ARM_FB_READ) || defined(ARM_FB_READ_V) || defined(ARM_FB_PATH) || defined(ARM_FB_PATH_V)
#define ARM_FB 1
#include <flatbuffers/flexbuffers.h>
static void fb_build(flexbuffers::Builder &f, bench::value const &x)
{
    bench::binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: f.UInt(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: {
        std::uint64_t const n = b.unsigned_of(x) - 1;
        if (n <= std::uint64_t(INT64_MAX)) {
            f.Int(-1 - std::int64_t(n));
        } else {
            auto const st = f.StartVector();
            f.UInt(n);
            f.EndVector(st, false, false);
        }
    } break;
    case cbor::kind::floating_point: f.Double(b.float_of(x)); break;
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); if (v == 20 || v == 21) f.Bool(v == 21); else f.Null(); } break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); f.String(t.data(), t.size()); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); f.Blob(t.data(), t.size()); } break;
    case cbor::kind::array: { auto const st = f.StartVector(); auto const n = b.array_size(x); for (std::uint64_t i = 0; i < n; ++i) fb_build(f, b.array_at(x, i)); f.EndVector(st, false, false); } break;
    case cbor::kind::map: {
        bool text_keys = true;
        b.map_for_each(x, [&](bench::value const &k, bench::value const &) { text_keys = text_keys && b.kind_of(k) == cbor::kind::text_string; });
        if (text_keys) {
            auto const st = f.StartMap();
            b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { std::string const t(b.text_of(k)); f.Key(t.c_str(), t.size()); fb_build(f, v); });
            f.EndMap(st);
        } else {
            auto const st = f.StartVector();
            b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { fb_build(f, k); fb_build(f, v); });
            f.EndVector(st, false, false);
        }
    } break;
    case cbor::kind::registered: { auto const st = f.StartVector(); f.UInt(b.registered_tag(x)); fb_build(f, b.before_encode(x)); f.EndVector(st, false, false); } break;
    default: std::abort();
    }
}
static std::uint64_t fb_read(flexbuffers::Reference const r)
{
    if (r.IsMap()) { auto const m = r.AsMap(); auto const k = m.Keys(); auto const v = m.Values(); std::uint64_t s = m.size(); for (std::size_t i = 0; i < m.size(); ++i) s += bytes_sum(k[i].AsKey()) + fb_read(v[i]); return s; }
    if (r.IsVector()) { auto const v = r.AsVector(); std::uint64_t s = v.size(); for (std::size_t i = 0; i < v.size(); ++i) s += fb_read(v[i]); return s; }
    if (r.IsString()) { auto const t = r.AsString(); return bytes_sum(std::string_view(t.c_str(), t.size())); }
    if (r.IsBlob()) { auto const t = r.AsBlob(); return bytes_sum(std::string_view(reinterpret_cast<char const *>(t.data()), t.size())); }
    if (r.IsUInt()) return r.AsUInt64();
    if (r.IsInt()) { auto const v = r.AsInt64(); return v < 0 ? std::uint64_t(-1 - v) : std::uint64_t(v); }
    if (r.IsFloat()) return std::bit_cast<std::uint64_t>(r.AsDouble());
    if (r.IsBool()) return 20u + r.AsBool();
    return 22;
}
#endif

#if defined(ARM_TC_READ) || defined(ARM_TC_PATH)
#include <tinycbor/cbor.h>
static void tc_check(CborError const e)
{
    if (e != CborNoError) [[unlikely]]
        std::abort();
}
static std::uint64_t tc_string(CborValue *const it)
{
    std::uint64_t sum = 0;
    bool const text = cbor_value_is_text_string(it);
    tc_check(cbor_value_begin_string_iteration(it));
    for (;;) {
        char const *p = nullptr;
        std::size_t n = 0;
        CborError const e = text ? cbor_value_get_text_string_chunk(it, &p, &n, it)
                                 : cbor_value_get_byte_string_chunk(it, reinterpret_cast<std::uint8_t const **>(&p), &n, it);
        if (e == CborErrorNoMoreStringChunks)
            break;
        tc_check(e);
        sum += bytes_sum(std::string_view(p, n));
    }
    tc_check(cbor_value_finish_string_iteration(it));
    return sum;
}
static std::uint64_t tc_float(CborValue const *const it)
{
    double d = 0;
    if (cbor_value_is_half_float(it)) {
        float f = 0;
        tc_check(cbor_value_get_half_float_as_float(it, &f));
        d = f;
    } else if (cbor_value_is_float(it)) {
        float f = 0;
        tc_check(cbor_value_get_float(it, &f));
        d = f;
    } else {
        tc_check(cbor_value_get_double(it, &d));
    }
    return std::bit_cast<std::uint64_t>(d);
}
#endif

#if defined(ARM_TC_READ)
static std::uint64_t tc_read(CborValue *const it, int const depth)
{
    if (depth > 128) [[unlikely]]
        std::abort();
    std::uint64_t sum = 0;
    switch (cbor_value_get_type(it)) {
    case CborIntegerType: tc_check(cbor_value_get_raw_integer(it, &sum)); tc_check(cbor_value_advance_fixed(it)); return sum;
    case CborByteStringType:
    case CborTextStringType: return tc_string(it);
    case CborArrayType:
    case CborMapType: {
        std::size_t n = 0;
        if (cbor_value_is_array(it))
            tc_check(cbor_value_get_array_length(it, &n));
        else
            tc_check(cbor_value_get_map_length(it, &n));
        sum = n;
        CborValue r;
        tc_check(cbor_value_enter_container(it, &r));
        while (!cbor_value_at_end(&r))
            sum += tc_read(&r, depth + 1);
        tc_check(cbor_value_leave_container(it, &r));
        return sum;
    }
    case CborTagType: {
        CborTag t = 0;
        tc_check(cbor_value_get_tag(it, &t));
        tc_check(cbor_value_advance_fixed(it));
        return t + tc_read(it, depth + 1);
    }
    case CborBooleanType: { bool b = false; tc_check(cbor_value_get_boolean(it, &b)); sum = 20u + b; } break;
    case CborNullType: sum = 22; break;
    case CborUndefinedType: sum = 23; break;
    case CborSimpleType: { std::uint8_t v = 0; tc_check(cbor_value_get_simple_type(it, &v)); sum = v; } break;
    case CborHalfFloatType:
    case CborFloatType:
    case CborDoubleType: sum = tc_float(it); break;
    default: std::abort();
    }
    tc_check(cbor_value_advance_fixed(it));
    return sum;
}
#endif

#if defined(ARM_TC_PATH)
static void tc_find(CborValue *const it, char const *const key)
{
    CborValue v;
    if (!cbor_value_is_map(it)) [[unlikely]]
        std::abort();
    tc_check(cbor_value_map_find_value(it, key, &v));
    if (!cbor_value_is_valid(&v)) [[unlikely]]
        std::abort();
    *it = v;
}
static void tc_index(CborValue *const it, std::size_t const i)
{
    std::size_t n = 0;
    if (!cbor_value_is_array(it)) [[unlikely]]
        std::abort();
    tc_check(cbor_value_get_array_length(it, &n));
    if (i >= n) [[unlikely]]
        std::abort();
    CborValue r;
    tc_check(cbor_value_enter_container(it, &r));
    for (std::size_t k = 0; k < i; ++k)
        tc_check(cbor_value_advance(&r));
    *it = r;
}
#endif

#if defined(ARM_JC_DECODE)
#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
static std::uint64_t json_sum(jsoncons::json const &j)
{
    switch (j.type()) {
    case jsoncons::json_type::null_value: return 22;
    case jsoncons::json_type::bool_value: return 20u + j.as<bool>();
    case jsoncons::json_type::uint64_value: return j.as<std::uint64_t>();
    case jsoncons::json_type::int64_value: { auto const v = j.as<std::int64_t>(); return v < 0 ? std::uint64_t(-1 - v) : std::uint64_t(v); }
    case jsoncons::json_type::half_value:
    case jsoncons::json_type::double_value: return std::bit_cast<std::uint64_t>(j.as<double>());
    case jsoncons::json_type::string_value: { auto const t = j.as_string_view(); return bytes_sum(std::string_view(t.data(), t.size())); }
    case jsoncons::json_type::byte_string_value: { auto const t = j.as_byte_string_view(); return bytes_sum(std::string_view(reinterpret_cast<char const *>(t.data()), t.size())); }
    case jsoncons::json_type::array_value: { std::uint64_t s = j.size(); for (auto const &e : j.array_range()) s += json_sum(e); return s; }
    case jsoncons::json_type::object_value: { std::uint64_t s = j.size(); for (auto const &m : j.object_range()) s += bytes_sum(std::string_view(m.key().data(), m.key().size())) + json_sum(m.value()); return s; }
    default: std::abort();
    }
}
#endif

#if defined(ARM_JC_PATH)
#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
using jc_cursor = jsoncons::cbor::cbor_bytes_cursor;
static void jc_skip(jc_cursor &c)
{
    auto const t = c.current().event_type();
    if (t != jsoncons::staj_event_type::begin_array && t != jsoncons::staj_event_type::begin_object)
        return;
    int depth = 1;
    while (depth > 0) {
        c.next();
        switch (c.current().event_type()) {
        case jsoncons::staj_event_type::begin_array:
        case jsoncons::staj_event_type::begin_object: ++depth; break;
        case jsoncons::staj_event_type::end_array:
        case jsoncons::staj_event_type::end_object: --depth; break;
        default: break;
        }
    }
}
static void jc_key(jc_cursor &c, std::string_view const key)
{
    if (c.current().event_type() != jsoncons::staj_event_type::begin_object) [[unlikely]]
        std::abort();
    for (;;) {
        c.next();
        if (c.current().event_type() != jsoncons::staj_event_type::key) [[unlikely]]
            std::abort();
        auto const k = c.current().get<jsoncons::string_view>();
        bool const hit = std::string_view(k.data(), k.size()) == key;
        c.next();
        if (hit)
            return;
        jc_skip(c);
    }
}
static void jc_index(jc_cursor &c, std::size_t const i)
{
    if (c.current().event_type() != jsoncons::staj_event_type::begin_array || i >= c.current().size()) [[unlikely]]
        std::abort();
    c.next();
    for (std::size_t k = 0; k < i; ++k) {
        jc_skip(c);
        c.next();
    }
}
static std::uint64_t jc_text(jc_cursor const &c)
{
    auto const t = c.current().get<jsoncons::string_view>();
    return bytes_sum(std::string_view(t.data(), t.size()));
}
#endif

static std::string doc;
static std::string_view in;
#if defined(ARM_MB_AT_PATH) || defined(ARM_MB_AT_PATH_NAMED)
static std::string const *copy = &doc;
#endif
static std::shared_ptr<void const> const keep = std::make_shared<int const>(0);

#if defined(ARM_MB_DECODE)
static std::uint64_t value_sum(bench::value const &x)
{
    bench::binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: return b.unsigned_of(x);
    case cbor::kind::negative_integer: return b.unsigned_of(x) - 1;
    case cbor::kind::floating_point: return std::bit_cast<std::uint64_t>(b.float_of(x));
    case cbor::kind::simple_value: return b.simple_of(x);
    case cbor::kind::text_string: return bytes_sum(b.text_of(x));
    case cbor::kind::byte_string: return bytes_sum(b.bytes_of(x));
    case cbor::kind::array: { std::uint64_t s = b.array_size(x); for (std::uint64_t i = 0; i < b.array_size(x); ++i) s += value_sum(b.array_at(x, i)); return s; }
    case cbor::kind::map: { std::uint64_t s = b.map_size(x); b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { s += value_sum(k) + value_sum(v); }); return s; }
    case cbor::kind::registered: return b.registered_tag(x) + value_sum(b.before_encode(x));
    default: std::abort();
    }
}
#endif

#if defined(ARM_LC_DECODE)
static std::uint64_t item_sum(cbor_item_t const *i)
{
    auto *x = const_cast<cbor_item_t *>(i);
    switch (cbor_typeof(x)) {
    case CBOR_TYPE_UINT: return cbor_get_int(x);
    case CBOR_TYPE_NEGINT: return cbor_get_int(x);
    case CBOR_TYPE_BYTESTRING: return bytes_sum(std::string_view(reinterpret_cast<char const *>(cbor_bytestring_handle(x)), cbor_bytestring_length(x)));
    case CBOR_TYPE_STRING: return bytes_sum(std::string_view(reinterpret_cast<char const *>(cbor_string_handle(x)), cbor_string_length(x)));
    case CBOR_TYPE_ARRAY: { std::uint64_t s = cbor_array_size(x); for (std::size_t k = 0; k < cbor_array_size(x); ++k) s += item_sum(cbor_array_handle(x)[k]); return s; }
    case CBOR_TYPE_MAP: { std::uint64_t s = cbor_map_size(x); for (std::size_t k = 0; k < cbor_map_size(x); ++k) s += item_sum(cbor_map_handle(x)[k].key) + item_sum(cbor_map_handle(x)[k].value); return s; }
    case CBOR_TYPE_TAG: return cbor_tag_value(x) + item_sum(cbor_tag_item(x));
    case CBOR_TYPE_FLOAT_CTRL:
        if (cbor_float_ctrl_is_ctrl(x)) return cbor_ctrl_value(x);
        return std::bit_cast<std::uint64_t>(cbor_float_get_float(x));
    }
    std::abort();
}
#endif

#if defined(ARM_MB_PATH)
static std::uint64_t mb_path()
{
    auto const l = cbor::lazy::from(keep, in);
#if defined(DOC_twitter)
    auto const v = l.and_then([](cbor::lazy const &d) { return d.at("statuses"); })
                       .and_then([](cbor::lazy const &s) { return s.at(50); })
                       .and_then([](cbor::lazy const &s) { return s.at("user"); })
                       .and_then([](cbor::lazy const &u) { return u.at("screen_name"); })
                       .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
#elif defined(DOC_floats)
    auto const v = l.and_then([](cbor::lazy const &d) { return d.at(30000); })
                       .and_then([](cbor::lazy const &x) { return x.get<double>(); });
    if (!v) [[unlikely]] std::abort();
    return std::bit_cast<std::uint64_t>(*v);
#elif defined(DOC_records)
    auto const v = l.and_then([](cbor::lazy const &d) { return d.at(1000); })
                       .and_then([](cbor::lazy const &r) { return r.at("user"); })
                       .and_then([](cbor::lazy const &u) { return u.at("name"); })
                       .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
#endif
}
#endif

#if defined(ARM_MB_AT_PATH) || defined(ARM_MB_AT_PATH_NAMED)
static std::uint64_t mb_at_path()
{
#if defined(DOC_twitter)
    auto const v = cbor::at_path<"$.statuses[50].user.screen_name", std::string_view>(*copy);
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(*v);
#elif defined(DOC_floats)
    auto const v = cbor::at_path<"$[30000]", double>(in);
    if (!v) [[unlikely]] std::abort();
    return std::bit_cast<std::uint64_t>(*v);
#elif defined(DOC_records)
    auto const v = cbor::at_path<"$[1000].user.name", std::string_view>(*copy);
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(*v);
#endif
}
#endif

#if defined(ARM_MB_AT_PATH_OWNED)
static std::uint64_t mb_at_path()
{
#if defined(DOC_twitter)
    auto const v = cbor::at_path<"$.statuses[50].user.screen_name", std::string_view>(keep, in);
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
#elif defined(DOC_floats)
    auto const v = cbor::at_path<"$[30000]", double>(in);
    if (!v) [[unlikely]] std::abort();
    return std::bit_cast<std::uint64_t>(*v);
#elif defined(DOC_records)
    auto const v = cbor::at_path<"$[1000].user.name", std::string_view>(keep, in);
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
#endif
}
#endif

#if defined(ARM_FB_PATH) || defined(ARM_FB_PATH_V)
static std::uint64_t fb_path(flexbuffers::Reference const root)
{
#if defined(DOC_twitter)
    auto const t = root.AsMap()["statuses"].AsVector()[50].AsMap()["user"].AsMap()["screen_name"].AsString();
    return bytes_sum(std::string_view(t.c_str(), t.size()));
#elif defined(DOC_floats)
    return std::bit_cast<std::uint64_t>(root.AsVector()[30000].AsDouble());
#elif defined(DOC_records)
    auto const t = root.AsVector()[1000].AsMap()["user"].AsMap()["name"].AsString();
    return bytes_sum(std::string_view(t.c_str(), t.size()));
#endif
}
#endif
static std::string const *bytes = &doc;
#if defined(ARM_S) || defined(ARM_VG_RAW) || defined(ARM_MP) || defined(ARM_FB)
static bench::value tree;
#endif
#if defined(ARM_MP) || defined(ARM_FB)
static std::string alt;
#endif
#if defined(ARM_MP_REUSE)
static msgpack::sbuffer mpbuf;
#endif
#if defined(ARM_FB_REUSE)
static flexbuffers::Builder fbb;
#endif
#if defined(ARM_S)
static std::vector<char> buf;
#endif
#if defined(ARM_LC_PREALLOC)
static cbor_item_t *lc;
static std::vector<unsigned char> lcbuf;
#endif
#if defined(ARM_JC_CLEAR)
static jsoncons::json jc;
static std::vector<std::uint8_t> jcbuf;
#endif
#if defined(ARM_VG_RAW)
static vg_out vo;
#endif

static void setup()
{
#if defined(ARM_S) || defined(ARM_VG_RAW) || defined(ARM_MP) || defined(ARM_FB)
    bench::binding b;
    auto t = cbor::lazy_decode<128>(b, *cbor::decode<128>(std::string_view(doc)));
    if (!t)
        std::abort();
    tree = std::move(*t);
#endif
#if defined(ARM_S)
    buf.resize(doc.size());
#endif
#if defined(ARM_MP)
    msgpack::sbuffer sb;
    msgpack::packer<msgpack::sbuffer> pk(sb);
    mp_pack(pk, tree);
    alt.assign(sb.data(), sb.size());
    bytes = &alt;
#endif
#if defined(ARM_FB)
    flexbuffers::Builder f;
    fb_build(f, tree);
    f.Finish();
    alt.assign(reinterpret_cast<char const *>(f.GetBuffer().data()), f.GetSize());
    bytes = &alt;
#endif
#if defined(ARM_LC_PREALLOC)
    cbor_load_result r;
    lc = cbor_load(reinterpret_cast<cbor_data>(doc.data()), doc.size(), &r);
    if (!lc)
        std::abort();
    lcbuf.resize(cbor_serialized_size(lc));
#endif
#if defined(ARM_JC_CLEAR)
    jc = jsoncons::cbor::decode_cbor<jsoncons::json>(std::vector<std::uint8_t>(doc.begin(), doc.end()));
#endif
#if defined(ARM_VG_RAW)
    vo.v.resize(2 * doc.size() + 64);
#endif
}

static std::uint64_t op()
{
#if defined(ARM_S)
    bench::binding b;
    if (!cbor::encode<128>(b, std::span<char>(buf), tree))
        std::abort();
    benchmark::ClobberMemory();
    return buf.size();
#elif defined(ARM_READ)
    read_binding b;
    auto const r = cbor::lazy_decode<128>(b, *cbor::lazy::from(keep, in));
    if (!r)
        std::abort();
    return *r;
#elif defined(ARM_MB_DECODE)
    bench::binding b;
    auto r = cbor::lazy_decode<128>(b, *cbor::lazy::from(keep, in));
    if (!r) [[unlikely]]
        std::abort();
    benchmark::DoNotOptimize(&*r);
    return r->kind.index();
#elif defined(ARM_LC_DECODE)
    cbor_load_result lr;
    cbor_item_t *it = cbor_load(reinterpret_cast<cbor_data>(in.data()), in.size(), &lr);
    if (!it) [[unlikely]]
        std::abort();
    benchmark::DoNotOptimize(it);
    std::uint64_t const t = cbor_typeof(it);
    cbor_decref(&it);
    return t;
#elif defined(ARM_MB_PATH)
    return mb_path();
#elif defined(ARM_MB_AT_PATH) || defined(ARM_MB_AT_PATH_NAMED) || defined(ARM_MB_AT_PATH_OWNED)
    return mb_at_path();
#elif defined(ARM_MB_PATH_ALL)
    read_binding b;
    auto const r = cbor::at_path<"$..*">(b, *cbor::lazy::from(keep, in));
    if (!r) [[unlikely]]
        std::abort();
    return *r;
#elif defined(ARM_TC_READ)
    CborParser parser;
    CborValue it;
    tc_check(cbor_parser_init(reinterpret_cast<std::uint8_t const *>(in.data()), in.size(), 0, &parser, &it));
    return tc_read(&it, 0);
#elif defined(ARM_TC_PATH)
    CborParser parser;
    CborValue it;
    tc_check(cbor_parser_init(reinterpret_cast<std::uint8_t const *>(in.data()), in.size(), 0, &parser, &it));
#if defined(DOC_twitter)
    tc_find(&it, "statuses");
    tc_index(&it, 50);
    tc_find(&it, "user");
    tc_find(&it, "screen_name");
    if (!cbor_value_is_text_string(&it)) [[unlikely]]
        std::abort();
    return tc_string(&it);
#elif defined(DOC_floats)
    tc_index(&it, 30000);
    return tc_float(&it);
#elif defined(DOC_records)
    tc_index(&it, 1000);
    tc_find(&it, "user");
    tc_find(&it, "name");
    if (!cbor_value_is_text_string(&it)) [[unlikely]]
        std::abort();
    return tc_string(&it);
#endif
#elif defined(ARM_JC_DECODE)
    auto j = jsoncons::cbor::decode_cbor<jsoncons::json>(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
    benchmark::DoNotOptimize(&j);
    return std::uint64_t(j.type());
#elif defined(ARM_JC_PATH)
    jc_cursor c(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
#if defined(DOC_twitter)
    jc_key(c, "statuses");
    jc_index(c, 50);
    jc_key(c, "user");
    jc_key(c, "screen_name");
    return jc_text(c);
#elif defined(DOC_floats)
    jc_index(c, 30000);
    return std::bit_cast<std::uint64_t>(c.current().get<double>());
#elif defined(DOC_records)
    jc_index(c, 1000);
    jc_key(c, "user");
    jc_key(c, "name");
    return jc_text(c);
#endif
#elif defined(ARM_FB_PATH)
    return fb_path(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
#elif defined(ARM_FB_PATH_V)
    if (!flexbuffers::VerifyBuffer(reinterpret_cast<std::uint8_t const *>(in.data()), in.size())) [[unlikely]]
        std::abort();
    return fb_path(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
#elif defined(ARM_FB_READ_V)
    if (!flexbuffers::VerifyBuffer(reinterpret_cast<std::uint8_t const *>(in.data()), in.size())) [[unlikely]]
        std::abort();
    return fb_read(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
#elif defined(ARM_LC_PREALLOC)
    std::size_t const n = cbor_serialize(lc, lcbuf.data(), lcbuf.size());
    if (n == 0)
        std::abort();
    benchmark::ClobberMemory();
    return n;
#elif defined(ARM_LC_READ)
    static cbor_callbacks const cb = [] {
        cbor_callbacks c = cbor_empty_callbacks;
        c.uint8 = [](void *x, std::uint8_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.uint16 = [](void *x, std::uint16_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.uint32 = [](void *x, std::uint32_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.uint64 = [](void *x, std::uint64_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.negint8 = [](void *x, std::uint8_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.negint16 = [](void *x, std::uint16_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.negint32 = [](void *x, std::uint32_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.negint64 = [](void *x, std::uint64_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.byte_string = [](void *x, cbor_data d, std::uint64_t n) { *static_cast<std::uint64_t *>(x) += bytes_sum(std::string_view(reinterpret_cast<char const *>(d), n)); };
        c.string = [](void *x, cbor_data d, std::uint64_t n) { *static_cast<std::uint64_t *>(x) += bytes_sum(std::string_view(reinterpret_cast<char const *>(d), n)); };
        c.array_start = [](void *x, std::uint64_t n) { *static_cast<std::uint64_t *>(x) += n; };
        c.map_start = [](void *x, std::uint64_t n) { *static_cast<std::uint64_t *>(x) += n; };
        c.tag = [](void *x, std::uint64_t v) { *static_cast<std::uint64_t *>(x) += v; };
        c.float2 = [](void *x, float v) { *static_cast<std::uint64_t *>(x) += std::bit_cast<std::uint64_t>(double(v)); };
        c.float4 = [](void *x, float v) { *static_cast<std::uint64_t *>(x) += std::bit_cast<std::uint64_t>(double(v)); };
        c.float8 = [](void *x, double v) { *static_cast<std::uint64_t *>(x) += std::bit_cast<std::uint64_t>(v); };
        c.boolean = [](void *x, bool v) { *static_cast<std::uint64_t *>(x) += 20u + v; };
        c.null = [](void *x) { *static_cast<std::uint64_t *>(x) += 22; };
        c.undefined = [](void *x) { *static_cast<std::uint64_t *>(x) += 23; };
        return c;
    }();
    auto const *p = reinterpret_cast<cbor_data>(in.data());
    std::uint64_t sum = 0;
    std::size_t at = 0;
    while (at < in.size()) {
        auto const r = cbor_stream_decode(p + at, in.size() - at, &cb, &sum);
        if (r.status != CBOR_DECODER_FINISHED)
            std::abort();
        at += r.read;
    }
    return sum;
#elif defined(ARM_JC_CLEAR)
    jcbuf.clear();
    jsoncons::cbor::encode_cbor(jc, jcbuf);
    benchmark::ClobberMemory();
    return jcbuf.size();
#elif defined(ARM_JC_READ)
    jsoncons::cbor::cbor_bytes_cursor cursor(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
    std::uint64_t sum = 0;
    for (; !cursor.done(); cursor.next()) {
        auto const &e = cursor.current();
        switch (e.event_type()) {
        case jsoncons::staj_event_type::key:
        case jsoncons::staj_event_type::string_value: { auto const t = e.get<jsoncons::string_view>(); sum += bytes_sum(std::string_view(t.data(), t.size())); } break;
        case jsoncons::staj_event_type::byte_string_value: { auto const t = e.get<jsoncons::byte_string_view>(); sum += bytes_sum(std::string_view(reinterpret_cast<char const *>(t.data()), t.size())); } break;
        case jsoncons::staj_event_type::uint64_value: sum += e.get<std::uint64_t>(); break;
        case jsoncons::staj_event_type::int64_value: { auto const v = e.get<std::int64_t>(); sum += v < 0 ? std::uint64_t(-1 - v) : std::uint64_t(v); } break;
        case jsoncons::staj_event_type::half_value:
        case jsoncons::staj_event_type::double_value: sum += std::bit_cast<std::uint64_t>(e.get<double>()); break;
        case jsoncons::staj_event_type::bool_value: sum += 20u + e.get<bool>(); break;
        case jsoncons::staj_event_type::null_value: sum += 22; break;
        case jsoncons::staj_event_type::begin_array:
        case jsoncons::staj_event_type::begin_object: sum += e.size(); break;
        case jsoncons::staj_event_type::end_array:
        case jsoncons::staj_event_type::end_object: break;
        default: std::abort();
        }
    }
    return sum;
#elif defined(ARM_VG_RAW)
    vo.p = vo.v.data();
    vg_encode(vo, tree);
    benchmark::ClobberMemory();
    return std::size_t(vo.p - vo.v.data());
#elif defined(ARM_VG_READ)
    span_decoder d;
    d.p = reinterpret_cast<std::uint8_t const *>(in.data());
    d.end = d.p + in.size();
    return vg_read(d, 0);
#elif defined(ARM_MP_REUSE)
    mpbuf.clear();
    msgpack::packer<msgpack::sbuffer> pk(mpbuf);
    mp_pack(pk, tree);
    benchmark::ClobberMemory();
    return mpbuf.size();
#elif defined(ARM_MP_READ)
    struct reader : msgpack::null_visitor {
        std::uint64_t sum = 0;
        bool visit_nil() { sum += 22; return true; }
        bool visit_boolean(bool v) { sum += 20u + v; return true; }
        bool visit_positive_integer(std::uint64_t v) { sum += v; return true; }
        bool visit_negative_integer(std::int64_t v) { sum += std::uint64_t(-1 - v); return true; }
        bool visit_float32(float v) { sum += std::bit_cast<std::uint32_t>(v); return true; }
        bool visit_float64(double v) { sum += std::bit_cast<std::uint64_t>(v); return true; }
        bool visit_str(char const *v, std::uint32_t n) { sum += bytes_sum(std::string_view(v, n)); return true; }
        bool visit_bin(char const *v, std::uint32_t n) { sum += bytes_sum(std::string_view(v, n)); return true; }
        bool start_array(std::uint32_t n) { sum += n; return true; }
        bool start_map(std::uint32_t n) { sum += n; return true; }
        bool visit_ext(char const *v, std::uint32_t n)
        {
            sum += std::uint8_t(v[0]);
            std::size_t off = 1;
            while (off < n)
                if (!msgpack::parse(v, n, off, *this))
                    std::abort();
            return true;
        }
    } v;
    std::size_t off = 0;
    if (!msgpack::parse(in.data(), in.size(), off, v))
        std::abort();
    return v.sum;
#elif defined(ARM_FB_REUSE)
    fbb.Clear();
    fb_build(fbb, tree);
    fbb.Finish();
    benchmark::ClobberMemory();
    return fbb.GetSize();
#elif defined(ARM_FB_READ)
    return fb_read(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(in.data()), in.size()));
#else
#error no arm
#endif
}

static std::vector<std::string> copies;

static std::size_t l3_bytes()
{
    std::ifstream f("/sys/devices/system/cpu/cpu0/cache/index3/size");
    std::size_t kib = 0;
    f >> kib;
    if (kib == 0)
        std::abort();
    return kib * 1024;
}

static void run(benchmark::State &state)
{
    std::size_t i = 0;
    for (auto _ : state) {
        in = copies[i];
#if defined(ARM_MB_AT_PATH) || defined(ARM_MB_AT_PATH_NAMED)
        copy = &copies[i];
#endif
        i = i + 1 == copies.size() ? 0 : i + 1;
        benchmark::DoNotOptimize(op());
        benchmark::ClobberMemory();
    }
    state.SetBytesProcessed(std::int64_t(state.iterations()) * std::int64_t(bytes->size()));
}

int main(int argc, char **argv)
{
    std::ifstream file(DOC_PATH, std::ios::binary);
    doc.assign(std::istreambuf_iterator<char>(file), {});
    if (doc.empty())
        std::abort();
    setup();
    in = *bytes;
#if defined(ARM_MB_DECODE)
    {
        bench::binding b;
        auto const r = cbor::lazy_decode<128>(b, *cbor::lazy::from(keep, std::string_view(doc)));
        benchmark::AddCustomContext("check", std::to_string(value_sum(*r)));
    }
#elif defined(ARM_LC_DECODE)
    {
        cbor_load_result lr;
        cbor_item_t *it = cbor_load(reinterpret_cast<cbor_data>(doc.data()), doc.size(), &lr);
        benchmark::AddCustomContext("check", std::to_string(item_sum(it)));
        cbor_decref(&it);
    }
#elif defined(ARM_JC_DECODE)
    benchmark::AddCustomContext("check", std::to_string(json_sum(jsoncons::cbor::decode_cbor<jsoncons::json>(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const *>(doc.data()), doc.size())))));
#else
    benchmark::AddCustomContext("check", std::to_string(op()));
#endif
    copies.assign(2 * l3_bytes() / bytes->size() + 1, *bytes);
    benchmark::AddCustomContext("copies", std::to_string(copies.size()));
    benchmark::AddCustomContext("document", DOC_PATH);
    benchmark::RegisterBenchmark(ARM_NAME, run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
