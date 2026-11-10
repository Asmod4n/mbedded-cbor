#include <benchmark/benchmark.h>
#include "../test/owned_copy.hpp"
#include <cbor/cbor.hpp>
#include <capnp/message.h>
#include <capnp/serialize.h>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#pragma GCC diagnostic ignored "-Warray-bounds"
#include <flatbuffers/flatbuffers.h>
#pragma GCC diagnostic pop
#include <bit>
#include <cstdio>
#include <cstdint>
#include <span>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#if defined(OP_MB_READ_UNVERIFIED)
#define OP_MB_READ
#endif
#if defined(OP_MB_FIELD_UNVERIFIED)
#define OP_MB_FIELD
#endif
#if defined(OP_MB_READ_UNVERIFIED) || defined(OP_MB_FIELD_UNVERIFIED)
#define MB_PATH unverified_path
#define MB_AT_PATH unverified_at_path
#else
#define MB_PATH path
#define MB_AT_PATH at_path
#endif

static std::shared_ptr<std::string const> held;
static std::string msg, fbmsg;
static kj::Array<capnp::word> cpmsg;

#if defined(DS_CARS) || defined(DS_RECORDS)
static double bytes_sum(std::string_view const s)
{
    return double(std::transform_reduce(s.begin(), s.end(), std::uint64_t{0}, std::plus<>{},
                                        [](char const c) { return std::uint64_t(std::uint8_t(c)); }));
}
#endif

static std::string file_read(char const *path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

#if defined(OP_CP_DECODE) || defined(OP_CP_READ) || defined(OP_CP_FIELD)
static capnp::FlatArrayMessageReader cp_reader()
{
    capnp::ReaderOptions opt;
    opt.traversalLimitInWords = std::uint64_t(1) << 40;
    return capnp::FlatArrayMessageReader(cpmsg.asPtr(), opt);
}
#endif

static void cp_store(capnp::MallocMessageBuilder &mb)
{
    cpmsg = capnp::messageToFlatArray(mb);
}

static void fb_store(flatbuffers::FlatBufferBuilder const &b)
{
    fbmsg.assign(reinterpret_cast<char const *>(b.GetBufferPointer()), b.GetSize());
}

static std::uint8_t const *fb_bytes()
{
    return reinterpret_cast<std::uint8_t const *>(fbmsg.data());
}

#if defined(DS_CARS)
#include <nlohmann/json.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#pragma GCC diagnostic ignored "-Warray-bounds"
#include "carsales_generated.h"
#pragma GCC diagnostic pop
#include "carsales.capnp.h"
struct Wheel { std::uint16_t diameter; float airPressure; bool snowTires; };

template <>
struct cbor::describe_members<Wheel> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1501};
    static constexpr std::tuple members{
        CBOR_MEMBER(Wheel, diameter),
        CBOR_MEMBER(Wheel, airPressure),
        CBOR_MEMBER(Wheel, snowTires),
    };
};

struct Engine { std::uint16_t horsepower; std::uint8_t cylinders; std::uint32_t cc; bool usesGas; bool usesElectric; };

template <>
struct cbor::describe_members<Engine> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1502};
    static constexpr std::tuple members{
        CBOR_MEMBER(Engine, horsepower),
        CBOR_MEMBER(Engine, cylinders),
        CBOR_MEMBER(Engine, cc),
        CBOR_MEMBER(Engine, usesGas),
        CBOR_MEMBER(Engine, usesElectric),
    };
};

struct Car {
    std::string_view make; std::string_view model; std::uint8_t color; std::uint8_t seats; std::uint8_t doors;
    std::vector<Wheel> wheels; std::uint16_t length; std::uint16_t width; std::uint16_t height; std::uint32_t weight;
    Engine engine; float fuelCapacity; float fuelLevel; bool hasPowerWindows; bool hasPowerSteering;
    bool hasCruiseControl; std::uint8_t cupHolders; bool hasNavSystem;
#if defined(MB_CAR_CONSTRUCTOR)
    Car(std::string_view make_, std::string_view model_, std::uint8_t color_, std::uint8_t seats_, std::uint8_t doors_,
        std::vector<Wheel> wheels_, std::uint16_t length_, std::uint16_t width_, std::uint16_t height_, std::uint32_t weight_,
        Engine engine_, float fuelCapacity_, float fuelLevel_, bool hasPowerWindows_, bool hasPowerSteering_,
        bool hasCruiseControl_, std::uint8_t cupHolders_, bool hasNavSystem_)
        : make(make_), model(model_), color(color_), seats(seats_), doors(doors_), wheels(std::move(wheels_)), length(length_),
          width(width_), height(height_), weight(weight_), engine(engine_), fuelCapacity(fuelCapacity_), fuelLevel(fuelLevel_),
          hasPowerWindows(hasPowerWindows_), hasPowerSteering(hasPowerSteering_), hasCruiseControl(hasCruiseControl_),
          cupHolders(cupHolders_), hasNavSystem(hasNavSystem_)
    {
    }
#endif
};

template <>
struct cbor::describe_members<Car> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1500};
    static constexpr std::tuple members{
        CBOR_MEMBER(Car, make),
        CBOR_MEMBER(Car, model),
        CBOR_MEMBER(Car, color),
        CBOR_MEMBER(Car, seats),
        CBOR_MEMBER(Car, doors),
        CBOR_MEMBER(Car, wheels),
        CBOR_MEMBER(Car, length),
        CBOR_MEMBER(Car, width),
        CBOR_MEMBER(Car, height),
        CBOR_MEMBER(Car, weight),
        CBOR_MEMBER(Car, engine),
        CBOR_MEMBER(Car, fuelCapacity),
        CBOR_MEMBER(Car, fuelLevel),
        CBOR_MEMBER(Car, hasPowerWindows),
        CBOR_MEMBER(Car, hasPowerSteering),
        CBOR_MEMBER(Car, hasCruiseControl),
        CBOR_MEMBER(Car, cupHolders),
        CBOR_MEMBER(Car, hasNavSystem),
    };
};

struct ParkingLot { std::vector<Car> cars; };

template <>
struct cbor::describe_members<ParkingLot> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1503};
    static constexpr std::tuple members{
        CBOR_MEMBER(ParkingLot, cars),
    };
};

using T = ParkingLot;
using S = cbor::schema<T>;
static std::string json_text;
static nlohmann::json json;

#define CAR_SUM(c, MAKE, MODEL, WHEELS, W, ENGINE, F) \
    (bytes_sum(MAKE) + bytes_sum(MODEL) + double(c F color) + double(c F seats) + double(c F doors) + WHEELS + double(c F length) + \
     double(c F width) + double(c F height) + double(c F weight) + double(ENGINE horsepower) + double(ENGINE cylinders) +      \
     double(ENGINE cc) + double(ENGINE usesGas) + double(ENGINE usesElectric) + double(c F fuelCapacity) +                      \
     double(c F fuelLevel) + double(c F hasPowerWindows) + double(c F hasPowerSteering) + double(c F hasCruiseControl) +       \
     double(c F cupHolders) + double(c F hasNavSystem))

static double sum(T const &l)
{
    double s = 0;
    for (auto const &c : l.cars) {
        double w = 0;
        for (auto const &x : c.wheels) w += double(x.diameter) + double(x.airPressure) + double(x.snowTires);
        s += CAR_SUM(c, c.make, c.model, w, x, c.engine., .);
    }
    return s;
}

