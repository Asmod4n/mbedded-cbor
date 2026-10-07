#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>
#include <capnp/message.h>
#include <capnp/serialize.h>
#include <flatbuffers/flatbuffers.h>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

static std::shared_ptr<void const> const keep = std::make_shared<int const>(0);
static std::string msg, fbmsg;
static std::vector<capnp::word> cpmsg;

static double bytes_sum(std::string_view const s)
{
    return double(std::transform_reduce(s.begin(), s.end(), std::uint64_t{0}, std::plus<>{},
                                        [](char const c) { return std::uint64_t(std::uint8_t(c)); }));
}

static std::string file_read(char const *path)
{
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

static capnp::FlatArrayMessageReader cp_reader()
{
    capnp::ReaderOptions opt;
    opt.traversalLimitInWords = std::uint64_t(1) << 40;
    return capnp::FlatArrayMessageReader(kj::arrayPtr(cpmsg.data(), cpmsg.size()), opt);
}

static void cp_store(capnp::MallocMessageBuilder &mb)
{
    auto const flat = capnp::messageToFlatArray(mb);
    cpmsg.assign(flat.begin(), flat.end());
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
#include "carsales_generated.h"
#include "carsales.capnp.h"
struct [[=cbor::tag(1501)]] Wheel { std::uint16_t diameter; float airPressure; bool snowTires; };
struct [[=cbor::tag(1502)]] Engine { std::uint16_t horsepower; std::uint8_t cylinders; std::uint32_t cc; bool usesGas; bool usesElectric; };
struct [[=cbor::tag(1500)]] Car {
    std::string_view make; std::string_view model; std::uint8_t color; std::uint8_t seats; std::uint8_t doors;
    std::vector<Wheel> wheels; std::uint16_t length; std::uint16_t width; std::uint16_t height; std::uint32_t weight;
    Engine engine; float fuelCapacity; float fuelLevel; bool hasPowerWindows; bool hasPowerSteering;
    bool hasCruiseControl; std::uint8_t cupHolders; bool hasNavSystem;
};
struct [[=cbor::tag(1503)]] ParkingLot { std::vector<Car> cars; };
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

static void foreign_build(T const &lot)
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
    fb_store(fbb);
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
    cp_store(mb);
}

static bool fb_verify()
{
    flatbuffers::Verifier v(fb_bytes(), fbmsg.size());
    return fbcar::VerifyParkingLotBuffer(v);
}

using FbT = fbcar::ParkingLotT;
static std::unique_ptr<FbT> fb_unpack() { return std::unique_ptr<FbT>(fbcar::GetParkingLot(fbmsg.data())->UnPack()); }
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

static double mb_read()
{
    auto const opened = S::path(keep, msg);
    if (!opened) [[unlikely]] std::abort();
    auto const cars = opened->at<"$.cars">();
    if (!cars) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < cars->size(); ++i) {
        auto const car = cars->at<"@[]">(i);
        if (!car) [[unlikely]] std::abort();
        auto const wheels = car->at<"@.wheels">();
        if (!wheels) [[unlikely]] std::abort();
        double w = 0;
        for (std::size_t k = 0; k < wheels->size(); ++k) {
            auto const x = wheels->at<"@[]">(k);
            if (!x) [[unlikely]] std::abort();
            w += num(x->at<"@.diameter">()) + num(x->at<"@.airPressure">()) + num(x->at<"@.snowTires">());
        }
        s += text(car->at<"@.make">()) + text(car->at<"@.model">()) + num(car->at<"@.color">()) + num(car->at<"@.seats">()) + num(car->at<"@.doors">()) + w +
             num(car->at<"@.length">()) + num(car->at<"@.width">()) + num(car->at<"@.height">()) + num(car->at<"@.weight">()) +
             num(car->at<"@.engine.horsepower">()) + num(car->at<"@.engine.cylinders">()) + num(car->at<"@.engine.cc">()) +
             num(car->at<"@.engine.usesGas">()) + num(car->at<"@.engine.usesElectric">()) + num(car->at<"@.fuelCapacity">()) +
             num(car->at<"@.fuelLevel">()) + num(car->at<"@.hasPowerWindows">()) + num(car->at<"@.hasPowerSteering">()) +
             num(car->at<"@.hasCruiseControl">()) + num(car->at<"@.cupHolders">()) + num(car->at<"@.hasNavSystem">());
    }
    return s;
}

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

