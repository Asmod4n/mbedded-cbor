#include <benchmark/benchmark.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "value.hpp"
#include <bit>
#include <functional>
#include <numeric>
static std::uint64_t bytes_sum(std::string_view const s)
{
    return std::transform_reduce(s.begin(), s.end(), std::uint64_t{0}, std::plus<>{},
                                 [](char const c) { return std::uint64_t(std::uint8_t(c)); });
}
#if defined(ARM_READ)
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

#if defined(ARM_LC_READ) || defined(ARM_LC_WALK) || defined(ARM_LC_TREE) || defined(ARM_LC_PREALLOC) || defined(ARM_LC_ALLOC) || defined(ARM_LC_EXACT)
#include <cbor.h>
#endif
#if defined(ARM_JC_READ) || defined(ARM_JC_TREE) || defined(ARM_JC_CLEAR) || defined(ARM_JC_ENCODER) || defined(ARM_JC_FRESH)
#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
#endif
#if defined(ARM_VG_WALK) || defined(ARM_VG_READ)
#include <cbor_decoder.h>
struct span_decoder final : cbor_decoder {
    std::uint8_t const *p, *end;
    std::uint8_t get_byte() override { if (p == end) throw cbor_decoder_exception("end"); return *p++; }
    void skip(std::uint64_t n) { if (n > std::uint64_t(end - p)) throw cbor_decoder_exception("end"); p += n; }
};
static void vg_item(span_decoder &d, int depth)
{
    if (depth > 128) throw cbor_decoder_exception("depth");
    cbor_object const o = d.read();
    if (o.is_bytes() || o.is_string()) d.skip(o.raw_value());
    else if (o.is_array()) { for (std::uint64_t i = o.as_array(); i > 0; --i) vg_item(d, depth + 1); }
    else if (o.is_map()) { for (std::uint64_t i = o.as_map(); i > 0; --i) { vg_item(d, depth + 1); vg_item(d, depth + 1); } }
    else if (o.is_tag()) vg_item(d, depth + 1);
}
static std::uint64_t vg_read(span_decoder &d, int depth)
{
    if (depth > 128) throw cbor_decoder_exception("depth");
    cbor_object const o = d.read();
    if (o.is_bytes() || o.is_string()) {
        std::uint64_t const n = o.raw_value();
        if (n > std::uint64_t(d.end - d.p)) throw cbor_decoder_exception("end");
        std::uint64_t const sum = bytes_sum(std::string_view(reinterpret_cast<char const *>(d.p), n));
        d.skip(n);
        return sum;
    }
    std::uint64_t sum = o.raw_value();
    if (o.is_array()) { for (std::uint64_t i = o.as_array(); i > 0; --i) sum += vg_read(d, depth + 1); }
    else if (o.is_map()) { for (std::uint64_t i = o.as_map(); i > 0; --i) { sum += vg_read(d, depth + 1); sum += vg_read(d, depth + 1); } }
    else if (o.is_tag()) sum += vg_read(d, depth + 1);
    return sum;
}
#endif
#if defined(ARM_VG_PUSH) || defined(ARM_VG_RAW)
#include <cbor_encoder.h>
#if defined(ARM_VG_PUSH)
struct vg_out final : cbor_encoder {
    std::vector<std::uint8_t> v;
    void put_byte(std::uint8_t b) override { v.push_back(b); }
    void payload(std::string_view s) { v.insert(v.end(), s.begin(), s.end()); }
    void reset() { v.clear(); }
    std::size_t size() const { return v.size(); }
    std::string str() const { return {v.begin(), v.end()}; }
    void neg(std::uint64_t a) { write_type_and_value(1, a); }
};
#else
struct vg_out final : cbor_encoder {
    std::vector<std::uint8_t> v;
    std::uint8_t *p = nullptr;
    void put_byte(std::uint8_t b) override { *p++ = b; }
    void payload(std::string_view s) { std::memcpy(p, s.data(), s.size()); p += s.size(); }
    void reset() { p = v.data(); }
    std::size_t size() const { return std::size_t(p - v.data()); }
    std::string str() const { return std::string(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(size())); }
    void neg(std::uint64_t a) { write_type_and_value(1, a); }
};
#endif
static void vg_encode(vg_out &o, test::value const &x)
{
    test::test_binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: o.write_uint(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: o.neg(b.unsigned_of(x) - 1); break;
    case cbor::kind::floating_point: o.write_float_shortest(b.float_of(x)); break;
    case cbor::kind::simple_value: o.write_simple(b.simple_of(x)); break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); o.write_string_header(t.size()); o.payload(t); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); o.write_bytes_header(t.size()); o.payload(t); } break;
    case cbor::kind::array: { auto const n = b.array_size(x); o.write_array(n); for (std::uint64_t i = 0; i < n; ++i) vg_encode(o, b.array_at(x, i)); } break;
    case cbor::kind::map: o.write_map(b.map_size(x)); b.map_for_each(x, [&](test::value const &k, test::value const &v) { vg_encode(o, k); vg_encode(o, v); }); break;
    case cbor::kind::registered: o.write_tag(b.registered_tag(x)); vg_encode(o, b.before_encode(x)); break;
    default: std::abort();
    }
}
#endif