static T load()
{
    json_text = file_read(CARS_JSON);
    json = nlohmann::json::parse(json_text);
    T lot;
    for (auto const &c : json) {
        Car car{c["make"].get_ref<std::string const &>(), c["model"].get_ref<std::string const &>(), c["color"], c["seats"], c["doors"], {}, c["length"], c["width"], c["height"], c["weight"],
                Engine{c["engine"]["horsepower"], c["engine"]["cylinders"], c["engine"]["cc"], c["engine"]["usesGas"], c["engine"]["usesElectric"]},
                c["fuelCapacity"], c["fuelLevel"], c["hasPowerWindows"], c["hasPowerSteering"], c["hasCruiseControl"], c["cupHolders"], c["hasNavSystem"]};
        for (auto const &w : c["wheels"]) car.wheels.push_back(Wheel{w["diameter"], w["airPressure"], w["snowTires"]});
        lot.cars.push_back(std::move(car));
    }
    return lot;
}

static std::size_t fb_encode(T const &lot, bool const kept)
{
    flatbuffers::FlatBufferBuilder fbb;
    std::vector<flatbuffers::Offset<fbcar::Car>> cs;
    for (auto const &c : lot.cars) {
        std::vector<fbcar::Wheel> ws;
        for (auto const &w : c.wheels) ws.emplace_back(w.diameter, w.airPressure, w.snowTires);
        fbcar::Engine const e(c.engine.horsepower, c.engine.cylinders, c.engine.cc, c.engine.usesGas, c.engine.usesElectric);
        cs.push_back(fbcar::CreateCar(fbb, fbb.CreateString(c.make.data(), c.make.size()), fbb.CreateString(c.model.data(), c.model.size()), c.color, c.seats, c.doors, fbb.CreateVectorOfStructs(ws), c.length, c.width, c.height, c.weight, &e,
                                      c.fuelCapacity, c.fuelLevel, c.hasPowerWindows, c.hasPowerSteering, c.hasCruiseControl, c.cupHolders, c.hasNavSystem));
    }
    fbb.Finish(fbcar::CreateParkingLot(fbb, fbb.CreateVector(cs)));
    if (kept)
        fb_store(fbb);
    return fbb.GetSize();
}

static std::size_t cp_encode(T const &lot, bool const kept)
{
    capnp::MallocMessageBuilder mb;
    auto bs = mb.initRoot<carsales::ParkingLot>().initCars(unsigned(lot.cars.size()));
    for (unsigned k = 0; k < lot.cars.size(); k++) {
        Car const &c = lot.cars[k];
        auto b = bs[k];
        b.setMake(capnp::Text::Reader(c.make.data(), c.make.size()));
        b.setModel(capnp::Text::Reader(c.model.data(), c.model.size()));
        b.setColor(c.color); b.setSeats(c.seats); b.setDoors(c.doors);
        auto ws = b.initWheels(unsigned(c.wheels.size()));
        for (unsigned i = 0; i < c.wheels.size(); i++) { ws[i].setDiameter(c.wheels[i].diameter); ws[i].setAirPressure(c.wheels[i].airPressure); ws[i].setSnowTires(c.wheels[i].snowTires); }
        b.setLength(c.length); b.setWidth(c.width); b.setHeight(c.height); b.setWeight(c.weight);
        auto e = b.initEngine();
        e.setHorsepower(c.engine.horsepower); e.setCylinders(c.engine.cylinders); e.setCc(c.engine.cc); e.setUsesGas(c.engine.usesGas); e.setUsesElectric(c.engine.usesElectric);
        b.setFuelCapacity(c.fuelCapacity); b.setFuelLevel(c.fuelLevel);
        b.setHasPowerWindows(c.hasPowerWindows); b.setHasPowerSteering(c.hasPowerSteering); b.setHasCruiseControl(c.hasCruiseControl); b.setCupHolders(c.cupHolders); b.setHasNavSystem(c.hasNavSystem);
    }
    if (kept)
        cp_store(mb);
    return mb.getSegmentsForOutput().size();
}

static void foreign_build(T const &lot)
{
    fb_encode(lot, true);
    cp_encode(lot, true);
}

static bool fb_verify()
{
    flatbuffers::Verifier v(fb_bytes(), fbmsg.size());
    return fbcar::VerifyParkingLotBuffer(v);
}

using FbT = fbcar::ParkingLotT;
#if defined(OP_FB_DECODE)
static std::unique_ptr<FbT> fb_unpack() { return std::unique_ptr<FbT>(fbcar::GetParkingLot(fbmsg.data())->UnPack()); }
#endif
#if defined(OP_FB_DECODE)
static double sum(FbT const &l)
{
    double s = 0;
    for (auto const &c : l.cars) {
        double w = 0;
        for (auto const &x : c->wheels) w += double(x.diameter()) + double(x.air_pressure()) + double(x.snow_tires());
        auto const &e = *c->engine;
        s += bytes_sum(c->make) + bytes_sum(c->model) + c->color + c->seats + c->doors + w + c->length + c->width + c->height + c->weight +
             e.horsepower() + e.cylinders() + e.cc() + e.uses_gas() + e.uses_electric() + double(c->fuel_capacity) + double(c->fuel_level) +
             c->has_power_windows + c->has_power_steering + c->has_cruise_control + c->cup_holders + c->has_nav_system;
    }
    return s;
}
#endif

#if defined(OP_CP_DECODE)
static T cp_copy()
{
    auto rd = cp_reader();
    auto const cars = rd.getRoot<carsales::ParkingLot>().getCars();
    T lot;
    lot.cars.reserve(cars.size());
    for (auto r : cars) {
        auto const ws = r.getWheels();
        std::vector<Wheel> wheels;
        wheels.reserve(ws.size());
        for (auto w : ws) wheels.push_back(Wheel{w.getDiameter(), w.getAirPressure(), w.getSnowTires()});
        auto const e = r.getEngine();
        auto const make = r.getMake();
        auto const model = r.getModel();
        lot.cars.push_back(Car{std::string_view(make.begin(), make.size()), std::string_view(model.begin(), model.size()), r.getColor(), r.getSeats(), r.getDoors(), std::move(wheels),
                               r.getLength(), r.getWidth(), r.getHeight(), r.getWeight(),
                               Engine{e.getHorsepower(), e.getCylinders(), e.getCc(), e.getUsesGas(), e.getUsesElectric()},
                               r.getFuelCapacity(), r.getFuelLevel(), r.getHasPowerWindows(), r.getHasPowerSteering(), r.getHasCruiseControl(), r.getCupHolders(), r.getHasNavSystem()});
    }
    return lot;
}
#endif

template <class V> static double num(V const &v)
{
    if (!v) [[unlikely]] std::abort();
    return double(*v);
}
template <class V> static double text(V const &v)
{
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(*v);
}

#if defined(OP_MB_READ)
static double mb_read()
{
    auto const opened = S::MB_PATH(held);
    if (!opened) [[unlikely]] std::abort();
    auto const cars = opened->at_path<"$.cars">();
    if (!cars) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < cars->size(); ++i) {
        auto const car = cars->at_path<"@[]">(i);
        if (!car) [[unlikely]] std::abort();
        auto const wheels = car->at_path<"@.wheels">();
        if (!wheels) [[unlikely]] std::abort();
        double w = 0;
        for (std::size_t k = 0; k < wheels->size(); ++k) {
            auto const x = wheels->at_path<"@[]">(k);
            if (!x) [[unlikely]] std::abort();
            w += num(x->at_path<"@.diameter">()) + num(x->at_path<"@.airPressure">()) + num(x->at_path<"@.snowTires">());
        }
        s += text(car->at_path<"@.make">()) + text(car->at_path<"@.model">()) + num(car->at_path<"@.color">()) + num(car->at_path<"@.seats">()) + num(car->at_path<"@.doors">()) + w +
             num(car->at_path<"@.length">()) + num(car->at_path<"@.width">()) + num(car->at_path<"@.height">()) + num(car->at_path<"@.weight">()) +
             num(car->at_path<"@.engine.horsepower">()) + num(car->at_path<"@.engine.cylinders">()) + num(car->at_path<"@.engine.cc">()) +
             num(car->at_path<"@.engine.usesGas">()) + num(car->at_path<"@.engine.usesElectric">()) + num(car->at_path<"@.fuelCapacity">()) +
             num(car->at_path<"@.fuelLevel">()) + num(car->at_path<"@.hasPowerWindows">()) + num(car->at_path<"@.hasPowerSteering">()) +
             num(car->at_path<"@.hasCruiseControl">()) + num(car->at_path<"@.cupHolders">()) + num(car->at_path<"@.hasNavSystem">());
    }
    return s;
}
#endif