static double mb_field()
{
    auto const opened = S::path(keep, msg);
    if (!opened) [[unlikely]] std::abort();
    return num(opened->at<"$.cars[500].engine.cc">());
}
static double fb_field() { return fbcar::GetParkingLot(fbmsg.data())->cars()->Get(500)->engine()->cc(); }
static double cp_field()
{
    auto rd = cp_reader();
    return rd.getRoot<carsales::ParkingLot>().getCars()[500].getEngine().getCc();
}
#endif

#if defined(DS_FLOATS)
#include "floats_generated.h"
#include "floats.capnp.h"
struct [[=cbor::tag(1510)]] Floats { std::vector<double> values; };
using T = Floats;
using S = cbor::schema<T>;

static double scalar(cbor::lazy const &e)
{
    if (auto const d = e.get<double>()) return *d;
    return 0.0;
}

static T load()
{
    auto const l = cbor::lazy::from(keep, std::string_view(*new std::string(file_read(DOC_PATH))));
    T f;
    auto const els = l->elements();
    for (auto const &e : *els) {
        if (!e) [[unlikely]] std::abort();
        f.values.push_back(scalar(*e));
    }
    return f;
}

static void foreign_build(T const &f)
{
    flatbuffers::FlatBufferBuilder fbb;
    fbb.Finish(fbfloats::CreateFloats(fbb, fbb.CreateVector(f.values)));
    fb_store(fbb);
    capnp::MallocMessageBuilder mb;
    auto v = mb.initRoot<cpfloats::Floats>().initValues(unsigned(f.values.size()));
    for (unsigned i = 0; i < f.values.size(); ++i) v.set(i, f.values[i]);
    cp_store(mb);
}

static double sum(T const &f) { return std::accumulate(f.values.begin(), f.values.end(), 0.0); }

static bool fb_verify()
{
    flatbuffers::Verifier v(fb_bytes(), fbmsg.size());
    return fbfloats::VerifyFloatsBuffer(v);
}
using FbT = fbfloats::FloatsT;
static std::unique_ptr<FbT> fb_unpack() { return std::unique_ptr<FbT>(fbfloats::GetFloats(fbmsg.data())->UnPack()); }
static double sum(FbT const &f) { return std::accumulate(f.values.begin(), f.values.end(), 0.0); }

static T cp_copy()
{
    auto rd = cp_reader();
    auto const v = rd.getRoot<cpfloats::Floats>().getValues();
    T f;
    f.values.reserve(v.size());
    for (double const x : v) f.values.push_back(x);
    return f;
}

static double mb_read()
{
    auto const opened = S::path(keep, msg);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->at<"$.values">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < v->size(); ++i) {
        auto const x = v->at<"@[]">(i);
        if (!x) [[unlikely]] std::abort();
        s += *x;
    }
    return s;
}
static double fb_read()
{
    double s = 0;
    for (double const x : *fbfloats::GetFloats(fbmsg.data())->values()) s += x;
    return s;
}
static double cp_read()
{
    auto rd = cp_reader();
    double s = 0;
    for (double const x : rd.getRoot<cpfloats::Floats>().getValues()) s += x;
    return s;
}
static double mb_field()
{
    auto const opened = S::path(keep, msg);
    if (!opened) [[unlikely]] std::abort();
    auto const x = opened->at<"$.values[30000]">();
    if (!x) [[unlikely]] std::abort();
    return *x;
}
static double fb_field() { return fbfloats::GetFloats(fbmsg.data())->values()->Get(30000); }
static double cp_field()
{
    auto rd = cp_reader();
    return rd.getRoot<cpfloats::Floats>().getValues()[30000];
}
#endif

#if defined(DS_RECORDS)
#include "records_generated.h"
#include "records.capnp.h"
struct [[=cbor::tag(1521)]] User { std::string_view name; std::uint64_t followers; };
struct [[=cbor::tag(1520)]] Record { std::int64_t id; std::string_view text; User user; double ratio; std::vector<std::string_view> tags; std::int64_t neg; };
struct [[=cbor::tag(1522)]] Records { std::vector<Record> records; };
using T = Records;
using S = cbor::schema<T>;

template <class V> static V must(cbor::result<V> const &r)
{
    if (!r) [[unlikely]] std::abort();
    return *r;
}
static std::string_view must_text(cbor::result<cbor::lazy> const &l)
{
    auto const t = l.get<std::string_view>();
    if (!t) [[unlikely]] std::abort();
    return **t;
}