#if defined(ARM_MP_READ) || defined(ARM_MP_WALK) || defined(ARM_MP_TREE) || defined(ARM_MP_REUSE) || defined(ARM_MP_FRESH)
#define ARM_MP 1
#include <msgpack.hpp>
static void mp_pack(msgpack::packer<msgpack::sbuffer> &pk, test::value const &x)
{
    test::test_binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: pk.pack_uint64(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: pk.pack_int64(-1 - std::int64_t(b.unsigned_of(x) - 1)); break;
    case cbor::kind::floating_point: pk.pack_double(b.float_of(x)); break;
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); if (v == 20) pk.pack_false(); else if (v == 21) pk.pack_true(); else pk.pack_nil(); } break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); pk.pack_str(std::uint32_t(t.size())); pk.pack_str_body(t.data(), std::uint32_t(t.size())); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); pk.pack_bin(std::uint32_t(t.size())); pk.pack_bin_body(t.data(), std::uint32_t(t.size())); } break;
    case cbor::kind::array: { auto const n = b.array_size(x); pk.pack_array(std::uint32_t(n)); for (std::uint64_t i = 0; i < n; ++i) mp_pack(pk, b.array_at(x, i)); } break;
    case cbor::kind::map: pk.pack_map(std::uint32_t(b.map_size(x))); b.map_for_each(x, [&](test::value const &k, test::value const &v) { mp_pack(pk, k); mp_pack(pk, v); }); break;
    case cbor::kind::registered: mp_pack(pk, b.before_encode(x)); break;
    default: std::abort();
    }
}
static bool mp_equal(msgpack::object const &o, test::value const &x)
{
    test::test_binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: return o.type == msgpack::type::POSITIVE_INTEGER && o.via.u64 == b.unsigned_of(x);
    case cbor::kind::negative_integer: return o.type == msgpack::type::NEGATIVE_INTEGER && o.via.i64 == -1 - std::int64_t(b.unsigned_of(x) - 1);
    case cbor::kind::floating_point: return o.type == msgpack::type::FLOAT64 && std::bit_cast<std::uint64_t>(o.via.f64) == std::bit_cast<std::uint64_t>(b.float_of(x));
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); return v == 20 || v == 21 ? o.type == msgpack::type::BOOLEAN && o.via.boolean == (v == 21) : o.type == msgpack::type::NIL; }
    case cbor::kind::text_string: return o.type == msgpack::type::STR && std::string_view(o.via.str.ptr, o.via.str.size) == b.text_of(x);
    case cbor::kind::byte_string: return o.type == msgpack::type::BIN && std::string_view(o.via.bin.ptr, o.via.bin.size) == b.bytes_of(x);
    case cbor::kind::array: { if (o.type != msgpack::type::ARRAY || o.via.array.size != b.array_size(x)) return false; for (std::uint32_t i = 0; i < o.via.array.size; ++i) if (!mp_equal(o.via.array.ptr[i], b.array_at(x, i))) return false; return true; }
    case cbor::kind::map: { if (o.type != msgpack::type::MAP || o.via.map.size != b.map_size(x)) return false; std::uint32_t i = 0; bool ok = true; b.map_for_each(x, [&](test::value const &k, test::value const &v) { ok = ok && mp_equal(o.via.map.ptr[i].key, k) && mp_equal(o.via.map.ptr[i].val, v); ++i; }); return ok; }
    case cbor::kind::registered: return mp_equal(o, b.before_encode(x));
    default: return false;
    }
}
#endif
#if defined(ARM_FB_READ) || defined(ARM_FB_WALK) || defined(ARM_FB_TREE) || defined(ARM_FB_REUSE) || defined(ARM_FB_FRESH)
#define ARM_FB 1
#include <flatbuffers/flexbuffers.h>
static std::string fb_key(test::value const &k)
{
    test::test_binding b;
    switch (b.kind_of(k)) {
    case cbor::kind::text_string: return std::string(b.text_of(k));
    case cbor::kind::unsigned_integer: return std::to_string(b.unsigned_of(k));
    case cbor::kind::negative_integer: return std::to_string(-1 - std::int64_t(b.unsigned_of(k) - 1));
    default: std::abort();
    }
}
static void fb_build(flexbuffers::Builder &f, test::value const &x)
{
    test::test_binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: f.UInt(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: f.Int(-1 - std::int64_t(b.unsigned_of(x) - 1)); break;
    case cbor::kind::floating_point: f.Double(b.float_of(x)); break;
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); if (v == 20 || v == 21) f.Bool(v == 21); else f.Null(); } break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); f.String(t.data(), t.size()); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); f.Blob(t.data(), t.size()); } break;
    case cbor::kind::array: { auto const st = f.StartVector(); auto const n = b.array_size(x); for (std::uint64_t i = 0; i < n; ++i) fb_build(f, b.array_at(x, i)); f.EndVector(st, false, false); } break;
    case cbor::kind::map: { auto const st = f.StartMap(); b.map_for_each(x, [&](test::value const &k, test::value const &v) { auto const s = fb_key(k); f.Key(s.data(), s.size()); fb_build(f, v); }); f.EndMap(st); } break;
    case cbor::kind::registered: fb_build(f, b.before_encode(x)); break;
    default: std::abort();
    }
}
static std::size_t fb_touch(flexbuffers::Reference const r)
{
    if (r.IsMap()) { auto const m = r.AsMap(); auto const k = m.Keys(); auto const v = m.Values(); std::size_t s = m.size(); for (std::size_t i = 0; i < m.size(); ++i) s += k[i].AsKey()[0] + fb_touch(v[i]); return s; }
    if (r.IsVector()) { auto const v = r.AsVector(); std::size_t s = v.size(); for (std::size_t i = 0; i < v.size(); ++i) s += fb_touch(v[i]); return s; }
    if (r.IsString()) { auto const t = r.AsString(); return t.size() + std::size_t(t.c_str()[0]); }
    if (r.IsBlob()) { auto const t = r.AsBlob(); return t.size() + std::size_t(t.data()[0]); }
    if (r.IsUInt()) return std::size_t(r.AsUInt64());
    if (r.IsInt()) return std::size_t(r.AsInt64());
    if (r.IsFloat()) return std::size_t(r.AsDouble());
    if (r.IsBool()) return std::size_t(r.AsBool());
    return 1;
}
static std::uint64_t fb_read(flexbuffers::Reference const r)
{
    if (r.IsMap()) { auto const m = r.AsMap(); auto const k = m.Keys(); auto const v = m.Values(); std::uint64_t s = m.size(); for (std::size_t i = 0; i < m.size(); ++i) s += bytes_sum(k[i].AsKey()) + fb_read(v[i]); return s; }
    if (r.IsVector()) { auto const v = r.AsVector(); std::uint64_t s = v.size(); for (std::size_t i = 0; i < v.size(); ++i) s += fb_read(v[i]); return s; }
    if (r.IsString()) { auto const t = r.AsString(); return bytes_sum(std::string_view(t.c_str(), t.size())); }
    if (r.IsBlob()) { auto const t = r.AsBlob(); return bytes_sum(std::string_view(reinterpret_cast<char const *>(t.data()), t.size())); }
    if (r.IsUInt()) return r.AsUInt64();
    if (r.IsInt()) return std::uint64_t(r.AsInt64());
    if (r.IsFloat()) return std::bit_cast<std::uint64_t>(r.AsDouble());
    if (r.IsBool()) return r.AsBool();
    return 1;
}
static bool fb_equal(flexbuffers::Reference const r, test::value const &x)
{
    test::test_binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: return r.IsUInt() && r.AsUInt64() == b.unsigned_of(x);
    case cbor::kind::negative_integer: return r.IsInt() && r.AsInt64() == -1 - std::int64_t(b.unsigned_of(x) - 1);
    case cbor::kind::floating_point: return r.IsFloat() && std::bit_cast<std::uint64_t>(r.AsDouble()) == std::bit_cast<std::uint64_t>(b.float_of(x));
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); return v == 20 || v == 21 ? r.IsBool() && r.AsBool() == (v == 21) : r.IsNull(); }
    case cbor::kind::text_string: return r.IsString() && std::string_view(r.AsString().c_str(), r.AsString().size()) == b.text_of(x);
    case cbor::kind::byte_string: return r.IsBlob() && std::string_view(reinterpret_cast<char const *>(r.AsBlob().data()), r.AsBlob().size()) == b.bytes_of(x);
    case cbor::kind::array: { if (!r.IsVector() || r.IsMap() || r.AsVector().size() != b.array_size(x)) return false; for (std::size_t i = 0; i < b.array_size(x); ++i) if (!fb_equal(r.AsVector()[i], b.array_at(x, i))) return false; return true; }
    case cbor::kind::map: { if (!r.IsMap() || r.AsMap().size() != b.map_size(x)) return false; bool ok = true; b.map_for_each(x, [&](test::value const &k, test::value const &v) { ok = ok && fb_equal(r.AsMap()[fb_key(k)], v); }); return ok; }
    case cbor::kind::registered: return fb_equal(r, b.before_encode(x));
    default: return false;
    }
}
#endif