#if defined(OP_FB_READ) || defined(OP_FB_READ_V) || defined(OP_FB_ENCODE)
static double fb_read()
{
    double s = 0;
    for (auto const *r : *fbcar::GetParkingLot(fbmsg.data())->cars()) {
        double w = 0;
        for (auto const *x : *r->wheels()) w += x->diameter() + double(x->air_pressure()) + x->snow_tires();
        auto const *e = r->engine();
        s += bytes_sum(r->make()->string_view()) + bytes_sum(r->model()->string_view()) + r->color() + r->seats() + r->doors() + w + r->length() + r->width() + r->height() + r->weight() +
             e->horsepower() + e->cylinders() + e->cc() + e->uses_gas() + e->uses_electric() + double(r->fuel_capacity()) + double(r->fuel_level()) +
             r->has_power_windows() + r->has_power_steering() + r->has_cruise_control() + r->cup_holders() + r->has_nav_system();
    }
    return s;
}
#endif

#if defined(OP_CP_READ)
static double cp_read()
{
    auto rd = cp_reader();
    double s = 0;
    for (auto r : rd.getRoot<carsales::ParkingLot>().getCars()) {
        double w = 0;
        for (auto x : r.getWheels()) w += x.getDiameter() + double(x.getAirPressure()) + x.getSnowTires();
        auto e = r.getEngine();
        auto const make = r.getMake();
        auto const model = r.getModel();
        s += bytes_sum(std::string_view(make.begin(), make.size())) + bytes_sum(std::string_view(model.begin(), model.size())) + r.getColor() + r.getSeats() + r.getDoors() + w +
             r.getLength() + r.getWidth() + r.getHeight() + r.getWeight() + e.getHorsepower() + e.getCylinders() + e.getCc() + e.getUsesGas() + e.getUsesElectric() +
             double(r.getFuelCapacity()) + double(r.getFuelLevel()) + r.getHasPowerWindows() + r.getHasPowerSteering() + r.getHasCruiseControl() + r.getCupHolders() + r.getHasNavSystem();
    }
    return s;
}
#endif

#if defined(OP_MB_FIELD)
static double mb_field()
{
    auto const opened = S::MB_PATH(held);
    if (!opened) [[unlikely]] std::abort();
    return num(opened->at_path<"$.cars[500].engine.cc">());
}
#endif
#if defined(OP_FB_FIELD) || defined(OP_FB_FIELD_V)
static double fb_field() { return fbcar::GetParkingLot(fbmsg.data())->cars()->Get(500)->engine()->cc(); }
#endif
#if defined(OP_CP_FIELD)
static double cp_field()
{
    auto rd = cp_reader();
    return rd.getRoot<carsales::ParkingLot>().getCars()[500].getEngine().getCc();
}
#endif

// The kinds table: every kind of access of mbedded-cbor, and beside it a library that does the same work. The
// arms read the CBOR of structs<T>::encode (plain), the schema arms read Packed CBOR (msg).
static std::shared_ptr<std::string const> plain;
static std::size_t written_bytes;

#if defined(OP_LAZY_READ)
static double lazy_sum(cbor::lazy const &l)
{
    auto const t = l.type();
    if (!t) [[unlikely]] std::abort();
    switch (*t) {
    case cbor::type::text_string: {
        auto const v = l.get<std::string_view>();
        if (!v) [[unlikely]] std::abort();
        return bytes_sum(**v);
    }
    case cbor::type::unsigned_integer: return num(l.get<std::uint64_t>());
    case cbor::type::negative_integer: return num(l.get<std::int64_t>());
    case cbor::type::half_float:
    case cbor::type::single_float:
    case cbor::type::double_float: return num(l.get<double>());
    case cbor::type::false_value:
    case cbor::type::true_value: return num(l.get<bool>());
    case cbor::type::array: {
        auto const e = l.elements();
        if (!e) [[unlikely]] std::abort();
        double s = 0;
        for (auto const x : *e) {
            if (!x) [[unlikely]] std::abort();
            s += lazy_sum(*x);
        }
        return s;
    }
    case cbor::type::tag:
    case cbor::type::map: {
        auto const e = l.entries();
        if (!e) [[unlikely]] std::abort();
        double s = 0;
        for (auto const x : *e) {
            if (!x) [[unlikely]] std::abort();
            s += lazy_sum(x->second);
        }
        return s;
    }
    default: std::abort();
    }
}
#endif

#if defined(OP_LAZY_FIELD)
static double lazy_field()
{
    auto const v = cbor::lazy::view(plain)
                       .and_then([](cbor::lazy const &l) { return l.at("cars"); })
                       .and_then([](cbor::lazy const &l) { return l.at(std::size_t{500}); })
                       .and_then([](cbor::lazy const &l) { return l.at("engine"); })
                       .and_then([](cbor::lazy const &l) { return l.at("cc"); });
    if (!v) [[unlikely]] std::abort();
    return num(v->get<std::uint64_t>());
}
#endif

#if defined(OP_LAZY_READ)
static double lazy_read()
{
    auto const root = cbor::lazy::view(plain);
    if (!root) [[unlikely]] std::abort();
    return lazy_sum(*root);
}
#endif

#if defined(OP_PATH_FIELD)
static double path_field() { return num(cbor::at_path<"$.cars[500].engine.cc", std::uint32_t>(std::string_view(*plain))); }
#endif

#if defined(OP_PATH_READ)
template <class V> static double lazy_text(V const &v)
{
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
}

static double path_read()
{
    auto const cars = cbor::lazy::view(plain).and_then([](cbor::lazy const &l) { return l.at_path<"$.cars">(); });
    if (!cars) [[unlikely]] std::abort();
    auto const all = cars->elements();
    if (!all) [[unlikely]] std::abort();
    double s = 0;
    for (auto const car : *all) {
        if (!car) [[unlikely]] std::abort();
        auto const wheels = car->at_path<"$.wheels">().and_then([](cbor::lazy const &l) { return l.elements(); });
        if (!wheels) [[unlikely]] std::abort();
        double w = 0;
        for (auto const x : *wheels) {
            if (!x) [[unlikely]] std::abort();
            w += num(x->at_path<"$.diameter", std::uint64_t>()) + num(x->at_path<"$.airPressure", double>()) + num(x->at_path<"$.snowTires", bool>());
        }
        s += lazy_text(car->at_path<"$.make", std::string_view>()) + lazy_text(car->at_path<"$.model", std::string_view>()) +
             num(car->at_path<"$.color", std::uint64_t>()) + num(car->at_path<"$.seats", std::uint64_t>()) + num(car->at_path<"$.doors", std::uint64_t>()) + w +
             num(car->at_path<"$.length", std::uint64_t>()) + num(car->at_path<"$.width", std::uint64_t>()) + num(car->at_path<"$.height", std::uint64_t>()) +
             num(car->at_path<"$.weight", std::uint64_t>()) + num(car->at_path<"$.engine.horsepower", std::uint64_t>()) +
             num(car->at_path<"$.engine.cylinders", std::uint64_t>()) + num(car->at_path<"$.engine.cc", std::uint64_t>()) +
             num(car->at_path<"$.engine.usesGas", bool>()) + num(car->at_path<"$.engine.usesElectric", bool>()) +
             num(car->at_path<"$.fuelCapacity", double>()) + num(car->at_path<"$.fuelLevel", double>()) + num(car->at_path<"$.hasPowerWindows", bool>()) +
             num(car->at_path<"$.hasPowerSteering", bool>()) + num(car->at_path<"$.hasCruiseControl", bool>()) +
             num(car->at_path<"$.cupHolders", std::uint64_t>()) + num(car->at_path<"$.hasNavSystem", bool>());
    }
    return s;
}
#endif

