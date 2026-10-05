#include <benchmark/benchmark.h>
#include <cbor/cbor.hpp>
#include <nlohmann/json.hpp>
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
#include "carsales_generated.h"
#include "carsales.capnp.h"
#include <capnp/message.h>
#include <capnp/serialize.h>
struct [[=cbor::tag(1501)]] Wheel { std::uint16_t diameter; float airPressure; bool snowTires; };
struct [[=cbor::tag(1502)]] Engine { std::uint16_t horsepower; std::uint8_t cylinders; std::uint32_t cc; bool usesGas; bool usesElectric; };
struct [[=cbor::tag(1500)]] Car {
  std::string_view make; std::string_view model; std::uint8_t color; std::uint8_t seats; std::uint8_t doors;
  std::vector<Wheel> wheels; std::uint16_t length; std::uint16_t width; std::uint16_t height; std::uint32_t weight;
  Engine engine; float fuelCapacity; float fuelLevel; bool hasPowerWindows; bool hasPowerSteering;
  bool hasCruiseControl; std::uint8_t cupHolders; bool hasNavSystem;
};

struct [[=cbor::tag(1503)]] ParkingLot { std::vector<Car> cars; };

using S = cbor::schema<ParkingLot>;
#if defined(OLDPATH)
template <class V> static double num(V const &v) {
  if (!v) [[unlikely]] std::abort();
  return double(*v);
}

template <class V> static double text_size(V const &v) {
  if (!v) [[unlikely]] std::abort();
  return double(v->size());
}

static double lot_read(std::shared_ptr<void const> const &keep, std::string_view msg) {
  auto const opened = S::view(keep, msg);
  if (!opened) [[unlikely]] std::abort();
  auto const &lot = *opened;
  auto const cars = S::at_path<".cars">(lot);
  if (!cars) [[unlikely]] std::abort();
  double s = 0;
  for (std::size_t i = 0;; ++i) {
    auto const car = cars->at(i);
    if (!car) {
      if (car.error() != cbor::error::index_out_of_bounds) [[unlikely]] std::abort();
      break;
    }
    s += text_size(S::at_path<".make">(*car)) + text_size(S::at_path<".model">(*car));
    s += num(S::at_path<".color">(*car)) + num(S::at_path<".seats">(*car)) + num(S::at_path<".doors">(*car));
    auto const wheels = S::at_path<".wheels">(*car);
    if (!wheels) [[unlikely]] std::abort();
    for (std::size_t k = 0;; ++k) {
      auto const w = wheels->at(k);
      if (!w) {
        if (w.error() != cbor::error::index_out_of_bounds) [[unlikely]] std::abort();
        break;
      }
      s += num(S::at_path<".diameter">(*w)) + num(S::at_path<".airPressure">(*w)) + num(S::at_path<".snowTires">(*w));
    }
    s += num(S::at_path<".length">(*car)) + num(S::at_path<".width">(*car)) + num(S::at_path<".height">(*car)) + num(S::at_path<".weight">(*car));
    s += num(S::at_path<".engine.horsepower">(*car)) + num(S::at_path<".engine.cylinders">(*car)) + num(S::at_path<".engine.cc">(*car)) +
         num(S::at_path<".engine.usesGas">(*car)) + num(S::at_path<".engine.usesElectric">(*car));
    s += num(S::at_path<".fuelCapacity">(*car)) + num(S::at_path<".fuelLevel">(*car)) + num(S::at_path<".hasPowerWindows">(*car)) +
         num(S::at_path<".hasPowerSteering">(*car)) + num(S::at_path<".hasCruiseControl">(*car)) + num(S::at_path<".cupHolders">(*car)) +
         num(S::at_path<".hasNavSystem">(*car));
  }
  return s;
}
#else
template <class V> static double num(V const &v) {
  if (!v) [[unlikely]] std::abort();
  return double(*v);
}

template <class V> static double text_size(V const &v) {
  if (!v) [[unlikely]] std::abort();
  return double(v->size());
}