static char const *const doc_path = DOC_PATH;
static std::string doc;
static std::size_t sink;
static std::string first;
static std::string expected_bytes;
#if defined(ARM_MP) || defined(ARM_FB)
static std::string alt;
#endif
#if defined(ARM_MP)
static msgpack::object_handle mph;
static msgpack::sbuffer mpbuf;
#endif
#if defined(ARM_FB)
static flexbuffers::Builder fbb;
#endif

#if defined(ARM_W) || defined(ARM_S) || defined(ARM_STRING) || defined(ARM_FRESH) || defined(ARM_VG_PUSH) || defined(ARM_VG_RAW) || defined(ARM_MP) || defined(ARM_FB)
static test::value tree;
#endif
#if defined(ARM_W)
static test::string_writer w;
#endif
#if defined(ARM_S)
static std::vector<char> buf;
#endif
#if defined(ARM_STRING)
static std::string target;
#endif
#if defined(ARM_LC_PREALLOC) || defined(ARM_LC_ALLOC) || defined(ARM_LC_EXACT)
static cbor_item_t *lc;
static std::vector<unsigned char> lcbuf;
#endif
#if defined(ARM_JC_CLEAR) || defined(ARM_JC_ENCODER) || defined(ARM_JC_FRESH)
static jsoncons::json jc;
static std::vector<std::uint8_t> jcbuf;
#endif
#if defined(ARM_JC_ENCODER)
static jsoncons::cbor::basic_cbor_encoder<jsoncons::bytes_sink<std::vector<std::uint8_t>>> *jce;
#endif
#if defined(ARM_VG_PUSH) || defined(ARM_VG_RAW)
static vg_out vo;
#endif