#if defined(OP_ITEM_FIELD) || defined(OP_ITEM_READ) || defined(OP_ITEM_ENCODE)
#if defined(OP_ITEM_FIELD) || defined(OP_ITEM_READ)
static cbor::item const &untagged(cbor::item const &i)
{
    cbor::item const *at = &i;
    while (at->type == cbor::type::tag) {
        auto const t = at->get<cbor::type::tag>();
        if (!t) [[unlikely]] std::abort();
        at = t->second;
    }
    return *at;
}

#endif

#if defined(OP_ITEM_FIELD)
static cbor::item const &member(cbor::item const &m, std::string_view const key)
{
    auto const entries = untagged(m).get<cbor::type::map>();
    if (!entries) [[unlikely]] std::abort();
    for (std::size_t i = 0; i + 1 < entries->size(); i += 2)
        if ((*entries)[i].get<cbor::type::text_string>().value_or(std::string_view{}) == key)
            return untagged((*entries)[i + 1]);
    std::abort();
}
#endif

#if defined(OP_ITEM_READ)
static double item_sum(cbor::item const &tagged)
{
    cbor::item const &i = untagged(tagged);
    switch (i.type) {
    case cbor::type::text_string: return bytes_sum(*i.get<cbor::type::text_string>());
    case cbor::type::unsigned_integer: return double(*i.get<cbor::type::unsigned_integer>());
    case cbor::type::negative_integer: return -1.0 - double(*i.get<cbor::type::negative_integer>());
    case cbor::type::half_float: return *i.get<cbor::type::half_float>();
    case cbor::type::single_float: return *i.get<cbor::type::single_float>();
    case cbor::type::double_float: return *i.get<cbor::type::double_float>();
    case cbor::type::false_value: return 0;
    case cbor::type::true_value: return 1;
    case cbor::type::array: {
        double s = 0;
        for (auto const &x : *i.get<cbor::type::array>()) s += item_sum(x);
        return s;
    }
    case cbor::type::map: {
        auto const e = *i.get<cbor::type::map>();
        double s = 0;
        for (std::size_t k = 1; k < e.size(); k += 2) s += item_sum(e[k]);
        return s;
    }
    default: std::abort();
    }
}
#endif
#endif

#if defined(OP_ITEM_FIELD)
static double item_field()
{
    auto const tree = cbor::decode(*plain);
    if (!tree) [[unlikely]] std::abort();
    auto const cars = member(**tree, "cars").get<cbor::type::array>();
    if (!cars) [[unlikely]] std::abort();
    return double(*member(member((*cars)[500], "engine"), "cc").get<cbor::type::unsigned_integer>());
}
#endif

#if defined(OP_ITEM_READ)
static double item_read()
{
    auto const tree = cbor::decode(*plain);
    if (!tree) [[unlikely]] std::abort();
    return item_sum(**tree);
}
#endif

#if defined(OP_ITEM_ENCODE)
static std::optional<cbor::owning<cbor::item>> item_source;
#endif

#if defined(OP_TC_FIELD) || defined(OP_TC_READ)
#include <tinycbor/cbor.h>
static void tc_untag(CborValue *v)
{
    while (cbor_value_is_tag(v))
        if (cbor_value_skip_tag(v) != CborNoError) [[unlikely]] std::abort();
}

#if defined(OP_TC_FIELD)
static CborValue tc_member(CborValue map, char const *key)
{
    tc_untag(&map);
    CborValue v;
    if (cbor_value_map_find_value(&map, key, &v) != CborNoError || cbor_value_get_type(&v) == CborInvalidType) [[unlikely]] std::abort();
    tc_untag(&v);
    return v;
}
#endif

static CborValue tc_root(CborParser *p)
{
    CborValue v;
    if (cbor_parser_init(reinterpret_cast<std::uint8_t const *>(plain->data()), plain->size(), 0, p, &v) != CborNoError) [[unlikely]] std::abort();
    return v;
}
#endif

#if defined(OP_TC_FIELD)
static double tc_field()
{
    CborParser p;
    CborValue const cars = tc_member(tc_root(&p), "cars");
    CborValue it;
    if (cbor_value_enter_container(&cars, &it) != CborNoError) [[unlikely]] std::abort();
    for (int k = 0; k < 500; ++k) {
        tc_untag(&it);
        if (cbor_value_advance(&it) != CborNoError) [[unlikely]] std::abort();
    }
    CborValue const cc = tc_member(tc_member(it, "engine"), "cc");
    std::uint64_t v;
    if (cbor_value_get_uint64(&cc, &v) != CborNoError) [[unlikely]] std::abort();
    return double(v);
}
#endif

#if defined(OP_TC_READ)
static double tc_sum(CborValue *v)
{
    tc_untag(v);
    double s = 0;
    switch (cbor_value_get_type(v)) {
    case CborTextStringType: {
        if (cbor_value_begin_string_iteration(v) != CborNoError) [[unlikely]] std::abort();
        while (!cbor_value_string_iteration_at_end(v)) {
            char const *p;
            std::size_t n;
            if (cbor_value_get_text_string_chunk(v, &p, &n, v) != CborNoError) [[unlikely]] std::abort();
            s += bytes_sum(std::string_view(p, n));
        }
        if (cbor_value_finish_string_iteration(v) != CborNoError) [[unlikely]] std::abort();
        return s;
    }
    case CborIntegerType: {
        std::int64_t i;
        if (cbor_value_get_int64(v, &i) != CborNoError) [[unlikely]] std::abort();
        s = double(i);
        break;
    }
    case CborBooleanType: {
        bool b;
        if (cbor_value_get_boolean(v, &b) != CborNoError) [[unlikely]] std::abort();
        s = b;
        break;
    }
    case CborHalfFloatType: {
        float f;
        if (cbor_value_get_half_float_as_float(v, &f) != CborNoError) [[unlikely]] std::abort();
        s = f;
        break;
    }
    case CborFloatType: {
        float f;
        if (cbor_value_get_float(v, &f) != CborNoError) [[unlikely]] std::abort();
        s = f;
        break;
    }
    case CborDoubleType: {
        double d;
        if (cbor_value_get_double(v, &d) != CborNoError) [[unlikely]] std::abort();
        s = d;
        break;
    }
    case CborArrayType:
    case CborMapType: {
        bool const map = cbor_value_get_type(v) == CborMapType;
        CborValue it;
        if (cbor_value_enter_container(v, &it) != CborNoError) [[unlikely]] std::abort();
        while (!cbor_value_at_end(&it)) {
            if (map && cbor_value_advance(&it) != CborNoError) [[unlikely]] std::abort();
            s += tc_sum(&it);
        }
        if (cbor_value_leave_container(v, &it) != CborNoError) [[unlikely]] std::abort();
        return s;
    }
    default: std::abort();
    }
    if (cbor_value_advance(v) != CborNoError) [[unlikely]] std::abort();
    return s;
}