static double lot_read(std::shared_ptr<void const> const &keep, std::string_view msg) {
  auto const opened = S::path(keep, msg);
  if (!opened) [[unlikely]] std::abort();
  auto const &lot = *opened;
  auto const cars = lot.at<"$.cars">();
  if (!cars) [[unlikely]] std::abort();
  double s = 0;
  for (std::size_t i = 0;; ++i) {
    auto const car = cars->at<"@[]">(i);
    if (!car) {
      if (car.error() != cbor::error::index_out_of_bounds) [[unlikely]] std::abort();
      break;
    }
    s += text_size(car->at<"@.make">()) + text_size(car->at<"@.model">());
    s += num(car->at<"@.color">()) + num(car->at<"@.seats">()) + num(car->at<"@.doors">());
    auto const wheels = car->at<"@.wheels">();
    if (!wheels) [[unlikely]] std::abort();
    for (std::size_t k = 0;; ++k) {
      auto const w = wheels->at<"@[]">(k);
      if (!w) {
        if (w.error() != cbor::error::index_out_of_bounds) [[unlikely]] std::abort();
        break;
      }
      s += num(w->at<"@.diameter">()) + num(w->at<"@.airPressure">()) + num(w->at<"@.snowTires">());
    }
    s += num(car->at<"@.length">()) + num(car->at<"@.width">()) + num(car->at<"@.height">()) + num(car->at<"@.weight">());
    s += num(car->at<"@.engine.horsepower">()) + num(car->at<"@.engine.cylinders">()) + num(car->at<"@.engine.cc">()) +
         num(car->at<"@.engine.usesGas">()) + num(car->at<"@.engine.usesElectric">());
    s += num(car->at<"@.fuelCapacity">()) + num(car->at<"@.fuelLevel">()) + num(car->at<"@.hasPowerWindows">()) +
         num(car->at<"@.hasPowerSteering">()) + num(car->at<"@.hasCruiseControl">()) + num(car->at<"@.cupHolders">()) +
         num(car->at<"@.hasNavSystem">());
  }
  return s;
}
#endif
static double lot_read(ParkingLot const &l) {
  double s = 0;
  for (auto const &c : l.cars) {
    s += double(c.make.size()) + double(c.model.size()) + c.color + c.seats + c.doors;
    for (auto const &w : c.wheels) s += w.diameter + double(w.airPressure) + w.snowTires;
    s += c.length + c.width + c.height + c.weight;
    s += c.engine.horsepower + c.engine.cylinders + c.engine.cc + c.engine.usesGas + c.engine.usesElectric;
    s += double(c.fuelCapacity) + double(c.fuelLevel) + c.hasPowerWindows + c.hasPowerSteering + c.hasCruiseControl + c.cupHolders +
         c.hasNavSystem;
  }
  return s;
}
static ParkingLot lot;
static std::string msg;
static std::shared_ptr<void const> const keep = std::make_shared<int const>(0);
static flatbuffers::FlatBufferBuilder fbb(1 << 20);
static std::string fbmsg;
static std::vector<capnp::word> cpscratch(1 << 18);
static std::vector<capnp::word> cpmsg;
static void run(benchmark::State &state)
{
    for (auto _ : state) {
#if defined(OP_ENC)
        msg.clear();
        auto const r = S::encode(lot, msg);
        if (!r) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(msg.data());
        benchmark::ClobberMemory();
#elif defined(OP_DEC)
        auto const l = S::decode(keep, msg);
        if (!l) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(lot_read(**l));
#elif defined(OP_PATH)
        benchmark::DoNotOptimize(lot_read(keep, msg));
#elif defined(OP_FB_ENC)
        fbb.Clear();
        std::vector<flatbuffers::Offset<fbcar::Car>> cs;
        cs.reserve(lot.cars.size());
        for (auto const &c : lot.cars) {
            auto const make = fbb.CreateString(c.make.data(), c.make.size());
            auto const model = fbb.CreateString(c.model.data(), c.model.size());
            fbcar::Wheel ws[4];
            for (std::size_t k = 0; k < c.wheels.size(); ++k) ws[k] = fbcar::Wheel(c.wheels[k].diameter, c.wheels[k].airPressure, c.wheels[k].snowTires);
            auto const wheels = fbb.CreateVectorOfStructs(ws, c.wheels.size());
            fbcar::Engine const e(c.engine.horsepower, c.engine.cylinders, c.engine.cc, c.engine.usesGas, c.engine.usesElectric);
            cs.push_back(fbcar::CreateCar(fbb, make, model, c.color, c.seats, c.doors, wheels, c.length, c.width, c.height, c.weight, &e,
                                          c.fuelCapacity, c.fuelLevel, c.hasPowerWindows, c.hasPowerSteering, c.hasCruiseControl, c.cupHolders, c.hasNavSystem));
        }
        fbb.Finish(fbcar::CreateParkingLot(fbb, fbb.CreateVector(cs)));
        benchmark::DoNotOptimize(fbb.GetBufferPointer());
        benchmark::ClobberMemory();
#elif defined(OP_FB_READ)
        flatbuffers::Verifier v(reinterpret_cast<std::uint8_t const *>(fbmsg.data()), fbmsg.size());
        if (!fbcar::VerifyParkingLotBuffer(v)) [[unlikely]] std::abort();
        double s = 0;
        for (auto const *r : *fbcar::GetParkingLot(fbmsg.data())->cars()) {
            s += double(r->make()->size()) + double(r->model()->size()) + r->color() + r->seats() + r->doors();
            for (auto const *w : *r->wheels()) s += w->diameter() + double(w->airPressure()) + w->snowTires();
            s += r->length() + r->width() + r->height() + r->weight();
            auto const *e = r->engine();
            s += e->horsepower() + e->cylinders() + e->cc() + e->usesGas() + e->usesElectric();
            s += double(r->fuelCapacity()) + double(r->fuelLevel()) + r->hasPowerWindows() + r->hasPowerSteering() + r->hasCruiseControl() + r->cupHolders() + r->hasNavSystem();
        }
        benchmark::DoNotOptimize(s);
#elif defined(OP_CP_ENC)
        capnp::MallocMessageBuilder mb(kj::arrayPtr(cpscratch.data(), cpscratch.size()));
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
        if (mb.getSegmentsForOutput().size() != 1) [[unlikely]] std::abort();
        benchmark::DoNotOptimize(mb.getSegmentsForOutput()[0].begin());
        benchmark::ClobberMemory();
#elif defined(OP_CP_READ)
        capnp::ReaderOptions opt;
        opt.traversalLimitInWords = std::uint64_t(1) << 40;
        capnp::FlatArrayMessageReader rd(kj::arrayPtr(cpmsg.data(), cpmsg.size()), opt);
        double s = 0;
        for (auto r : rd.getRoot<carsales::ParkingLot>().getCars()) {
            s += double(r.getMake().size()) + double(r.getModel().size()) + r.getColor() + r.getSeats() + r.getDoors();
            for (auto w : r.getWheels()) s += w.getDiameter() + double(w.getAirPressure()) + w.getSnowTires();
            s += r.getLength() + r.getWidth() + r.getHeight() + r.getWeight();
            auto e = r.getEngine();
            s += e.getHorsepower() + e.getCylinders() + e.getCc() + e.getUsesGas() + e.getUsesElectric();
            s += double(r.getFuelCapacity()) + double(r.getFuelLevel()) + r.getHasPowerWindows() + r.getHasPowerSteering() + r.getHasCruiseControl() + r.getCupHolders() + r.getHasNavSystem();
        }
        benchmark::DoNotOptimize(s);
#endif
    }
    state.SetItemsProcessed(std::int64_t(state.iterations()) * std::int64_t(lot.cars.size()));
}
int main(int argc, char **argv)
{
    std::ifstream f(CARS_JSON);
    auto const j = nlohmann::json::parse(f);
  auto &cars = lot.cars;
  for (auto const &c : j) {
    Car car{c["make"].get_ref<std::string const &>(), c["model"].get_ref<std::string const &>(), c["color"], c["seats"], c["doors"], {}, c["length"], c["width"], c["height"], c["weight"],
            Engine{c["engine"]["horsepower"], c["engine"]["cylinders"], c["engine"]["cc"], c["engine"]["usesGas"], c["engine"]["usesElectric"]},
            c["fuelCapacity"], c["fuelLevel"], c["hasPowerWindows"], c["hasPowerSteering"], c["hasCruiseControl"], c["cupHolders"], c["hasNavSystem"]};
    for (auto const &w : c["wheels"]) car.wheels.push_back(Wheel{w["diameter"], w["airPressure"], w["snowTires"]});
    cars.push_back(std::move(car));
  }
    if (!S::encode(lot, msg)) std::abort();
    {
        fbb.Clear();
        std::vector<flatbuffers::Offset<fbcar::Car>> cs;
        for (auto const &c : lot.cars) {
            std::vector<fbcar::Wheel> ws;
            for (auto const &w : c.wheels) ws.emplace_back(w.diameter, w.airPressure, w.snowTires);
            fbcar::Engine const e(c.engine.horsepower, c.engine.cylinders, c.engine.cc, c.engine.usesGas, c.engine.usesElectric);
            cs.push_back(fbcar::CreateCar(fbb, fbb.CreateString(c.make.data(), c.make.size()), fbb.CreateString(c.model.data(), c.model.size()), c.color, c.seats, c.doors, fbb.CreateVectorOfStructs(ws), c.length, c.width, c.height, c.weight, &e,
                                          c.fuelCapacity, c.fuelLevel, c.hasPowerWindows, c.hasPowerSteering, c.hasCruiseControl, c.cupHolders, c.hasNavSystem));
        }
        fbb.Finish(fbcar::CreateParkingLot(fbb, fbb.CreateVector(cs)));
        fbmsg.assign(reinterpret_cast<char const *>(fbb.GetBufferPointer()), fbb.GetSize());
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
        auto const flat = capnp::messageToFlatArray(mb);
        cpmsg.assign(flat.begin(), flat.end());
        benchmark::AddCustomContext("bytes_mb", std::to_string(msg.size()));
        benchmark::AddCustomContext("bytes_fb", std::to_string(fbmsg.size()));
        benchmark::AddCustomContext("bytes_cp", std::to_string(cpmsg.size() * 8));
    }
    benchmark::RegisterBenchmark("op", run);
    benchmark::Initialize(&argc, argv);
    benchmark::RunSpecifiedBenchmarks();
}