static void setup()
{
    expected_bytes = doc;
#if defined(ARM_W) || defined(ARM_S) || defined(ARM_STRING) || defined(ARM_FRESH) || defined(ARM_VG_PUSH) || defined(ARM_VG_RAW) || defined(ARM_MP) || defined(ARM_FB)
    test::test_binding b;
    auto t = cbor::decode<128>(b, std::string_view(doc));
    if (!t) std::abort();
    tree = std::move(*t);
#endif
#if defined(ARM_S)
    buf.resize(doc.size());
#endif
#if defined(ARM_MP)
    {
        msgpack::sbuffer sb;
        msgpack::packer<msgpack::sbuffer> pk(sb);
        mp_pack(pk, tree);
        alt.assign(sb.data(), sb.size());
        mph = msgpack::unpack(alt.data(), alt.size());
        if (!mp_equal(mph.get(), tree)) std::abort();
        expected_bytes = alt;
    }
#endif
#if defined(ARM_FB)
    {
        flexbuffers::Builder f;
        fb_build(f, tree);
        f.Finish();
        alt.assign(reinterpret_cast<char const *>(f.GetBuffer().data()), f.GetSize());
        if (!fb_equal(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size()), tree)) std::abort();
        if (!flexbuffers::VerifyBuffer(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size(), nullptr)) std::abort();
        expected_bytes = alt;
    }