static double tc_read()
{
    CborParser p;
    CborValue v = tc_root(&p);
    return tc_sum(&v);
}
#endif

#if defined(OP_JC_ITEM_FIELD) || defined(OP_JC_ITEM_READ) || defined(OP_JC_ITEM_ENCODE) || defined(OP_JC_PATH_FIELD) || defined(OP_JC_PATH_READ) || \
    defined(OP_JC_STRUCT_FIELD) || defined(OP_JC_STRUCT_READ) || defined(OP_JC_STRUCT_ENCODE)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wnull-dereference"
#if !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#pragma GCC diagnostic ignored "-Warray-bounds"
#include <jsoncons/json.hpp>
#include <jsoncons_ext/cbor/cbor.hpp>
#include <jsoncons_ext/jsonpath/jsonpath.hpp>
#pragma GCC diagnostic pop
#if defined(OP_JC_ITEM_FIELD) || defined(OP_JC_ITEM_READ) || defined(OP_JC_ITEM_ENCODE) || defined(OP_JC_PATH_FIELD) || \
	defined(OP_JC_PATH_READ)
static jsoncons::json jc_decode()
{
    return jsoncons::cbor::decode_cbor<jsoncons::json>(*plain);
}
#endif

#if defined(OP_JC_ITEM_READ) || defined(OP_JC_PATH_READ)
static double jc_sum(jsoncons::json const &j)
{
    if (j.is_string()) return bytes_sum(j.as_string_view());
    if (j.is_bool()) return j.as<bool>();
    if (j.is_uint64()) return double(j.as<std::uint64_t>());
    if (j.is_int64()) return double(j.as<std::int64_t>());
    if (j.is_double()) return j.as<double>();
    double s = 0;
    if (j.is_array()) {
        for (auto const &x : j.array_range()) s += jc_sum(x);
        return s;
    }
    if (j.is_object()) {
        for (auto const &x : j.object_range()) s += jc_sum(x.value());
        return s;
    }
    std::abort();
}
#endif

struct JcWheel { std::uint16_t diameter; float airPressure; bool snowTires; };
struct JcEngine { std::uint16_t horsepower; std::uint8_t cylinders; std::uint32_t cc; bool usesGas; bool usesElectric; };
struct JcCar {
    std::string make; std::string model; std::uint8_t color; std::uint8_t seats; std::uint8_t doors;
    std::vector<JcWheel> wheels; std::uint16_t length; std::uint16_t width; std::uint16_t height; std::uint32_t weight;
    JcEngine engine; float fuelCapacity; float fuelLevel; bool hasPowerWindows; bool hasPowerSteering;
    bool hasCruiseControl; std::uint8_t cupHolders; bool hasNavSystem;
};
struct JcLot { std::vector<JcCar> cars; };
JSONCONS_ALL_MEMBER_TRAITS(JcWheel, diameter, airPressure, snowTires)
JSONCONS_ALL_MEMBER_TRAITS(JcEngine, horsepower, cylinders, cc, usesGas, usesElectric)
JSONCONS_ALL_MEMBER_TRAITS(JcCar, make, model, color, seats, doors, wheels, length, width, height, weight, engine, fuelCapacity, fuelLevel,
                           hasPowerWindows, hasPowerSteering, hasCruiseControl, cupHolders, hasNavSystem)
JSONCONS_ALL_MEMBER_TRAITS(JcLot, cars)

#if defined(OP_JC_STRUCT_READ) || defined(OP_JC_ITEM_ENCODE) || defined(OP_JC_STRUCT_ENCODE)
static double sum(JcLot const &l)
{
    double s = 0;
    for (auto const &c : l.cars) {
        double w = 0;
        for (auto const &x : c.wheels) w += double(x.diameter) + double(x.airPressure) + double(x.snowTires);
        s += CAR_SUM(c, c.make, c.model, w, x, c.engine., .);
    }
    return s;
}
#endif

#if defined(OP_JC_STRUCT_ENCODE)
static JcLot jc_lot(T const &lot)
{
    JcLot l;
    for (auto const &c : lot.cars) {
        JcCar j{std::string(c.make), std::string(c.model), c.color, c.seats, c.doors, {}, c.length, c.width, c.height, c.weight,
                JcEngine{c.engine.horsepower, c.engine.cylinders, c.engine.cc, c.engine.usesGas, c.engine.usesElectric}, c.fuelCapacity,
                c.fuelLevel, c.hasPowerWindows, c.hasPowerSteering, c.hasCruiseControl, c.cupHolders, c.hasNavSystem};
        for (auto const &w : c.wheels) j.wheels.push_back(JcWheel{w.diameter, w.airPressure, w.snowTires});
        l.cars.push_back(std::move(j));
    }
    return l;
}
#endif
#endif

#if defined(OP_JC_PATH_FIELD)
static double jc_path_field()
{
    static auto const e = jsoncons::jsonpath::make_expression<jsoncons::json>("$.cars[500].engine.cc");
    auto const r = e.evaluate(jc_decode());
    if (r.size() != 1) [[unlikely]] std::abort();
    return double(r[0].as<std::uint32_t>());
}
#endif

#if defined(OP_JC_PATH_READ)
static double jc_path_read()
{
    using expression = jsoncons::jsonpath::jsonpath_expression<jsoncons::json>;
    static auto const paths = [] {
        std::vector<expression> p;
        for (char const *f : {"$.make", "$.model", "$.color", "$.seats", "$.doors", "$.length", "$.width", "$.height", "$.weight",
                              "$.engine.horsepower", "$.engine.cylinders", "$.engine.cc", "$.engine.usesGas", "$.engine.usesElectric",
                              "$.fuelCapacity", "$.fuelLevel", "$.hasPowerWindows", "$.hasPowerSteering", "$.hasCruiseControl",
                              "$.cupHolders", "$.hasNavSystem", "$.wheels[*].diameter", "$.wheels[*].airPressure", "$.wheels[*].snowTires"})
            p.push_back(jsoncons::jsonpath::make_expression<jsoncons::json>(f));
        return p;
    }();
    static auto const cars_path = jsoncons::jsonpath::make_expression<jsoncons::json>("$.cars[*]");
    auto const cars = cars_path.evaluate(jc_decode());
    double s = 0;
    for (auto const &car : cars.array_range())
        for (auto const &p : paths)
            for (auto const &v : p.evaluate(car).array_range()) s += jc_sum(v);
    return s;
}
#endif

#if defined(OP_JC_ITEM_ENCODE)
static jsoncons::json jc_item_source;
#endif
#if defined(OP_JC_STRUCT_ENCODE)
static JcLot jc_struct_source;
#endif
#endif

#if defined(DS_FLOATS)
#include "floats_generated.h"
#include "floats.capnp.h"
struct Floats { std::vector<double> values; };

template <>
struct cbor::describe_members<Floats> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1510};
    static constexpr std::tuple members{
        CBOR_MEMBER(Floats, values),
    };
};

using T = Floats;
using S = cbor::schema<T>;

static double scalar(cbor::lazy const &e)
{
    if (auto const d = e.get<double>()) return *d;
    return 0.0;
}

static T load()
{
    static auto const l = test::lazy_of_copy(file_read(DOC_PATH));
    T f;
    auto const els = l->elements();
    for (auto const &e : *els) {
        if (!e) [[unlikely]] std::abort();
        f.values.push_back(scalar(*e));
    }
    return f;
}