static T load()
{
    auto const l = cbor::lazy::from(keep, std::string_view(*new std::string(file_read(DOC_PATH))));
    T rs;
    auto const els = l->elements();
    for (auto const &e : *els) {
        if (!e) [[unlikely]] std::abort();
        cbor::result<cbor::lazy> const r = *e;
        auto const ratio = r.at("ratio").get<double>();
        Record x{must(r.at("id").get<std::int64_t>()), must_text(r.at("text")), User{must_text(r.at("user").at("name")), must(r.at("user").at("followers").get<std::uint64_t>())},
                 ratio ? *ratio : 0.0, {}, must(r.at("neg").get<std::int64_t>())};
        auto const tags = r.at("tags").elements();
        for (auto const &t : *tags) x.tags.push_back(must_text(*t));
        rs.records.push_back(std::move(x));
    }
    return rs;
}

static void foreign_build(T const &rs)
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
    fb_store(fbb);
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
    cp_store(mb);
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
static std::unique_ptr<FbT> fb_unpack() { return std::unique_ptr<FbT>(fbrecords::GetRecords(fbmsg.data())->UnPack()); }
static double sum(FbT const &rs)
{
    double s = 0;
    for (auto const &r : rs.records) {
        s += double(r->id) + bytes_sum(r->text) + bytes_sum(r->user->name) + double(r->user->followers) + r->ratio + double(r->neg);
        for (auto const &t : r->tags) s += bytes_sum(t);
    }
    return s;
}

static std::string_view view(capnp::Text::Reader const t) { return {t.begin(), t.size()}; }

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

static double mb_read()
{
    auto const opened = S::path(keep, msg);
    if (!opened) [[unlikely]] std::abort();
    auto const v = opened->at<"$.records">();
    if (!v) [[unlikely]] std::abort();
    double s = 0;
    for (std::size_t i = 0; i < v->size(); ++i) {
        auto const r = v->at<"@[]">(i);
        if (!r) [[unlikely]] std::abort();
        s += num(r->at<"@.id">()) + text(r->at<"@.text">()) + text(r->at<"@.user.name">()) + num(r->at<"@.user.followers">()) + num(r->at<"@.ratio">()) + num(r->at<"@.neg">());
        auto const tags = r->at<"@.tags">();
        if (!tags) [[unlikely]] std::abort();
        for (std::size_t k = 0; k < tags->size(); ++k) s += text(tags->at<"@[]">(k));
    }
    return s;
}
static double fb_read()
{
    double s = 0;
    for (auto const *r : *fbrecords::GetRecords(fbmsg.data())->records()) {
        s += double(r->id()) + bytes_sum(r->text()->string_view()) + bytes_sum(r->user()->name()->string_view()) + double(r->user()->followers()) + r->ratio() + double(r->neg());
        for (auto const *t : *r->tags()) s += bytes_sum(t->string_view());
    }
    return s;
}
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
static double mb_field()
{
    auto const opened = S::path(keep, msg);
    if (!opened) [[unlikely]] std::abort();
    return text(opened->at<"$.records[1000].user.name">());
}
static double fb_field() { return bytes_sum(fbrecords::GetRecords(fbmsg.data())->records()->Get(1000)->user()->name()->string_view()); }
static double cp_field()
{
    auto rd = cp_reader();
    return bytes_sum(view(rd.getRoot<cprecords::Records>().getRecords()[1000].getUser().getName()));
}
#endif

static double op()
{
#if defined(OP_MB_DECODE)
    auto const l = S::decode(keep, msg);
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
#else
#error no op
#endif
}

static double check()
{
#if defined(OP_MB_DECODE)
    auto const l = S::decode(keep, msg);
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
    if (!S::encode(data, msg)) std::abort();
    foreign_build(data);
    if (!fb_verify()) std::abort();
    char text[64];
    std::snprintf(text, sizeof text, "%.17g", check());
    benchmark::AddCustomContext("check", text);
    benchmark::AddCustomContext("check_source", [] { char t[64]; std::snprintf(t, sizeof t, "%.17g", sum(data)); return std::string(t); }());
    benchmark::AddCustomContext("bytes_mb", std::to_string(msg.size()));
    benchmark::AddCustomContext("bytes_fb", std::to_string(fbmsg.size()));
    benchmark::AddCustomContext("bytes_cp", std::to_string(cpmsg.size() * 8));
    benchmark::RegisterBenchmark("op", run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
}