#endif
#if defined(ARM_LC_PREALLOC) || defined(ARM_LC_ALLOC) || defined(ARM_LC_EXACT)
    cbor_load_result r;
    lc = cbor_load(reinterpret_cast<cbor_data>(doc.data()), doc.size(), &r);
    if (!lc) std::abort();
    lcbuf.resize(cbor_serialized_size(lc));
#endif
#if defined(ARM_JC_CLEAR) || defined(ARM_JC_ENCODER) || defined(ARM_JC_FRESH)
    jc = jsoncons::cbor::decode_cbor<jsoncons::json>(std::vector<std::uint8_t>(doc.begin(), doc.end()));
#endif
#if defined(ARM_JC_ENCODER)
    jce = new jsoncons::cbor::basic_cbor_encoder<jsoncons::bytes_sink<std::vector<std::uint8_t>>>(jcbuf);
#endif
#if defined(ARM_VG_RAW)
    vo.v.resize(2 * doc.size() + 64);
#endif
}

__attribute__((noinline)) static std::size_t op(bool const keep)
{
#if defined(ARM_READ)
    read_binding b;
    auto const r = cbor::decode<128>(b, std::string_view(doc));
    if (!r) std::abort();
    return *r;
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
        c.float2 = [](void *x, float v) { *static_cast<std::uint64_t *>(x) += std::bit_cast<std::uint32_t>(v); };
        c.float4 = [](void *x, float v) { *static_cast<std::uint64_t *>(x) += std::bit_cast<std::uint32_t>(v); };
        c.float8 = [](void *x, double v) { *static_cast<std::uint64_t *>(x) += std::bit_cast<std::uint64_t>(v); };
        c.boolean = [](void *x, bool v) { *static_cast<std::uint64_t *>(x) += v; };
        return c;
    }();
    auto const *p = reinterpret_cast<cbor_data>(doc.data());
    std::uint64_t sum = 0;
    std::size_t at = 0;
    while (at < doc.size()) {
        auto const r = cbor_stream_decode(p + at, doc.size() - at, &cb, &sum);
        if (r.status != CBOR_DECODER_FINISHED) std::abort();
        at += r.read;
    }
    return sum;
#elif defined(ARM_VG_READ)
    span_decoder d;
    d.p = reinterpret_cast<std::uint8_t const *>(doc.data());
    d.end = d.p + doc.size();
    return vg_read(d, 0);
#elif defined(ARM_MP_READ)
    struct reader : msgpack::null_visitor {
        std::uint64_t sum = 0;
        bool visit_boolean(bool v) { sum += v; return true; }
        bool visit_positive_integer(std::uint64_t v) { sum += v; return true; }
        bool visit_negative_integer(std::int64_t v) { sum += std::uint64_t(v); return true; }
        bool visit_float32(float v) { sum += std::bit_cast<std::uint32_t>(v); return true; }
        bool visit_float64(double v) { sum += std::bit_cast<std::uint64_t>(v); return true; }
        bool visit_str(char const *v, std::uint32_t n) { sum += bytes_sum(std::string_view(v, n)); return true; }
        bool visit_bin(char const *v, std::uint32_t n) { sum += bytes_sum(std::string_view(v, n)); return true; }
        bool start_array(std::uint32_t n) { sum += n; return true; }
        bool start_map(std::uint32_t n) { sum += n; return true; }
    } v;
    std::size_t off = 0;
    if (!msgpack::parse(alt.data(), alt.size(), off, v)) std::abort();
    return v.sum;