static std::size_t fb_encode(T const &f, bool const kept)
{
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(fbfloats::CreateFloats(fbb, fbb.CreateVector(f.values)));
    if (kept)
        fb_store(fbb);
    return fbb.GetSize();
}

static std::size_t cp_encode(T const &f, bool const kept)
{
    capnp::MallocMessageBuilder mb;
    auto v = mb.initRoot<cpfloats::Floats>().initValues(unsigned(f.values.size()));
    for (unsigned i = 0; i < f.values.size(); ++i) v.set(i, f.values[i]);
    if (kept)
        cp_store(mb);
    return mb.getSegmentsForOutput().size();
}

static void foreign_build(T const &f)
{
    fb_encode(f, true);
    cp_encode(f, true);
}

static double sum(T const &f) { return std::accumulate(f.values.begin(), f.values.end(), 0.0); }

static bool fb_verify()
{
    flatbuffers::Verifier v(fb_bytes(), fbmsg.size());
    return fbfloats::VerifyFloatsBuffer(v);
}
using FbT = fbfloats::FloatsT;
#if defined(OP_FB_DECODE)
static std::unique_ptr<FbT> fb_unpack() { return std::unique_ptr<FbT>(fbfloats::GetFloats(fbmsg.data())->UnPack()); }
#endif
#if defined(OP_FB_DECODE)
static double sum(FbT const &f) { return std::accumulate(f.values.begin(), f.values.end(), 0.0); }
#endif

#if defined(OP_CP_DECODE)
static T cp_copy()
{
    auto rd = cp_reader();
    auto const v = rd.getRoot<cpfloats::Floats>().getValues();
    T f;
    f.values.reserve(v.size());
    for (double const x : v) f.values.push_back(x);
    return f;
}
#endif

#if defined(OP_MB_READ)
static double mb_read()
{
    auto const opened = S::MB_PATH(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->at_path<"$.values">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < v->size(); ++i) {
        auto const x = v->at_path<"@[]">(i);
        if (!x) [[unlikely]] std::abort();
        s += *x;
    }
    return s;
}
#endif
#if defined(OP_FB_READ) || defined(OP_FB_READ_V)
static double fb_read()
{
    double s = 0;
    for (double const x : *fbfloats::GetFloats(fbmsg.data())->values()) s += x;
    return s;
}
#endif
#if defined(OP_CP_READ)
static double cp_read()
{
    auto rd = cp_reader();
    double s = 0;
    for (double const x : rd.getRoot<cpfloats::Floats>().getValues()) s += x;
    return s;
}
#endif
#if defined(OP_MB_FIELD)
static double mb_field()
{
    auto const x = S::template MB_AT_PATH<"$.values[30000]">(msg);
    if (!x) [[unlikely]] std::abort();
    return *x;
}
#endif
#if defined(OP_FB_FIELD) || defined(OP_FB_FIELD_V)
static double fb_field()
{
    auto const *v = fbfloats::GetFloats(fbmsg.data())->values();
    benchmark::DoNotOptimize(v);
    double x = v->Get(30000);
    benchmark::DoNotOptimize(x);
    if (std::bit_cast<std::uint64_t>(x) == 0) [[unlikely]] std::abort();
    return x;
}
#endif
#if defined(OP_MB_READ_PATH)
static double mb_read_path()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->at_path<"$.values">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < v->size(); ++i) {
        auto const x = opened->at_path<"$.values[]">(i);
        if (!x) [[unlikely]] std::abort();
        s += *x;
    }
    return s;
}
#endif
#if defined(OP_MB_READ_VIEW)
static double mb_read_view()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < v->size(); ++i)
        s += (*v)[i];
    return s;
}
#endif
#if defined(OP_MB_FIELD_VIEW)
static double mb_field_view()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    return (*v)[30000];
}
#endif
#if defined(OP_MB_FIELD_VIEW_MOVED)
static double mb_field_view_moved()
{
    auto opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = std::move(*opened).view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    return (*v)[30000];
}
#endif
#if defined(OP_MB_FRONT)
static double mb_front()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    return v->front();
}
#endif
#if defined(OP_MB_BACK)
static double mb_back()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    return v->back();
}
#endif
#if defined(OP_MB_READ_FOR)
static double mb_read_for()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (double const x : *v) s += x;
    return s;
}
#endif
#if defined(OP_MB_READ_FOLD)
static double mb_read_fold()
{
    auto const opened = S::path(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->view<"$.values">();
    if (!v) [[unlikely]] std::abort();
    return std::ranges::fold_left(*v, 0.0, std::plus<>{});
}
#endif

#if defined(OP_CP_FIELD)
static double cp_field()
{
    auto rd = cp_reader();
    return rd.getRoot<cpfloats::Floats>().getValues()[30000];
}
#endif
#endif

#if defined(DS_RECORDS)
#include "records_generated.h"
#include "records.capnp.h"
struct User { std::string_view name; std::uint64_t followers; };

template <>
struct cbor::describe_members<User> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1521};
    static constexpr std::tuple members{
        CBOR_MEMBER(User, name),
        CBOR_MEMBER(User, followers),
    };
};

struct Record { std::int64_t id; std::string_view text; User user; double ratio; std::vector<std::string_view> tags; std::int64_t neg; };

template <>
struct cbor::describe_members<Record> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1520};
    static constexpr std::tuple members{
        CBOR_MEMBER(Record, id),
        CBOR_MEMBER(Record, text),
        CBOR_MEMBER(Record, user),
        CBOR_MEMBER(Record, ratio),
        CBOR_MEMBER(Record, tags),
        CBOR_MEMBER(Record, neg),
    };
};

struct Records { std::vector<Record> records; };

template <>
struct cbor::describe_members<Records> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1522};
    static constexpr std::tuple members{
        CBOR_MEMBER(Records, records),
    };
};

using T = Records;
using S = cbor::schema<T>;

template <class V> static V must(std::expected<V, cbor::error> const &r)
{
    if (!r) [[unlikely]] std::abort();
    return *r;
}
static std::string_view must_text(cbor::lazy const &l)
{
    auto const t = l.get<std::string_view>();
    if (!t) [[unlikely]] std::abort();
    return **t;
}

static T load()
{
    static auto const l = test::lazy_of_copy(file_read(DOC_PATH));
    T rs;
    auto const els = l->elements();
    for (auto const &e : *els) {
        if (!e) [[unlikely]] std::abort();
        cbor::lazy const r = must(e);
        cbor::lazy const u = must(r.at("user"));
        auto const ratio = must(r.at("ratio")).get<double>();
        Record x{must(must(r.at("id")).get<std::int64_t>()), must_text(must(r.at("text"))), User{must_text(must(u.at("name"))), must(must(u.at("followers")).get<std::uint64_t>())},
                 ratio ? *ratio : 0.0, {}, must(must(r.at("neg")).get<std::int64_t>())};
        auto const tags = must(must(r.at("tags")).elements());
        for (auto const &t : tags) x.tags.push_back(must_text(must(t)));
        rs.records.push_back(std::move(x));
    }
    return rs;
}

static std::size_t fb_encode(T const &rs, bool const kept)
{
    flatbuffers::FlatBufferBuilder fbb;
    std::vector<flatbuffers::Offset<fbrecords::Record>> os;
    for (auto const &r : rs.records) {
        std::vector<flatbuffers::Offset<flatbuffers::String>> ts;
        for (auto const t : r.tags) ts.push_back(fbb.CreateString(t.data(), t.size()));
        auto const text = fbb.CreateString(r.text.data(), r.text.size());
        auto const user = fbrecords::CreateUser(fbb, fbb.CreateString(r.user.name.data(), r.user.name.size()), r.user.followers);
        os.push_back(fbrecords::CreateRecord(fbb, r.id, text, user, r.ratio, fbb.CreateVector(ts), r.neg));
    }
    fbb.Finish(fbrecords::CreateRecords(fbb, fbb.CreateVector(os)));
    if (kept)
        fb_store(fbb);
    return fbb.GetSize();
}

