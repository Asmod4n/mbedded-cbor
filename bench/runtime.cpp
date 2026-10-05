#include <benchmark/benchmark.h>

#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
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

#if defined(ARM_LC_PREALLOC) || defined(ARM_LC_READ)
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
    case cbor::kind::negative_integer: pk.pack_int64(-1 - std::int64_t(b.unsigned_of(x) - 1)); break;
    case cbor::kind::floating_point: pk.pack_double(b.float_of(x)); break;
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); if (v == 20) pk.pack_false(); else if (v == 21) pk.pack_true(); else pk.pack_nil(); } break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); pk.pack_str(std::uint32_t(t.size())); pk.pack_str_body(t.data(), std::uint32_t(t.size())); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); pk.pack_bin(std::uint32_t(t.size())); pk.pack_bin_body(t.data(), std::uint32_t(t.size())); } break;
    case cbor::kind::array: { auto const n = b.array_size(x); pk.pack_array(std::uint32_t(n)); for (std::uint64_t i = 0; i < n; ++i) mp_pack(pk, b.array_at(x, i)); } break;
    case cbor::kind::map: pk.pack_map(std::uint32_t(b.map_size(x))); b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { mp_pack(pk, k); mp_pack(pk, v); }); break;
    case cbor::kind::registered: mp_pack(pk, b.before_encode(x)); break;
    default: std::abort();
    }
}
#endif

#if defined(ARM_FB_REUSE) || defined(ARM_FB_READ)
#define ARM_FB 1
#include <flatbuffers/flexbuffers.h>
static std::string fb_key(bench::value const &k)
{
    bench::binding b;
    switch (b.kind_of(k)) {
    case cbor::kind::text_string: return std::string(b.text_of(k));
    case cbor::kind::unsigned_integer: return std::to_string(b.unsigned_of(k));
    case cbor::kind::negative_integer: return std::to_string(-1 - std::int64_t(b.unsigned_of(k) - 1));
    default: std::abort();
    }
}
static void fb_build(flexbuffers::Builder &f, bench::value const &x)
{
    bench::binding b;
    switch (b.kind_of(x)) {
    case cbor::kind::unsigned_integer: f.UInt(b.unsigned_of(x)); break;
    case cbor::kind::negative_integer: f.Int(-1 - std::int64_t(b.unsigned_of(x) - 1)); break;
    case cbor::kind::floating_point: f.Double(b.float_of(x)); break;
    case cbor::kind::simple_value: { auto const v = b.simple_of(x); if (v == 20 || v == 21) f.Bool(v == 21); else f.Null(); } break;
    case cbor::kind::text_string: { auto const t = b.text_of(x); f.String(t.data(), t.size()); } break;
    case cbor::kind::byte_string: { auto const t = b.bytes_of(x); f.Blob(t.data(), t.size()); } break;
    case cbor::kind::array: { auto const st = f.StartVector(); auto const n = b.array_size(x); for (std::uint64_t i = 0; i < n; ++i) fb_build(f, b.array_at(x, i)); f.EndVector(st, false, false); } break;
    case cbor::kind::map: { auto const st = f.StartMap(); b.map_for_each(x, [&](bench::value const &k, bench::value const &v) { auto const s = fb_key(k); f.Key(s.data(), s.size()); fb_build(f, v); }); f.EndMap(st); } break;
    case cbor::kind::registered: fb_build(f, b.before_encode(x)); break;
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
    if (r.IsInt()) return std::uint64_t(r.AsInt64());
    if (r.IsFloat()) return std::bit_cast<std::uint64_t>(r.AsDouble());
    if (r.IsBool()) return r.AsBool();
    return 1;
}
#endif

static std::string doc;
static std::string const *bytes = &doc;
#if defined(ARM_S) || defined(ARM_VG_RAW) || defined(ARM_MP) || defined(ARM_FB)
static bench::value tree;
#endif
#if defined(ARM_MP) || defined(ARM_FB)
static std::string alt;
#endif
#if defined(ARM_MP_REUSE)
static msgpack::object_handle mph;
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
    auto t = cbor::decode<128>(b, std::string_view(doc));
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
#if defined(ARM_MP_REUSE)
    mph = msgpack::unpack(alt.data(), alt.size());
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
    auto const r = cbor::decode<128>(b, std::string_view(doc));
    if (!r)
        std::abort();
    return *r;
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
#elif defined(ARM_VG_RAW)
    vo.p = vo.v.data();
    vg_encode(vo, tree);
    benchmark::ClobberMemory();
    return std::size_t(vo.p - vo.v.data());
#elif defined(ARM_VG_READ)
    span_decoder d;
    d.p = reinterpret_cast<std::uint8_t const *>(doc.data());
    d.end = d.p + doc.size();
    return vg_read(d, 0);
#elif defined(ARM_MP_REUSE)
    mpbuf.clear();
    msgpack::pack(mpbuf, mph.get());
    benchmark::ClobberMemory();
    return mpbuf.size();
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
    if (!msgpack::parse(alt.data(), alt.size(), off, v))
        std::abort();
    return v.sum;
#elif defined(ARM_FB_REUSE)
    fbb.Clear();
    fb_build(fbb, tree);
    fbb.Finish();
    benchmark::ClobberMemory();
    return fbb.GetSize();
#elif defined(ARM_FB_READ)
    if (!flexbuffers::VerifyBuffer(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size(), nullptr))
        std::abort();
    return fb_read(flexbuffers::GetRoot(reinterpret_cast<std::uint8_t const *>(alt.data()), alt.size()));
#else
#error no arm
#endif
}

static void run(benchmark::State &state)
{
    for (auto _ : state)
        benchmark::DoNotOptimize(op());
    state.SetBytesProcessed(std::int64_t(state.iterations()) * std::int64_t(bytes->size()));
}

int main(int argc, char **argv)
{
    std::ifstream in(DOC_PATH, std::ios::binary);
    doc.assign(std::istreambuf_iterator<char>(in), {});
    if (doc.empty())
        std::abort();
    setup();
    benchmark::AddCustomContext("document", DOC_PATH);
    benchmark::RegisterBenchmark(ARM_NAME, run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