#elif defined(ARM_FB_READ)
    if (!flexbuffers::VerifyBuffer(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size(), nullptr)) std::abort();
    return fb_read(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size()));
#elif defined(ARM_JC_READ)
    jsoncons::cbor::cbor_bytes_cursor cursor(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const *>(doc.data()), doc.size()));
    std::uint64_t sum = 0;
    for (; !cursor.done(); cursor.next()) {
        auto const &e = cursor.current();
        switch (e.event_type()) {
        case jsoncons::staj_event_type::key:
        case jsoncons::staj_event_type::string_value: { auto const t = e.get<jsoncons::string_view>(); sum += bytes_sum(std::string_view(t.data(), t.size())); } break;
        case jsoncons::staj_event_type::byte_string_value: { auto const t = e.get<jsoncons::byte_string_view>(); sum += bytes_sum(std::string_view(reinterpret_cast<char const *>(t.data()), t.size())); } break;
        case jsoncons::staj_event_type::uint64_value: sum += e.get<std::uint64_t>(); break;
        case jsoncons::staj_event_type::int64_value: sum += std::uint64_t(e.get<std::int64_t>()); break;
        case jsoncons::staj_event_type::double_value: sum += std::bit_cast<std::uint64_t>(e.get<double>()); break;
        case jsoncons::staj_event_type::bool_value: sum += e.get<bool>(); break;
        default: sum += 1; break;
        }
    }
    return sum;
#elif defined(ARM_WALK)
    auto const r = cbor::doc_end<128>(std::string_view(doc));
    if (!r) std::abort();
    return *r;
#elif defined(ARM_TREE)
    test::test_binding b;
    auto const r = cbor::decode<128>(b, std::string_view(doc));
    if (!r) std::abort();
    return r->kind.index();
#elif defined(ARM_W)
    test::test_binding b;
    w.encoded.clear();
    if (!cbor::encode<128>(b, w, tree)) std::abort();
    if (keep) first = w.encoded;
    return w.encoded.size();
#elif defined(ARM_S)
    test::test_binding b;
    if (!cbor::encode<128>(b, std::span<char>(buf), tree)) std::abort();
    if (keep) first.assign(buf.data(), buf.size());
    return std::size_t(buf[0]);
#elif defined(ARM_STRING)
    test::test_binding b;
    target.clear();
    if (!cbor::encode<128>(b, target, tree)) std::abort();
    if (keep) first = target;
    return target.size();
#elif defined(ARM_FRESH)
    test::test_binding b;
    test::string_writer fw;
    if (!cbor::encode<128>(b, fw, tree)) std::abort();
    if (keep) first = fw.encoded;
    return fw.encoded.size();
#elif defined(ARM_LC_WALK)
    auto const *p = reinterpret_cast<cbor_data>(doc.data());
    std::size_t at = 0;
    while (at < doc.size()) {
        auto const r = cbor_stream_decode(p + at, doc.size() - at, &cbor_empty_callbacks, nullptr);
        if (r.status != CBOR_DECODER_FINISHED) std::abort();
        at += r.read;
    }
    return at;
#elif defined(ARM_LC_TREE)
    cbor_load_result r;
    cbor_item_t *i = cbor_load(reinterpret_cast<cbor_data>(doc.data()), doc.size(), &r);
    if (!i) std::abort();
    std::size_t const n = r.read;
    cbor_decref(&i);
    return n;
#elif defined(ARM_LC_PREALLOC)
    std::size_t const n = cbor_serialize(lc, lcbuf.data(), lcbuf.size());
    if (n == 0) std::abort();
    if (keep) first.assign(lcbuf.begin(), lcbuf.begin() + n);
    return n;
#elif defined(ARM_LC_ALLOC)
    unsigned char *out = nullptr;
    std::size_t size = 0;
    std::size_t const n = cbor_serialize_alloc(lc, &out, &size);
    if (n == 0) std::abort();
    if (keep) first.assign(out, out + n);
    std::free(out);
    return n;
