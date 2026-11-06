#pragma once

#include <cbor/describe.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

namespace reference_ref
{

struct person {
    std::string_view name;
    int age;
};

} // namespace reference_ref

namespace doc_reference_schema
{

struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
};

struct point {
    float x;
    float y;
};

struct animal {
    std::string name;
};

struct dog : animal {
    std::uint8_t bark;
};

struct cat : animal {
    std::vector<std::string> toys;
};

struct home {
    std::variant<dog, cat> pet;
};

struct property {
    std::string name;
    std::variant<bool, std::int64_t, std::string> value;
};

} // namespace doc_reference_schema

CBOR_DEFINE_TYPE_MEMBERS(doc_reference_schema::tire, cbor::tag{1800}, diameter, pressure);

CBOR_DEFINE_TYPE_MEMBERS(doc_reference_schema::car, cbor::tag{1801}, make, seats, tires, track);

CBOR_DEFINE_TYPE_MEMBERS(doc_reference_schema::point, cbor::tag{1802}, x, y);

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(doc_reference_schema::dog, doc_reference_schema::animal, cbor::tag{1803}, name, bark);

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(doc_reference_schema::cat, doc_reference_schema::animal, cbor::tag{1804}, name, toys);

CBOR_DEFINE_TYPE_MEMBERS(doc_reference_schema::home, cbor::tag{1805}, pet);

CBOR_DEFINE_TYPE_MEMBERS(doc_reference_schema::property, cbor::tag{1806}, name, value);

namespace reference_structs
{

struct person {
    std::string name;
    int age;
};

struct header {
    std::string content_type;
};

struct senml {
    std::string n;
    double v;
};

struct account {
    std::string user;
    std::uint8_t level;
};

struct reading {
    std::uint8_t id;
    std::optional<std::uint8_t> unit;
};

struct trailer {
    std::uint8_t checksum;
};

struct device {
    std::string name;
    std::uint8_t port;
};

} // namespace reference_structs

CBOR_DEFINE_TYPE(reference_structs::header, cbor::tag{160060},
                 cbor::member{"content-type", &reference_structs::header::content_type});

CBOR_DEFINE_TYPE(reference_structs::senml, cbor::untagged,
                 cbor::member{0, &reference_structs::senml::n},
                 cbor::member{2, &reference_structs::senml::v});

CBOR_DEFINE_TYPE(reference_structs::account, cbor::untagged,
                 CBOR_MEMBER(reference_structs::account, user),
                 CBOR_MEMBER(reference_structs::account, level));

CBOR_DEFINE_TYPE(reference_structs::trailer, cbor::tag{160061},
                 cbor::member{"checksum", &reference_structs::trailer::checksum});

CBOR_DEFINE_TYPE_MEMBERS(reference_structs::device, cbor::untagged, name, port);