static std::size_t cp_encode(T const &rs, bool const kept)
{
    capnp::MallocMessageBuilder mb;
    auto bs = mb.initRoot<cprecords::Records>().initRecords(unsigned(rs.records.size()));
    for (unsigned k = 0; k < rs.records.size(); ++k) {
        auto const &r = rs.records[k];
        auto b = bs[k];
        b.setId(r.id);
        b.setText(capnp::Text::Reader(r.text.data(), r.text.size()));
        auto u = b.initUser();
        u.setName(capnp::Text::Reader(r.user.name.data(), r.user.name.size()));
        u.setFollowers(r.user.followers);
        b.setRatio(r.ratio);
        auto ts = b.initTags(unsigned(r.tags.size()));
        for (unsigned i = 0; i < r.tags.size(); ++i) ts.set(i, capnp::Text::Reader(r.tags[i].data(), r.tags[i].size()));
        b.setNeg(r.neg);
    }
    if (kept)
        cp_store(mb);
    return mb.getSegmentsForOutput().size();
}

static void foreign_build(T const &rs)
{
    fb_encode(rs, true);
    cp_encode(rs, true);
}

static double sum(T const &rs)
{
    double s = 0;
    for (auto const &r : rs.records) {
        s += double(r.id) + bytes_sum(r.text) + bytes_sum(r.user.name) + double(r.user.followers) + r.ratio + double(r.neg);
        for (auto const t : r.tags) s += bytes_sum(t);
    }
    return s;
}

static bool fb_verify()
{
    flatbuffers::Verifier v(fb_bytes(), fbmsg.size());
    return fbrecords::VerifyRecordsBuffer(v);
}
using FbT = fbrecords::RecordsT;
#if defined(OP_FB_DECODE)
static std::unique_ptr<FbT> fb_unpack() { return std::unique_ptr<FbT>(fbrecords::GetRecords(fbmsg.data())->UnPack()); }
#endif
#if defined(OP_FB_DECODE)
static double sum(FbT const &rs)
{
    double s = 0;
    for (auto const &r : rs.records) {
        s += double(r->id) + bytes_sum(r->text) + bytes_sum(r->user->name) + double(r->user->followers) + r->ratio + double(r->neg);
        for (auto const &t : r->tags) s += bytes_sum(t);
    }
    return s;
}
#endif

#if defined(OP_CP_DECODE) || defined(OP_CP_READ) || defined(OP_CP_FIELD)
static std::string_view view(capnp::Text::Reader const t) { return {t.begin(), t.size()}; }
#endif

#if defined(OP_CP_DECODE)
static T cp_copy()
{
    auto rd = cp_reader();
    auto const v = rd.getRoot<cprecords::Records>().getRecords();
    T rs;
    rs.records.reserve(v.size());
    for (auto r : v) {
        auto const u = r.getUser();
        auto const ts = r.getTags();
        std::vector<std::string_view> tags;
        tags.reserve(ts.size());
        for (auto t : ts) tags.push_back(view(t));
        rs.records.push_back(Record{r.getId(), view(r.getText()), User{view(u.getName()), u.getFollowers()}, r.getRatio(), std::move(tags), r.getNeg()});
    }
    return rs;
}
#endif

template <class V> static double num(V const &v)
{
    if (!v) [[unlikely]] std::abort();
    return double(*v);
}
template <class V> static double text(V const &v)
{
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(*v);
}

#if defined(OP_MB_READ)
static double mb_read()
{
    auto const opened = S::MB_PATH(held);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->at_path<"$.records">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < v->size(); ++i) {
        auto const r = v->at_path<"@[]">(i);
        if (!r) [[unlikely]] std::abort();
        s += num(r->at_path<"@.id">()) + text(r->at_path<"@.text">()) + text(r->at_path<"@.user.name">()) + num(r->at_path<"@.user.followers">()) + num(r->at_path<"@.ratio">()) + num(r->at_path<"@.neg">());
        auto const tags = r->at_path<"@.tags">();
        if (!tags) [[unlikely]] std::abort();
        for (std::size_t k = 0; k < tags->size(); ++k) s += text(tags->at_path<"@[]">(k));
    }
    return s;
}
#endif
#if defined(OP_FB_READ) || defined(OP_FB_READ_V)
static double fb_read()
{
    double s = 0;
    for (auto const *r : *fbrecords::GetRecords(fbmsg.data())->records()) {
        s += double(r->id()) + bytes_sum(r->text()->string_view()) + bytes_sum(r->user()->name()->string_view()) + double(r->user()->followers()) + r->ratio() + double(r->neg());
        for (auto const *t : *r->tags()) s += bytes_sum(t->string_view());
    }
    return s;
}
#endif
#if defined(OP_CP_READ)
static double cp_read()
{
    auto rd = cp_reader();
    double s = 0;
    for (auto r : rd.getRoot<cprecords::Records>().getRecords()) {
        auto const u = r.getUser();
        s += double(r.getId()) + bytes_sum(view(r.getText())) + bytes_sum(view(u.getName())) + double(u.getFollowers()) + r.getRatio() + double(r.getNeg());
        for (auto t : r.getTags()) s += bytes_sum(view(t));
    }
    return s;
}
#endif
#if defined(OP_MB_FIELD)
static double mb_field()
{
    auto const opened = S::MB_PATH(held);
    if (!opened) [[unlikely]] std::abort();
    return text(opened->at_path<"$.records[1000].user.name">());
}
#endif
#if defined(OP_FB_FIELD) || defined(OP_FB_FIELD_V)
static double fb_field() { return bytes_sum(fbrecords::GetRecords(fbmsg.data())->records()->Get(1000)->user()->name()->string_view()); }
#endif
#if defined(OP_CP_FIELD)
static double cp_field()
{
    auto rd = cp_reader();
    return bytes_sum(view(rd.getRoot<cprecords::Records>().getRecords()[1000].getUser().getName()));
}
#endif
#endif