#elif defined(ARM_LC_EXACT)
    std::size_t const size = cbor_serialized_size(lc);
    auto *out = static_cast<unsigned char *>(std::malloc(size));
    std::size_t const n = cbor_serialize(lc, out, size);
    if (n == 0) std::abort();
    if (keep) first.assign(out, out + n);
    std::free(out);
    return n;
#elif defined(ARM_JC_TREE)
    return jsoncons::cbor::decode_cbor<jsoncons::json>(std::span<std::uint8_t const>(reinterpret_cast<std::uint8_t const *>(doc.data()), doc.size())).size();
#elif defined(ARM_JC_CLEAR)
    jcbuf.clear();
    jsoncons::cbor::encode_cbor(jc, jcbuf);
    if (keep) first.assign(jcbuf.begin(), jcbuf.end());
    return jcbuf.size();
#elif defined(ARM_JC_ENCODER)
    jcbuf.clear();
    jce->reset();
    jc.dump(*jce);
    if (keep) first.assign(jcbuf.begin(), jcbuf.end());
    return jcbuf.size();
#elif defined(ARM_JC_FRESH)
    std::vector<std::uint8_t> v;
    jsoncons::cbor::encode_cbor(jc, v);
    if (keep) first.assign(v.begin(), v.end());
    return v.size();
#elif defined(ARM_VG_WALK)
    span_decoder d;
    d.p = reinterpret_cast<std::uint8_t const *>(doc.data());
    d.end = d.p + doc.size();
    vg_item(d, 0);
    return std::size_t(d.p - reinterpret_cast<std::uint8_t const *>(doc.data()));
#elif defined(ARM_VG_PUSH) || defined(ARM_VG_RAW)
    vo.reset();
    vg_encode(vo, tree);
    if (keep) first = vo.str();
    return vo.size();
#elif defined(ARM_MP_WALK)
    msgpack::null_visitor v;
    std::size_t off = 0;
    if (!msgpack::parse(alt.data(), alt.size(), off, v)) std::abort();
    return off;
#elif defined(ARM_MP_TREE)
    msgpack::object_handle const h = msgpack::unpack(alt.data(), alt.size());
    return std::size_t(h.get().type);
#elif defined(ARM_MP_REUSE)
    mpbuf.clear();
    msgpack::pack(mpbuf, mph.get());
    if (keep) first.assign(mpbuf.data(), mpbuf.size());
    return mpbuf.size();
#elif defined(ARM_MP_FRESH)
    msgpack::sbuffer sb;
    msgpack::pack(sb, mph.get());
    if (keep) first.assign(sb.data(), sb.size());
    return sb.size();
#elif defined(ARM_FB_WALK)
    if (!flexbuffers::VerifyBuffer(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size(), nullptr)) std::abort();
    return fb_touch(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size()));
#elif defined(ARM_FB_TREE)
    return fb_touch(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size()));
#elif defined(ARM_FB_REUSE)
    fbb.Clear();
    fb_build(fbb, tree);
    fbb.Finish();
    if (keep) first.assign(reinterpret_cast<char const *>(fbb.GetBuffer().data()), fbb.GetSize());
    return fbb.GetSize();
#elif defined(ARM_FB_FRESH)
    flexbuffers::Builder f;
    fb_build(f, tree);
    f.Finish();
    if (keep) first.assign(reinterpret_cast<char const *>(f.GetBuffer().data()), f.GetSize());
    return f.GetSize();
#else
#error no arm
#endif
}

static void run(benchmark::State &state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(op(false));
    state.SetBytesProcessed(std::int64_t(state.iterations()) * std::int64_t(expected_bytes.size()));
}

int main(int argc, char **argv)
{
    std::ifstream in(doc_path, std::ios::binary);
    doc.assign(std::istreambuf_iterator<char>(in), {});
    if (doc.empty()) std::abort();
    setup();
    sink += op(true);
    benchmark::AddCustomContext("same", first.empty() ? "n/a" : first == expected_bytes ? "yes" : "no");
    benchmark::AddCustomContext("document", doc_path);
    benchmark::RegisterBenchmark(ARM_NAME, run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