#if defined(OP_MB_ENCODE) || defined(OP_FB_ENCODE) || defined(OP_CP_ENCODE) || defined(OP_STRUCTS_ENCODE)
static T const *encoded_source;
#endif
static double op()
{
#if defined(OP_MB_ENCODE)
    auto const e = S::encode(*encoded_source);
    if (!e)
        std::abort();
    benchmark::DoNotOptimize(e->data());
    return double(e->size());
#elif defined(OP_FB_ENCODE)
    return double(fb_encode(*encoded_source, false));
#elif defined(OP_CP_ENCODE)
    return double(cp_encode(*encoded_source, false));
#elif defined(OP_MB_DECODE)
    auto const l = S::decode(held);
    if (!l) [[unlikely]] std::abort();
    benchmark::DoNotOptimize(&**l);
    return double((**l).OP_FIRST.size());
#elif defined(OP_FB_DECODE)
    if (!fb_verify()) [[unlikely]] std::abort();
    auto const u = fb_unpack();
    benchmark::DoNotOptimize(u.get());
    return double(u->OP_FIRST.size());
#elif defined(OP_CP_DECODE)
    auto const t = cp_copy();
    benchmark::DoNotOptimize(&t);
    return double(t.OP_FIRST.size());
#elif defined(OP_MB_FIELD)
    return mb_field();
#elif defined(OP_FB_FIELD)
    return fb_field();
#elif defined(OP_FB_FIELD_V)
    if (!fb_verify()) [[unlikely]] std::abort();
    return fb_field();
#elif defined(OP_CP_FIELD)
    return cp_field();
#elif defined(OP_MB_READ)
    return mb_read();
#elif defined(OP_FB_READ)
    return fb_read();
#elif defined(OP_FB_READ_V)
    if (!fb_verify()) [[unlikely]] std::abort();
    return fb_read();
#elif defined(OP_CP_READ)
    return cp_read();
#elif defined(OP_MB_READ_PATH)
    return mb_read_path();
#elif defined(OP_MB_READ_VIEW)
    return mb_read_view();
#elif defined(OP_MB_FIELD_VIEW)
    return mb_field_view();
#elif defined(OP_MB_FIELD_VIEW_MOVED)
    return mb_field_view_moved();
#elif defined(OP_MB_FRONT)
    return mb_front();
#elif defined(OP_MB_BACK)
    return mb_back();
#elif defined(OP_MB_READ_FOR)
    return mb_read_for();
#elif defined(OP_MB_READ_FOLD)
    return mb_read_fold();
#elif defined(OP_LAZY_FIELD)
    return lazy_field();
#elif defined(OP_LAZY_READ)
    return lazy_read();
#elif defined(OP_PATH_FIELD)
    return path_field();
#elif defined(OP_PATH_READ)
    return path_read();
#elif defined(OP_ITEM_FIELD)
    return item_field();
#elif defined(OP_ITEM_READ)
    return item_read();
#elif defined(OP_ITEM_ENCODE)
    std::string out;
    auto const n = cbor::encode(**item_source, out);
    if (!n) [[unlikely]] std::abort();
    benchmark::DoNotOptimize(out.data());
    return double(out.size());
#elif defined(OP_STRUCTS_FIELD)
    auto const l = cbor::structs<T>::decode(plain);
    if (!l) [[unlikely]] std::abort();
    return double((**l).cars[500].engine.cc);
#elif defined(OP_STRUCTS_READ)
    auto const l = cbor::structs<T>::decode(plain);
    if (!l) [[unlikely]] std::abort();
    return sum(**l);
#elif defined(OP_STRUCTS_ENCODE)
    auto const e = cbor::structs<T>::encode(*encoded_source);
    if (!e) [[unlikely]] std::abort();
    benchmark::DoNotOptimize(e->data());
    return double(e->size());
#elif defined(OP_TC_FIELD)
    return tc_field();
#elif defined(OP_TC_READ)
    return tc_read();
#elif defined(OP_JC_PATH_FIELD)
    return jc_path_field();
#elif defined(OP_JC_PATH_READ)
    return jc_path_read();
#elif defined(OP_JC_ITEM_FIELD)
    return double(jc_decode()["cars"][500]["engine"]["cc"].as<std::uint32_t>());
#elif defined(OP_JC_ITEM_READ)
    return jc_sum(jc_decode());
#elif defined(OP_JC_ITEM_ENCODE)
    std::vector<std::uint8_t> out;
    jsoncons::cbor::encode_cbor(jc_item_source, out);
    benchmark::DoNotOptimize(out.data());
    return double(out.size());
#elif defined(OP_JC_STRUCT_FIELD)
    return double(jsoncons::cbor::decode_cbor<JcLot>(*plain).cars[500].engine.cc);
#elif defined(OP_JC_STRUCT_READ)
    return sum(jsoncons::cbor::decode_cbor<JcLot>(*plain));
#elif defined(OP_JC_STRUCT_ENCODE)
    std::vector<std::uint8_t> out;
    jsoncons::cbor::encode_cbor(jc_struct_source, out);
    benchmark::DoNotOptimize(out.data());
    return double(out.size());
#else
#error no op
#endif
}

static double check()
{
#if defined(OP_MB_ENCODE) && defined(DS_CARS)
    auto const e = S::encode(*encoded_source);
    if (!e) std::abort();
    written_bytes = e->size();
    auto const l = S::decode(std::make_shared<std::string const>(*e));
    if (!l) std::abort();
    return sum(**l);
#elif defined(OP_FB_ENCODE) && defined(DS_CARS)
    written_bytes = fb_encode(*encoded_source, true);
    if (!fb_verify()) std::abort();
    return fb_read();
#elif defined(OP_STRUCTS_ENCODE) || defined(OP_ITEM_ENCODE)
    std::string out;
#if defined(OP_STRUCTS_ENCODE)
    out = *cbor::structs<T>::encode(*encoded_source);
#else
    if (!cbor::encode(**item_source, out)) std::abort();
#endif
    written_bytes = out.size();
    auto const l = cbor::structs<T>::decode(std::make_shared<std::string const>(std::move(out)));
    if (!l) std::abort();
    return sum(**l);
#elif defined(OP_JC_ITEM_ENCODE) || defined(OP_JC_STRUCT_ENCODE)
    std::vector<std::uint8_t> out;
#if defined(OP_JC_ITEM_ENCODE)
    jsoncons::cbor::encode_cbor(jc_item_source, out);
#else
    jsoncons::cbor::encode_cbor(jc_struct_source, out);
#endif
    written_bytes = out.size();
    return sum(jsoncons::cbor::decode_cbor<JcLot>(out));
#elif defined(OP_MB_DECODE)
    auto const l = S::decode(held);
    return sum(**l);
#elif defined(OP_FB_DECODE)
    return sum(*fb_unpack());
#elif defined(OP_CP_DECODE)
    return sum(cp_copy());
#else
    return op();
#endif
}

static void run(benchmark::State &state)
{
    for (auto _ : state) {
        benchmark::DoNotOptimize(op());
        benchmark::ClobberMemory();
    }
}

int main(int argc, char **argv)
{
    static T const data = load();
#if defined(OP_MB_ENCODE) || defined(OP_FB_ENCODE) || defined(OP_CP_ENCODE) || defined(OP_STRUCTS_ENCODE)
    encoded_source = &data;
#endif
    if (!S::encode(data, msg)) std::abort();
    held = std::make_shared<std::string const>(msg);
#if defined(DS_CARS)
    plain = std::make_shared<std::string const>(*cbor::structs<T>::encode(data));
    benchmark::AddCustomContext("bytes_plain", std::to_string(plain->size()));
    benchmark::AddCustomContext("check_field", std::to_string(data.cars[500].engine.cc));
#endif
#if defined(OP_ITEM_ENCODE)
    item_source.emplace(*cbor::decode(*plain));
#endif
#if defined(OP_JC_ITEM_ENCODE)
    jc_item_source = jc_decode();
#endif
#if defined(OP_JC_STRUCT_ENCODE)
    jc_struct_source = jc_lot(data);
#endif
    foreign_build(data);
    if (!fb_verify()) std::abort();
    char text[64];
    std::snprintf(text, sizeof text, "%.17g", check());
    benchmark::AddCustomContext("check", text);
    benchmark::AddCustomContext("bytes_written", std::to_string(written_bytes));
    benchmark::AddCustomContext("check_source", [] { char t[64]; std::snprintf(t, sizeof t, "%.17g", sum(data)); return std::string(t); }());
    benchmark::AddCustomContext("bytes_mb", std::to_string(msg.size()));
    benchmark::AddCustomContext("bytes_fb", std::to_string(fbmsg.size()));
    benchmark::AddCustomContext("bytes_cp", std::to_string(cpmsg.size() * 8));
    benchmark::RegisterBenchmark("op", run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
